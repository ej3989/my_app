#include "lvgl_ej.h"

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
#define LVGL_EJ_THREAD_STACK_SIZE 8192
#define LVGL_EJ_THREAD_PRIORITY K_PRIO_PREEMPT(5)

static lv_obj_t *counter_label;
static lv_obj_t *text_box;
static lv_obj_t *log_label;
static uint32_t click_count;
static struct k_thread lvgl_ej_thread;
K_THREAD_STACK_DEFINE(lvgl_ej_thread_stack, LVGL_EJ_THREAD_STACK_SIZE);

static struct led_rgb led_strip_pixels[] = {
	{ .r = 10, .g = 0, .b = 0 },   /* Red */
	{ .r = 0, .g = 10, .b = 0 },   /* Green */
	{ .r = 0, .g = 0, .b = 10 },   /* Blue */
	{ .r = 10, .g = 10, .b = 0 },  /* Yellow */
	{ .r = 0, .g = 10, .b = 10 },  /* Cyan */
	{ .r = 10, .g = 0, .b = 10 },  /* Magenta */
	{ .r = 10, .g = 10, .b = 10 }, /* White */
	{ .r = 10, .g = 5, .b = 0 },   /* Orange */
	{ .r = 5, .g = 0, .b = 10 },   /* Purple */
	{ .r = 2, .g = 2, .b = 2 },    /* Dim white */
};

static struct led_rgb pending_led_pixel;
K_MUTEX_DEFINE(pending_led_lock);
static struct k_work led_strip_work;

#define BUTTON_ID_MAIN 0
#define BUTTON_ID_SUB 1
#define BUTTON_ID_MSGBOX 2

static void led_strip_work_handler(struct k_work *work);
static void log_box_add_text(const char *text);
static void button_event_cb(lv_event_t *event);
static void box_button_event_cb(lv_event_t *event);
static void lvgl_ej_thread_handler(void *p1, void *p2, void *p3);

#if defined(CONFIG_LVGL_EJ_STACK_USAGE_LOG)
static void lvgl_ej_print_stack_usage(void);

static void lvgl_ej_print_stack_usage(void)
{
	size_t unused;
	size_t total;
	int ret;

	ret = k_thread_stack_space_get(&lvgl_ej_thread, &unused);
	if (ret < 0) {
		printk("LVGL stack check failed: %d\n", ret);
		return;
	}

	total = K_THREAD_STACK_SIZEOF(lvgl_ej_thread_stack);
	printk("LVGL stack: used=%zu unused=%zu total=%zu\n",
	       total - unused, unused, total);
}
#endif /* CONFIG_LVGL_EJ_STACK_USAGE_LOG */

static void led_strip_work_handler(struct k_work *work)
{
	struct led_rgb pixel;

	ARG_UNUSED(work);

	k_mutex_lock(&pending_led_lock, K_FOREVER);
	pixel = pending_led_pixel;
	k_mutex_unlock(&pending_led_lock);

	led_strip_update_rgb(LED_STRIP_DEV, &pixel, 1);
}

static void log_box_add_text(const char *text)
{
	lv_label_ins_text(log_label, LV_LABEL_POS_LAST, text);
	lv_obj_scroll_to_y(text_box, LV_COORD_MAX, LV_ANIM_OFF);

}

static void box_button_event_cb(lv_event_t *event)
{
	if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
		return;
	}

	lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(event);
	lv_msgbox_close(mbox);
}
static void button_event_cb(lv_event_t *event)
{
	if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
		return;
	}

	uintptr_t user_id = (uintptr_t)lv_event_get_user_data(event);

	if (user_id == BUTTON_ID_MAIN) {
		click_count++;
		lv_label_set_text_fmt(counter_label, "Clicked: %u", click_count);
		char buf[64];

		snprintk(buf, sizeof(buf), "Clicked: %u\n", click_count);
		log_box_add_text(buf);
	} else if (user_id == BUTTON_ID_SUB) {
		static uint32_t led_count = 0;
		uint8_t color_index = led_count % ARRAY_SIZE(led_strip_pixels);

		k_mutex_lock(&pending_led_lock, K_FOREVER);
		pending_led_pixel = led_strip_pixels[color_index];
		k_mutex_unlock(&pending_led_lock);

		k_work_submit(&led_strip_work);
		char buf[64];

		snprintk(buf, sizeof(buf), "LED_click: %u\n", led_count);
		log_box_add_text(buf);
		led_count++;
	} else if (user_id == BUTTON_ID_MSGBOX) {
		lv_obj_t *mbox = lv_msgbox_create(NULL);
		lv_msgbox_add_title(mbox, "Title");
		lv_msgbox_add_text(mbox, "Hello from LVGL EJ");
		lv_obj_t *ok_btn = lv_msgbox_add_footer_button(mbox, "OK");
		lv_obj_set_size(ok_btn, 80, 40);
		lv_obj_add_event_cb(ok_btn, box_button_event_cb, LV_EVENT_CLICKED, mbox);
	}
}


