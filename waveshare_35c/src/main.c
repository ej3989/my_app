#include <inttypes.h>
#include <stdbool.h>

#define APP_USE_LVGL_UI 1
#define APP_ENABLE_BUTTONS 1
#define APP_ENABLE_TOUCH 1
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
#if APP_ENABLE_BUTTONS || APP_ENABLE_TOUCH || APP_ENABLE_LCD
#include <zephyr/dt-bindings/input/input-event-codes.h>
#endif
#if APP_ENABLE_BUTTONS || APP_ENABLE_TOUCH
#include <zephyr/input/input.h>
#endif
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#if APP_USE_LVGL_UI || APP_ENABLE_LED_STRIP
#include <zephyr/sys/atomic.h>
#endif

#if APP_ENABLE_BUTTONS
#define BUTTONS_NODE DT_ALIAS(app_buttons)
#define BUTTONS_DEV DEVICE_DT_GET(BUTTONS_NODE)
#endif

#if APP_ENABLE_TOUCH
#define TOUCH_NODE DT_CHOSEN(zephyr_touch)
#define TOUCH_DEV DEVICE_DT_GET(TOUCH_NODE)
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
#define KEY_CARD_COUNT 4
#endif

#if APP_ENABLE_LED_STRIP || APP_USE_LVGL_UI
enum app_input_source {
    APP_INPUT_SOURCE_BOOT,
    APP_INPUT_SOURCE_GPIO,
    APP_INPUT_SOURCE_TOUCH,
};
#endif

#if APP_USE_LVGL_UI || APP_ENABLE_LED_STRIP
struct app_state {
    atomic_t selected_key;
    atomic_t selected_source;
    atomic_t event_id;
    atomic_t led_brightness;
#if APP_USE_LVGL_UI
    atomic_t key_press_counts[KEY_CARD_COUNT];
#endif
};

static struct app_state app_state;

static void init_app_state(void)
{
    atomic_set(&app_state.selected_key, INPUT_KEY_1);
    atomic_set(&app_state.selected_source, APP_INPUT_SOURCE_BOOT);
    atomic_set(&app_state.event_id, 0);
    atomic_set(&app_state.led_brightness, 10);
}
#endif

#if APP_ENABLE_LED_STRIP
static void update_led_strip(size_t selected_index)
{
    uint8_t brightness = (uint8_t)atomic_get(&app_state.led_brightness);
    struct led_rgb pixel = { 0 };

    switch (selected_index) {
    case 0:
        pixel.r = brightness;
        break;
    case 1:
        pixel.g = brightness;
        break;
    case 2:
        pixel.b = brightness;
        break;
    case 3:
        pixel.r = brightness;
        pixel.g = brightness;
        pixel.b = brightness;
        break;
    default:
        return;
    }

    led_strip_update_rgb(STRIP_DEV, &pixel, 1);
}
#endif

#if APP_USE_LVGL_UI
static struct k_thread ui_thread_data;
static K_THREAD_STACK_DEFINE(ui_thread_stack, UI_THREAD_STACK_SIZE);
static lv_style_t screen_style;
static lv_style_t key_label_style;
static lv_style_t info_label_style;
static lv_style_t badge_style;

struct ui_widgets {
    lv_obj_t *main_screen;
    lv_obj_t *detail_screen;
    lv_obj_t *keyboard_screen;
    lv_obj_t *keyboard_textarea;
    lv_obj_t *entered_text_label;
    lv_obj_t *brightness_value_label;
    lv_obj_t *brightness_bar;
    lv_obj_t *detail_key_label;
    lv_obj_t *detail_color_label;
    lv_obj_t *detail_source_label;
    lv_obj_t *detail_count_label;
};

static struct ui_widgets ui;
#endif

#if APP_USE_LVGL_UI
extern const unsigned char nanum_gothic_regular_ttf[];
extern const unsigned int nanum_gothic_regular_ttf_len;
#endif

