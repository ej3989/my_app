/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bt_practice, LOG_LEVEL_INF);

#define SPEED_1_RELAY_NODE DT_ALIAS(fan_speed_1_relay)
#define SPEED_2_RELAY_NODE DT_ALIAS(fan_speed_2_relay)
#define SPEED_3_RELAY_NODE DT_ALIAS(fan_speed_3_relay)
#define OSC_RELAY_NODE     DT_ALIAS(fan_osc_relay)

#define FAN_SPEED_OFF 0U
#define FAN_SPEED_MAX 3U
#define RELAY_BREAK_TIME K_MSEC(100)

#define FAN_COMMAND_SET_SPEED 0x01U
#define FAN_COMMAND_SET_OSC   0x02U
#define FAN_COMMAND_POWER_OFF 0x03U

#define BT_UUID_FAN_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x9f1d1000, 0x3d2f, 0x4f3a, 0x8b11, 0x123456789abc)
#define BT_UUID_FAN_COMMAND_VAL \
	BT_UUID_128_ENCODE(0x9f1d1001, 0x3d2f, 0x4f3a, 0x8b11, 0x123456789abc)
#define BT_UUID_FAN_STATE_VAL \
	BT_UUID_128_ENCODE(0x9f1d1002, 0x3d2f, 0x4f3a, 0x8b11, 0x123456789abc)

#define BT_UUID_FAN_SERVICE BT_UUID_DECLARE_128(BT_UUID_FAN_SERVICE_VAL)
#define BT_UUID_FAN_COMMAND BT_UUID_DECLARE_128(BT_UUID_FAN_COMMAND_VAL)
#define BT_UUID_FAN_STATE   BT_UUID_DECLARE_128(BT_UUID_FAN_STATE_VAL)

static const struct gpio_dt_spec speed_relays[] = {
	GPIO_DT_SPEC_GET(SPEED_1_RELAY_NODE, gpios),
	GPIO_DT_SPEC_GET(SPEED_2_RELAY_NODE, gpios),
	GPIO_DT_SPEC_GET(SPEED_3_RELAY_NODE, gpios),
};
static const struct gpio_dt_spec oscillation_relay =
	GPIO_DT_SPEC_GET(OSC_RELAY_NODE, gpios);

/* State packet: byte 0 = speed (0..3), byte 1 = oscillation (0/1). */
static uint8_t fan_state[2];
static uint8_t requested_speed;
static bool requested_oscillation;
static int64_t speed_enable_at;

K_MUTEX_DEFINE(fan_lock);
static struct k_work_delayable fan_control_work;
static struct k_work advertising_work;

static int notify_fan_state(void);
static int start_advertising(void);

static const struct bt_data advertising_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_FAN_SERVICE_VAL),
};

static const struct bt_data scan_response_data[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE,
		CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static int set_all_speed_relays_off(void)
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

static int set_speed_relay_on(uint8_t speed)
{
	int err;

	if ((speed < 1U) || (speed > FAN_SPEED_MAX)) {
		return -EINVAL;
	}

	err = gpio_pin_set_dt(&speed_relays[speed - 1U], 1);
	if (err != 0) {
		LOG_ERR("Failed to turn speed relay %u on (err %d)",
			(unsigned int)speed, err);
		return err;
	}

	return 0;
}

static void fan_control_work_handler(struct k_work *work)
{
	uint8_t target_speed;
	bool target_oscillation;
	bool state_changed = false;
	int64_t now;
	int err;

	ARG_UNUSED(work);

	k_mutex_lock(&fan_lock, K_FOREVER);
	target_speed = requested_speed;
	target_oscillation = requested_oscillation;

	if (fan_state[1] != (uint8_t)target_oscillation) {
		err = gpio_pin_set_dt(&oscillation_relay, target_oscillation ? 1 : 0);
		if (err == 0) {
			fan_state[1] = (uint8_t)target_oscillation;
			state_changed = true;
			LOG_INF("Oscillation %s", target_oscillation ? "ON" : "OFF");
		} else {
			LOG_ERR("Failed to control oscillation relay (err %d)", err);
		}
	}

	/* A request for OFF cancels a speed relay waiting to be enabled. */
	if (target_speed == FAN_SPEED_OFF) {
		speed_enable_at = 0;
		if (fan_state[0] != FAN_SPEED_OFF) {
			err = set_all_speed_relays_off();
			if (err == 0) {
				fan_state[0] = FAN_SPEED_OFF;
				state_changed = true;
				LOG_INF("Fan speed OFF");
			}
		}
		goto done;
	}

	if ((fan_state[0] == target_speed) && (speed_enable_at == 0)) {
		goto done;
	}

	now = k_uptime_get();
	if (speed_enable_at == 0) {
		err = set_all_speed_relays_off();
		if (err != 0) {
			goto done;
		}

		fan_state[0] = FAN_SPEED_OFF;
		speed_enable_at = now + 100;
		(void)k_work_reschedule(&fan_control_work, RELAY_BREAK_TIME);
		LOG_INF("All speed relays OFF; selecting speed %u after 100 ms",
			(unsigned int)target_speed);
		goto done;
	}

	if (now < speed_enable_at) {
		(void)k_work_reschedule(&fan_control_work,
					K_MSEC(speed_enable_at - now));
		goto done;
	}

	/* Use the newest request after the break-before-make interval. */
	target_speed = requested_speed;
	speed_enable_at = 0;
	if (target_speed != FAN_SPEED_OFF) {
		err = set_speed_relay_on(target_speed);
		if (err == 0) {
			fan_state[0] = target_speed;
			state_changed = true;
			LOG_INF("Fan speed %u ON", (unsigned int)target_speed);
		}
	}

done:
	k_mutex_unlock(&fan_lock);

	if (state_changed) {
		err = notify_fan_state();
		if (err != 0) {
			LOG_WRN("Fan state notification failed (err %d)", err);
		}
	}
}

static ssize_t read_fan_state(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr,
			      void *buf, uint16_t len, uint16_t offset)
{
	uint8_t state[sizeof(fan_state)];

	k_mutex_lock(&fan_lock, K_FOREVER);
	memcpy(state, fan_state, sizeof(state));
	k_mutex_unlock(&fan_lock);

	LOG_INF("GATT read: speed %u, oscillation %u",
		(unsigned int)state[0], (unsigned int)state[1]);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 state, sizeof(state));
}

