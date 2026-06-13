#include <inttypes.h>

#define APP_USE_LVGL_UI 1
#define APP_ENABLE_BUTTONS 1
#define APP_ENABLE_LCD 1
#define APP_ENABLE_LED_STRIP 1

#if APP_USE_LVGL_UI
#include <lvgl.h>
#include <lvgl_zephyr.h>
#endif
#include <zephyr/device.h>
#if APP_ENABLE_LCD
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/led.h>
#endif
#if APP_ENABLE_LED_STRIP
#include <zephyr/drivers/led_strip.h>
#endif
#if APP_ENABLE_BUTTONS
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#endif
#include <zephyr/kernel.h>
#if APP_USE_LVGL_UI
#include <zephyr/sys/atomic.h>
#endif

#if APP_ENABLE_BUTTONS
#define BUTTONS_NODE DT_ALIAS(app_buttons)
#define BUTTONS_DEV DEVICE_DT_GET(BUTTONS_NODE)
#endif

#if APP_ENABLE_LED_STRIP
#define STRIP_NODE DT_ALIAS(led_strip)
#define STRIP_DEV DEVICE_DT_GET(STRIP_NODE)
#endif

#if APP_ENABLE_LCD
#define DISPLAY_NODE DT_CHOSEN(zephyr_display)
#define DISPLAY_DEV DEVICE_DT_GET(DISPLAY_NODE)
#define LCD_BL_NODE DT_NODELABEL(lcd_bl)
#define LCD_BL_DEV DEVICE_DT_GET(LCD_BL_NODE)
#endif

#define RED_PIXEL_INDEX 0

#if APP_USE_LVGL_UI
#define UI_THREAD_STACK_SIZE 8192
#define UI_THREAD_PRIORITY 5
#endif

#if APP_ENABLE_LED_STRIP
static struct led_rgb pixels[4] = {
    { .r = 10, .g = 0, .b = 0 },
    { .r = 0, .g = 10, .b = 0 },
    { .r = 0, .g = 0, .b = 10 },
    { .r = 10, .g = 10, .b = 10 }
};
#endif

#if APP_USE_LVGL_UI
static atomic_t pending_key = ATOMIC_INIT(INPUT_KEY_1);
static struct k_thread ui_thread_data;
static K_THREAD_STACK_DEFINE(ui_thread_stack, UI_THREAD_STACK_SIZE);
static lv_style_t screen_style;
static lv_style_t key_label_style;
static lv_style_t info_label_style;
#endif

#if APP_USE_LVGL_UI
extern const unsigned char nanum_gothic_regular_ttf[];
extern const unsigned int nanum_gothic_regular_ttf_len;
#endif

#if APP_USE_LVGL_UI
static void init_ui_styles(void)
{
    lv_style_init(&screen_style);
    lv_style_set_bg_color(&screen_style, lv_color_hex(0xcc2020));
    lv_style_set_bg_opa(&screen_style, LV_OPA_COVER);

    lv_style_init(&key_label_style);
    lv_style_set_text_color(&key_label_style, lv_color_white());
    lv_style_set_text_align(&key_label_style, LV_TEXT_ALIGN_CENTER);
    lv_style_set_text_letter_space(&key_label_style, 1);

    lv_style_init(&info_label_style);
    lv_style_set_text_color(&info_label_style, lv_color_white());
    lv_style_set_text_align(&info_label_style, LV_TEXT_ALIGN_CENTER);
    lv_style_set_text_letter_space(&info_label_style, 1);
}

