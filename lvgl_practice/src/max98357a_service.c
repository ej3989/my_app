#include "max98357a_service.h"

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(max98357a_service, LOG_LEVEL_INF);

#define MAX98357A_I2S_NODE DT_NODELABEL(i2s1)

#define SAMPLE_RATE_HZ        16000U
#define CHANNEL_COUNT         2U
#define BITS_PER_SAMPLE       16U
#define BLOCK_FRAME_COUNT     160U
#define BLOCK_SIZE_BYTES      (BLOCK_FRAME_COUNT * CHANNEL_COUNT * sizeof(int16_t))
#define TX_BLOCK_COUNT        4U
#define TEST_TONE_BLOCK_COUNT 50U

BUILD_ASSERT(DT_NODE_HAS_STATUS(MAX98357A_I2S_NODE, okay),
	     "I2S1 must be enabled for MAX98357A");

K_MEM_SLAB_DEFINE_STATIC(max98357a_tx_slab,
			 BLOCK_SIZE_BYTES,
			 TX_BLOCK_COUNT,
			 4);

static const struct device *const i2s_dev = DEVICE_DT_GET(MAX98357A_I2S_NODE);
static bool initialized;
static uint8_t wave_index;

/* 20 samples at 16 kHz produce an 800 Hz sine wave. */
static const int16_t test_wave[] = {
	0, 927, 1763, 2427, 2853, 3000, 2853, 2427, 1763, 927,
	0, -927, -1763, -2427, -2853, -3000, -2853, -2427, -1763, -927,
};

static void fill_test_tone(int16_t *samples)
{
	for (size_t frame = 0; frame < BLOCK_FRAME_COUNT; frame++) {
		int16_t sample = test_wave[wave_index];

		/* Duplicate the tone on both channels, regardless of amplifier slot selection. */
		samples[frame * CHANNEL_COUNT] = sample;
		samples[frame * CHANNEL_COUNT + 1U] = sample;

		wave_index++;
		if (wave_index == ARRAY_SIZE(test_wave)) {
			wave_index = 0U;
		}
	}
}

int max98357a_service_init(void)
{
	struct i2s_config config = {
		.word_size = BITS_PER_SAMPLE,
		.channels = CHANNEL_COUNT,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_FRAME_CLK_CONTROLLER | I2S_OPT_BIT_CLK_CONTROLLER,
		.frame_clk_freq = SAMPLE_RATE_HZ,
		.mem_slab = &max98357a_tx_slab,
		.block_size = BLOCK_SIZE_BYTES,
		.timeout = 1000,
	};
	int ret;

	if (initialized) {
		return 0;
	}

	if (!device_is_ready(i2s_dev)) {
		LOG_ERR("I2S1 device is not ready");
		return -ENODEV;
	}

	ret = i2s_configure(i2s_dev, I2S_DIR_TX, &config);
	if (ret < 0) {
		LOG_ERR("I2S1 configure failed: %d", ret);
		return ret;
	}

	initialized = true;
	LOG_INF("MAX98357A ready: %u Hz, %u-bit stereo",
		SAMPLE_RATE_HZ, BITS_PER_SAMPLE);
	return 0;
}

int max98357a_service_play_test_tone(void)
{
	bool started = false;
	int ret;

	if (!initialized) {
		return -EACCES;
	}

	wave_index = 0U;

	for (uint32_t block_index = 0; block_index < TEST_TONE_BLOCK_COUNT;
	     block_index++) {
		void *block;

		ret = k_mem_slab_alloc(&max98357a_tx_slab, &block, K_MSEC(1000));
		if (ret < 0) {
			LOG_ERR("Audio block allocation failed: %d", ret);
			goto stop;
		}

		fill_test_tone(block);

		ret = i2s_write(i2s_dev, block, BLOCK_SIZE_BYTES);
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
	}

	ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
	if (ret < 0) {
		LOG_ERR("I2S1 drain failed: %d", ret);
		goto stop;
	}

	LOG_INF("MAX98357A 800 Hz test tone queued");
	return 0;

stop:
	(void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
	return ret;
}
