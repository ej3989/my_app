/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#define SW0_NODE DT_ALIAS(sw0)
#define STRIP_NODE DT_ALIAS(led_strip)

constexpr int kLedCount = 1;
constexpr int kButtonDebounceMs = 30;
constexpr int64_t kLongPressMs = 1000;
constexpr int kBleAdvertisingWindowMs = 60000;

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);
static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

static const struct bt_data advertising_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

static const struct bt_data scan_response_data[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

enum class Mode {
	Auto,
	Manual,
};

struct Color {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

static constexpr Color kColors[] = {
	{20, 0, 0},
	{0, 20, 0},
	{0, 0, 20},
	{20, 20, 20},
};

static const char *mode_to_string(Mode mode)
{
	switch (mode) {
	case Mode::Auto:
		return "auto";
	case Mode::Manual:
		return "manual";
	default:
		return "unknown";
	}
}

class RgbBlinker {
public:
	explicit RgbBlinker(const device *strip) : strip_(strip)
	{
	}

	int init()
	{
		if (!device_is_ready(strip_)) {
			printf("LED strip device is not ready\n");
			return -ENODEV;
		}

		printf("LED strip device: %s\n", strip_->name);
		return off();
	}

	int set(uint8_t red, uint8_t green, uint8_t blue)
	{
		int ret = set_raw(red, green, blue);
		if (ret < 0) {
			return ret;
		}

		on_ = red != 0 || green != 0 || blue != 0;
		return 0;
	}

	int off()
	{
		return set(0, 0, 0);
	}

	int toggle()
	{
		if (on_) {
			return off();
		}

		return set(20, 0, 0);
	}

	bool is_on() const
	{
		return on_;
	}

	Color current_color() const
	{
		return current_color_;
	}

private:
	int set_raw(uint8_t red, uint8_t green, uint8_t blue)
	{
		pixels_[0] = {
			.r = red,
			.g = green,
			.b = blue,
		};

		int ret = led_strip_update_rgb(strip_, pixels_, ARRAY_SIZE(pixels_));
		if (ret < 0) {
			printf("Failed to update LED strip: %d\n", ret);
			return ret;
		}

		current_color_ = {
			.red = red,
			.green = green,
			.blue = blue,
		};

		return 0;
	}

	const device *strip_;
	led_rgb pixels_[kLedCount] = {};
	Color current_color_ = {};
	bool on_ = false;
};

static RgbBlinker blinker(strip);
static Mode mode = Mode::Auto;
static size_t color_index;
static int64_t pressed_at_ms;

class ButtonWatcher {
public:
	using Handler = void (*)(int state);

	explicit ButtonWatcher(const gpio_dt_spec *button) : button_(button)
	{
	}

	int start(Handler handler)
	{
		handler_ = handler;

		if (!gpio_is_ready_dt(button_)) {
			printf("Button device is not ready\n");
			return -ENODEV;
		}

		int ret = gpio_pin_configure_dt(button_, GPIO_INPUT);
		if (ret < 0) {
			printf("Failed to configure button: %d\n", ret);
			return ret;
		}

		last_state_ = read();
		if (last_state_ < 0) {
			printf("Failed to read initial button state: %d\n", last_state_);
			return last_state_;
		}

		k_work_init_delayable(&debounce_work_, ButtonWatcher::debounce_entry);
		gpio_init_callback(&callback_, ButtonWatcher::callback_entry, BIT(button_->pin));

		ret = gpio_add_callback(button_->port, &callback_);
		if (ret < 0) {
			printf("Failed to add button callback: %d\n", ret);
			return ret;
		}

		ret = gpio_pin_interrupt_configure_dt(button_, GPIO_INT_EDGE_BOTH);
		if (ret < 0) {
			printf("Failed to configure button interrupt: %d\n", ret);
			return ret;
		}

		return 0;
	}

	int read() const
	{
		return gpio_pin_get_dt(button_);
	}

private:
	static void callback_entry(const device *dev, gpio_callback *cb, uint32_t pins)
	{
		auto *self = CONTAINER_OF(cb, ButtonWatcher, callback_);

		self->schedule_debounce();
	}

	static void debounce_entry(k_work *work)
	{
		auto *delayable = k_work_delayable_from_work(work);
		auto *self = CONTAINER_OF(delayable, ButtonWatcher, debounce_work_);

		self->handle_debounced();
	}

	void schedule_debounce()
	{
		(void)k_work_reschedule(&debounce_work_, K_MSEC(kButtonDebounceMs));
	}

	void handle_debounced()
	{
		int state = read();

		if (state < 0) {
			printf("Failed to read button: %d\n", state);
			return;
		}

		if (state == last_state_) {
			return;
		}

		last_state_ = state;
		printf("Button changed: %s\n", state ? "pressed" : "released");

		if (handler_ != nullptr) {
			handler_(state);
		}
	}

	const gpio_dt_spec *button_;
	gpio_callback callback_ = {};
	k_work_delayable debounce_work_ = {};
	Handler handler_ = nullptr;
	int last_state_ = -1;
};

static ButtonWatcher watcher(&button);

static struct bt_uuid_128 rgb_service_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x9f1d0000, 0x3d2f, 0x4f3a, 0x8b11, 0x123456789abc));

