#include "lvgl_ej.h"
#include "app_controller.h"
#include "app_state.h"

#include <errno.h>
#include <stdint.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define DISPLAY_NODE DT_CHOSEN(zephyr_display)
#define DISPLAY_DEV DEVICE_DT_GET(DISPLAY_NODE)
#define LVGL_EJ_THREAD_STACK_SIZE 8192
#define LVGL_EJ_THREAD_PRIORITY K_PRIO_PREEMPT(5)

static lv_obj_t *counter_label;
static lv_obj_t *text_box;
static lv_obj_t *log_label;
static lv_obj_t *state_label;
static struct k_thread lvgl_ej_thread;
K_THREAD_STACK_DEFINE(lvgl_ej_thread_stack, LVGL_EJ_THREAD_STACK_SIZE);

enum button_id_ej {
	BUTTON_ID_MAIN,
	BUTTON_ID_SUB,
	BUTTON_ID_MSGBOX,
	BUTTON_ID_SETUP,
};

struct user_data_ej {
	lv_obj_t *screen;
	enum button_id_ej button_id;
};

static void log_box_add_text(const char *text);
static void button_event_cb(lv_event_t *event);
static void box_button_event_cb(lv_event_t *event);
static void screen_back_event_cb(lv_event_t *event);
static void lvgl_ej_thread_handler(void *p1, void *p2, void *p3);
static void ui_state_timer_cb(lv_timer_t *timer);

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

