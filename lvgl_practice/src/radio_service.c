#include "radio_service.h"
#include "max98357a_service.h"
#include "wifi_service.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/atomic.h>

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include <minimp3.h>

LOG_MODULE_REGISTER(radio_service, LOG_LEVEL_INF);

/* minimp3 uses a roughly 16 KB scratch frame while decoding each MP3 frame. */
#define RADIO_THREAD_STACK_SIZE  (24U * 1024U)
#define RADIO_THREAD_PRIORITY    K_PRIO_PREEMPT(10)
#define RADIO_MP3_BUFFER_SIZE    (32U * 1024U)
#define RADIO_MP3_PREFILL_SIZE   (24U * 1024U)
#define RADIO_RECEIVE_TIMEOUT_S  10
#define RADIO_PORT               "80"

BUILD_ASSERT(RADIO_MP3_PREFILL_SIZE < RADIO_MP3_BUFFER_SIZE,
	     "MP3 prefill must leave free receive buffer space");

K_THREAD_STACK_DEFINE(radio_thread_stack, RADIO_THREAD_STACK_SIZE);
K_SEM_DEFINE(radio_start_sem, 0, 1);
K_MUTEX_DEFINE(radio_socket_lock);

static struct k_thread radio_thread;
static atomic_t initialized;
static atomic_t playing;
static atomic_t stop_requested;
static int active_socket = -1;
static uint8_t mp3_buffer[RADIO_MP3_BUFFER_SIZE];
static mp3d_sample_t pcm_buffer[MINIMP3_MAX_SAMPLES_PER_FRAME];
static mp3dec_t mp3_decoder;

static int send_all(int socket_fd, const char *data, size_t length)
{
	size_t sent = 0U;

	while (sent < length) {
		ssize_t ret = zsock_send(socket_fd, data + sent, length - sent, 0);

		if (ret < 0) {
			return -errno;
		}
		if (ret == 0) {
			return -ECONNRESET;
		}
		sent += (size_t)ret;
	}

	return 0;
}

static int open_stream_socket(void)
{
	static const char request[] =
		"GET " CONFIG_APP_RADIO_PATH " HTTP/1.0\r\n"
		"Host: " CONFIG_APP_RADIO_HOST "\r\n"
		"User-Agent: EJ3989-Zephyr-Radio/1.0\r\n"
		"Icy-MetaData: 0\r\n"
		"Connection: close\r\n\r\n";
	struct zsock_addrinfo hints = { 0 };
	struct zsock_addrinfo *addresses = NULL;
	struct timeval receive_timeout = {
		.tv_sec = RADIO_RECEIVE_TIMEOUT_S,
		.tv_usec = 0,
	};
	int socket_fd = -1;
	int ret;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	ret = zsock_getaddrinfo(CONFIG_APP_RADIO_HOST, RADIO_PORT,
				&hints, &addresses);
	if (ret != 0 || addresses == NULL) {
		LOG_ERR("Radio DNS lookup failed: %d", ret);
		return -EHOSTUNREACH;
	}

	socket_fd = zsock_socket(addresses->ai_family, SOCK_STREAM, IPPROTO_TCP);
	if (socket_fd < 0) {
		ret = -errno;
		LOG_ERR("Radio TCP socket creation failed: %d", ret);
		goto out;
	}

	ret = zsock_setsockopt(socket_fd, ZSOCK_SOL_SOCKET, ZSOCK_SO_RCVTIMEO,
			       &receive_timeout, sizeof(receive_timeout));
	if (ret < 0) {
		ret = -errno;
		if (ret == -ENOPROTOOPT) {
			LOG_WRN("Radio receive timeout is not supported");
		} else {
			LOG_ERR("Radio receive timeout setup failed: %d", ret);
			goto out;
		}
	}

	ret = zsock_connect(socket_fd, addresses->ai_addr,
			    addresses->ai_addrlen);
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("Radio TCP connection failed: %d", ret);
		goto out;
	}

	ret = send_all(socket_fd, request, sizeof(request) - 1U);
	if (ret < 0) {
		LOG_ERR("Radio HTTP request failed: %d", ret);
		goto out;
	}

	LOG_INF("Connected to http://%s%s",
		CONFIG_APP_RADIO_HOST, CONFIG_APP_RADIO_PATH);
	ret = socket_fd;
	socket_fd = -1;

out:
	if (socket_fd >= 0) {
		(void)zsock_close(socket_fd);
	}
	zsock_freeaddrinfo(addresses);
	return ret;
}