static struct bt_uuid_128 rgb_color_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x9f1d0001, 0x3d2f, 0x4f3a, 0x8b11, 0x123456789abc));

class BluetoothRgbPeripheral {
public:
	explicit BluetoothRgbPeripheral(RgbBlinker *blinker) : blinker_(blinker)
	{
		instance_ = this;
	}

	int start()
	{
		k_work_init_delayable(&advertising_timeout_work_,
				      BluetoothRgbPeripheral::advertising_timeout_entry);

		int ret = bt_enable(nullptr);
		if (ret < 0) {
			printf("Failed to initialize Bluetooth: %d\n", ret);
			return ret;
		}

		printf("Bluetooth initialized\n");
		printf("Bluetooth advertising is closed. Long press the button to open it for %d seconds.\n",
		       kBleAdvertisingWindowMs / 1000);
		return 0;
	}

	void open_advertising_window()
	{
		advertising_window_open_ = true;

		int ret = start_advertising();
		if (ret < 0 && ret != -EALREADY) {
			printf("Failed to open Bluetooth advertising window: %d\n", ret);
			return;
		}

		(void)k_work_reschedule(&advertising_timeout_work_,
					K_MSEC(kBleAdvertisingWindowMs));

		printf("Bluetooth advertising window open for %d seconds\n",
		       kBleAdvertisingWindowMs / 1000);
	}

	static void connected_entry(bt_conn *conn, uint8_t err)
	{
		ARG_UNUSED(conn);

		if (err != 0) {
			printf("Bluetooth connection failed: 0x%02x\n", err);
			return;
		}

		if (instance_ != nullptr) {
			instance_->advertising_active_ = false;
		}

		printf("Bluetooth connected\n");
	}

	static void disconnected_entry(bt_conn *conn, uint8_t reason)
	{
		ARG_UNUSED(conn);

		printf("Bluetooth disconnected: 0x%02x\n", reason);
	}

	static void recycled_entry()
	{
		if (instance_ != nullptr) {
			instance_->handle_recycled();
		}
	}

	static ssize_t read_entry(bt_conn *conn,
				  const bt_gatt_attr *attr,
				  void *buf,
				  uint16_t len,
				  uint16_t offset)
	{
		auto *self = static_cast<BluetoothRgbPeripheral *>(attr->user_data);

		return self->read(conn, attr, buf, len, offset);
	}

	static ssize_t write_entry(bt_conn *conn,
				   const bt_gatt_attr *attr,
				   const void *buf,
				   uint16_t len,
				   uint16_t offset,
				   uint8_t flags)
	{
		auto *self = static_cast<BluetoothRgbPeripheral *>(attr->user_data);

		return self->write(conn, attr, buf, len, offset, flags);
	}

private:
	static void advertising_timeout_entry(k_work *work)
	{
		auto *delayable = k_work_delayable_from_work(work);
		auto *self = CONTAINER_OF(delayable,
					  BluetoothRgbPeripheral,
					  advertising_timeout_work_);

		self->close_advertising_window();
	}

	int start_advertising()
	{
		int ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
				      advertising_data,
				      ARRAY_SIZE(advertising_data),
				      scan_response_data,
				      ARRAY_SIZE(scan_response_data));
		if (ret < 0) {
			if (ret == -EALREADY) {
				advertising_active_ = true;
				return ret;
			}

			printf("Failed to start Bluetooth advertising: %d\n", ret);
			return ret;
		}

