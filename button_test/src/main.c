#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define APP_USE_LVGL_UI 1
#define APP_ENABLE_BUTTONS 1
#define APP_ENABLE_LCD 1
#define APP_ENABLE_LED_STRIP 1

#if APP_USE_LVGL_UI
#include <lvgl.h>
#include <lvgl_zephyr.h>
#endif
#include <soc/soc_memory_layout.h>
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
#include <zephyr/multi_heap/shared_multi_heap.h>
#if APP_USE_LVGL_UI
#include <zephyr/sys/atomic.h>
#endif
#include <zephyr/sys/util.h>

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

#define LCD_WIDTH DT_PROP(DISPLAY_NODE, width)
#define LCD_HEIGHT DT_PROP(DISPLAY_NODE, height)
#define RGB565(r, g, b) ((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3))
#define RED_PIXEL_INDEX 0
#define GREEN_PIXEL_INDEX 1
#define BLUE_PIXEL_INDEX 2
#define WHITE_PIXEL_INDEX 3
#endif
#define APP_PSRAM_SMALL_TEST 1
#define APP_USE_PSRAM_FRAMEBUFFER 0

#if APP_USE_LVGL_UI
#define UI_THREAD_STACK_SIZE 8192
#define UI_THREAD_PRIORITY 5
#endif

#if APP_PSRAM_SMALL_TEST
#define PSRAM_SMALL_TEST_SIZE (32U * 1024U)
#define PSRAM_TEST_BLOCK_SIZE 1024
#define PSRAM_TEST_ALIGNMENT 32
#define PSRAM_BYTE_PATTERN 0xa5
#define PSRAM_WORD_PATTERN 0xa5a55a5a
#endif

#if APP_USE_PSRAM_FRAMEBUFFER
#define LCD_FRAMEBUFFER_SIZE (LCD_WIDTH * LCD_HEIGHT)
#define LCD_TRANSFER_ROWS 16
#define LCD_FRAMEBUFFER_ALIGNMENT 32
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
#endif

#if APP_USE_PSRAM_FRAMEBUFFER
static uint16_t lcd_framebuffer[LCD_FRAMEBUFFER_SIZE]
    __attribute__((section(".ext_ram.bss.lcd_framebuffer"), aligned(LCD_FRAMEBUFFER_ALIGNMENT)));
static uint16_t lcd_transfer_buffer[LCD_WIDTH * LCD_TRANSFER_ROWS] __aligned(32);
#endif

#if APP_USE_LVGL_UI
extern const unsigned char nanum_gothic_regular_ttf[];
extern const unsigned int nanum_gothic_regular_ttf_len;
#endif

#if APP_PSRAM_SMALL_TEST
static bool run_psram_small_test(void)
{
    uint8_t *psram_small_test_buffer;
    volatile uint32_t *word_buffer;
    const size_t words_per_block = PSRAM_TEST_BLOCK_SIZE / sizeof(uint32_t);
    const size_t total_words = PSRAM_SMALL_TEST_SIZE / sizeof(uint32_t);

    printk("PSRAM small test start\n");
    printk("PSRAM small test alloc start size=%u\n", PSRAM_SMALL_TEST_SIZE);

    psram_small_test_buffer =
        shared_multi_heap_aligned_alloc(SMH_REG_ATTR_EXTERNAL,
                                        PSRAM_TEST_ALIGNMENT,
                                        PSRAM_SMALL_TEST_SIZE);

    printk("PSRAM small test alloc result=%p\n", psram_small_test_buffer);

    if (psram_small_test_buffer == NULL) {
        printk("PSRAM small test alloc failed\n");
        return false;
    }

    word_buffer = (volatile uint32_t *)psram_small_test_buffer;

    if (!esp_ptr_external_ram(psram_small_test_buffer)) {
        printk("PSRAM small test buffer is not external RAM\n");
        shared_multi_heap_free(psram_small_test_buffer);
        return false;
    }

    printk("PSRAM small test buffer is external RAM\n");
    printk("PSRAM small test word write start\n");

    word_buffer[0] = PSRAM_WORD_PATTERN;
    word_buffer[1] = PSRAM_WORD_PATTERN;
    word_buffer[total_words - 1] = PSRAM_WORD_PATTERN;

    if (word_buffer[0] != PSRAM_WORD_PATTERN ||
        word_buffer[1] != PSRAM_WORD_PATTERN ||
        word_buffer[total_words - 1] != PSRAM_WORD_PATTERN) {
        printk("PSRAM small test edge word failed\n");
        shared_multi_heap_free(psram_small_test_buffer);
        return false;
    }

    for (size_t block = 0; block < PSRAM_SMALL_TEST_SIZE; block += PSRAM_TEST_BLOCK_SIZE) {
        size_t word_start = block / sizeof(uint32_t);
        size_t word_end = word_start + words_per_block;

        for (size_t i = word_start; i < word_end; i++) {
            word_buffer[i] = PSRAM_WORD_PATTERN;
        }
    }

    for (size_t block = 0; block < PSRAM_SMALL_TEST_SIZE; block += PSRAM_TEST_BLOCK_SIZE) {
        size_t word_start = block / sizeof(uint32_t);
        size_t word_end = word_start + words_per_block;

        for (size_t i = word_start; i < word_end; i++) {
            if (word_buffer[i] != PSRAM_WORD_PATTERN) {
                printk("PSRAM small test word failed at %zu: 0x%08x\n",
                       i, word_buffer[i]);
                shared_multi_heap_free(psram_small_test_buffer);
                return false;
            }
        }
    }

    printk("PSRAM small test byte write start\n");

    for (size_t i = 0; i < PSRAM_SMALL_TEST_SIZE; i++) {
        psram_small_test_buffer[i] = PSRAM_BYTE_PATTERN;
    }

    for (size_t i = 0; i < PSRAM_SMALL_TEST_SIZE; i++) {
        if (psram_small_test_buffer[i] != PSRAM_BYTE_PATTERN) {
            printk("PSRAM small test byte failed at %zu: 0x%02x\n",
                   i, psram_small_test_buffer[i]);
            shared_multi_heap_free(psram_small_test_buffer);
            return false;
        }
    }

    printk("PSRAM small test OK\n");
    shared_multi_heap_free(psram_small_test_buffer);
    return true;
}
#endif

