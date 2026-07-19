#include "wifi_service.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(wifi_service, LOG_LEVEL_INF);

#define WIFI_EVENT_CONNECTED BIT(0)
#define WIFI_EVENT_IPV4_READY BIT(1)
#define WIFI_CONNECT_TIMEOUT K_SECONDS(30)

K_EVENT_DEFINE(wifi_events);

static struct net_mgmt_event_callback wifi_mgmt_cb;
static struct net_mgmt_event_callback ipv4_mgmt_cb;
static struct net_if *wifi_iface;
static atomic_t initialized;
static atomic_t ipv4_ready;

static int disable_wifi_power_save(void)
{
	struct wifi_ps_params params = {
		.enabled = WIFI_PS_DISABLED,
	};
	int ret;

	ret = net_mgmt(NET_REQUEST_WIFI_PS, wifi_iface,
		       &params, sizeof(params));
	if (ret < 0) {
		LOG_ERR("Wi-Fi power save disable failed: %d", ret);
		return ret;
	}

	LOG_INF("Wi-Fi power save disabled for streaming");
	return 0;
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
			       uint64_t event, struct net_if *iface)
{
	const struct wifi_status *status = cb->info;

	if (iface != wifi_iface) {
		return;
	}

	switch (event) {
	case NET_EVENT_WIFI_CONNECT_RESULT:
		if (status != NULL && status->status == 0) {
			LOG_INF("Wi-Fi associated with %s", CONFIG_APP_WIFI_SSID);
			k_event_post(&wifi_events, WIFI_EVENT_CONNECTED);
		} else {
			LOG_WRN("Wi-Fi connection attempt failed: %d; waiting for reconnect",
				status != NULL ? status->status : -EIO);
		}
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		atomic_clear(&ipv4_ready);
		k_event_clear(&wifi_events,
			      WIFI_EVENT_CONNECTED | WIFI_EVENT_IPV4_READY);
		LOG_WRN("Wi-Fi disconnected");
		break;
	default:
		break;
	}
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb,
			       uint64_t event, struct net_if *iface)
{
	ARG_UNUSED(cb);

	if (event != NET_EVENT_IPV4_ADDR_ADD || iface != wifi_iface) {
		return;
	}

	atomic_set(&ipv4_ready, 1);
	k_event_post(&wifi_events, WIFI_EVENT_IPV4_READY);
	LOG_INF("Wi-Fi IPv4 address is ready");
}

int wifi_service_init(void)
{
	if (!atomic_cas(&initialized, 0, 1)) {
		return 0;
	}

	wifi_iface = net_if_get_wifi_sta();
	if (wifi_iface == NULL) {
		atomic_clear(&initialized);
		return -ENODEV;
	}

	net_mgmt_init_event_callback(
		&wifi_mgmt_cb, wifi_event_handler,
		NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_mgmt_cb);

	net_mgmt_init_event_callback(&ipv4_mgmt_cb, ipv4_event_handler,
				     NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_mgmt_cb);

	return 0;
}

int wifi_service_connect(void)
{
	struct wifi_connect_req_params params = { 0 };
	size_t psk_length = strlen(CONFIG_APP_WIFI_PSK);
	size_t ssid_length = strlen(CONFIG_APP_WIFI_SSID);
	uint32_t events;
	int ret;

	if (!atomic_get(&initialized) || wifi_iface == NULL) {
		return -EACCES;
	}

	if (atomic_get(&ipv4_ready)) {
		return disable_wifi_power_save();
	}

	if (ssid_length == 0U || ssid_length > WIFI_SSID_MAX_LEN) {
		LOG_ERR("CONFIG_APP_WIFI_SSID must contain 1 to 32 characters");
		return -EINVAL;
	}
	if (psk_length != 0U &&
	    (psk_length < WIFI_PSK_MIN_LEN || psk_length > WIFI_PSK_MAX_LEN)) {
		LOG_ERR("CONFIG_APP_WIFI_PSK must contain 8 to 64 characters");
		return -EINVAL;
	}

	params.ssid = (const uint8_t *)CONFIG_APP_WIFI_SSID;
	params.ssid_length = (uint8_t)ssid_length;
	params.channel = WIFI_CHANNEL_ANY;
	params.band = WIFI_FREQ_BAND_UNKNOWN;
	params.timeout = 30;
	params.mfp = WIFI_MFP_OPTIONAL;

	if (psk_length == 0U) {
		params.security = WIFI_SECURITY_TYPE_NONE;
	} else {
		params.psk = (const uint8_t *)CONFIG_APP_WIFI_PSK;
		params.psk_length = (uint8_t)psk_length;
		params.security = WIFI_SECURITY_TYPE_PSK;
	}

	k_event_clear(&wifi_events, WIFI_EVENT_CONNECTED |
				    WIFI_EVENT_IPV4_READY);

	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, wifi_iface,
		       &params, sizeof(params));
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("Wi-Fi connect request failed: %d", ret);
		return ret;
	}

	LOG_INF("Waiting for Wi-Fi and DHCP");
	events = k_event_wait(&wifi_events,
			      WIFI_EVENT_IPV4_READY,
			      false, WIFI_CONNECT_TIMEOUT);
	if ((events & WIFI_EVENT_IPV4_READY) != 0U) {
		return disable_wifi_power_save();
	}

	return -ETIMEDOUT;
}