static void screen_back_event_cb(lv_event_t *event)
{
	if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
		return;
	}

	lv_obj_t *screen = lv_event_get_user_data(event);

	app_state_set_screen(APP_SCREEN_MAIN);

	lv_screen_load_anim(screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

static void button_event_cb(lv_event_t *event)
{
	if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
		return;
	}

	struct user_data_ej *user_data = (struct user_data_ej *)lv_event_get_user_data(event);

	if (user_data->button_id == BUTTON_ID_MAIN) {
		uint32_t count = app_state_increment_click_count();
		lv_label_set_text_fmt(counter_label, "Clicked: %u", count);
		char buf[64];

		snprintk(buf, sizeof(buf), "Clicked: %u\n", count);
		log_box_add_text(buf);
	} else if (user_data->button_id == BUTTON_ID_SUB) {
		char buf[64];
		int ret;

		ret = app_controller_send(APP_EVENT_LED_NEXT, 0);
		if (ret < 0) {
			snprintk(buf, sizeof(buf), "Event Queue full: %d\n", ret);
			log_box_add_text(buf);
			return;
		}

		log_box_add_text("LED event sent\n");

	} else if (user_data->button_id == BUTTON_ID_MSGBOX) {
		lv_obj_t *mbox = lv_msgbox_create(NULL);
		lv_msgbox_add_title(mbox, "Title");
		lv_msgbox_add_text(mbox, "Hello from LVGL EJ");
		lv_obj_t *ok_btn = lv_msgbox_add_footer_button(mbox, "OK");
		lv_obj_set_size(ok_btn, 80, 40);
		lv_obj_add_event_cb(ok_btn, box_button_event_cb, LV_EVENT_CLICKED, mbox);
	} else if (user_data->button_id == BUTTON_ID_SETUP) {
		app_state_set_screen(APP_SCREEN_SETUP);
		lv_screen_load_anim(user_data->screen,
				    LV_SCR_LOAD_ANIM_MOVE_LEFT,
				    300, 0, false);
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

	ret = display_blanking_off(DISPLAY_DEV);
	if (ret < 0 && ret != -ENOSYS) {
		printk("Display blanking off failed: %d\n", ret);
		return ;
	}

	ret = lvgl_init();
	if (ret < 0) {
		printk("LVGL init failed: %d\n", ret);
		return ;
	}

	lv_obj_t *main_screen = lv_obj_create(NULL);
	lv_obj_t *setup_screen = lv_obj_create(NULL);

	lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x20252b), LV_PART_MAIN);
	lv_obj_set_style_pad_all(main_screen, 24, LV_PART_MAIN);

	lv_obj_t *title = lv_label_create(main_screen);

	lv_label_set_text(title, "LVGL Practice 01");
	lv_obj_set_style_text_color(title, lv_color_hex(0x00ffff), LV_PART_MAIN);
	lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

	lv_obj_t *setup_title = lv_label_create(setup_screen);

	lv_label_set_text(setup_title, "Setup Screen");
	lv_obj_set_style_text_color(setup_title, lv_color_hex(0x00ffff), LV_PART_MAIN);
	lv_obj_set_style_text_font(setup_title, &lv_font_montserrat_24, LV_PART_MAIN);
	lv_obj_align(setup_title, LV_ALIGN_TOP_MID, 0, 50);
	lv_obj_set_style_bg_color(setup_screen, lv_color_hex(0x20252b), LV_PART_MAIN);
	lv_obj_set_style_pad_all(setup_screen, 24, LV_PART_MAIN);
	lv_obj_add_flag(setup_screen, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(setup_screen, screen_back_event_cb, LV_EVENT_CLICKED, main_screen);


	state_label = lv_label_create(setup_screen);

	lv_label_set_text(state_label,
			"Tap: 0\nLED: 0\nColor: 0\n");
	lv_obj_set_style_text_color(state_label,
			lv_color_hex(0xd9e2ec),
			LV_PART_MAIN);
	lv_obj_align(state_label, LV_ALIGN_TOP_MID, 0, 100);

	counter_label = lv_label_create(main_screen);
	lv_label_set_text(counter_label, "Clicked: 0");
	lv_obj_set_style_text_color(counter_label, lv_color_hex(0xd9e2ec), LV_PART_MAIN);
	lv_obj_align(counter_label, LV_ALIGN_TOP_MID, 0, 30);

	lv_obj_t *button = lv_button_create(main_screen);
	static struct user_data_ej button_data = {
		.button_id = BUTTON_ID_MAIN,
	};

	button_data.screen = main_screen;
	lv_obj_set_size(button, 74, 40);
	lv_obj_align(button, LV_ALIGN_BOTTOM_MID, -50, -4);
	lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, &button_data);

	lv_obj_t *button_label = lv_label_create(button);

	lv_label_set_text(button_label, "Tap");
	lv_obj_center(button_label);

	lv_obj_t *button1 = lv_button_create(main_screen);
	static struct user_data_ej button1_data = {
		.button_id = BUTTON_ID_SUB,
	};

	lv_obj_set_size(button1, 74, 40);
	lv_obj_align(button1, LV_ALIGN_BOTTOM_MID, -160, -4);
	button1_data.screen = main_screen;
	lv_obj_add_event_cb(button1, button_event_cb, LV_EVENT_CLICKED,
			    &button1_data);

	lv_obj_t *button1_label = lv_label_create(button1);
	lv_label_set_text(button1_label, "LED");
	lv_obj_center(button1_label);

	
	lv_obj_t *button2 = lv_button_create(main_screen);
	static struct user_data_ej button2_data = {
		.button_id = BUTTON_ID_MSGBOX,
	};

	lv_obj_set_size(button2, 74, 40);
	lv_obj_align(button2, LV_ALIGN_BOTTOM_MID, 50, -4);
	button2_data.screen = main_screen;
	lv_obj_add_event_cb(button2, button_event_cb, LV_EVENT_CLICKED,
			    &button2_data);

	lv_obj_t *button2_label = lv_label_create(button2);
	lv_label_set_text(button2_label, "MsgBox");
	lv_obj_center(button2_label);

	lv_obj_t *button3 = lv_button_create(main_screen);
	static struct user_data_ej button3_data = {
		.button_id = BUTTON_ID_SETUP,
	};
	lv_obj_set_size(button3, 74, 40);
	lv_obj_align(button3, LV_ALIGN_BOTTOM_MID, 160, -4);
	button3_data.screen = setup_screen;
	lv_obj_add_event_cb(button3, button_event_cb, LV_EVENT_CLICKED,
			    &button3_data);
	lv_obj_t *button3_label = lv_label_create(button3);
	lv_label_set_text(button3_label, "Setup");
	lv_obj_center(button3_label);


	lv_obj_t *mbox = lv_msgbox_create(NULL);
	lv_msgbox_add_title(mbox, "Title");
	lv_msgbox_add_text(mbox, "Hello from LVGL EJ");
	lv_obj_t *ok_btn = lv_msgbox_add_footer_button(mbox, "OK");
	lv_obj_set_size(ok_btn, 80, 40);
	lv_obj_add_event_cb(ok_btn, box_button_event_cb, LV_EVENT_CLICKED, mbox);

	text_box = lv_obj_create(main_screen);
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

	lv_screen_load(main_screen);
	lv_refr_now(NULL);

	lv_timer_create(ui_state_timer_cb, 1000, NULL);
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

static void ui_state_timer_cb(lv_timer_t *timer)
{
	struct app_state_snapshot snapshot;

	ARG_UNUSED(timer);

	app_state_get_snapshot(&snapshot);

	lv_label_set_text_fmt(state_label,
			    "Tap: %u\nLED: %u\nColor: %u",
			    snapshot.click_count,
			    snapshot.led_click_count,
			    snapshot.led_color_index);
}
