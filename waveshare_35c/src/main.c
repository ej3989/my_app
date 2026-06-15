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
#include <zephyr/drivers/gpio.h>
#endif
#if APP_ENABLE_BUTTONS || APP_ENABLE_TOUCH
#include <zephyr/input/input.h>
#endif
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#if APP_USE_LVGL_UI
#include <zephyr/sys/atomic.h>
#endif

#if APP_ENABLE_BUTTONS
#define BUTTONS_NODE DT_ALIAS(app_buttons)
#define BUTTONS_DEV DEVICE_DT_GET(BUTTONS_NODE)
#endif

#if APP_ENABLE_TOUCH
#define TOUCH_NODE DT_CHOSEN(zephyr_touch)
#define TOUCH_DEV DEVICE_DT_GET(TOUCH_NODE)
#define TOUCH_IRQ_GPIO GPIO_DT_SPEC_GET(TOUCH_NODE, int_gpios)
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
#define LCD_DBI_NODE DT_NODELABEL(lcd_dbi)
#define LCD_DC_GPIO GPIO_DT_SPEC_GET(LCD_DBI_NODE, dc_gpios)
#define LCD_RST_GPIO GPIO_DT_SPEC_GET(LCD_DBI_NODE, reset_gpios)
#define LCD_CS_GPIO GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(spi2), cs_gpios, 0)
#endif

#define RED_PIXEL_INDEX 0

#if APP_USE_LVGL_UI
#define UI_THREAD_STACK_SIZE 8192
#define UI_THREAD_PRIORITY 5
#define KEY_CARD_COUNT 4
#endif

#if APP_ENABLE_TOUCH
#define TOUCH_DIAG_THREAD_STACK_SIZE 2048
#define TOUCH_DIAG_THREAD_PRIORITY 6
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
static lv_style_t badge_style;
static lv_style_t key_card_style;
static lv_style_t active_key_card_style;
static lv_style_t touch_target_style;
static lv_obj_t *main_screen;
static lv_obj_t *detail_screen;
static lv_obj_t *detail_key_label;
static lv_obj_t *detail_color_label;
#endif

#if APP_ENABLE_TOUCH
static const struct gpio_dt_spec touch_irq_gpio = TOUCH_IRQ_GPIO;
static struct k_thread touch_diag_thread_data;
static K_THREAD_STACK_DEFINE(touch_diag_thread_stack, TOUCH_DIAG_THREAD_STACK_SIZE);
#endif

#if APP_USE_LVGL_UI
extern const unsigned char nanum_gothic_regular_ttf[];
extern const unsigned int nanum_gothic_regular_ttf_len;
#endif

#if APP_ENABLE_LED_STRIP || APP_USE_LVGL_UI
static void select_key(uint32_t key_code)
{
    if (key_code < INPUT_KEY_1 || key_code > INPUT_KEY_4) {
        return;
    }

#if APP_ENABLE_LED_STRIP
    size_t pixel_index = key_code - INPUT_KEY_1;

    led_strip_update_rgb(STRIP_DEV, &pixels[pixel_index], 1);
#endif

#if APP_USE_LVGL_UI
    atomic_set(&pending_key, key_code);
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

    lv_style_init(&key_card_style);
    lv_style_set_bg_color(&key_card_style, lv_color_hex(0x202020));
    lv_style_set_bg_opa(&key_card_style, LV_OPA_60);
    lv_style_set_border_color(&key_card_style, lv_color_hex(0x606060));
    lv_style_set_border_width(&key_card_style, 1);
    lv_style_set_radius(&key_card_style, 6);

    lv_style_init(&active_key_card_style);
    lv_style_set_bg_color(&active_key_card_style, lv_color_hex(0xffffff));
    lv_style_set_bg_opa(&active_key_card_style, LV_OPA_30);
    lv_style_set_border_color(&active_key_card_style, lv_color_white());
    lv_style_set_border_width(&active_key_card_style, 2);

    lv_style_init(&touch_target_style);
    lv_style_set_text_color(&touch_target_style, lv_color_white());
    lv_style_set_text_align(&touch_target_style, LV_TEXT_ALIGN_CENTER);
    lv_style_set_bg_color(&touch_target_style, lv_color_hex(0x101010));
    lv_style_set_bg_opa(&touch_target_style, LV_OPA_80);
    lv_style_set_border_color(&touch_target_style, lv_color_white());
    lv_style_set_border_width(&touch_target_style, 2);
    lv_style_set_radius(&touch_target_style, 4);
    lv_style_set_pad_left(&touch_target_style, 4);
    lv_style_set_pad_right(&touch_target_style, 4);
    lv_style_set_pad_top(&touch_target_style, 4);
    lv_style_set_pad_bottom(&touch_target_style, 4);
}

