/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bt_practice, LOG_LEVEL_INF);

#define LED_STRIP_NODE DT_ALIAS(led_strip)
#define LED_BRIGHTNESS 20U

#define BT_UUID_LED_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x9f1d1000, 0x3d2f, 0x4f3a, 0x8b11, 0x123456789abc)
#define BT_UUID_LED_CONTROL_VAL \
	BT_UUID_128_ENCODE(0x9f1d1001, 0x3d2f, 0x4f3a, 0x8b11, 0x123456789abc)

#define BT_UUID_LED_SERVICE BT_UUID_DECLARE_128(BT_UUID_LED_SERVICE_VAL)
#define BT_UUID_LED_CONTROL BT_UUID_DECLARE_128(BT_UUID_LED_CONTROL_VAL)

static const struct device *const led_strip = DEVICE_DT_GET(LED_STRIP_NODE);
static struct led_rgb led_pixel;
static uint8_t led_state;

static int notify_led_state(struct bt_conn *conn);

static const struct bt_data advertising_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LED_SERVICE_VAL),
};

static const struct bt_data scan_response_data[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE,
		CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static int set_led_state(uint8_t state)
{
	int err;

	led_pixel.r = 0U;
	led_pixel.g = 0U;
	led_pixel.b = state != 0U ? LED_BRIGHTNESS : 0U;

	err = led_strip_update_rgb(led_strip, &led_pixel, 1U);
	if (err != 0) {
		LOG_ERR("Failed to update RGB LED (err %d)", err);
		return err;
	}

	led_state = state != 0U ? 1U : 0U;
	LOG_INF("LED %s", led_state != 0U ? "ON" : "OFF");
	return 0;
}

static ssize_t read_led_state(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr,
			      void *buf, uint16_t len, uint16_t offset)
{
	ARG_UNUSED(attr);

	LOG_INF("GATT read: LED state %u", led_state);

	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &led_state, sizeof(led_state));
}

static ssize_t write_led_state(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr,
			       const void *buf, uint16_t len,
			       uint16_t offset, uint8_t flags)
{
	const uint8_t *value = buf;
	int err;

	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len != sizeof(led_state)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	if (value[0] > 1U) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	LOG_INF("GATT write: %u", value[0]);

	err = set_led_state(value[0]);
	if (err != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	err = notify_led_state(conn);
	if (err != 0) {
		LOG_WRN("LED state notification failed (err %d)", err);
	}

	return len;
}

static void led_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	LOG_INF("LED notifications %s",
		value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(led_service,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_LED_SERVICE),
	BT_GATT_CHARACTERISTIC(BT_UUID_LED_CONTROL,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_led_state, write_led_state, NULL),
	BT_GATT_CCC(led_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static int notify_led_state(struct bt_conn *conn)
{
	const struct bt_gatt_attr *value_attr = &led_service.attrs[2];
	int err;

	if (!bt_gatt_is_subscribed(conn, value_attr, BT_GATT_CCC_NOTIFY)) {
		LOG_INF("Notification skipped: client is not subscribed");
		return 0;
	}

	err = bt_gatt_notify(conn, value_attr, &led_state, sizeof(led_state));
	if (err != 0) {
		return err;
	}

	LOG_INF("Notification sent: %u", led_state);
	return 0;
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

	bt_addr_le_to_str(bt_conn_get_dst(conn), address, sizeof(address));
	LOG_INF("Disconnected: %s (reason 0x%02x)", address, reason);
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

int main(void)
{
	int err;

	LOG_INF("Bluetooth practice start");

	if (!device_is_ready(led_strip)) {
		LOG_ERR("RGB LED device is not ready");
		return -ENODEV;
	}

	err = set_led_state(0U);
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