static int receive_http_header(int socket_fd, size_t *body_size)
{
	size_t used = 0U;

	while (used < RADIO_MP3_BUFFER_SIZE) {
		ssize_t received = zsock_recv(socket_fd, &mp3_buffer[used],
					      RADIO_MP3_BUFFER_SIZE - used, 0);
		if (received < 0) {
			return -errno;
		}
		if (received == 0) {
			return -ECONNRESET;
		}
		used += (size_t)received;

		for (size_t index = 3U; index < used; index++) {
			if (memcmp(&mp3_buffer[index - 3U], "\r\n\r\n", 4U) == 0) {
				size_t header_size = index + 1U;

				if (used < 12U ||
				    (memcmp(mp3_buffer, "HTTP/1.0 200", 12U) != 0 &&
				     memcmp(mp3_buffer, "HTTP/1.1 200", 12U) != 0)) {
					return -EPROTO;
				}

				*body_size = used - header_size;
				memmove(mp3_buffer, &mp3_buffer[header_size], *body_size);
				return 0;
			}
		}
	}

	return -EMSGSIZE;
}

static int receive_mp3_until(int socket_fd, size_t *used, size_t target,
			     bool log_first_payload)
{
	while (*used < target) {
		ssize_t received = zsock_recv(socket_fd, &mp3_buffer[*used],
					      RADIO_MP3_BUFFER_SIZE - *used, 0);

		if (received < 0) {
			int ret = -errno;

			LOG_ERR("Radio MP3 receive failed: %d", ret);
			return ret;
		}
		if (received == 0) {
			return -ECONNRESET;
		}

		*used += (size_t)received;
		if (log_first_payload) {
			log_first_payload = false;
			LOG_INF("First MP3 payload received: %zd bytes; first bytes=%02x %02x",
				received, mp3_buffer[0],
				*used > 1U ? mp3_buffer[1] : 0U);
		}
	}

	return 0;
}

static int receive_mp3_available(int socket_fd, size_t *used)
{
	while (*used < RADIO_MP3_BUFFER_SIZE) {
		ssize_t received = zsock_recv(socket_fd, &mp3_buffer[*used],
					      RADIO_MP3_BUFFER_SIZE - *used,
					      ZSOCK_MSG_DONTWAIT);

		if (received < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				return 0;
			}

			return -errno;
		}
		if (received == 0) {
			return -ECONNRESET;
		}

		*used += (size_t)received;
	}

	return 0;
}

static void log_receive_rate(const char *label, size_t received,
			     int64_t started_at)
{
	int64_t elapsed_ms = MAX(k_uptime_get() - started_at, 1);
	uint32_t bytes_per_second =
		(uint32_t)(((uint64_t)received * MSEC_PER_SEC) /
			   (uint64_t)elapsed_ms);

	LOG_INF("%s: %zu bytes in %lld ms (%u B/s)",
		label, received, (long long)elapsed_ms, bytes_per_second);
}

static int play_mp3_stream(int socket_fd, bool *audio_owned)
{
	size_t used = 0U;
	int64_t receive_started_at;
	int ret;

	*audio_owned = false;

	ret = receive_http_header(socket_fd, &used);
	if (ret < 0) {
		LOG_ERR("Radio HTTP response header failed: %d", ret);
		return ret;
	}
	LOG_INF("Radio HTTP response accepted; initial MP3 data=%zu bytes", used);
	if (used == 0U) {
		LOG_INF("Waiting for first MP3 payload");
	}

	receive_started_at = k_uptime_get();
	ret = receive_mp3_until(socket_fd, &used, RADIO_MP3_PREFILL_SIZE,
				used == 0U);
	if (ret < 0) {
		return ret;
	}
	log_receive_rate("MP3 prebuffer ready", used, receive_started_at);

	mp3dec_init(&mp3_decoder);

	while (1) {
		mp3dec_frame_info_t frame_info = { 0 };
		int samples;

		if (atomic_get(&stop_requested)) {
			return -ECANCELED;
		}

		samples = mp3dec_decode_frame(&mp3_decoder, mp3_buffer,
					      (int)used, pcm_buffer, &frame_info);
		if (frame_info.frame_bytes == 0) {
			if (used == RADIO_MP3_BUFFER_SIZE) {
				LOG_ERR("No MP3 frame found in %u bytes; first bytes=%02x %02x",
					RADIO_MP3_BUFFER_SIZE,
					mp3_buffer[0], mp3_buffer[1]);
				return -EBADMSG;
			}

			ret = receive_mp3_until(socket_fd, &used, used + 1U, false);
			if (ret < 0) {
				return ret;
			}
			continue;
		}
		if ((size_t)frame_info.frame_bytes > used) {
			return -EPROTO;
		}

		memmove(mp3_buffer, &mp3_buffer[frame_info.frame_bytes],
			used - (size_t)frame_info.frame_bytes);
		used -= (size_t)frame_info.frame_bytes;

		if (samples <= 0) {
			continue;
		}

		if (!*audio_owned) {
			LOG_INF("First MP3 frame decoded: %d Hz, %d channel(s)",
				frame_info.hz, frame_info.channels);
			ret = max98357a_service_stream_start((uint32_t)frame_info.hz);
			if (ret < 0) {
				LOG_ERR("I2S stream start failed: %d", ret);
				return ret;
			}
			*audio_owned = true;
			LOG_INF("MP3 audio: %d Hz, %d channel(s), %d kbps",
				frame_info.hz, frame_info.channels,
				frame_info.bitrate_kbps);
		}

		ret = max98357a_service_stream_write(
			pcm_buffer, (size_t)samples, (uint8_t)frame_info.channels);
		if (ret == -EIO) {
			size_t refill_start_size = used;

			LOG_WRN("I2S underrun; refilling MP3 jitter buffer");
			(void)max98357a_service_stream_stop(false);
			*audio_owned = false;

			receive_started_at = k_uptime_get();
			ret = receive_mp3_until(socket_fd, &used,
						RADIO_MP3_PREFILL_SIZE, false);
			if (ret < 0) {
				return ret;
			}
			log_receive_rate("MP3 jitter buffer refilled",
					 used - refill_start_size,
					 receive_started_at);
			continue;
		}
		if (ret < 0) {
			return ret;
		}

		/* Keep draining TCP while I2S consumes the decoded PCM queue. */
		ret = receive_mp3_available(socket_fd, &used);
		if (ret < 0) {
			LOG_ERR("Radio MP3 background receive failed: %d", ret);
			return ret;
		}
	}
}

