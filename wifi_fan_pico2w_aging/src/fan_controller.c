/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "fan_controller.h"

LOG_MODULE_REGISTER(fan_controller, LOG_LEVEL_INF);

#define SPEED_1_RELAY_NODE DT_ALIAS(fan_speed_1_relay)
#define SPEED_2_RELAY_NODE DT_ALIAS(fan_speed_2_relay)
#define SPEED_3_RELAY_NODE DT_ALIAS(fan_speed_3_relay)
#define OSC_RELAY_NODE     DT_ALIAS(fan_osc_relay)

#define FAN_SPEED_MAX 3U
#define RELAY_BREAK_TIME_MS 100

static const struct gpio_dt_spec speed_relays[] = {
	GPIO_DT_SPEC_GET(SPEED_1_RELAY_NODE, gpios),
	GPIO_DT_SPEC_GET(SPEED_2_RELAY_NODE, gpios),
	GPIO_DT_SPEC_GET(SPEED_3_RELAY_NODE, gpios),
};
static const struct gpio_dt_spec oscillation_relay =
	GPIO_DT_SPEC_GET(OSC_RELAY_NODE, gpios);

static struct fan_state current_state;
static uint8_t last_nonzero_speed = 1U;

static int all_speed_relays_off(void)
{
	int err;

	for (size_t i = 0; i < ARRAY_SIZE(speed_relays); ++i) {
		err = gpio_pin_set_dt(&speed_relays[i], 0);
		if (err != 0) {
			LOG_ERR("Failed to turn speed relay %u off (err %d)",
				(unsigned int)i + 1U, err);
			return err;
		}
	}

	return 0;
}

int fan_controller_init(void)
{
	int err;

	for (size_t i = 0; i < ARRAY_SIZE(speed_relays); ++i) {
		if (!gpio_is_ready_dt(&speed_relays[i])) {
			LOG_ERR("Speed relay %u GPIO is not ready", (unsigned int)i + 1U);
			return -ENODEV;
		}

		err = gpio_pin_configure_dt(&speed_relays[i], GPIO_OUTPUT_INACTIVE);
		if (err != 0) {
			LOG_ERR("Failed to initialize speed relay %u (err %d)",
				(unsigned int)i + 1U, err);
			return err;
		}
	}

	if (!gpio_is_ready_dt(&oscillation_relay)) {
		LOG_ERR("Oscillation relay GPIO is not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&oscillation_relay, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		LOG_ERR("Failed to initialize oscillation relay (err %d)", err);
		return err;
	}

	current_state.speed = 0U;
	current_state.oscillating = false;
	LOG_INF("All active-low relays initialized OFF (GPIO HIGH)");
	return 0;
}

int fan_set_speed(uint8_t speed)
{
	int err;

	if (speed > FAN_SPEED_MAX) {
		return -EINVAL;
	}

	if (current_state.speed == speed) {
		return 0;
	}

	err = all_speed_relays_off();
	if (err != 0) {
		return err;
	}
	current_state.speed = 0U;

	if (speed == 0U) {
		LOG_INF("Fan speed OFF");
		return 0;
	}

	/* Break-before-make prevents two motor speed taps overlapping. */
	k_msleep(RELAY_BREAK_TIME_MS);
	err = gpio_pin_set_dt(&speed_relays[speed - 1U], 1);
	if (err != 0) {
		LOG_ERR("Failed to turn speed relay %u on (err %d)",
			(unsigned int)speed, err);
		return err;
	}

	current_state.speed = speed;
	last_nonzero_speed = speed;
	LOG_INF("Fan speed %u ON", (unsigned int)speed);
	return 0;
}

int fan_set_oscillation(bool oscillating)
{
	int err;

	if (current_state.oscillating == oscillating) {
		return 0;
	}

	err = gpio_pin_set_dt(&oscillation_relay, oscillating ? 1 : 0);
	if (err != 0) {
		LOG_ERR("Failed to control oscillation relay (err %d)", err);
		return err;
	}

	current_state.oscillating = oscillating;
	LOG_INF("Oscillation %s", oscillating ? "ON" : "OFF");
	return 0;
}

int fan_turn_on(void)
{
	return fan_set_speed(last_nonzero_speed);
}

int fan_turn_off(void)
{
	int speed_err;
	int oscillation_err;

	speed_err = fan_set_speed(0U);
	oscillation_err = fan_set_oscillation(false);

	return speed_err != 0 ? speed_err : oscillation_err;
}

struct fan_state fan_get_state(void)
{
	return current_state;
}
