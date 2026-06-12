#include <inttypes.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/led.h>
#include <zephyr/input/input.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

#define BUTTONS_NODE DT_ALIAS(app_buttons)
#define BUTTONS_DEV DEVICE_DT_GET(BUTTONS_NODE)
#define STRIP_NODE DT_ALIAS(led_strip)
#define STRIP_DEV DEVICE_DT_GET(STRIP_NODE)
#define DISPLAY_NODE DT_CHOSEN(zephyr_display)
#define DISPLAY_DEV DEVICE_DT_GET(DISPLAY_NODE)
#define LCD_BL_NODE DT_NODELABEL(lcd_bl)
#define LCD_BL_DEV DEVICE_DT_GET(LCD_BL_NODE)

#define LCD_WIDTH 240
#define LCD_HEIGHT 320

#define RGB565_BLACK 0x0000
#define RGB565_RED 0xf800
#define RGB565_GREEN 0x07e0
#define RGB565_BLUE 0x001f
#define RGB565_WHITE 0xffff

#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define FONT_SCALE 6
#define FONT_SPACING 2

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
static uint16_t lcd_line[LCD_WIDTH];
static atomic_t pending_key = ATOMIC_INIT(-1);
static struct k_work lcd_work;

static const uint8_t glyph_k[FONT_HEIGHT] = {
    0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11
};

static const uint8_t glyph_e[FONT_HEIGHT] = {
    0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f
};

static const uint8_t glyph_y[FONT_HEIGHT] = {
    0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04
};

static const uint8_t glyph_0[FONT_HEIGHT] = {
    0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e
};

static const uint8_t glyph_1[FONT_HEIGHT] = {
    0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e
};

static const uint8_t glyph_2[FONT_HEIGHT] = {
    0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f
};

static const uint8_t glyph_3[FONT_HEIGHT] = {
    0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e
};

static const uint8_t *lcd_get_glyph(char c)
{
    switch (c) {
    case 'K':
        return glyph_k;
    case 'E':
        return glyph_e;
    case 'Y':
        return glyph_y;
    case '0':
        return glyph_0;
    case '1':
        return glyph_1;
    case '2':
        return glyph_2;
    case '3':
        return glyph_3;
    default:
        return NULL;
    }
}

static int lcd_write_line(uint16_t x, uint16_t y, uint16_t width)
{
    struct display_buffer_descriptor desc = {
        .buf_size = width * sizeof(lcd_line[0]),
        .width = width,
        .height = 1,
        .pitch = width,
    };

    return display_write(DISPLAY_DEV, x, y, &desc, lcd_line);
}

static int lcd_fill_screen(uint16_t color)
{
    uint16_t pixel = sys_cpu_to_le16(color);

    for (size_t x = 0; x < LCD_WIDTH; x++) {
        lcd_line[x] = pixel;
    }

    for (size_t y = 0; y < LCD_HEIGHT; y++) {
        int ret = lcd_write_line(0, y, LCD_WIDTH);

        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static int lcd_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg)
{
    const uint8_t *glyph = lcd_get_glyph(c);
    uint16_t width = FONT_WIDTH * FONT_SCALE;

    if (glyph == NULL) {
        return 0;
    }

    if ((x + width) > LCD_WIDTH || (y + (FONT_HEIGHT * FONT_SCALE)) > LCD_HEIGHT) {
        return -EINVAL;
    }

    for (size_t row = 0; row < FONT_HEIGHT; row++) {
        for (size_t scale_y = 0; scale_y < FONT_SCALE; scale_y++) {
            for (size_t col = 0; col < FONT_WIDTH; col++) {
                bool on = (glyph[row] & BIT(FONT_WIDTH - 1 - col)) != 0;
                uint16_t pixel = sys_cpu_to_le16(on ? fg : bg);

                for (size_t scale_x = 0; scale_x < FONT_SCALE; scale_x++) {
                    lcd_line[(col * FONT_SCALE) + scale_x] = pixel;
                }
            }

            int ret = lcd_write_line(x, y + (row * FONT_SCALE) + scale_y, width);

            if (ret < 0) {
                return ret;
            }
        }
    }

    return 0;
}

static int lcd_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    uint16_t cursor_x = x;
    uint16_t step = (FONT_WIDTH * FONT_SCALE) + FONT_SPACING;

    for (size_t i = 0; text[i] != '\0'; i++) {
        int ret = lcd_draw_char(cursor_x, y, text[i], fg, bg);

        if (ret < 0) {
            return ret;
        }

        cursor_x += step;
    }

    return 0;
}

static void lcd_show_key(uint32_t key_code)
{
    const char *text = "KEY0";
    uint16_t bg = RGB565_RED;

    switch (key_code) {
    case INPUT_KEY_1:
        text = "KEY0";
        bg = RGB565_RED;
        break;
    case INPUT_KEY_2:
        text = "KEY1";
        bg = RGB565_GREEN;
        break;
    case INPUT_KEY_3:
        text = "KEY2";
        bg = RGB565_BLUE;
        break;
    case INPUT_KEY_4:
        text = "KEY3";
        bg = RGB565_BLACK;
        break;
    default:
        return;
    }

    int ret = lcd_fill_screen(bg);

    if (ret < 0) {
        printk("lcd_fill_screen failed: %d\n", ret);
        return;
    }

    ret = lcd_draw_text(45, 135, text, RGB565_WHITE, bg);

    if (ret < 0) {
        printk("lcd_draw_text failed: %d\n", ret);
    }
}

static void lcd_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    uint32_t key_code = (uint32_t)atomic_get(&pending_key);

    lcd_show_key(key_code);
}

static void button_input_cb(struct input_event *evt, void *user_data) {
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
        current_pixel_index = evt->code - INPUT_KEY_1;
        led_strip_update_rgb(STRIP_DEV, &pixels[current_pixel_index], 1);
        atomic_set(&pending_key, evt->code);
        k_work_submit(&lcd_work);
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
    k_work_init(&lcd_work, lcd_work_handler);

    led_strip_update_rgb(STRIP_DEV, &pixels[RED_PIXEL_INDEX], 1);
    atomic_set(&pending_key, INPUT_KEY_1);
    k_work_submit(&lcd_work);

    printk("Button input test started\n");

    k_sleep(K_FOREVER);
    return 0;
}