#if APP_USE_PSRAM_FRAMEBUFFER
static void fill_lcd_framebuffer(void)
{
    for (uint16_t y = 0; y < LCD_HEIGHT; y++) {
        for (uint16_t x = 0; x < LCD_WIDTH; x++) {
            uint16_t color;

            if (x < LCD_WIDTH / 2 && y < LCD_HEIGHT / 2) {
                color = RGB565(255, 0, 0);
            } else if (x >= LCD_WIDTH / 2 && y < LCD_HEIGHT / 2) {
                color = RGB565(0, 255, 0);
            } else if (x < LCD_WIDTH / 2 && y >= LCD_HEIGHT / 2) {
                color = RGB565(0, 0, 255);
            } else {
                color = RGB565(255, 255, 255);
            }

            lcd_framebuffer[y * LCD_WIDTH + x] = color;
        }
    }
}

static void test_lcd_framebuffer_from_psram(void)
{
    const size_t framebuffer_size = LCD_FRAMEBUFFER_SIZE * sizeof(*lcd_framebuffer);
    int ret = 0;

    printk("PSRAM framebuffer addr=%p size=%zu\n",
           lcd_framebuffer, framebuffer_size);
    printk("PSRAM framebuffer fill start\n");

    fill_lcd_framebuffer();

    printk("PSRAM framebuffer fill OK\n");
    printk("LCD framebuffer: %ux%u, %zu bytes, addr=%p\n",
           LCD_WIDTH, LCD_HEIGHT, framebuffer_size, lcd_framebuffer);

    printk("LCD line-buffer display_write start\n");

    for (uint16_t y = 0; y < LCD_HEIGHT; y += LCD_TRANSFER_ROWS) {
        uint16_t rows = MIN(LCD_TRANSFER_ROWS, LCD_HEIGHT - y);
        const struct display_buffer_descriptor desc = {
            .buf_size = LCD_WIDTH * rows * sizeof(*lcd_transfer_buffer),
            .width = LCD_WIDTH,
            .height = rows,
            .pitch = LCD_WIDTH,
        };

        memcpy(lcd_transfer_buffer,
               &lcd_framebuffer[y * LCD_WIDTH],
               LCD_WIDTH * rows * sizeof(*lcd_transfer_buffer));

        ret = display_write(DISPLAY_DEV, 0, y, &desc, lcd_transfer_buffer);
        if (ret < 0) {
            printk("LCD line-buffer display_write failed at y=%u: %d\n", y, ret);
            break;
        }
    }

    if (ret == 0) {
        printk("LCD line-buffer display_write OK\n");
    }
}
#endif

#if APP_USE_LVGL_UI
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
    printk("peripheral drivers disabled for PSRAM isolation\n");
#endif

#if APP_PSRAM_SMALL_TEST
    if (!run_psram_small_test()) {
        printk("PSRAM small test failed\n");
        return 0;
    }
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

#if APP_USE_PSRAM_FRAMEBUFFER
    test_lcd_framebuffer_from_psram();
    k_sleep(K_MSEC(1000));
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
#else
    printk("PSRAM isolation test finished\n");
#endif

    k_sleep(K_FOREVER);
    return 0;
}
