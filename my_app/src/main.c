/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
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

#define LED_COUNT 1
#define BUTTON_DEBOUNCE_MS 30
#define LONG_PRESS_MS 1000
#define BLE_ADVERTISING_WINDOW_MS 60000

struct color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

enum mode {
    MODE_AUTO,
    MODE_MANUAL,
};

struct rgb_blinker {
    const struct device *strip;
    struct led_rgb pixels[LED_COUNT];
    struct color current_color;
    bool on;
};

struct button_watcher {
    const struct gpio_dt_spec *button;
    struct gpio_callback callback;
    struct k_work_delayable debounce_work;
    void (*handler)(int state);
    int last_state;
};

struct bluetooth_rgb_peripheral {
    struct rgb_blinker *blinker;
    struct k_work_delayable advertising_timeout_work;
    bool advertising_window_open;
    bool advertising_active;
};

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);
static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

static const struct bt_data advertising_data[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

static const struct bt_data scan_response_data[] = {
        BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct color colors[] = {
        {20, 0, 0},
        {0, 20, 0},
        {0, 0, 20},
        {20, 20, 20},
};

static struct rgb_blinker blinker = {
        .strip = strip,
};
static struct button_watcher watcher = {
        .button = &button,
        .last_state = -1,
};
static struct bluetooth_rgb_peripheral bluetooth = {
        .blinker = &blinker,
};
static struct bluetooth_rgb_peripheral *bluetooth_instance;
static enum mode mode = MODE_AUTO;
static size_t color_index;
static int64_t pressed_at_ms;

static const char *mode_to_string(enum mode value) {
    switch (value) {
    case MODE_AUTO:
        return "auto";
    case MODE_MANUAL:
        return "manual";
    default:
        return "unknown";
    }
}

static int rgb_blinker_set_raw(struct rgb_blinker *self, uint8_t red, uint8_t green, uint8_t blue) {
    int ret;

    self->pixels[0].r = red;
    self->pixels[0].g = green;
    self->pixels[0].b = blue;

    ret = led_strip_update_rgb(self->strip, self->pixels, ARRAY_SIZE(self->pixels));
    if (ret < 0) {
        printf("Failed to update LED strip: %d\n", ret);
        return ret;
    }

    self->current_color.red = red;
    self->current_color.green = green;
    self->current_color.blue = blue;

    return 0;
}

static int rgb_blinker_set(struct rgb_blinker *self, uint8_t red, uint8_t green, uint8_t blue) {
    int ret = rgb_blinker_set_raw(self, red, green, blue);

    if (ret < 0) {
        return ret;
    }

    self->on = red != 0 || green != 0 || blue != 0;
    return 0;
}

static int rgb_blinker_off(struct rgb_blinker *self) { return rgb_blinker_set(self, 0, 0, 0); }

static int rgb_blinker_init(struct rgb_blinker *self) {
    if (!device_is_ready(self->strip)) {
        printf("LED strip device is not ready\n");
        return -ENODEV;
    }

    printf("LED strip device: %s\n", self->strip->name);
    return rgb_blinker_off(self);
}

static int button_watcher_read(const struct button_watcher *self) {
    return gpio_pin_get_dt(self->button);
}

static void button_watcher_schedule_debounce(struct button_watcher *self) {
    (void)k_work_reschedule(&self->debounce_work, K_MSEC(BUTTON_DEBOUNCE_MS));
}

static void button_watcher_handle_debounced(struct button_watcher *self) {
    int state = button_watcher_read(self);

    if (state < 0) {
        printf("Failed to read button: %d\n", state);
        return;
    }

    if (state == self->last_state) {
        return;
    }

    self->last_state = state;
    printf("Button changed: %s\n", state ? "pressed" : "released");

    if (self->handler != NULL) {
        self->handler(state);
    }
}

static void button_watcher_callback_entry(const struct device *dev, struct gpio_callback *cb,
                                          uint32_t pins) {
    struct button_watcher *self = CONTAINER_OF(cb, struct button_watcher, callback);

    ARG_UNUSED(dev);
    ARG_UNUSED(pins);

    button_watcher_schedule_debounce(self);
}

static void button_watcher_debounce_entry(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct button_watcher *self = CONTAINER_OF(delayable, struct button_watcher, debounce_work);

    button_watcher_handle_debounced(self);
}

static int button_watcher_start(struct button_watcher *self, void (*handler)(int state)) {
    int ret;

    self->handler = handler;

    if (!gpio_is_ready_dt(self->button)) {
        printf("Button device is not ready\n");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(self->button, GPIO_INPUT);
    if (ret < 0) {
        printf("Failed to configure button: %d\n", ret);
        return ret;
    }

    self->last_state = button_watcher_read(self);
    if (self->last_state < 0) {
        printf("Failed to read initial button state: %d\n", self->last_state);
        return self->last_state;
    }

    k_work_init_delayable(&self->debounce_work, button_watcher_debounce_entry);
    gpio_init_callback(&self->callback, button_watcher_callback_entry, BIT(self->button->pin));

    ret = gpio_add_callback(self->button->port, &self->callback);
    if (ret < 0) {
        printf("Failed to add button callback: %d\n", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(self->button, GPIO_INT_EDGE_BOTH);
    if (ret < 0) {
        printf("Failed to configure button interrupt: %d\n", ret);
        return ret;
    }

    return 0;
}

static struct bt_uuid_128 rgb_service_uuid =
        BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x9f1d0000, 0x3d2f, 0x4f3a, 0x8b11, 0x123456789abc));

static struct bt_uuid_128 rgb_color_uuid =
        BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x9f1d0001, 0x3d2f, 0x4f3a, 0x8b11, 0x123456789abc));

static int parse_rgb_text(const char *text, uint8_t *red, uint8_t *green, uint8_t *blue) {
    char *end = NULL;
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

    *red = (uint8_t)values[0];
    *green = (uint8_t)values[1];
    *blue = (uint8_t)values[2];
    return 0;
}

static int bluetooth_start_advertising(struct bluetooth_rgb_peripheral *self) {
    int ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, advertising_data, ARRAY_SIZE(advertising_data),
                              scan_response_data, ARRAY_SIZE(scan_response_data));

    if (ret < 0) {
        if (ret == -EALREADY) {
            self->advertising_active = true;
            return ret;
        }

        printf("Failed to start Bluetooth advertising: %d\n", ret);
        return ret;
    }

    self->advertising_active = true;
    printf("Bluetooth advertising as \"%s\"\n", CONFIG_BT_DEVICE_NAME);
    return 0;
}

