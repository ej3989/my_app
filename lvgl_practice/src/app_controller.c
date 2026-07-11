#include "app_controller.h"
#include "app_state.h"
#include "led_service.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define APP_THREAD_STACK_SIZE    2048
#define APP_THREAD_PRIORITY      K_PRIO_PREEMPT(6)
#define APP_EVENT_QUEUE_LENGTH  8
#define STATUS_TIMER_PERIOD     K_SECONDS(5)

static void status_timer_expiry(struct k_timer *timer);
static void state_log_work_handler(struct k_work *work);
#if defined(CONFIG_APP_STACK_USAGE_LOG)
static void app_print_stack_usage(void);
#endif

K_TIMER_DEFINE(status_timer, status_timer_expiry, NULL);
K_WORK_DELAYABLE_DEFINE(state_log_work, state_log_work_handler);
K_MSGQ_DEFINE(app_event_queue, sizeof(struct app_event), APP_EVENT_QUEUE_LENGTH, 4);
K_THREAD_STACK_DEFINE(app_thread_stack, APP_THREAD_STACK_SIZE);
K_SEM_DEFINE(app_ready_sem, 0, 1);
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
		printk("APP stack check failed: %d\n", ret);
		return;
	}

	total = K_THREAD_STACK_SIZEOF(app_thread_stack);
	printk("APP stack: used=%zu unused=%zu total=%zu\n",
	       total - unused, unused, total);
}
#endif /* CONFIG_APP_STACK_USAGE_LOG */

static void app_thread_handler(void *p1, void *p2, void *p3)
{
	struct app_event event;
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	ret = led_service_init();
	app_init_result = ret;
	k_sem_give(&app_ready_sem);

	if (ret < 0) {
		printk("LED service init failed: %d\n", ret);
		return;
	}

	while (1) {
		ret = k_msgq_get(&app_event_queue, &event, K_FOREVER);
		if (ret < 0) {
			printk("App event receive failed: %d\n", ret);
			continue;
		}

		switch (event.type) {
		case APP_EVENT_LED_NEXT: {
			uint32_t led_count;

			led_count = app_state_increment_led_click_count();

			ret = led_service_next();
			if (ret < 0) {
				printk("LED update failed: %d\n", ret);
				break;
			}

			app_state_set_led_color_index((uint8_t)ret);
			printk("LED event: count=%u, color=%d\n", led_count, ret);
			(void)k_work_reschedule(&state_log_work, K_SECONDS(2));
			break;
		}
		case APP_EVENT_STATUS_TICK: {
			struct app_state_snapshot snapshot;
			atomic_val_t dropped;

			app_state_get_snapshot(&snapshot);
			dropped = atomic_get(&dropped_event_count);

			printk("Status: uptime=%u screen=%d tap=%u led=%u color=%u dropped=%ld\n",
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
		default:
			printk("Unhandled event type: %d\n", event.type);
			break;
		}
	}
}

int app_controller_send(enum app_event_type type, uint32_t value)
{
	struct app_event event = {
		.type = type,
		.value = value,
	};
	int ret;

	ret = k_msgq_put(&app_event_queue, &event, K_NO_WAIT);
	if (ret < 0) {
		atomic_inc(&dropped_event_count);
	}

	return ret;
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
		printk("App initialization timeout: %d\n", ret);
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

static void state_log_work_handler(struct k_work *work)
{
	struct app_state_snapshot snapshot;

	ARG_UNUSED(work);

	app_state_get_snapshot(&snapshot);

	printk("Delayed state: tap=%u led=%u color=%u\n",
	       snapshot.click_count,
	       snapshot.led_click_count,
	       snapshot.led_color_index);
}
