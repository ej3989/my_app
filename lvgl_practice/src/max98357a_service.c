#include "max98357a_service.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(max98357a_service, LOG_LEVEL_INF);

#define MAX98357A_I2S_NODE DT_NODELABEL(i2s1)

#define AUDIO_BLOCK_SIZE_BYTES       2048U
#define TX_BLOCK_COUNT               20U
#define STREAM_PREBUFFER_BLOCK_COUNT 16U
#define AUDIO_THREAD_STACK_SIZE 1536U
#define AUDIO_THREAD_PRIORITY   K_PRIO_PREEMPT(4)
#define AUDIO_VOLUME_DIVISOR   6

#define WAV_FORMAT_PCM 1U
#define WAV_BITS_PER_SAMPLE 16U
#define WAV_CHANNEL_COUNT 2U

struct wav_info {
	const uint8_t *data;
	size_t data_size;
	uint32_t sample_rate;
	uint16_t channels;
	uint16_t bits_per_sample;
};

BUILD_ASSERT(DT_NODE_HAS_STATUS(MAX98357A_I2S_NODE, okay),
	     "I2S1 must be enabled for MAX98357A");

K_MEM_SLAB_DEFINE_STATIC(max98357a_tx_slab,
			 AUDIO_BLOCK_SIZE_BYTES,
			 TX_BLOCK_COUNT,
			 4);
K_THREAD_STACK_DEFINE(audio_thread_stack, AUDIO_THREAD_STACK_SIZE);
K_SEM_DEFINE(playback_request_sem, 0, 1);

static const uint8_t startup_wav[] = {
#include <8_xp_wav.inc>
};

static const struct device *const i2s_dev = DEVICE_DT_GET(MAX98357A_I2S_NODE);
static struct k_thread audio_thread;
static struct wav_info startup_wav_info;
static atomic_t playback_busy;
static bool initialized;
static bool pcm_stream_started;
static size_t pcm_stream_queued_blocks;

BUILD_ASSERT(STREAM_PREBUFFER_BLOCK_COUNT < TX_BLOCK_COUNT,
	     "Stream prebuffer must leave room in the I2S TX queue");

static uint16_t read_le16(const uint8_t *data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
	return (uint32_t)data[0] |
	       ((uint32_t)data[1] << 8) |
	       ((uint32_t)data[2] << 16) |
	       ((uint32_t)data[3] << 24);
}

static bool chunk_id_is(const uint8_t *data, const char id[4])
{
	return memcmp(data, id, 4) == 0;
}

static int parse_startup_wav(struct wav_info *info)
{
	size_t offset = 12U;
	bool format_found = false;
	bool data_found = false;
	uint16_t audio_format = 0U;

	if (sizeof(startup_wav) < offset ||
	    !chunk_id_is(&startup_wav[0], "RIFF") ||
	    !chunk_id_is(&startup_wav[8], "WAVE")) {
		return -EINVAL;
	}

	while (offset + 8U <= sizeof(startup_wav)) {
		const uint8_t *chunk = &startup_wav[offset];
		uint32_t chunk_size = read_le32(&chunk[4]);
		size_t chunk_data_offset = offset + 8U;

		if (chunk_size > sizeof(startup_wav) - chunk_data_offset) {
			return -EINVAL;
		}

		if (chunk_id_is(chunk, "fmt ")) {
			if (chunk_size < 16U) {
				return -EINVAL;
			}

			audio_format = read_le16(&startup_wav[chunk_data_offset]);
			info->channels = read_le16(&startup_wav[chunk_data_offset + 2U]);
			info->sample_rate = read_le32(&startup_wav[chunk_data_offset + 4U]);
			info->bits_per_sample =
				read_le16(&startup_wav[chunk_data_offset + 14U]);
			format_found = true;
		} else if (chunk_id_is(chunk, "data")) {
			info->data = &startup_wav[chunk_data_offset];
			info->data_size = chunk_size;
			data_found = true;
		}

		offset = chunk_data_offset + ROUND_UP(chunk_size, 2U);
	}

	if (!format_found || !data_found || audio_format != WAV_FORMAT_PCM ||
	    info->channels != WAV_CHANNEL_COUNT ||
	    info->bits_per_sample != WAV_BITS_PER_SAMPLE ||
	    info->sample_rate == 0U ||
	    (info->data_size % (WAV_CHANNEL_COUNT * sizeof(int16_t))) != 0U) {
		return -ENOTSUP;
	}

	return 0;
}