static void bluetooth_stop_advertising(struct bluetooth_rgb_peripheral *self) {
    int ret;

    if (!self->advertising_active) {
        return;
    }

    ret = bt_le_adv_stop();
    if (ret < 0) {
        printf("Failed to stop Bluetooth advertising: %d\n", ret);
        return;
    }

    self->advertising_active = false;
    printf("Bluetooth advertising stopped\n");
}

static void bluetooth_close_advertising_window(struct bluetooth_rgb_peripheral *self) {
    self->advertising_window_open = false;
    bluetooth_stop_advertising(self);
    printf("Bluetooth advertising window closed\n");
}

static void bluetooth_advertising_timeout_entry(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct bluetooth_rgb_peripheral *self =
            CONTAINER_OF(delayable, struct bluetooth_rgb_peripheral, advertising_timeout_work);

    bluetooth_close_advertising_window(self);
}

static void bluetooth_handle_recycled(struct bluetooth_rgb_peripheral *self) {
    int ret;

    if (!self->advertising_window_open) {
        return;
    }

    ret = bluetooth_start_advertising(self);
    if (ret < 0 && ret != -EALREADY) {
        printf("Failed to restart Bluetooth advertising: %d\n", ret);
    }
}

static int bluetooth_start(struct bluetooth_rgb_peripheral *self) {
    int ret;

    bluetooth_instance = self;
    k_work_init_delayable(&self->advertising_timeout_work, bluetooth_advertising_timeout_entry);

    ret = bt_enable(NULL);
    if (ret < 0) {
        printf("Failed to initialize Bluetooth: %d\n", ret);
        return ret;
    }

    printf("Bluetooth initialized\n");
    printf("Bluetooth advertising is closed. Long press the button to open it "
           "for %d seconds.\n",
           BLE_ADVERTISING_WINDOW_MS / 1000);
    return 0;
}

static void bluetooth_open_advertising_window(struct bluetooth_rgb_peripheral *self) {
    int ret;

    self->advertising_window_open = true;

    ret = bluetooth_start_advertising(self);
    if (ret < 0 && ret != -EALREADY) {
        printf("Failed to open Bluetooth advertising window: %d\n", ret);
        return;
    }

    (void)k_work_reschedule(&self->advertising_timeout_work, K_MSEC(BLE_ADVERTISING_WINDOW_MS));

    printf("Bluetooth advertising window open for %d seconds\n", BLE_ADVERTISING_WINDOW_MS / 1000);
}

static void connected_entry(struct bt_conn *conn, uint8_t err) {
    ARG_UNUSED(conn);

    if (err != 0) {
        printf("Bluetooth connection failed: 0x%02x\n", err);
        return;
    }

    if (bluetooth_instance != NULL) {
        bluetooth_instance->advertising_active = false;
    }

    printf("Bluetooth connected\n");
}

static void disconnected_entry(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(conn);

    printf("Bluetooth disconnected: 0x%02x\n", reason);
}

static void recycled_entry(void) {
    if (bluetooth_instance != NULL) {
        bluetooth_handle_recycled(bluetooth_instance);
    }
}

static ssize_t read_entry(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                          uint16_t len, uint16_t offset) {
    struct bluetooth_rgb_peripheral *self = attr->user_data;
    const struct color color = self->blinker->current_color;
    char text[16];
    int written = snprintf(text, sizeof(text), "%u,%u,%u", color.red, color.green, color.blue);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, text, written);
}