static void add_touch_target(lv_obj_t *parent,
                             const char *text,
                             lv_align_t align,
                             int32_t x_ofs,
                             int32_t y_ofs)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_add_style(label, &touch_target_style, LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_set_size(label, 76, 42);
    lv_obj_align(label, align, x_ofs, y_ofs);
}

static void add_touch_calibration_targets(lv_obj_t *parent)
{
    add_touch_target(parent, "TL\n20,20", LV_ALIGN_TOP_LEFT, 10, 10);
    add_touch_target(parent, "TR\n460,20", LV_ALIGN_TOP_RIGHT, -10, 10);
    add_touch_target(parent, "C\n240,160", LV_ALIGN_CENTER, 0, 0);
    add_touch_target(parent, "BL\n20,300", LV_ALIGN_BOTTOM_LEFT, 10, -10);
    add_touch_target(parent, "BR\n460,300", LV_ALIGN_BOTTOM_RIGHT, -10, -10);
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

    lv_label_set_text(detail_key_label, key_text);
    lv_label_set_text_fmt(detail_color_label, "COLOR: %s", color_text);
}

static void set_label_for_key(lv_obj_t *key_label,
                              lv_obj_t *key_badge_label,
                              lv_obj_t *color_badge_label,
                              lv_obj_t *key_cards[KEY_CARD_COUNT],
                              uint32_t key_code)
{
    const char *text;
    const char *key_text;
    const char *color_text;
    lv_color_t bg_color;
    size_t selected_index;

    if (!get_key_info(key_code, &text, &key_text, &color_text, &bg_color, &selected_index)) {
        return;
    }

    lv_style_set_bg_color(&screen_style, bg_color);
    lv_obj_report_style_change(&screen_style);

    lv_label_set_text(key_label, text);
    lv_obj_center(key_label);

    lv_label_set_text(key_badge_label, key_text);
    lv_label_set_text(color_badge_label, color_text);

    for (size_t i = 0; i < KEY_CARD_COUNT; i++) {
        lv_obj_remove_style(key_cards[i], &active_key_card_style, LV_PART_MAIN);
    }
    lv_obj_add_style(key_cards[selected_index], &active_key_card_style, LV_PART_MAIN);
}

static void back_card_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    lv_screen_load_anim(main_screen,
                        LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                        200,
                        0,
                        false);
}