static void copy_audio_with_volume(int16_t *destination,
				   const uint8_t *source, size_t size)
{
	size_t sample_count = size / sizeof(int16_t);

	for (size_t index = 0; index < sample_count; index++) {
		int16_t sample = (int16_t)read_le16(&source[index * sizeof(int16_t)]);

		destination[index] = sample / AUDIO_VOLUME_DIVISOR;
	}
}

static int stream_startup_wav(void)
{
	size_t offset = 0U;
	bool started = false;
	int ret;

	while (offset < startup_wav_info.data_size) {
		size_t write_size = MIN(AUDIO_BLOCK_SIZE_BYTES,
					startup_wav_info.data_size - offset);
		void *block;

		ret = k_mem_slab_alloc(&max98357a_tx_slab, &block, K_MSEC(1000));
		if (ret < 0) {
			LOG_ERR("Audio block allocation failed: %d", ret);
			goto stop;
		}

		copy_audio_with_volume(block, &startup_wav_info.data[offset], write_size);

		ret = i2s_write(i2s_dev, block, write_size);
		if (ret < 0) {
			k_mem_slab_free(&max98357a_tx_slab, block);
			LOG_ERR("I2S1 write failed: %d", ret);
			goto stop;
		}

		if (!started) {
			ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
			if (ret < 0) {
				LOG_ERR("I2S1 start failed: %d", ret);
				goto stop;
			}
			started = true;
		}

		offset += write_size;
	}

	ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
	if (ret < 0) {
		LOG_ERR("I2S1 drain failed: %d", ret);
		goto stop;
	}

	/* Allow the final queued blocks to drain before another playback request. */
	k_msleep(100);
	LOG_INF("Startup WAV playback finished");
	return 0;

stop:
	(void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
	return ret;
}

static void audio_thread_handler(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		int ret;

		k_sem_take(&playback_request_sem, K_FOREVER);
		ret = stream_startup_wav();
		if (ret < 0) {
			LOG_ERR("Startup WAV playback failed: %d", ret);
		}
		atomic_clear(&playback_busy);
	}
}

static int configure_i2s(uint32_t sample_rate)
{
	struct i2s_config config = {
		.word_size = WAV_BITS_PER_SAMPLE,
		.channels = WAV_CHANNEL_COUNT,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_FRAME_CLK_CONTROLLER | I2S_OPT_BIT_CLK_CONTROLLER,
		.frame_clk_freq = sample_rate,
		.mem_slab = &max98357a_tx_slab,
		.block_size = AUDIO_BLOCK_SIZE_BYTES,
		.timeout = 1000,
	};

	return i2s_configure(i2s_dev, I2S_DIR_TX, &config);
}

int max98357a_service_init(void)
{
	int ret;

	if (initialized) {
		return 0;
	}

	if (!device_is_ready(i2s_dev)) {
		LOG_ERR("I2S1 device is not ready");
		return -ENODEV;
	}

	ret = parse_startup_wav(&startup_wav_info);
	if (ret < 0) {
		LOG_ERR("Unsupported startup WAV: %d", ret);
		return ret;
	}

	ret = configure_i2s(startup_wav_info.sample_rate);
	if (ret < 0) {
		LOG_ERR("I2S1 configure failed: %d", ret);
		return ret;
	}

	k_thread_create(&audio_thread,
			audio_thread_stack,
			K_THREAD_STACK_SIZEOF(audio_thread_stack),
			audio_thread_handler,
			NULL, NULL, NULL,
			AUDIO_THREAD_PRIORITY,
			0,
			K_NO_WAIT);
	k_thread_name_set(&audio_thread, "max98357a_audio");

	initialized = true;
	LOG_INF("MAX98357A ready: %u Hz, %u-bit stereo, WAV=%u bytes",
		startup_wav_info.sample_rate,
		startup_wav_info.bits_per_sample,
		(uint32_t)startup_wav_info.data_size);
	return 0;
}