static ssize_t write_fan_command(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr,
				 const void *buf, uint16_t len,
				 uint16_t offset, uint8_t flags)
{
	const uint8_t *command = buf;

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len != 2U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	k_mutex_lock(&fan_lock, K_FOREVER);
	if (command[0] == FAN_COMMAND_SET_SPEED) {
		if (command[1] > FAN_SPEED_MAX) {
			k_mutex_unlock(&fan_lock);
			return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
		}
		requested_speed = command[1];
	} else if (command[0] == FAN_COMMAND_SET_OSC) {
		if (command[1] > 1U) {
			k_mutex_unlock(&fan_lock);
			return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
		}
		requested_oscillation = command[1] != 0U;
	} else if (command[0] == FAN_COMMAND_POWER_OFF) {
		if (command[1] != 0U) {
			k_mutex_unlock(&fan_lock);
			return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
		}
		requested_speed = FAN_SPEED_OFF;
		requested_oscillation = false;
	} else {
		k_mutex_unlock(&fan_lock);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	k_mutex_unlock(&fan_lock);

	LOG_INF("GATT command: 0x%02x, value %u",
		(unsigned int)command[0], (unsigned int)command[1]);
	(void)k_work_reschedule(&fan_control_work, K_NO_WAIT);
	return len;
}

static void fan_state_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	LOG_INF("Fan state notifications %s",
		value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(fan_service,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_FAN_SERVICE),
	BT_GATT_CHARACTERISTIC(BT_UUID_FAN_COMMAND,
			       BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE,
			       NULL, write_fan_command, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_FAN_STATE,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_fan_state, NULL, NULL),
	BT_GATT_CCC(fan_state_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static int notify_fan_state(void)
{
	const struct bt_gatt_attr *value_attr = &fan_service.attrs[4];
	uint8_t state[sizeof(fan_state)];

	k_mutex_lock(&fan_lock, K_FOREVER);
	memcpy(state, fan_state, sizeof(state));
	k_mutex_unlock(&fan_lock);

	return bt_gatt_notify(NULL, value_attr, state, sizeof(state));
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char address[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), address, sizeof(address));

	if (err != 0U) {
		LOG_ERR("Connection to %s failed (err 0x%02x)", address, err);
		return;
	}

	LOG_INF("Connected: %s", address);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char address[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), address, sizeof(address));
	LOG_INF("Disconnected: %s (reason 0x%02x)", address, reason);

	err = k_work_submit(&advertising_work);
	if (err < 0) {
		LOG_ERR("Failed to schedule advertising restart (err %d)", err);
	}
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static int start_advertising(void)
{
	int err;

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
			      advertising_data, ARRAY_SIZE(advertising_data),
			      scan_response_data, ARRAY_SIZE(scan_response_data));
	if (err != 0) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return err;
	}

	LOG_INF("Advertising as '%s'", CONFIG_BT_DEVICE_NAME);
	return 0;
}

static void advertising_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_INF("Restarting advertising");
	(void)start_advertising();
}

static int initialize_relays(void)
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

	LOG_INF("All active-high relays initialized OFF");
	return 0;
}

int main(void)
{
	int err;

	LOG_INF("Bluetooth fan controller start");

	k_work_init_delayable(&fan_control_work, fan_control_work_handler);
	k_work_init(&advertising_work, advertising_work_handler);

	err = initialize_relays();
	if (err != 0) {
		return err;
	}

	err = bt_enable(NULL);
	if (err != 0) {
		LOG_ERR("Bluetooth initialization failed (err %d)", err);
		return err;
	}

	LOG_INF("Bluetooth initialized");

	err = start_advertising();
	if (err != 0) {
		return err;
	}

	return 0;
}