		advertising_active_ = true;
		printf("Bluetooth advertising as \"%s\"\n", CONFIG_BT_DEVICE_NAME);
		return 0;
	}

	void stop_advertising()
	{
		if (!advertising_active_) {
			return;
		}

		int ret = bt_le_adv_stop();
		if (ret < 0) {
			printf("Failed to stop Bluetooth advertising: %d\n", ret);
			return;
		}

		advertising_active_ = false;
		printf("Bluetooth advertising stopped\n");
	}

	void close_advertising_window()
	{
		advertising_window_open_ = false;
		stop_advertising();
		printf("Bluetooth advertising window closed\n");
	}

	void handle_recycled()
	{
		if (!advertising_window_open_) {
			return;
		}

		int ret = start_advertising();
		if (ret < 0 && ret != -EALREADY) {
			printf("Failed to restart Bluetooth advertising: %d\n", ret);
		}
	}

	ssize_t read(bt_conn *conn,
		     const bt_gatt_attr *attr,
		     void *buf,
		     uint16_t len,
		     uint16_t offset)
	{
		const Color color = blinker_->current_color();
		char text[16];
		int written = snprintf(text,
				       sizeof(text),
				       "%u,%u,%u",
				       color.red,
				       color.green,
				       color.blue);

		return bt_gatt_attr_read(conn, attr, buf, len, offset, text, written);
	}

	ssize_t write(bt_conn *conn,
		      const bt_gatt_attr *attr,
		      const void *buf,
		      uint16_t len,
		      uint16_t offset,
		      uint8_t flags)
	{
		ARG_UNUSED(conn);
		ARG_UNUSED(attr);
		ARG_UNUSED(flags);

		if (offset != 0) {
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
		}

		uint8_t red;
		uint8_t green;
		uint8_t blue;

		if (len == 3) {
			const auto *bytes = static_cast<const uint8_t *>(buf);
			red = bytes[0];
			green = bytes[1];
			blue = bytes[2];
		} else {
			if (len >= 16) {
				return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
			}

			char text[16];
			memcpy(text, buf, len);
			text[len] = '\0';

			if (parse_rgb_text(text, &red, &green, &blue) < 0) {
				return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
			}
		}

		int ret = blinker_->set(red, green, blue);
		if (ret < 0) {
			return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
		}

		mode = Mode::Manual;
		printf("BLE RGB write: %u, %u, %u\n", red, green, blue);
		return len;
	}

	static int parse_rgb_text(const char *text, uint8_t *red, uint8_t *green, uint8_t *blue)
	{
		char *end = nullptr;
		long values[3];
		const char *cursor = text;

		for (size_t i = 0; i < ARRAY_SIZE(values); ++i) {
			while (*cursor == ' ' || *cursor == ',') {
				++cursor;
			}

			values[i] = strtol(cursor, &end, 10);
			if (end == cursor || values[i] < 0 || values[i] > 255) {
				return -EINVAL;
			}

			cursor = end;
		}

		while (*cursor == ' ' || *cursor == ',') {
			++cursor;
		}

		if (*cursor != '\0') {
			return -EINVAL;
		}

		*red = static_cast<uint8_t>(values[0]);
		*green = static_cast<uint8_t>(values[1]);
		*blue = static_cast<uint8_t>(values[2]);
		return 0;
	}

	RgbBlinker *blinker_;
	k_work_delayable advertising_timeout_work_ = {};
	bool advertising_window_open_ = false;
	bool advertising_active_ = false;

	static BluetoothRgbPeripheral *instance_;
};

BluetoothRgbPeripheral *BluetoothRgbPeripheral::instance_ = nullptr;

static BluetoothRgbPeripheral bluetooth(&blinker);

BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = BluetoothRgbPeripheral::connected_entry,
	.disconnected = BluetoothRgbPeripheral::disconnected_entry,
	.recycled = BluetoothRgbPeripheral::recycled_entry,
};