int max98357a_service_play_startup_wav(void)
{
	if (!initialized) {
		return -EACCES;
	}

	if (!atomic_cas(&playback_busy, 0, 1)) {
		return -EBUSY;
	}

	k_sem_give(&playback_request_sem);
	return 0;
}

int max98357a_service_stream_start(uint32_t sample_rate)
{
	int ret;

	if (!initialized || sample_rate == 0U) {
		return -EINVAL;
	}

	if (!atomic_cas(&playback_busy, 0, 1)) {
		return -EBUSY;
	}

	ret = configure_i2s(sample_rate);
	if (ret < 0) {
		atomic_clear(&playback_busy);
		return ret;
	}

	pcm_stream_started = false;
	pcm_stream_queued_blocks = 0U;
	return 0;
}

int max98357a_service_stream_write(const int16_t *pcm,
				   size_t samples_per_channel,
				   uint8_t source_channels)
{
	size_t frame_offset = 0U;
	const size_t frames_per_block =
		AUDIO_BLOCK_SIZE_BYTES / (WAV_CHANNEL_COUNT * sizeof(int16_t));

	if (pcm == NULL || samples_per_channel == 0U ||
	    (source_channels != 1U && source_channels != 2U) ||
	    atomic_get(&playback_busy) == 0) {
		return -EINVAL;
	}

	while (frame_offset < samples_per_channel) {
		size_t frame_count = MIN(frames_per_block,
					 samples_per_channel - frame_offset);
		int16_t *block;
		int ret;

		ret = k_mem_slab_alloc(&max98357a_tx_slab,
				       (void **)&block, K_MSEC(1000));
		if (ret < 0) {
			LOG_ERR("I2S stream block allocation failed: %d", ret);
			return ret;
		}

		for (size_t frame = 0; frame < frame_count; frame++) {
			int16_t left = pcm[(frame_offset + frame) * source_channels];
			int16_t right = source_channels == 2U
				? pcm[(frame_offset + frame) * source_channels + 1U]
				: left;

			block[frame * 2U] = left / AUDIO_VOLUME_DIVISOR;
			block[frame * 2U + 1U] = right / AUDIO_VOLUME_DIVISOR;
		}

		ret = i2s_write(i2s_dev, block,
				frame_count * WAV_CHANNEL_COUNT * sizeof(int16_t));
		if (ret < 0) {
			k_mem_slab_free(&max98357a_tx_slab, block);
			LOG_ERR("I2S stream write failed: %d", ret);
			return ret;
		}

		if (!pcm_stream_started) {
			pcm_stream_queued_blocks++;
			if (pcm_stream_queued_blocks >= STREAM_PREBUFFER_BLOCK_COUNT) {
				ret = i2s_trigger(i2s_dev, I2S_DIR_TX,
						  I2S_TRIGGER_START);
				if (ret < 0) {
					(void)i2s_trigger(i2s_dev, I2S_DIR_TX,
							  I2S_TRIGGER_DROP);
					LOG_ERR("I2S stream start failed: %d", ret);
					return ret;
				}
				pcm_stream_started = true;
				LOG_INF("I2S stream started with %u buffered blocks",
					STREAM_PREBUFFER_BLOCK_COUNT);
			}
		}

		frame_offset += frame_count;
	}

	return 0;
}

int max98357a_service_stream_stop(bool drain)
{
	int ret = 0;

	if (atomic_get(&playback_busy) == 0) {
		return -EALREADY;
	}

	if (pcm_stream_started) {
		ret = i2s_trigger(i2s_dev, I2S_DIR_TX,
				  drain ? I2S_TRIGGER_DRAIN : I2S_TRIGGER_DROP);
		if (drain && ret == 0) {
			k_msleep(100);
		}
	} else if (pcm_stream_queued_blocks > 0U) {
		ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
	}

	pcm_stream_started = false;
	pcm_stream_queued_blocks = 0U;
	atomic_clear(&playback_busy);
	return ret;
}