static void lvgl_ej_thread_handler(void *p1, void *p2, void *p3)
{
#if defined(CONFIG_LVGL_EJ_STACK_USAGE_LOG)
	int64_t last_stack_log_time = 0;
#endif

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int ret;

	if (!device_is_ready(DISPLAY_DEV)) {
		printk("Display device is not ready\n");
		return ;
	}

	if (!device_is_ready(LED_STRIP_DEV)) {
		printk("LED strip device is not ready\n");
		return ;
	}

	ret = display_blanking_off(DISPLAY_DEV);
	if (ret < 0 && ret != -ENOSYS) {
		printk("Display blanking off failed: %d\n", ret);
		return ;
	}

	k_work_init(&led_strip_work, led_strip_work_handler);

	ret = lvgl_init();
	if (ret < 0) {
		printk("LVGL init failed: %d\n", ret);
		return ;
	}

	lv_obj_t *screen = lv_screen_active();

	lv_obj_set_style_bg_color(screen, lv_color_hex(0x20252b), LV_PART_MAIN);
	lv_obj_set_style_pad_all(screen, 24, LV_PART_MAIN);

	lv_obj_t *title = lv_label_create(screen);

	lv_label_set_text(title, "LVGL Practice 01");
	lv_obj_set_style_text_color(title, lv_color_hex(0x00ffff), LV_PART_MAIN);
	lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

	counter_label = lv_label_create(screen);
	lv_label_set_text(counter_label, "Clicked: 0");
	lv_obj_set_style_text_color(counter_label, lv_color_hex(0xd9e2ec), LV_PART_MAIN);
	lv_obj_align(counter_label, LV_ALIGN_TOP_MID, 0, 30);

	lv_obj_t *button = lv_button_create(screen);

	lv_obj_set_size(button, 90, 44);
	lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -4);
	lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)BUTTON_ID_MAIN);

	lv_obj_t *button_label = lv_label_create(button);

	lv_label_set_text(button_label, "Tap");
	lv_obj_center(button_label);

	lv_obj_t *button1 = lv_button_create(screen);

	lv_obj_set_size(button1, 90, 44);
	lv_obj_align(button1, LV_ALIGN_BOTTOM_LEFT, 0, -4);
	lv_obj_add_event_cb(button1, button_event_cb, LV_EVENT_CLICKED,
			    (void *)(uintptr_t)BUTTON_ID_SUB);

	lv_obj_t *button1_label = lv_label_create(button1);
	lv_label_set_text(button1_label, "LED");
	lv_obj_center(button1_label);

	
	lv_obj_t *button2 = lv_button_create(screen);

	lv_obj_set_size(button2, 90, 44);
	lv_obj_align(button2, LV_ALIGN_BOTTOM_RIGHT, 0, -4);
	lv_obj_add_event_cb(button2, button_event_cb, LV_EVENT_CLICKED,
			    (void *)(uintptr_t)BUTTON_ID_MSGBOX);

	lv_obj_t *button2_label = lv_label_create(button2);
	lv_label_set_text(button2_label, "MsgBox");
	lv_obj_center(button2_label);

	lv_obj_t *mbox = lv_msgbox_create(NULL);
	lv_msgbox_add_title(mbox, "Title");
	lv_msgbox_add_text(mbox, "Hello from LVGL EJ");
	lv_obj_t *ok_btn = lv_msgbox_add_footer_button(mbox, "OK");
	lv_obj_set_size(ok_btn, 80, 40);
	lv_obj_add_event_cb(ok_btn, box_button_event_cb, LV_EVENT_CLICKED, mbox);

	text_box = lv_obj_create(screen);
	lv_obj_set_size(text_box, 260, 120);
	lv_obj_align(text_box, LV_ALIGN_TOP_MID, 0, 60);
	lv_obj_add_flag(text_box, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_scroll_dir(text_box, LV_DIR_VER);
	lv_obj_set_scrollbar_mode(text_box, LV_SCROLLBAR_MODE_ON);
	lv_obj_set_style_pad_all(text_box, 8, LV_PART_MAIN);

	log_label = lv_label_create(text_box);
	lv_obj_set_width(log_label, LV_PCT(100));
	lv_label_set_long_mode(log_label, LV_LABEL_LONG_WRAP);
	lv_label_set_text(log_label, "Log start\n");

	lv_refr_now(NULL);

	led_strip_update_rgb(LED_STRIP_DEV, &led_strip_pixels[0], 1);
#if defined(CONFIG_LVGL_EJ_STACK_USAGE_LOG)
	lvgl_ej_print_stack_usage();
#endif

	while (1) {
#if defined(CONFIG_LVGL_EJ_STACK_USAGE_LOG)
		int64_t now = k_uptime_get();
#endif
		uint32_t sleep_ms = lv_timer_handler();

#if defined(CONFIG_LVGL_EJ_STACK_USAGE_LOG)
		if ((now - last_stack_log_time) >= CONFIG_LVGL_EJ_STACK_LOG_INTERVAL_MS) {
			last_stack_log_time = now;
			lvgl_ej_print_stack_usage();
		}
#endif

		if (sleep_ms == 0) {
			sleep_ms = 1;
		}

		k_msleep(MIN(sleep_ms, INT32_MAX));
	}
}

int lvgl_ej_start(void)
{
	k_thread_create(&lvgl_ej_thread, lvgl_ej_thread_stack,
			K_THREAD_STACK_SIZEOF(lvgl_ej_thread_stack), lvgl_ej_thread_handler,
			NULL, NULL, NULL, LVGL_EJ_THREAD_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(&lvgl_ej_thread, "lvgl_ej");
	return 0;
}