static ssize_t write_entry(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
                           uint16_t len, uint16_t offset, uint8_t flags) {
    struct bluetooth_rgb_peripheral *self = attr->user_data;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    int ret;

    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);

    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len == 3) {
        const uint8_t *bytes = buf;

        red = bytes[0];
        green = bytes[1];
        blue = bytes[2];
    } else {
        char text[16];

        if (len >= sizeof(text)) {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }

        memcpy(text, buf, len);
        text[len] = '\0';

        if (parse_rgb_text(text, &red, &green, &blue) < 0) {
            return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
        }
    }

    ret = rgb_blinker_set(self->blinker, red, green, blue);
    if (ret < 0) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    mode = MODE_MANUAL;
    printf("BLE RGB write: %u, %u, %u\n", red, green, blue);
    return len;
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
        .connected = connected_entry,
        .disconnected = disconnected_entry,
        .recycled = recycled_entry,
};

BT_GATT_SERVICE_DEFINE(rgb_service, BT_GATT_PRIMARY_SERVICE(&rgb_service_uuid),
                       BT_GATT_CHARACTERISTIC(&rgb_color_uuid.uuid,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
                                                      BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_entry,
                                              write_entry, &bluetooth), );

static void on_button_changed(int state) {
    const struct color color = colors[color_index];

    if (state) {
        pressed_at_ms = k_uptime_get();
        return;
    }

    int64_t held_ms = k_uptime_get() - pressed_at_ms;

    if (held_ms >= LONG_PRESS_MS) {
        color_index = 0;
        (void)rgb_blinker_off(&blinker);
        bluetooth_open_advertising_window(&bluetooth);
        printf("Long press: LED off and Bluetooth advertising open\n");
        return;
    }

    if (mode != MODE_AUTO) {
        return;
    }

    (void)rgb_blinker_set(&blinker, color.red, color.green, color.blue);
    printf("Short press: RGB = %u, %u, %u\n", color.red, color.green, color.blue);

    color_index = (color_index + 1) % ARRAY_SIZE(colors);
}

static int parse_color_arg(const struct shell *shell, const char *text, const char *name,
                           uint8_t *out) {
    char *end = NULL;
    long value = strtol(text, &end, 10);

    if (*end != '\0') {
        shell_error(shell, "%s must be a number", name);
        return -EINVAL;
    }

    if (value < 0 || value > 255) {
        shell_error(shell, "%s must be 0..255", name);
        return -EINVAL;
    }

    *out = (uint8_t)value;
    return 0;
}

static int cmd_rgb(const struct shell *shell, size_t argc, char **argv) {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    int ret;

    if (argc != 4) {
        shell_error(shell, "usage: rgb <red> <green> <blue>");
        return -EINVAL;
    }

    ret = parse_color_arg(shell, argv[1], "red", &red);
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

    ret = rgb_blinker_set(&blinker, red, green, blue);
    if (ret < 0) {
        shell_error(shell, "failed to update LED: %d", ret);
        return ret;
    }

    shell_print(shell, "RGB = %u, %u, %u", red, green, blue);
    return 0;
}

static int cmd_mode(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 2) {
        shell_error(shell, "usage: mode <auto|manual>");
        return -EINVAL;
    }

    if (strcmp(argv[1], "auto") == 0) {
        mode = MODE_AUTO;
        shell_print(shell, "mode = %s", mode_to_string(mode));
        return 0;
    }

    if (strcmp(argv[1], "manual") == 0) {
        mode = MODE_MANUAL;
        shell_print(shell, "mode = %s", mode_to_string(mode));
        return 0;
    }

    shell_error(shell, "mode must be auto or manual");
    return -EINVAL;
}

static int cmd_status(const struct shell *shell, size_t argc, char **argv) {
    int state;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    state = button_watcher_read(&watcher);
    if (state < 0) {
        shell_error(shell, "failed to read button: %d", state);
        return state;
    }

    shell_print(shell, "button = %s", state ? "pressed" : "released");
    shell_print(shell, "mode = %s", mode_to_string(mode));
    shell_print(shell, "led = %s", blinker.on ? "on" : "off");
    shell_print(shell, "next color index = %u", color_index);

    return 0;
}

SHELL_CMD_REGISTER(rgb, NULL, "Set RGB LED: rgb <red> <green> <blue>", cmd_rgb);
SHELL_CMD_REGISTER(mode, NULL, "Set mode: mode <auto|manual>", cmd_mode);
SHELL_CMD_REGISTER(status, NULL, "Show app status", cmd_status);

int main(void) {
    int ret = rgb_blinker_init(&blinker);

    if (ret < 0) {
        return 0;
    }

    ret = button_watcher_start(&watcher, on_button_changed);
    if (ret < 0) {
        return 0;
    }

    ret = bluetooth_start(&bluetooth);
    if (ret < 0) {
        return 0;
    }

    printf("RGB interrupt shell example started\n");
    printf("Try: rgb 20 0 0\n");
    printf("Try: mode auto\n");
    printf("Try: status\n");

    return 0;
}
