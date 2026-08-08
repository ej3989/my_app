/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "fan_controller.h"
#include "mqtt_fan.h"
#include "mqtt_tls.h"
#include "status_led.h"
#include "time_manager.h"
#include "wifi_manager.h"

LOG_MODULE_REGISTER(wifi_fan_pico2w, LOG_LEVEL_INF);

int main(void)
{
	int err;

	LOG_INF("Pico 2 W Wi-Fi fan controller start");

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

	while (true) {
		if (!wifi_manager_is_connected()) {
			err = wifi_manager_connect();
			if (err != 0) {
				LOG_WRN("Wi-Fi unavailable; retrying in 10 seconds");
				k_sleep(K_SECONDS(10));
				continue;
			}
		}

		err = time_manager_sync();
		if (err != 0) {
			LOG_WRN("Time unavailable; retrying in 10 seconds");
			k_sleep(K_SECONDS(10));
			continue;
		}

		err = mqtt_fan_run();
		LOG_WRN("MQTT session ended (err %d); reconnecting", err);
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