static void key_card_event_cb(lv_event_t *event)
{
    uint32_t key_code;

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    key_code = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    printk("LVGL key card clicked: %" PRIu32 "\n", key_code);
    select_key(key_code);
    set_detail_screen_for_key(key_code);
    lv_screen_load_anim(detail_screen,
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
    lv_obj_t *key_cards[KEY_CARD_COUNT];
    lv_obj_t *key_card_labels[KEY_CARD_COUNT];
    lv_obj_t *key_badge;
    lv_obj_t *color_badge;
    lv_obj_t *key_badge_label;
    lv_obj_t *color_badge_label;
    lv_obj_t *detail_title_label;
    lv_obj_t *detail_back_card;
    lv_obj_t *detail_back_label;
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
    atomic_val_t displayed_key = -1;

    printk("LVGL UI thread start\n");

    init_ui_styles();

    main_screen = lv_obj_create(NULL);
    detail_screen = lv_obj_create(NULL);
    lv_obj_add_style(main_screen, &screen_style, LV_PART_MAIN);
    lv_obj_add_style(detail_screen, &screen_style, LV_PART_MAIN);

    header = lv_obj_create(main_screen);
    content = lv_obj_create(main_screen);
    button_row = lv_obj_create(content);
    footer = lv_obj_create(main_screen);

    lv_obj_remove_style_all(header);
    lv_obj_remove_style_all(content);
    lv_obj_remove_style_all(button_row);
    lv_obj_remove_style_all(footer);

    lv_obj_set_size(header, LV_PCT(100), 48);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_set_size(content, LV_PCT(100), 220);
    lv_obj_align(content, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_size(button_row, LV_PCT(100), 38);
    lv_obj_align(button_row, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(button_row, 6, LV_PART_MAIN);

    lv_obj_set_size(footer, LV_PCT(100), 44);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(footer, 8, LV_PART_MAIN);

    title_label = lv_label_create(header);
    key_label = lv_label_create(content);
    for (size_t i = 0; i < KEY_CARD_COUNT; i++) {
        key_cards[i] = lv_obj_create(button_row);
        key_card_labels[i] = lv_label_create(key_cards[i]);

        lv_obj_remove_style_all(key_cards[i]);
        lv_obj_add_style(key_cards[i], &key_card_style, LV_PART_MAIN);
        lv_obj_set_size(key_cards[i], 48, 32);
        lv_obj_add_flag(key_cards[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(key_cards[i],
                            key_card_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)key_card_codes[i]);
    }
    key_badge = lv_obj_create(footer);
    color_badge = lv_obj_create(footer);
    key_badge_label = lv_label_create(key_badge);
    color_badge_label = lv_label_create(color_badge);

    lv_obj_remove_style_all(key_badge);
    lv_obj_remove_style_all(color_badge);
    lv_obj_add_style(key_badge, &badge_style, LV_PART_MAIN);
    lv_obj_add_style(color_badge, &badge_style, LV_PART_MAIN);
    lv_obj_set_size(key_badge, 82, 32);
    lv_obj_set_size(color_badge, 92, 32);

    lv_obj_add_style(title_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(key_label, &key_label_style, LV_PART_MAIN);
    for (size_t i = 0; i < KEY_CARD_COUNT; i++) {
        lv_obj_add_style(key_card_labels[i], &info_label_style, LV_PART_MAIN);
        lv_label_set_text(key_card_labels[i], key_card_texts[i]);
        lv_obj_center(key_card_labels[i]);
    }
    lv_obj_add_style(key_badge_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(color_badge_label, &info_label_style, LV_PART_MAIN);
    lv_obj_center(key_badge_label);
    lv_obj_center(color_badge_label);

    if (nanum_font != NULL) {
        printk("Nanum Gothic font init OK\n");
        lv_obj_set_style_text_font(key_label, nanum_font, LV_PART_MAIN);
    } else {
        printk("Nanum Gothic font init failed\n");
        lv_obj_set_style_text_font(key_label, &lv_font_montserrat_48, LV_PART_MAIN);
    }

    lv_label_set_text(title_label, "Waveshare 3.5C");
    lv_obj_center(title_label);
    add_touch_calibration_targets(main_screen);

    detail_title_label = lv_label_create(detail_screen);
    detail_key_label = lv_label_create(detail_screen);
    detail_color_label = lv_label_create(detail_screen);
    detail_back_card = lv_obj_create(detail_screen);
    detail_back_label = lv_label_create(detail_back_card);

    lv_obj_remove_style_all(detail_back_card);
    lv_obj_add_style(detail_title_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(detail_key_label, &key_label_style, LV_PART_MAIN);
    lv_obj_add_style(detail_color_label, &info_label_style, LV_PART_MAIN);
    lv_obj_add_style(detail_back_card, &badge_style, LV_PART_MAIN);
    lv_obj_add_style(detail_back_label, &info_label_style, LV_PART_MAIN);

    if (nanum_font != NULL) {
        lv_obj_set_style_text_font(detail_key_label, nanum_font, LV_PART_MAIN);
    } else {
        lv_obj_set_style_text_font(detail_key_label, &lv_font_montserrat_48, LV_PART_MAIN);
    }

    lv_label_set_text(detail_title_label, "Detail");
    lv_obj_align(detail_title_label, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_center(detail_key_label);
    lv_obj_align(detail_color_label, LV_ALIGN_CENTER, 0, 56);

    lv_obj_set_size(detail_back_card, 96, 36);
    lv_obj_align(detail_back_card, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_flag(detail_back_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(detail_back_card, back_card_event_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(detail_back_label, "BACK");
    lv_obj_center(detail_back_label);

    set_detail_screen_for_key(INPUT_KEY_1);
    lv_screen_load(main_screen);
    lv_refr_now(NULL);
    printk("LVGL main screen loaded\n");

    while (1) {
        atomic_val_t key_code = atomic_get(&pending_key);

        if (key_code != displayed_key) {
            set_label_for_key(key_label,
                              key_badge_label,
                              color_badge_label,
                              key_cards,
                              (uint32_t)key_code);
            set_detail_screen_for_key((uint32_t)key_code);
            displayed_key = key_code;
        }

        lv_timer_handler();
        k_sleep(K_MSEC(10));
    }
}
#endif

#if APP_ENABLE_LCD
#define LCD_TEST_BLOCK_ROWS 16U

static uint8_t test_line[480 * 3];
static uint8_t test_block[480 * LCD_TEST_BLOCK_ROWS * 3];
static const struct gpio_dt_spec lcd_dc_gpio = LCD_DC_GPIO;
static const struct gpio_dt_spec lcd_rst_gpio = LCD_RST_GPIO;
static const struct gpio_dt_spec lcd_cs_gpio = LCD_CS_GPIO;

static void print_gpio_state(const char *name, const struct gpio_dt_spec *gpio)
{
    int logical = gpio_pin_get_dt(gpio);
    int raw = gpio_pin_get_raw(gpio->port, gpio->pin);

    printk("%s gpio port=%s pin=%u logical=%d raw=%d flags=0x%x\n",
           name,
           gpio->port->name,
           gpio->pin,
           logical,
           raw,
           gpio->dt_flags);
}

static void print_lcd_gpio_states(const char *stage)
{
    printk("LCD GPIO state: %s\n", stage);
    print_gpio_state("LCD_DC", &lcd_dc_gpio);
    print_gpio_state("LCD_RST", &lcd_rst_gpio);
    print_gpio_state("LCD_CS", &lcd_cs_gpio);
}

static uint8_t lcd_bytes_per_pixel(const struct display_capabilities *caps)
{
    if (caps->current_pixel_format == PIXEL_FORMAT_RGB_888) {
        return 3;
    }

    return 2;
}

static void fill_lcd_line(const struct display_capabilities *caps,
                          uint32_t rgb,
                          uint16_t width)
{
    uint8_t bytes_per_pixel = lcd_bytes_per_pixel(caps);
    uint8_t red = (rgb >> 16) & 0xff;
    uint8_t green = (rgb >> 8) & 0xff;
    uint8_t blue = rgb & 0xff;

    if (bytes_per_pixel == 3) {
        for (size_t x = 0; x < width; x++) {
            test_line[(x * 3U) + 0U] = red;
            test_line[(x * 3U) + 1U] = green;
            test_line[(x * 3U) + 2U] = blue;
        }
    } else {
        uint16_t rgb565 = ((red & 0xf8) << 8) |
                          ((green & 0xfc) << 3) |
                          (blue >> 3);

        for (size_t x = 0; x < width; x++) {
            test_line[(x * 2U) + 0U] = rgb565 >> 8;
            test_line[(x * 2U) + 1U] = rgb565 & 0xff;
        }
    }
}

static int write_lcd_solid_color(const struct display_capabilities *caps,
                                 uint32_t color,
                                 const char *name)
{
    uint16_t width = caps->x_resolution;
    uint16_t height = caps->y_resolution;
    uint8_t bytes_per_pixel = lcd_bytes_per_pixel(caps);
    uint8_t red = (color >> 16) & 0xff;
    uint8_t green = (color >> 8) & 0xff;
    uint8_t blue = color & 0xff;
    uint16_t block_rows = MIN(height, LCD_TEST_BLOCK_ROWS);

    printk("LCD solid %s start: color=0x%06x bpp=%u\n",
           name,
           color,
           bytes_per_pixel);

    if ((width * block_rows * bytes_per_pixel) > ARRAY_SIZE(test_block)) {
        printk("LCD solid %s skipped: width %u exceeds line buffer %zu\n",
               name,
               width,
               ARRAY_SIZE(test_block));
        return -EINVAL;
    }

    if (bytes_per_pixel == 3) {
        for (size_t i = 0; i < (width * block_rows); i++) {
            test_block[(i * 3U) + 0U] = red;
            test_block[(i * 3U) + 1U] = green;
            test_block[(i * 3U) + 2U] = blue;
        }
    } else {
        uint16_t rgb565 = ((red & 0xf8) << 8) |
                          ((green & 0xfc) << 3) |
                          (blue >> 3);

        for (size_t i = 0; i < (width * block_rows); i++) {
            test_block[(i * 2U) + 0U] = rgb565 >> 8;
            test_block[(i * 2U) + 1U] = rgb565 & 0xff;
        }
    }

    for (uint16_t y = 0; y < height; y += block_rows) {
        uint16_t rows = MIN(block_rows, height - y);
        struct display_buffer_descriptor desc = {
            .buf_size = width * rows * bytes_per_pixel,
            .width = width,
            .height = rows,
            .pitch = width,
        };
        int ret = display_write(DISPLAY_DEV, 0, y, &desc, test_block);

        if (ret < 0) {
            printk("LCD solid %s failed at row %u rows %u: %d\n",
                   name,
                   y,
                   rows,
                   ret);
            return ret;
        }
    }

    printk("LCD solid %s OK\n", name);
    return 0;
}

static void write_lcd_solid_color_sequence(const struct display_capabilities *caps)
{
    write_lcd_solid_color(caps, 0x0000, "black");
    k_sleep(K_MSEC(700));
    write_lcd_solid_color(caps, 0xff0000, "red");
    k_sleep(K_MSEC(700));
    write_lcd_solid_color(caps, 0x00ff00, "green");
    k_sleep(K_MSEC(700));
    write_lcd_solid_color(caps, 0x0000ff, "blue");
    k_sleep(K_MSEC(700));
    write_lcd_solid_color(caps, 0xffffff, "white");
    k_sleep(K_MSEC(700));
}

static void write_lcd_test_pattern(const struct display_capabilities *caps)
{
    uint16_t width = caps->x_resolution;
    uint16_t height = caps->y_resolution;
    uint8_t bytes_per_pixel = lcd_bytes_per_pixel(caps);
    struct display_buffer_descriptor desc = {
        .buf_size = width * bytes_per_pixel,
        .width = width,
        .height = 1,
        .pitch = width,
    };
    int ret;

    printk("LCD direct display_write test start\n");

    if ((width * bytes_per_pixel) > ARRAY_SIZE(test_line)) {
        printk("LCD direct display_write skipped: width %u exceeds line buffer %zu\n",
               width,
               ARRAY_SIZE(test_line));
        return;
    }

    for (uint16_t y = 0; y < height; y++) {
        uint32_t color;
        uint16_t band_height = height / 4U;

        if (y < band_height) {
            color = 0xff0000;
        } else if (y < (band_height * 2U)) {
            color = 0x00ff00;
        } else if (y < (band_height * 3U)) {
            color = 0x0000ff;
        } else {
            color = 0xffffff;
        }

        fill_lcd_line(caps, color, width);

        ret = display_write(DISPLAY_DEV, 0, y, &desc, test_line);
        if (ret < 0) {
            printk("LCD direct display_write failed at row %u: %d\n", y, ret);
            return;
        }
    }

    printk("LCD direct display_write test OK\n");
}
#endif

#if APP_ENABLE_TOUCH
static void touch_diag_thread(void *p1, void *p2, void *p3)
{
    int last_raw = -1;
    int last_logical = -1;

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        int raw = gpio_pin_get_raw(touch_irq_gpio.port, touch_irq_gpio.pin);
        int logical = gpio_pin_get_dt(&touch_irq_gpio);

        if (raw != last_raw || logical != last_logical) {
            printk("touch IRQ state raw=%d logical=%d\n", raw, logical);
            last_raw = raw;
            last_logical = logical;
        }

        k_sleep(K_MSEC(200));
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
        select_key(evt->code);
    }
#endif
}

INPUT_CALLBACK_DEFINE(BUTTONS_DEV, button_input_cb, NULL);
#endif

#if APP_ENABLE_TOUCH
static void touch_input_cb(struct input_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);

    if (evt->type != INPUT_EV_ABS && evt->type != INPUT_EV_KEY) {
        return;
    }

    if (evt->type == INPUT_EV_ABS) {
        if (evt->code == INPUT_ABS_X) {
            printk("touch x=%d\n", evt->value);
        } else if (evt->code == INPUT_ABS_Y) {
            printk("touch y=%d\n", evt->value);
        }
    } else if (evt->code == INPUT_BTN_TOUCH) {
        printk("touch %s\n", evt->value ? "pressed" : "released");
    }
}

INPUT_CALLBACK_DEFINE(TOUCH_DEV, touch_input_cb, NULL);
#endif

int main(void)
{
#if APP_USE_LVGL_UI || APP_ENABLE_LCD
    int ret;
#endif

    printk("main start\n");

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
    if (!gpio_is_ready_dt(&touch_irq_gpio)) {
        printk("Touch IRQ GPIO is not ready\n");
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
    print_lcd_gpio_states("after backlight on");
    printk("display blanking off start\n");
    ret = display_blanking_off(DISPLAY_DEV);
    printk("display blanking off result: %d\n", ret);
    print_lcd_gpio_states("after blanking off");
#if !APP_USE_LVGL_UI
    write_lcd_solid_color_sequence(&caps);
    print_lcd_gpio_states("after solid color sequence");
    write_lcd_test_pattern(&caps);
    print_lcd_gpio_states("after color bar test");
#endif
    k_sleep(K_MSEC(1000));
#endif

#if APP_ENABLE_LED_STRIP
    led_strip_update_rgb(STRIP_DEV, &pixels[RED_PIXEL_INDEX], 1);
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

#if APP_ENABLE_TOUCH
    printk("touch IRQ diagnostic thread create start\n");
    k_thread_create(&touch_diag_thread_data,
                    touch_diag_thread_stack,
                    K_THREAD_STACK_SIZEOF(touch_diag_thread_stack),
                    touch_diag_thread,
                    NULL,
                    NULL,
                    NULL,
                    TOUCH_DIAG_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);
    printk("touch IRQ diagnostic thread create OK\n");
#endif

#if APP_ENABLE_BUTTONS
    printk("Button input test started\n");
#endif

    k_sleep(K_FOREVER);
    return 0;
}