static void radio_thread_handler(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		bool audio_owned = false;
		int socket_fd = -1;
		int ret;

		k_sem_take(&radio_start_sem, K_FOREVER);

		ret = wifi_service_connect();
		if (ret < 0) {
			LOG_ERR("Radio Wi-Fi setup failed: %d", ret);
			goto done;
		}
		if (atomic_get(&stop_requested)) {
			ret = -ECANCELED;
			goto done;
		}

		socket_fd = open_stream_socket();
		if (socket_fd < 0) {
			LOG_ERR("Radio HTTP connection failed: %d", socket_fd);
			goto done;
		}

		k_mutex_lock(&radio_socket_lock, K_FOREVER);
		active_socket = socket_fd;
		k_mutex_unlock(&radio_socket_lock);

		if (atomic_get(&stop_requested)) {
			ret = -ECANCELED;
			goto done;
		}

		ret = play_mp3_stream(socket_fd, &audio_owned);
		if (ret == -ECANCELED || atomic_get(&stop_requested)) {
			LOG_INF("Radio playback stopped");
		} else {
			LOG_ERR("Radio stream stopped: %d", ret);
		}
		if (audio_owned) {
			(void)max98357a_service_stream_stop(false);
		}

done:
		if (socket_fd >= 0) {
			k_mutex_lock(&radio_socket_lock, K_FOREVER);
			active_socket = -1;
			(void)zsock_close(socket_fd);
			k_mutex_unlock(&radio_socket_lock);
		}
		atomic_clear(&playing);
		atomic_clear(&stop_requested);
	}
}

int radio_service_init(void)
{
	int ret;

	if (!atomic_cas(&initialized, 0, 1)) {
		return 0;
	}

	ret = wifi_service_init();
	if (ret < 0) {
		atomic_clear(&initialized);
		return ret;
	}

	k_thread_create(&radio_thread, radio_thread_stack,
			K_THREAD_STACK_SIZEOF(radio_thread_stack),
			radio_thread_handler, NULL, NULL, NULL,
			RADIO_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&radio_thread, "internet_radio");

	return 0;
}

int radio_service_start(void)
{
	if (!atomic_get(&initialized)) {
		return -EACCES;
	}

	if (strlen(CONFIG_APP_WIFI_SSID) == 0U) {
		return -EINVAL;
	}

	if (!atomic_cas(&playing, 0, 1)) {
		return -EALREADY;
	}

	atomic_clear(&stop_requested);
	k_sem_give(&radio_start_sem);
	return 0;
}

int radio_service_stop(void)
{
	if (!atomic_get(&playing)) {
		return -EALREADY;
	}

	atomic_set(&stop_requested, 1);

	/* Wake a blocking recv() so the radio thread can stop immediately. */
	k_mutex_lock(&radio_socket_lock, K_FOREVER);
	if (active_socket >= 0) {
		(void)zsock_shutdown(active_socket, ZSOCK_SHUT_RDWR);
	}
	k_mutex_unlock(&radio_socket_lock);

	return 0;
}

bool radio_service_is_playing(void)
{
	return atomic_get(&playing) != 0 &&
	       atomic_get(&stop_requested) == 0;
}
