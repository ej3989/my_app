#include <inttypes.h>
#include <stdio.h>

#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define BUTTONS_NODE DT_ALIAS(app_buttons)
#define BUTTONS_DEV DEVICE_DT_GET(BUTTONS_NODE)
#define STRIP_NODE DT_ALIAS(led_strip)
#define STRIP_DEV DEVICE_DT_GET(STRIP_NODE)
#define DISPLAY_NODE DT_CHOSEN(zephyr_display)
#define DISPLAY_DEV DEVICE_DT_GET(DISPLAY_NODE)
#define LCD_BL_NODE DT_NODELABEL(lcd_bl)
#define LCD_BL_DEV DEVICE_DT_GET(LCD_BL_NODE)

#define RED_PIXEL_INDEX 0
#define GREEN_PIXEL_INDEX 1
#define BLUE_PIXEL_INDEX 2
#define WHITE_PIXEL_INDEX 3
#define UI_THREAD_STACK_SIZE 8192
#define UI_THREAD_PRIORITY 5

static struct led_rgb pixels[4] = {
    { .r = 10, .g = 0, .b = 0 },
    { .r = 0, .g = 10, .b = 0 },
    { .r = 0, .g = 0, .b = 10 },
    { .r = 10, .g = 10, .b = 10 }
};

static atomic_t pending_key = ATOMIC_INIT(INPUT_KEY_1);
static struct k_thread ui_thread_data;
static K_THREAD_STACK_DEFINE(ui_thread_stack, UI_THREAD_STACK_SIZE);

extern const unsigned char nanum_gothic_regular_ttf[];
extern const unsigned int nanum_gothic_regular_ttf_len;

static void set_label_for_key(lv_obj_t *label, uint32_t key_code)
{
    const char *text = "버튼0";
    lv_color_t bg_color = lv_color_hex(0xcc2020);
    lv_color_t text_color = lv_color_white();

    switch (key_code) {
    case INPUT_KEY_1:
        text = "버튼0";
        bg_color = lv_color_hex(0xcc2020);
        break;
    case INPUT_KEY_2:
        text = "버튼1";
        bg_color = lv_color_hex(0x209c3a);
        break;
    case INPUT_KEY_3:
        text = "버튼2";
        bg_color = lv_color_hex(0x1d4ed8);
        break;
    case INPUT_KEY_4:
        text = "버튼3";
        bg_color = lv_color_black();
        break;
    default:
        return;
    }

    lv_obj_set_style_bg_color(lv_screen_active(), bg_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, text_color, LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_center(label);
}

static void ui_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    lv_font_t *nanum_font = lv_tiny_ttf_create_data_ex(nanum_gothic_regular_ttf,
                                                       nanum_gothic_regular_ttf_len,
                                                       36,
                                                       LV_FONT_KERNING_NONE,
                                                       8);
    lv_obj_t *key_label = lv_label_create(lv_screen_active());
    atomic_val_t displayed_key = -1;

    if (nanum_font != NULL) {
        lv_obj_set_style_text_font(key_label, nanum_font, LV_PART_MAIN);
    } else {
        printk("Nanum Gothic font init failed\n");
        lv_obj_set_style_text_font(key_label, &lv_font_montserrat_48, LV_PART_MAIN);
    }

    lv_obj_set_style_text_letter_space(key_label, 1, LV_PART_MAIN);
    lv_obj_set_style_text_align(key_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    while (1) {
        atomic_val_t key_code = atomic_get(&pending_key);

        if (key_code != displayed_key) {
            set_label_for_key(key_label, (uint32_t)key_code);
            displayed_key = key_code;
        }

        lv_timer_handler();
        k_sleep(K_MSEC(10));
    }
}

static void button_input_cb(struct input_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);

    if (evt->sync == 0) {
        return;
    }

    if (evt->type != INPUT_EV_KEY) {
        return;
    }

    if (evt->value != 1) {
        printk("key code %u released\n", evt->code);
        return;
    }

    printk("key code %u pressed at %" PRIu32 "\n", evt->code, k_cycle_get_32());

    if (evt->code >= INPUT_KEY_1 && evt->code <= INPUT_KEY_4) {
        size_t pixel_index = evt->code - INPUT_KEY_1;

        led_strip_update_rgb(STRIP_DEV, &pixels[pixel_index], 1);
        atomic_set(&pending_key, evt->code);
    }
}

INPUT_CALLBACK_DEFINE(BUTTONS_DEV, button_input_cb, NULL);

int main(void)
{
    if (!device_is_ready(BUTTONS_DEV)) {
        printk("Button input device is not ready\n");
        return 0;
    }
    if (!device_is_ready(STRIP_DEV)) {
        printk("LED strip device is not ready\n");
        return 0;
    }
    if (!device_is_ready(DISPLAY_DEV)) {
        printk("Display device is not ready\n");
        return 0;
    }
    if (!device_is_ready(LCD_BL_DEV)) {
        printk("LCD backlight device is not ready\n");
        return 0;
    }

    led_on(LCD_BL_DEV, 0);
    display_blanking_off(DISPLAY_DEV);

    led_strip_update_rgb(STRIP_DEV, &pixels[RED_PIXEL_INDEX], 1);

    k_thread_create(&ui_thread_data,
                    ui_thread_stack,
                    K_THREAD_STACK_SIZEOF(ui_thread_stack),
                    ui_thread,
                    NULL,
                    NULL,
                    NULL,
                    UI_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);

    printk("Button input test started\n");

    k_sleep(K_FOREVER);
    return 0;
}
