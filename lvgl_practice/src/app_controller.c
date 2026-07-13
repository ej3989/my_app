#include "app_controller.h"
#include "app_state.h"
#include "app_settings.h"
#include "led_service.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(app_controller, LOG_LEVEL_INF);

#define APP_THREAD_STACK_SIZE    2048
#define APP_THREAD_PRIORITY      K_PRIO_PREEMPT(6)
#define APP_EVENT_QUEUE_LENGTH  8
#define STATUS_TIMER_PERIOD     K_SECONDS(5)
#define APP_LOG_TEXT_SIZE       64
#define APP_LOG_BLOCK_COUNT     4

BUILD_ASSERT(APP_LOG_BLOCK_COUNT <= APP_EVENT_QUEUE_LENGTH,
	     "Log slab blocks must fit in event queue");
BUILD_ASSERT(APP_LOG_TEXT_SIZE > 1,
	     "Log message buffer is too small");

struct app_log_message {
	char text[APP_LOG_TEXT_SIZE];
};

static void status_timer_expiry(struct k_timer *timer);
static void settings_save_work_handler(struct k_work *work);
static int app_controller_put_event(const struct app_event *event);
#if defined(CONFIG_APP_STACK_USAGE_LOG)
static void app_print_stack_usage(void);
#endif

K_TIMER_DEFINE(status_timer, status_timer_expiry, NULL);
K_WORK_DELAYABLE_DEFINE(settings_save_work, settings_save_work_handler);
K_MSGQ_DEFINE(app_event_queue, sizeof(struct app_event), APP_EVENT_QUEUE_LENGTH, 4);
K_THREAD_STACK_DEFINE(app_thread_stack, APP_THREAD_STACK_SIZE);
K_SEM_DEFINE(app_ready_sem, 0, 1);

K_MEM_SLAB_DEFINE_STATIC_TYPE(log_message_slab,
			      struct app_log_message,
			      APP_LOG_BLOCK_COUNT);

static struct k_thread app_thread;
static atomic_t dropped_event_count;
static int app_init_result;

#if defined(CONFIG_APP_STACK_USAGE_LOG)
static void app_print_stack_usage(void)
{
	size_t unused;
	size_t total;
	int ret;

	ret = k_thread_stack_space_get(&app_thread, &unused);
	if (ret < 0) {
		LOG_ERR("APP stack check failed: %d", ret);
		return;
	}

	total = K_THREAD_STACK_SIZEOF(app_thread_stack);
	LOG_INF("APP stack: used=%zu unused=%zu total=%zu",
		total - unused, unused, total);
}
#endif /* CONFIG_APP_STACK_USAGE_LOG */

static void app_thread_handler(void *p1, void *p2, void *p3)
{
	struct app_event event;
	struct app_persistent_settings settings;
	struct app_state_snapshot snapshot;
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	ret = app_settings_init();
	if (ret < 0) {
		LOG_ERR("Settings init failed: %d", ret);
		goto init_done;
	}

	ret = app_settings_load(&settings);
	if (ret < 0) {
		LOG_WRN("Settings load failed, using default: %d", ret);
	} else {
		app_state_set_led_color_index(settings.led_color_index);
		app_state_set_led_enabled(settings.led_enabled);

		LOG_INF("Settings Loaded: color=%u enabled=%d",
			settings.led_color_index,
			settings.led_enabled);
	}
init_done:
	ret = led_service_init();
	if (ret < 0) {
		goto service_init_done;
	}

	app_state_get_snapshot(&snapshot);
	ret = led_service_restore(snapshot.led_color_index,
				  snapshot.led_enabled);
	if (ret == -EINVAL) {
		LOG_WRN("Invalid saved LED state, using defaults");
		app_state_set_led_color_index(0);
		app_state_set_led_enabled(true);
		ret = led_service_restore(0, true);
	}

service_init_done:
	app_init_result = ret;
	k_sem_give(&app_ready_sem);

	if (ret < 0) {
		LOG_ERR("LED service init failed: %d", ret);
		return;
	}

	while (1) {
		ret = k_msgq_get(&app_event_queue, &event, K_FOREVER);
		if (ret < 0) {
			LOG_ERR("App event receive failed: %d", ret);
			continue;
		}

		switch (event.type) {
		case APP_EVENT_LED_NEXT: {
			uint32_t led_count;

			led_count = app_state_increment_led_click_count();

			ret = led_service_next();
			if (ret < 0) {
				LOG_ERR("LED update failed: %d", ret);
				break;
			}

			app_state_set_led_color_index((uint8_t)ret);
			app_state_set_led_enabled(true);
			LOG_INF("LED event: count=%u color=%d", led_count, ret);
			(void)k_work_reschedule(&settings_save_work, K_SECONDS(2));
			break;
		}
		case APP_EVENT_STATUS_TICK: {
			struct app_state_snapshot snapshot;
			atomic_val_t dropped;

			app_state_get_snapshot(&snapshot);
			dropped = atomic_get(&dropped_event_count);

			LOG_INF("Status: uptime=%u screen=%d tap=%u led=%u color=%u dropped=%ld",
				event.value,
				snapshot.current_screen,
				snapshot.click_count,
				snapshot.led_click_count,
				snapshot.led_color_index,
				(long)dropped);
#if defined(CONFIG_APP_STACK_USAGE_LOG)
			app_print_stack_usage();
#endif
			break;
		}
		case APP_EVENT_LOG_MESSAGE:
			if (event.log_message == NULL) {
				LOG_WRN("Log event has no message");
				break;
			}
			LOG_INF("App log: %s", (char *)event.log_message->text);

			k_mem_slab_free(&log_message_slab,
					event.log_message);
			break;
		case APP_EVENT_SAVE_SETTINGS: {
			struct app_state_snapshot snapshot;
			struct app_persistent_settings settings;

			app_state_get_snapshot(&snapshot);

			settings = (struct app_persistent_settings) {
				.version = APP_SETTINGS_VERSION,
				.led_color_index = snapshot.led_color_index,
				.led_enabled = snapshot.led_enabled,
				.reserved = 0,
			};
			ret = app_settings_save(&settings);
			if (ret < 0) {
				LOG_ERR("Settings save failed: %d", ret);
			} else {
				LOG_INF("Settings save: color=%u enabled=%d",
					settings.led_color_index,
					settings.led_enabled);
			}
			break;
		}
		default:
			LOG_WRN("Unhandled event type: %d", event.type);
			break;
		}
	}
}

