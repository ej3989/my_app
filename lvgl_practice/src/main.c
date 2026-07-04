#include <errno.h>
#include <stdint.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define DISPLAY_NODE DT_CHOSEN(zephyr_display)
#define DISPLAY_DEV DEVICE_DT_GET(DISPLAY_NODE)
#define LED_STRIP_NODE DT_ALIAS(led_strip)
#define LED_STRIP_DEV DEVICE_DT_GET(LED_STRIP_NODE)

static lv_obj_t *counter_label;
static uint32_t click_count;
static struct led_rgb led_strip_pixels[] = {
    { .r = 10, .g = 0, .b = 0 },    /* Red */
    { .r = 0, .g = 10, .b = 0 },    /* Green */
    { .r = 0, .g = 0, .b = 10 },    /* Blue */
    { .r = 10, .g = 10, .b = 0 },   /* Yellow */
    { .r = 0, .g = 10, .b = 10 },   /* Cyan */
    { .r = 10, .g = 0, .b = 10 },   /* Magenta */
    { .r = 10, .g = 10, .b = 10 },  /* White */
    { .r = 10, .g = 5, .b = 0 },    /* Orange */
    { .r = 5, .g = 0, .b = 10 },    /* Purple */
    { .r = 2, .g = 2, .b = 2 },     /* Dim white */
};
static void button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    click_count++;
    lv_label_set_text_fmt(counter_label, "Clicked: %u", click_count);
    uint8_t color_index = click_count % ARRAY_SIZE(led_strip_pixels);
    led_strip_update_rgb(LED_STRIP_DEV, &led_strip_pixels[color_index], 1);
}

static void create_first_screen(void)
{
    lv_obj_t *screen = lv_screen_active();

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x20252b), LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 24, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "LVGL Practice 01");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00ffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    counter_label = lv_label_create(screen);
    lv_label_set_text(counter_label, "Clicked: 0");
    lv_obj_set_style_text_color(counter_label, lv_color_hex(0xd9e2ec), LV_PART_MAIN);
    lv_obj_align(counter_label, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *button = lv_button_create(screen);
    lv_obj_set_size(button, 160, 56);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, 56);
    lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *button_label = lv_label_create(button);
    lv_label_set_text(button_label, "Tap");
    lv_obj_center(button_label);

    lv_refr_now(NULL);
}

int main(void)
{
    int ret;

    printk("LVGL practice start\n");

    if (!device_is_ready(DISPLAY_DEV)) {
        printk("Display device is not ready\n");
        return 0;
    }

    if (!device_is_ready(LED_STRIP_DEV)) {
        printk("LED strip device is not ready\n");
        return 0;
    }

    ret = lvgl_init();
    if (ret < 0) {
        printk("LVGL init failed: %d\n", ret);
        return 0;
    }

    ret = display_blanking_off(DISPLAY_DEV);
    if (ret < 0 && ret != -ENOSYS) {
        printk("Display blanking off failed: %d\n", ret);
        return 0;
    }

    create_first_screen();
    printk("LVGL practice screen loaded\n");

    while (1) {
        uint32_t sleep_ms = lv_timer_handler();

        k_msleep(MIN(sleep_ms, INT32_MAX));
    }
}
