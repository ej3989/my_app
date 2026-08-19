/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIFI_FAN_FAN_CONTROLLER_H_
#define WIFI_FAN_FAN_CONTROLLER_H_

#include <stdbool.h>
#include <stdint.h>

struct fan_state {
	uint8_t speed;
	bool oscillating;
};

int fan_controller_init(void);
int fan_set_speed(uint8_t speed);
int fan_set_oscillation(bool oscillating);
int fan_turn_on(void);
int fan_turn_off(void);
struct fan_state fan_get_state(void);
void fan_preserve_state_for_network_watchdog(void);
int fan_restore_state_after_network_watchdog(void);

#endif /* WIFI_FAN_FAN_CONTROLLER_H_ */
