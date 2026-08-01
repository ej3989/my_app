/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/atomic.h>

#include "wifi_manager.h"

LOG_MODULE_REGISTER(wifi_manager, LOG_LEVEL_INF);

#define WIFI_EVENTS \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)
#define WIFI_CONNECT_TIMEOUT K_SECONDS(20)
#define DHCP_TIMEOUT_SECONDS 30

static struct net_if *wifi_iface;
static struct net_mgmt_event_callback wifi_callback;
static K_SEM_DEFINE(wifi_result_sem, 0, 1);
static atomic_t wifi_connected;
static int wifi_connect_result;

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
			       uint64_t event, struct net_if *iface)
{
	const struct wifi_status *status = cb->info;

	ARG_UNUSED(iface);

	if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		wifi_connect_result = status != NULL ? status->status : -EIO;
		if (wifi_connect_result == 0) {
			atomic_set(&wifi_connected, 1);
			LOG_INF("Connected to Wi-Fi access point");
		} else {
			atomic_clear(&wifi_connected);
			LOG_ERR("Wi-Fi connection failed (status %d)",
				wifi_connect_result);
		}
		k_sem_give(&wifi_result_sem);
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		atomic_clear(&wifi_connected);
		LOG_WRN("Wi-Fi disconnected");
	}
}

int wifi_manager_init(void)
{
	wifi_iface = net_if_get_wifi_sta();
	if (wifi_iface == NULL) {
		LOG_ERR("Wi-Fi station interface was not found");
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&wifi_callback, wifi_event_handler, WIFI_EVENTS);
	net_mgmt_add_event_callback(&wifi_callback);
	return 0;
}

int wifi_manager_connect(void)
{
	struct wifi_connect_req_params params = {0};
	struct net_in_addr *address;
	int err;

	if (strlen(CONFIG_WIFI_FAN_WIFI_SSID) == 0U) {
		LOG_ERR("CONFIG_WIFI_FAN_WIFI_SSID is empty");
		return -EINVAL;
	}

	params.ssid = CONFIG_WIFI_FAN_WIFI_SSID;
	params.ssid_length = strlen(CONFIG_WIFI_FAN_WIFI_SSID);
	params.channel = WIFI_CHANNEL_ANY;
	params.band = WIFI_FREQ_BAND_UNKNOWN;
	params.mfp = WIFI_MFP_OPTIONAL;

	if (strlen(CONFIG_WIFI_FAN_WIFI_PASSWORD) == 0U) {
		params.security = WIFI_SECURITY_TYPE_NONE;
	} else {
		params.security = WIFI_SECURITY_TYPE_PSK;
		params.psk = CONFIG_WIFI_FAN_WIFI_PASSWORD;
		params.psk_length = strlen(CONFIG_WIFI_FAN_WIFI_PASSWORD);
	}

	atomic_clear(&wifi_connected);
	k_sem_reset(&wifi_result_sem);
	LOG_INF("Connecting to Wi-Fi SSID '%s'", CONFIG_WIFI_FAN_WIFI_SSID);

	err = net_mgmt(NET_REQUEST_WIFI_CONNECT, wifi_iface, &params, sizeof(params));
	if (err != 0) {
		LOG_ERR("Wi-Fi connect request failed (err %d)", err);
		return err;
	}

	err = k_sem_take(&wifi_result_sem, WIFI_CONNECT_TIMEOUT);
	if (err != 0) {
		LOG_ERR("Wi-Fi connection timed out");
		return err;
	}
	if (wifi_connect_result != 0) {
		return -EIO;
	}

	net_dhcpv4_restart(wifi_iface);
	for (int i = 0; i < DHCP_TIMEOUT_SECONDS; ++i) {
		if (!wifi_manager_is_connected()) {
			return -ENETDOWN;
		}

		address = net_if_ipv4_get_global_addr(wifi_iface, NET_ADDR_PREFERRED);
		if (address != NULL) {
			LOG_INF("IPv4 address acquired");
			return 0;
		}
		k_sleep(K_SECONDS(1));
	}

	LOG_ERR("DHCP timed out");
	return -ETIMEDOUT;
}

bool wifi_manager_is_connected(void)
{
	return atomic_get(&wifi_connected) != 0;
}

int wifi_manager_get_rssi(int *rssi)
{
	struct wifi_iface_status status = {0};
	int err;

	if (rssi == NULL) {
		return -EINVAL;
	}

	err = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, wifi_iface,
		       &status, sizeof(status));
	if (err != 0) {
		return err;
	}

	*rssi = status.rssi;
	return 0;
}