BT_GATT_SERVICE_DEFINE(rgb_service,
	BT_GATT_PRIMARY_SERVICE(&rgb_service_uuid),
	BT_GATT_CHARACTERISTIC(&rgb_color_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       BluetoothRgbPeripheral::read_entry,
			       BluetoothRgbPeripheral::write_entry,
			       &bluetooth),
);

static void on_button_changed(int state)
{
	if (state) {
		pressed_at_ms = k_uptime_get();
		return;
	}

	int64_t held_ms = k_uptime_get() - pressed_at_ms;

	if (held_ms >= kLongPressMs) {
		color_index = 0;
		(void)blinker.off();
		bluetooth.open_advertising_window();
		printf("Long press: LED off and Bluetooth advertising open\n");
		return;
	}

	if (mode != Mode::Auto) {
		return;
	}

	const Color color = kColors[color_index];
	(void)blinker.set(color.red, color.green, color.blue);
	printf("Short press: RGB = %u, %u, %u\n", color.red, color.green, color.blue);

	color_index = (color_index + 1) % ARRAY_SIZE(kColors);
}

static int parse_color_arg(const struct shell *shell,
			   const char *text,
			   const char *name,
			   uint8_t *out)
{
	char *end = nullptr;
	long value = strtol(text, &end, 10);

	if (*end != '\0') {
		shell_error(shell, "%s must be a number", name);
		return -EINVAL;
	}

	if (value < 0 || value > 255) {
		shell_error(shell, "%s must be 0..255", name);
		return -EINVAL;
	}

	*out = static_cast<uint8_t>(value);
	return 0;
}

static int cmd_rgb(const struct shell *shell, size_t argc, char **argv)
{
	if (argc != 4) {
		shell_error(shell, "usage: rgb <red> <green> <blue>");
		return -EINVAL;
	}

	uint8_t red;
	uint8_t green;
	uint8_t blue;

	int ret = parse_color_arg(shell, argv[1], "red", &red);
	if (ret < 0) {
		return ret;
	}

	ret = parse_color_arg(shell, argv[2], "green", &green);
	if (ret < 0) {
		return ret;
	}

	ret = parse_color_arg(shell, argv[3], "blue", &blue);
	if (ret < 0) {
		return ret;
	}

	ret = blinker.set(red, green, blue);
	if (ret < 0) {
		shell_error(shell, "failed to update LED: %d", ret);
		return ret;
	}

	shell_print(shell, "RGB = %u, %u, %u", red, green, blue);
	return 0;
}

static int cmd_mode(const struct shell *shell, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_error(shell, "usage: mode <auto|manual>");
		return -EINVAL;
	}

	if (strcmp(argv[1], "auto") == 0) {
		mode = Mode::Auto;
		shell_print(shell, "mode = %s", mode_to_string(mode));
		return 0;
	}

	if (strcmp(argv[1], "manual") == 0) {
		mode = Mode::Manual;
		shell_print(shell, "mode = %s", mode_to_string(mode));
		return 0;
	}

	shell_error(shell, "mode must be auto or manual");
	return -EINVAL;
}

static int cmd_status(const struct shell *shell, size_t argc, char **argv)
{
	int state = watcher.read();

	if (state < 0) {
		shell_error(shell, "failed to read button: %d", state);
		return state;
	}

	shell_print(shell, "button = %s", state ? "pressed" : "released");
	shell_print(shell, "mode = %s", mode_to_string(mode));
	shell_print(shell, "led = %s", blinker.is_on() ? "on" : "off");
	shell_print(shell, "next color index = %u", color_index);

	return 0;
}

SHELL_CMD_REGISTER(rgb, NULL, "Set RGB LED: rgb <red> <green> <blue>", cmd_rgb);
SHELL_CMD_REGISTER(mode, NULL, "Set mode: mode <auto|manual>", cmd_mode);
SHELL_CMD_REGISTER(status, NULL, "Show app status", cmd_status);

int main()
{
	int ret = blinker.init();
	if (ret < 0) {
		return 0;
	}

	ret = watcher.start(on_button_changed);
	if (ret < 0) {
		return 0;
	}

	ret = bluetooth.start();
	if (ret < 0) {
		return 0;
	}

	printf("RGB interrupt shell example started\n");
	printf("Try: rgb 20 0 0\n");
	printf("Try: mode auto\n");
	printf("Try: status\n");

	return 0;
}
