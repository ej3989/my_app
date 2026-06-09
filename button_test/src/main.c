#include <inttypes.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>

#define BUTTONS_NODE DT_ALIAS(app_buttons)
#define BUTTONS_DEV DEVICE_DT_GET(BUTTONS_NODE)
#define STRIP_NODE DT_ALIAS(led_strip)
#define STRIP_DEV DEVICE_DT_GET(STRIP_NODE)

static struct led_rgb pixels[4] = {
    { .r = 10, .g = 0, .b = 0 },
    { .r = 0 , .g = 10, .b = 0 },
    { .r = 0, .g = 0, .b = 10 },
    { .r = 10, .g = 10, .b = 10 }
};
#define RED_PIXEL_INDEX 0
#define GREEN_PIXEL_INDEX 1
#define BLUE_PIXEL_INDEX 2
#define WHITE_PIXEL_INDEX 3

static size_t current_pixel_index = 0;

static void button_input_cb(struct input_event *evt, void *user_data) {
    ARG_UNUSED(user_data);

    if (evt->sync == 0) {
        return;
    }

    if (evt->type != INPUT_EV_KEY) {
        return;
    }

    if (evt->code == INPUT_KEY_1 && evt->value == 1) {
        printk("INPUT_KEY_1 %s at %" PRIu32 "\n", evt->value ? "pressed" : "released",
               k_cycle_get_32());
        
        if(current_pixel_index == WHITE_PIXEL_INDEX) {
            current_pixel_index = 0;
        } else {
            current_pixel_index++;
        }
        led_strip_update_rgb(STRIP_DEV, &pixels[current_pixel_index], 1);
        

    } else {
        printk("key code %u %s\n", evt->code, evt->value ? "pressed" : "released");
    }
}

INPUT_CALLBACK_DEFINE(BUTTONS_DEV, button_input_cb, NULL);

int main(void) {
    if (!device_is_ready(BUTTONS_DEV)) {
        printk("Button input device is not ready\n");
        return 0;
    }
    if (!device_is_ready(STRIP_DEV)) {
        printk("LED strip device is not ready\n");
        return 0;
    }
    led_strip_update_rgb(STRIP_DEV, &pixels[RED_PIXEL_INDEX], 1);

    printk("Button input test started\n");

    k_sleep(K_FOREVER);
    return 0;
}
