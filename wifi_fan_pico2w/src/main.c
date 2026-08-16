/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/app_version.h>
#include <zephyr/logging/log.h>
#if defined(CONFIG_MCUMGR_TRANSPORT_UDP)
#include <zephyr/mgmt/mcumgr/transport/smp_udp.h>
#endif

#include "fan_controller.h"
#include "mqtt_fan.h"
#include "mqtt_tls.h"
#include "status_led.h"
#include "system_watchdog.h"
#include "time_manager.h"
#include "wifi_manager.h"

LOG_MODULE_REGISTER(wifi_fan_pico2w, LOG_LEVEL_INF);

int main(void)
{
	int err;
#if defined(CONFIG_MCUMGR_TRANSPORT_UDP)
	bool ota_server_started = false;
#endif

	LOG_INF("Pico 2 W Wi-Fi fan controller v%s start", APP_VERSION_STRING);

	err = fan_controller_init();
	if (err != 0) {
		return err;
	}

	err = wifi_manager_init();
	if (err != 0) {
		LOG_ERR("CYW43439 unavailable; onboard LED test cannot run");
		return err;
	}

	err = status_led_turn_on();
	if (err != 0) {
		return err;
	}

	err = mqtt_tls_init();
	if (err != 0) {
		return err;
	}

	err = system_watchdog_init();
	if (err != 0) {
		return err;
	}

	while (true) {
		system_watchdog_feed();

		if (!wifi_manager_is_connected()) {
			err = wifi_manager_connect();
			if (err != 0) {
				LOG_WRN("Wi-Fi unavailable; retrying in 10 seconds");
				k_sleep(K_SECONDS(10));
				continue;
			}
		}
		system_watchdog_feed();

#if defined(CONFIG_MCUMGR_TRANSPORT_UDP)
		/* Start SMP/UDP only after DHCP has supplied an IPv4 address. */
		if (!ota_server_started) {
			err = smp_udp_open();
			if (err != 0) {
				LOG_ERR("Failed to start MCUmgr OTA server (err %d)", err);
				k_sleep(K_SECONDS(10));
				continue;
			}

			ota_server_started = true;
			LOG_INF("MCUmgr OTA server listening on UDP port %d",
				CONFIG_MCUMGR_TRANSPORT_UDP_PORT);
		}
#endif

		err = time_manager_sync();
		if (err != 0) {
			LOG_WRN("Time unavailable; retrying in 10 seconds");
			k_sleep(K_SECONDS(10));
			continue;
		}
		system_watchdog_feed();

		err = mqtt_fan_run();
		system_watchdog_feed();
		LOG_WRN("MQTT session ended (err %d); reconnecting", err);
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