int app_controller_send(enum app_event_type type, uint32_t value)
{
	struct app_event event = {
		.type = type,
		.value = value,
		.log_message = NULL,
	};

	return app_controller_put_event(&event);
}

int app_controller_start(void)
{
	int ret;

	k_thread_create(&app_thread,
			app_thread_stack,
			K_THREAD_STACK_SIZEOF(app_thread_stack),
			app_thread_handler,
			NULL, NULL, NULL,
			APP_THREAD_PRIORITY,
			0,
			K_NO_WAIT);
	k_thread_name_set(&app_thread, "app_controller");

	ret = k_sem_take(&app_ready_sem, K_SECONDS(3));
	if (ret < 0) {
		LOG_ERR("App initialization timeout: %d", ret);
		return ret;
	}

	if (app_init_result < 0) {
		return app_init_result;
	}

	k_timer_start(&status_timer,
		      STATUS_TIMER_PERIOD,
		      STATUS_TIMER_PERIOD);

	return 0;
}

static void status_timer_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	(void)app_controller_send(APP_EVENT_STATUS_TICK,
				  k_uptime_get_32());
}

static void settings_save_work_handler(struct k_work *work)
{
	int ret;

	ARG_UNUSED(work);

	ret = app_controller_send(APP_EVENT_SAVE_SETTINGS, 0);
	if (ret < 0) {
		LOG_WRN("Settings save event send failed: %d", ret);
	}
}

static int app_controller_put_event(const struct app_event *event)
{
	int ret;

	__ASSERT(event != NULL, "event must not be NULL");
	if (event == NULL) {
		return -EINVAL;
	}

	ret = k_msgq_put(&app_event_queue, event, K_NO_WAIT);
	if (ret < 0) {
		atomic_inc(&dropped_event_count);
	}

	return ret;
}

int app_controller_send_log(const char *text)
{
	struct app_log_message *message;
	struct app_event event;
	int ret;

	if (text == NULL) {
		LOG_WRN("Cannot send a NULL log message");
		return -EINVAL;
	}

	ret = k_mem_slab_alloc(&log_message_slab,
			       (void **)&message,
			       K_NO_WAIT);

	if (ret < 0) {
		atomic_inc(&dropped_event_count);
		LOG_WRN("Log slab allocation failed: %d", ret);
		return ret;
	}

	snprintk(message->text, sizeof(message->text), "%s", text);

	event = (struct app_event) {
		.type = APP_EVENT_LOG_MESSAGE,
		.value = 0,
		.log_message = message,
	};

	ret = app_controller_put_event(&event);
	if (ret < 0) {
		k_mem_slab_free(&log_message_slab, message);
		return ret;
	}

	return 0;
}