static void set_label_for_key(lv_obj_t *key_label,
                              lv_obj_t *status_label,
                              uint32_t key_code)
{
    const char *text = "버튼0";
    const char *status = "KEY1 / RED";
    lv_color_t bg_color = lv_color_hex(0xcc2020);

    switch (key_code) {
    case INPUT_KEY_1:
        text = "버튼0";
        status = "KEY1 / RED";
        bg_color = lv_color_hex(0xcc2020);
        break;
    case INPUT_KEY_2:
        text = "버튼1";
        status = "KEY2 / GREEN";
        bg_color = lv_color_hex(0x209c3a);
        break;
    case INPUT_KEY_3:
        text = "버튼2";
        status = "KEY3 / BLUE";
        bg_color = lv_color_hex(0x1d4ed8);
        break;
    case INPUT_KEY_4:
        text = "버튼3";
        status = "KEY4 / WHITE";
        bg_color = lv_color_black();
        break;
    default:
        return;
    }

    lv_style_set_bg_color(&screen_style, bg_color);
    lv_obj_report_style_change(&screen_style);

    lv_label_set_text(key_label, text);
    lv_obj_center(key_label);

    lv_label_set_text(status_label, status);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static void ui_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    lv_font_t *nanum_font = lv_tiny_ttf_create_data_ex(nanum_gothic_regular_ttf,
                                                       nanum_gothic_regular_ttf_len,
                                                       48,
                                                       LV_FONT_KERNING_NONE,
                                                       8);
    lv_obj_t *title_label = lv_label_create(lv_screen_active());
    lv_obj_t *key_label = lv_label_create(lv_screen_active());
    lv_obj_t *status_label = lv_label_create(lv_screen_active());
    atomic_val_t displayed_key = -1;

    init_ui_styles();
    lv_obj_add_style(lv_screen_active(), &screen_style, LV_PART_MAIN);
    lv_obj_add_style(title_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(key_label, &key_label_style, LV_PART_MAIN);
    lv_obj_add_style(status_label, &info_label_style, LV_PART_MAIN);

    if (nanum_font != NULL) {
        lv_obj_set_style_text_font(key_label, nanum_font, LV_PART_MAIN);
    } else {
        printk("Nanum Gothic font init failed\n");
        lv_obj_set_style_text_font(key_label, &lv_font_montserrat_48, LV_PART_MAIN);
    }

    lv_label_set_text(title_label, "Button Test");
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 12);

    while (1) {
        atomic_val_t key_code = atomic_get(&pending_key);

        if (key_code != displayed_key) {
            set_label_for_key(key_label, status_label, (uint32_t)key_code);
            displayed_key = key_code;
        }

        lv_timer_handler();
        k_sleep(K_MSEC(10));
    }
}
#endif

#if APP_ENABLE_BUTTONS
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

#if APP_ENABLE_LED_STRIP
    if (evt->code >= INPUT_KEY_1 && evt->code <= INPUT_KEY_4) {
        size_t pixel_index = evt->code - INPUT_KEY_1;

        led_strip_update_rgb(STRIP_DEV, &pixels[pixel_index], 1);
#if APP_USE_LVGL_UI
        atomic_set(&pending_key, evt->code);
#endif
    }
#endif
}

INPUT_CALLBACK_DEFINE(BUTTONS_DEV, button_input_cb, NULL);
#endif

int main(void)
{
#if APP_USE_LVGL_UI
    int ret;
#endif

    printk("main start\n");

#if APP_ENABLE_BUTTONS
    if (!device_is_ready(BUTTONS_DEV)) {
        printk("Button input device is not ready\n");
        return 0;
    }
#endif
#if APP_ENABLE_LED_STRIP
    if (!device_is_ready(STRIP_DEV)) {
        printk("LED strip device is not ready\n");
        return 0;
    }
#endif
#if APP_ENABLE_LCD
    if (!device_is_ready(DISPLAY_DEV)) {
        printk("Display device is not ready\n");
        return 0;
    }
    if (!device_is_ready(LCD_BL_DEV)) {
        printk("LCD backlight device is not ready\n");
        return 0;
    }
#endif

#if APP_ENABLE_BUTTONS || APP_ENABLE_LCD || APP_ENABLE_LED_STRIP
    printk("all devices ready\n");
#else
    printk("no peripheral drivers enabled\n");
#endif

#if APP_USE_LVGL_UI
    printk("LVGL init start\n");
    ret = lvgl_init();
    if (ret < 0) {
        printk("LVGL init failed: %d\n", ret);
        return 0;
    }
    printk("LVGL init OK\n");
#endif

#if APP_ENABLE_LCD
    printk("LCD backlight on start\n");
    led_on(LCD_BL_DEV, 0);
    printk("LCD backlight on OK\n");
    printk("display blanking off start\n");
    display_blanking_off(DISPLAY_DEV);
    printk("display blanking off OK\n");
#endif

#if APP_ENABLE_LED_STRIP
    led_strip_update_rgb(STRIP_DEV, &pixels[RED_PIXEL_INDEX], 1);
#endif

#if APP_USE_LVGL_UI
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
#endif

#if APP_ENABLE_BUTTONS
    printk("Button input test started\n");
#endif

    k_sleep(K_FOREVER);
    return 0;
}