static bool key_index_from_code(uint32_t key_code, size_t *selected_index)
{
    if (key_code < INPUT_KEY_1 || key_code > INPUT_KEY_4) {
        return false;
    }

    *selected_index = key_code - INPUT_KEY_1;
    return true;
}

#if APP_ENABLE_LED_STRIP || APP_USE_LVGL_UI
static void select_key(uint32_t key_code,
                       enum app_input_source source,
                       bool update_led,
                       bool update_ui)
{
    size_t selected_index;

    if (!key_index_from_code(key_code, &selected_index)) {
        return;
    }

#if APP_ENABLE_LED_STRIP
    if (update_led) {
        update_led_strip(selected_index);
    }
#endif

#if APP_USE_LVGL_UI
    if (update_ui) {
        atomic_inc(&app_state.key_press_counts[selected_index]);
        atomic_set(&app_state.selected_source, source);
        atomic_set(&app_state.selected_key, key_code);
        atomic_inc(&app_state.event_id);
    }
#endif
}
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

    lv_style_init(&badge_style);
    lv_style_set_bg_color(&badge_style, lv_color_hex(0x202020));
    lv_style_set_bg_opa(&badge_style, LV_OPA_70);
    lv_style_set_radius(&badge_style, 8);
    lv_style_set_pad_left(&badge_style, 12);
    lv_style_set_pad_right(&badge_style, 12);
    lv_style_set_pad_top(&badge_style, 6);
    lv_style_set_pad_bottom(&badge_style, 6);

}

static bool get_key_info(uint32_t key_code,
                         const char **main_text,
                         const char **key_text,
                         const char **color_text,
                         lv_color_t *bg_color,
                         size_t *selected_index)
{
    switch (key_code) {
    case INPUT_KEY_1:
        *main_text = "버튼0";
        *key_text = "KEY1";
        *color_text = "RED";
        *bg_color = lv_color_hex(0xcc2020);
        *selected_index = 0;
        return true;
    case INPUT_KEY_2:
        *main_text = "버튼1";
        *key_text = "KEY2";
        *color_text = "GREEN";
        *bg_color = lv_color_hex(0x209c3a);
        *selected_index = 1;
        return true;
    case INPUT_KEY_3:
        *main_text = "버튼2";
        *key_text = "KEY3";
        *color_text = "BLUE";
        *bg_color = lv_color_hex(0x1d4ed8);
        *selected_index = 2;
        return true;
    case INPUT_KEY_4:
        *main_text = "버튼3";
        *key_text = "KEY4";
        *color_text = "WHITE";
        *bg_color = lv_color_black();
        *selected_index = 3;
        return true;
    default:
        return false;
    }
}

static void set_detail_screen_for_key(uint32_t key_code)
{
    const char *main_text;
    const char *key_text;
    const char *color_text;
    lv_color_t bg_color;
    size_t selected_index;

    if (!get_key_info(key_code, &main_text, &key_text, &color_text, &bg_color, &selected_index)) {
        return;
    }

    ARG_UNUSED(main_text);
    ARG_UNUSED(selected_index);

    lv_label_set_text(ui.detail_key_label, key_text);
    lv_label_set_text_fmt(ui.detail_color_label, "COLOR: %s", color_text);
}

static const char *source_text(enum app_input_source source)
{
    switch (source) {
    case APP_INPUT_SOURCE_GPIO:
        return "GPIO";
    case APP_INPUT_SOURCE_TOUCH:
        return "TOUCH";
    case APP_INPUT_SOURCE_BOOT:
    default:
        return "BOOT";
    }
}

static void set_label_for_key(lv_obj_t *key_label,
                              lv_obj_t *key_badge_label,
                              lv_obj_t *color_badge_label,
                              lv_obj_t *key_cards[KEY_CARD_COUNT],
                              uint32_t key_code,
                              enum app_input_source source)
{
    const char *text;
    const char *key_text;
    const char *color_text;
    lv_color_t bg_color;
    size_t selected_index;
    atomic_val_t count;

    if (!get_key_info(key_code, &text, &key_text, &color_text, &bg_color, &selected_index)) {
        return;
    }

    lv_obj_set_style_bg_color(ui.main_screen, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.detail_screen, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.keyboard_screen, bg_color, LV_PART_MAIN);

    lv_label_set_text(key_label, text);
    lv_obj_center(key_label);

    lv_label_set_text(key_badge_label, key_text);
    lv_label_set_text_fmt(color_badge_label, "%s %s",
                          color_text,
                          source_text(source));
    for (size_t i = 0; i < KEY_CARD_COUNT; i++) {
        lv_obj_set_style_bg_color(key_cards[i], lv_color_hex(0x202020), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(key_cards[i], LV_OPA_60, LV_PART_MAIN);
        lv_obj_set_style_border_color(key_cards[i], lv_color_hex(0x606060), LV_PART_MAIN);
        lv_obj_set_style_border_width(key_cards[i], 1, LV_PART_MAIN);
    }

    lv_obj_set_style_bg_color(key_cards[selected_index], lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(key_cards[selected_index], LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_border_color(key_cards[selected_index], lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(key_cards[selected_index], 2, LV_PART_MAIN);

    count = atomic_get(&app_state.key_press_counts[selected_index]);
    lv_label_set_text_fmt(ui.detail_source_label, "SOURCE: %s", source_text(source));
    lv_label_set_text_fmt(ui.detail_count_label, "COUNT: %" PRIiPTR, (intptr_t)count);
}

static void back_card_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    lv_screen_load_anim(ui.main_screen,
                        LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                        200,
                        0,
                        false);
}

static void open_keyboard_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    lv_screen_load_anim(ui.keyboard_screen,
                        LV_SCR_LOAD_ANIM_MOVE_LEFT,
                        200,
                        0,
                        false);
}

static void keyboard_done_event_cb(lv_event_t *event)
{
    const char *text;

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    text = lv_textarea_get_text(ui.keyboard_textarea);
    if (text[0] == '\0') {
        lv_label_set_text(ui.entered_text_label, "TEXT: empty");
    } else {
        lv_label_set_text_fmt(ui.entered_text_label, "TEXT: %s", text);
    }

    lv_screen_load_anim(ui.main_screen,
                        LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                        200,
                        0,
                        false);
}

static void brightness_slider_event_cb(lv_event_t *event)
{
    lv_obj_t *slider;
    int32_t brightness;
    size_t selected_index;

    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    slider = lv_event_get_target_obj(event);
    brightness = lv_slider_get_value(slider);
    atomic_set(&app_state.led_brightness, brightness);
    lv_label_set_text_fmt(ui.brightness_value_label, "LED: %" PRId32, brightness);
    lv_bar_set_value(ui.brightness_bar, brightness, LV_ANIM_OFF);

#if APP_ENABLE_LED_STRIP
    if (key_index_from_code((uint32_t)atomic_get(&app_state.selected_key), &selected_index)) {
        update_led_strip(selected_index);
    }
#endif
}

static void color_dropdown_event_cb(lv_event_t *event)
{
    lv_obj_t *dropdown;
    uint32_t selected;
    uint32_t key_code;

    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    dropdown = lv_event_get_target_obj(event);
    selected = lv_dropdown_get_selected(dropdown);
    if (selected >= KEY_CARD_COUNT) {
        return;
    }

    key_code = INPUT_KEY_1 + selected;
    select_key(key_code, APP_INPUT_SOURCE_TOUCH, true, true);
}

static void key_card_event_cb(lv_event_t *event)
{
    uint32_t key_code;

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    key_code = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    select_key(key_code, APP_INPUT_SOURCE_TOUCH, true, true);
    set_detail_screen_for_key(key_code);
    lv_screen_load_anim(ui.detail_screen,
                        LV_SCR_LOAD_ANIM_MOVE_LEFT,
                        200,
                        0,
                        false);
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
    lv_obj_t *header;
    lv_obj_t *content;
    lv_obj_t *button_row;
    lv_obj_t *footer;
    lv_obj_t *title_label;
    lv_obj_t *key_label;
    lv_obj_t *brightness_slider;
    lv_obj_t *key_cards[KEY_CARD_COUNT];
    lv_obj_t *key_card_labels[KEY_CARD_COUNT];
    lv_obj_t *key_badge;
    lv_obj_t *color_badge;
    lv_obj_t *keyboard_badge;
    lv_obj_t *key_badge_label;
    lv_obj_t *color_badge_label;
    lv_obj_t *keyboard_badge_label;
    lv_obj_t *color_dropdown;
    lv_obj_t *detail_title_label;
    lv_obj_t *detail_back_card;
    lv_obj_t *detail_back_label;
    lv_obj_t *keyboard_title_label;
    lv_obj_t *keyboard_hint_label;
    lv_obj_t *keyboard_done_card;
    lv_obj_t *keyboard_back_card;
    lv_obj_t *keyboard_done_label;
    lv_obj_t *keyboard_back_label;
    lv_obj_t *keyboard;
    static const char *const key_card_texts[KEY_CARD_COUNT] = {
        "KEY1",
        "KEY2",
        "KEY3",
        "KEY4"
    };
    static const uint32_t key_card_codes[KEY_CARD_COUNT] = {
        INPUT_KEY_1,
        INPUT_KEY_2,
        INPUT_KEY_3,
        INPUT_KEY_4
    };
    atomic_val_t displayed_event_id = -1;

    printk("LVGL UI thread start\n");

    init_ui_styles();

    ui.main_screen = lv_obj_create(NULL);
    ui.detail_screen = lv_obj_create(NULL);
    ui.keyboard_screen = lv_obj_create(NULL);
    lv_obj_add_style(ui.main_screen, &screen_style, LV_PART_MAIN);
    lv_obj_add_style(ui.detail_screen, &screen_style, LV_PART_MAIN);
    lv_obj_add_style(ui.keyboard_screen, &screen_style, LV_PART_MAIN);

    header = lv_obj_create(ui.main_screen);
    content = lv_obj_create(ui.main_screen);
    button_row = lv_obj_create(content);
    footer = lv_obj_create(ui.main_screen);

    lv_obj_remove_style_all(header);
    lv_obj_remove_style_all(content);
    lv_obj_remove_style_all(button_row);
    lv_obj_remove_style_all(footer);

    lv_obj_set_size(header, LV_PCT(100), 48);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_set_size(content, LV_PCT(100), 220);
    lv_obj_align(content, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_size(button_row, LV_PCT(100), 64);
    lv_obj_align(button_row, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(button_row, 8, LV_PART_MAIN);

    lv_obj_set_size(footer, LV_PCT(100), 44);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(footer, 8, LV_PART_MAIN);

    title_label = lv_label_create(header);
    color_dropdown = lv_dropdown_create(header);
    ui.entered_text_label = lv_label_create(content);
    ui.brightness_value_label = lv_label_create(content);
    brightness_slider = lv_slider_create(content);
    ui.brightness_bar = lv_bar_create(content);
    key_label = lv_label_create(content);
    for (size_t i = 0; i < KEY_CARD_COUNT; i++) {
        key_cards[i] = lv_button_create(button_row);
        key_card_labels[i] = lv_label_create(key_cards[i]);

        lv_obj_set_style_bg_color(key_cards[i], lv_color_hex(0x202020), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(key_cards[i], LV_OPA_60, LV_PART_MAIN);
        lv_obj_set_style_border_color(key_cards[i], lv_color_hex(0x606060), LV_PART_MAIN);
        lv_obj_set_style_border_width(key_cards[i], 1, LV_PART_MAIN);
        lv_obj_set_style_radius(key_cards[i], 6, LV_PART_MAIN);
        lv_obj_set_size(key_cards[i], 92, 54);
        lv_obj_add_event_cb(key_cards[i],
                            key_card_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)key_card_codes[i]);
    }
    key_badge = lv_obj_create(footer);
    color_badge = lv_obj_create(footer);
    keyboard_badge = lv_button_create(footer);
    key_badge_label = lv_label_create(key_badge);
    color_badge_label = lv_label_create(color_badge);
    keyboard_badge_label = lv_label_create(keyboard_badge);

    lv_obj_remove_style_all(key_badge);
    lv_obj_remove_style_all(color_badge);
    lv_obj_add_style(key_badge, &badge_style, LV_PART_MAIN);
    lv_obj_add_style(color_badge, &badge_style, LV_PART_MAIN);
    lv_obj_set_style_bg_color(keyboard_badge, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(keyboard_badge, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(keyboard_badge, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_left(keyboard_badge, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(keyboard_badge, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(keyboard_badge, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(keyboard_badge, 6, LV_PART_MAIN);
    lv_obj_set_size(key_badge, 70, 32);
    lv_obj_set_size(color_badge, 130, 32);
    lv_obj_set_size(keyboard_badge, 118, 32);
    lv_obj_add_event_cb(keyboard_badge, open_keyboard_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_add_style(title_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(ui.entered_text_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(ui.brightness_value_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(key_label, &key_label_style, LV_PART_MAIN);
    for (size_t i = 0; i < KEY_CARD_COUNT; i++) {
        lv_obj_add_style(key_card_labels[i], &info_label_style, LV_PART_MAIN);
        lv_label_set_text(key_card_labels[i], key_card_texts[i]);
        lv_obj_add_flag(key_card_labels[i], LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_center(key_card_labels[i]);
    }
    lv_obj_add_style(key_badge_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(color_badge_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(keyboard_badge_label, &info_label_style, LV_PART_MAIN);
    lv_obj_center(key_badge_label);
    lv_obj_center(color_badge_label);
    lv_obj_center(keyboard_badge_label);
    lv_label_set_text(keyboard_badge_label, "KEYBOARD");
    lv_obj_add_flag(keyboard_badge_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_label_set_text(ui.entered_text_label, "TEXT: empty");
    lv_obj_align(ui.entered_text_label, LV_ALIGN_TOP_MID, 0, 4);

    lv_label_set_text(ui.brightness_value_label, "LED: 10");
    lv_obj_align(ui.brightness_value_label, LV_ALIGN_TOP_MID, 0, 32);

    lv_obj_set_size(brightness_slider, 300, 18);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_MID, 0, 58);
    lv_slider_set_range(brightness_slider, 1, 50);
    lv_slider_set_value(brightness_slider, 10, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider,
                        brightness_slider_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        NULL);

    lv_obj_set_size(ui.brightness_bar, 300, 8);
    lv_obj_align(ui.brightness_bar, LV_ALIGN_TOP_MID, 0, 82);
    lv_bar_set_range(ui.brightness_bar, 1, 50);
    lv_bar_set_value(ui.brightness_bar, 10, LV_ANIM_OFF);

    if (nanum_font != NULL) {
        printk("Nanum Gothic font init OK\n");
        lv_obj_set_style_text_font(key_label, nanum_font, LV_PART_MAIN);
    } else {
        printk("Nanum Gothic font init failed\n");
        lv_obj_set_style_text_font(key_label, &lv_font_montserrat_48, LV_PART_MAIN);
    }

    lv_label_set_text(title_label, "Waveshare 3.5C");
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 10, 0);

    lv_dropdown_set_options(color_dropdown, "RED\nGREEN\nBLUE\nWHITE");
    lv_dropdown_set_selected(color_dropdown, 0);
    lv_obj_set_size(color_dropdown, 148, 36);
    lv_obj_align(color_dropdown, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_event_cb(color_dropdown,
                        color_dropdown_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        NULL);

    detail_title_label = lv_label_create(ui.detail_screen);
    ui.detail_key_label = lv_label_create(ui.detail_screen);
    ui.detail_color_label = lv_label_create(ui.detail_screen);
    ui.detail_source_label = lv_label_create(ui.detail_screen);
    ui.detail_count_label = lv_label_create(ui.detail_screen);
    detail_back_card = lv_obj_create(ui.detail_screen);
    detail_back_label = lv_label_create(detail_back_card);

    lv_obj_remove_style_all(detail_back_card);
    lv_obj_add_style(detail_title_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(ui.detail_key_label, &key_label_style, LV_PART_MAIN);
    lv_obj_add_style(ui.detail_color_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(ui.detail_source_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(ui.detail_count_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(detail_back_card, &badge_style, LV_PART_MAIN);
    lv_obj_add_style(detail_back_label, &info_label_style, LV_PART_MAIN);

    if (nanum_font != NULL) {
        lv_obj_set_style_text_font(ui.detail_key_label, nanum_font, LV_PART_MAIN);
    } else {
        lv_obj_set_style_text_font(ui.detail_key_label, &lv_font_montserrat_48, LV_PART_MAIN);
    }

    lv_label_set_text(detail_title_label, "Detail");
    lv_obj_align(detail_title_label, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_center(ui.detail_key_label);
    lv_obj_align(ui.detail_color_label, LV_ALIGN_CENTER, 0, 56);
    lv_obj_align(ui.detail_source_label, LV_ALIGN_CENTER, 0, 88);
    lv_obj_align(ui.detail_count_label, LV_ALIGN_CENTER, 0, 120);

    lv_obj_set_size(detail_back_card, 96, 36);
    lv_obj_align(detail_back_card, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_flag(detail_back_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(detail_back_card, back_card_event_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(detail_back_label, "BACK");
    lv_obj_center(detail_back_label);

    keyboard_title_label = lv_label_create(ui.keyboard_screen);
    keyboard_hint_label = lv_label_create(ui.keyboard_screen);
    ui.keyboard_textarea = lv_textarea_create(ui.keyboard_screen);
    keyboard_done_card = lv_obj_create(ui.keyboard_screen);
    keyboard_back_card = lv_obj_create(ui.keyboard_screen);
    keyboard_done_label = lv_label_create(keyboard_done_card);
    keyboard_back_label = lv_label_create(keyboard_back_card);
    keyboard = lv_keyboard_create(ui.keyboard_screen);

    lv_obj_add_style(keyboard_title_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(keyboard_hint_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(keyboard_done_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(keyboard_back_label, &info_label_style, LV_PART_MAIN);

    lv_label_set_text(keyboard_title_label, "Text Input");
    lv_obj_align(keyboard_title_label, LV_ALIGN_TOP_MID, 0, 10);

    lv_label_set_text(keyboard_hint_label, "Touch the input box and type");
    lv_obj_align(keyboard_hint_label, LV_ALIGN_TOP_MID, 0, 34);

    lv_obj_set_size(ui.keyboard_textarea, 430, 44);
    lv_obj_align(ui.keyboard_textarea, LV_ALIGN_TOP_MID, 0, 60);
    lv_textarea_set_one_line(ui.keyboard_textarea, true);
    lv_textarea_set_placeholder_text(ui.keyboard_textarea, "Input text");

    lv_obj_remove_style_all(keyboard_done_card);
    lv_obj_remove_style_all(keyboard_back_card);
    lv_obj_add_style(keyboard_done_card, &badge_style, LV_PART_MAIN);
    lv_obj_add_style(keyboard_back_card, &badge_style, LV_PART_MAIN);
    lv_obj_set_size(keyboard_done_card, 110, 34);
    lv_obj_set_size(keyboard_back_card, 110, 34);
    lv_obj_align(keyboard_done_card, LV_ALIGN_TOP_MID, -62, 110);
    lv_obj_align(keyboard_back_card, LV_ALIGN_TOP_MID, 62, 110);
    lv_obj_add_flag(keyboard_done_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(keyboard_back_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(keyboard_done_card, keyboard_done_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(keyboard_back_card, back_card_event_cb, LV_EVENT_CLICKED, NULL);

    lv_label_set_text(keyboard_done_label, "DONE");
    lv_label_set_text(keyboard_back_label, "BACK");
    lv_obj_add_flag(keyboard_done_label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(keyboard_back_label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(keyboard_done_label);
    lv_obj_center(keyboard_back_label);

    lv_obj_set_size(keyboard, 460, 160);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_keyboard_set_textarea(keyboard, ui.keyboard_textarea);

    set_detail_screen_for_key(INPUT_KEY_1);
    lv_screen_load(ui.main_screen);
    lv_refr_now(NULL);
    printk("LVGL main screen loaded\n");

    while (1) {
        atomic_val_t key_code = atomic_get(&app_state.selected_key);
        atomic_val_t source = atomic_get(&app_state.selected_source);
        atomic_val_t event_id = atomic_get(&app_state.event_id);

        if (event_id != displayed_event_id) {
            set_label_for_key(key_label,
                              key_badge_label,
                              color_badge_label,
                              key_cards,
                              (uint32_t)key_code,
                              (enum app_input_source)source);
            set_detail_screen_for_key((uint32_t)key_code);
            displayed_event_id = event_id;
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

#if APP_ENABLE_LED_STRIP || APP_USE_LVGL_UI
    if (evt->code >= INPUT_KEY_1 && evt->code <= INPUT_KEY_4) {
        select_key(evt->code, APP_INPUT_SOURCE_GPIO, true, true);
    }
#endif
}

INPUT_CALLBACK_DEFINE(BUTTONS_DEV, button_input_cb, NULL);
#endif

int main(void)
{
#if APP_USE_LVGL_UI || APP_ENABLE_LCD
    int ret;
#endif

    printk("main start\n");

#if APP_USE_LVGL_UI || APP_ENABLE_LED_STRIP
    init_app_state();
#endif

#if APP_ENABLE_BUTTONS
    if (!device_is_ready(BUTTONS_DEV)) {
        printk("Button input device is not ready\n");
        return 0;
    }
#endif
#if APP_ENABLE_TOUCH
    if (!device_is_ready(TOUCH_DEV)) {
        printk("Touch device is not ready\n");
        return 0;
    }
    printk("Touch device is ready: %s\n", TOUCH_DEV->name);
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
    printk("Display device is ready: %s\n", DISPLAY_DEV->name);
    if (!device_is_ready(LCD_BL_DEV)) {
        printk("LCD backlight device is not ready\n");
        return 0;
    }
    printk("LCD backlight device is ready: %s\n", LCD_BL_DEV->name);
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
    struct display_capabilities caps;

    display_get_capabilities(DISPLAY_DEV, &caps);
    printk("display capabilities: %ux%u fmt=%d supported=0x%08" PRIx32 " orientation=%d\n",
           caps.x_resolution,
           caps.y_resolution,
           caps.current_pixel_format,
           caps.supported_pixel_formats,
           caps.current_orientation);

    printk("LCD backlight on start\n");
    ret = led_on(LCD_BL_DEV, 0);
    printk("LCD backlight on result: %d\n", ret);
    printk("display blanking off start\n");
    ret = display_blanking_off(DISPLAY_DEV);
    printk("display blanking off result: %d\n", ret);
    k_sleep(K_MSEC(1000));
#endif

#if APP_ENABLE_LED_STRIP
    update_led_strip(RED_PIXEL_INDEX);
#endif

#if APP_USE_LVGL_UI
    printk("LVGL UI thread create start\n");
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
    printk("LVGL UI thread create OK\n");
#endif

#if APP_ENABLE_BUTTONS
    printk("Button input test started\n");
#endif

    k_sleep(K_FOREVER);
    return 0;
}
