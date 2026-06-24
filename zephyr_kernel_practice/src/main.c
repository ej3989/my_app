/*
 * Zephyr kernel practice app.
 *
 * Set PRACTICE_EXERCISE in this file or pass -DPRACTICE_EXERCISE=N to CMake.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#ifndef PRACTICE_EXERCISE
#define PRACTICE_EXERCISE 3
#endif

#define STACK_SIZE 1024
#define THREAD_PRIORITY 5

struct sample_msg {
	int producer_id;
	int value;
};

K_MSGQ_DEFINE(sample_msgq, sizeof(struct sample_msg), 8, 4);
K_SEM_DEFINE(signal_sem, 0, 1);
K_MUTEX_DEFINE(shared_counter_mutex);

K_THREAD_STACK_DEFINE(thread_a_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_b_stack, STACK_SIZE);

static struct k_thread thread_a;
static struct k_thread thread_b;

static struct k_work immediate_work;
static struct k_work_delayable delayable_work;
static struct k_work timer_work;
static struct k_timer practice_timer;

static atomic_t fake_irq_count;
static atomic_t delayable_count;
static atomic_t timer_count;
static int shared_counter;

static void thread_counter(void *name_ptr, void *start_ptr, void *unused)
{
	const char *name = name_ptr;
	int value = POINTER_TO_INT(start_ptr);

	ARG_UNUSED(unused);

	for (int i = 0; i < 5; i++) {
		printk("[%s] value=%d\n", name, value++);
		k_sleep(K_MSEC(300));
	}

	printk("[%s] done\n", name);
}

static void run_thread_demo(void)
{
	printk("exercise 1: two threads run independently\n");

	k_thread_create(&thread_a, thread_a_stack, STACK_SIZE,
			thread_counter, "thread A", INT_TO_POINTER(10), NULL,
			THREAD_PRIORITY, 0, K_NO_WAIT);

	k_thread_create(&thread_b, thread_b_stack, STACK_SIZE,
			thread_counter, "thread B", INT_TO_POINTER(100), NULL,
			THREAD_PRIORITY, 0, K_NO_WAIT);

	for (int i = 0; i < 6; i++) {
		printk("[main] still alive %d\n", i);
		k_sleep(K_MSEC(500));
	}
}

static void sem_waiter(void *unused_a, void *unused_b, void *unused_c)
{
	ARG_UNUSED(unused_a);
	ARG_UNUSED(unused_b);
	ARG_UNUSED(unused_c);

	for (int i = 0; i < 5; i++) {
		printk("[waiter] waiting for semaphore\n");
		k_sem_take(&signal_sem, K_FOREVER);
		printk("[waiter] got signal %d\n", i);
	}
}

static void run_semaphore_demo(void)
{
	printk("exercise 2: main thread signals a worker with k_sem_give()\n");

	k_thread_create(&thread_a, thread_a_stack, STACK_SIZE,
			sem_waiter, NULL, NULL, NULL,
			THREAD_PRIORITY, 0, K_NO_WAIT);

	for (int i = 0; i < 5; i++) {
		k_sleep(K_MSEC(700));
		printk("[main] give semaphore %d\n", i);
		k_sem_give(&signal_sem);
	}
}

static void msgq_producer(void *unused_a, void *unused_b, void *unused_c)
{
	ARG_UNUSED(unused_a);
	ARG_UNUSED(unused_b);
	ARG_UNUSED(unused_c);

	for (int i = 0; i < 8; i++) {
		struct sample_msg msg = {
			.producer_id = 1,
			.value = i * 10,
		};

		k_msgq_put(&sample_msgq, &msg, K_FOREVER);
		printk("[producer] put value=%d\n", msg.value);
		k_sleep(K_MSEC(250));
	}
}

static void msgq_consumer(void *unused_a, void *unused_b, void *unused_c)
{
	ARG_UNUSED(unused_a);
	ARG_UNUSED(unused_b);
	ARG_UNUSED(unused_c);

	for (int i = 0; i < 8; i++) {
		struct sample_msg msg;

		k_msgq_get(&sample_msgq, &msg, K_FOREVER);
		printk("[consumer] producer=%d value=%d\n", msg.producer_id, msg.value);
	}
}

static void run_msgq_demo(void)
{
	printk("exercise 3: producer and consumer communicate through k_msgq\n");

	k_thread_create(&thread_a, thread_a_stack, STACK_SIZE,
			msgq_producer, NULL, NULL, NULL,
			THREAD_PRIORITY, 0, K_NO_WAIT);

	k_thread_create(&thread_b, thread_b_stack, STACK_SIZE,
			msgq_consumer, NULL, NULL, NULL,
			THREAD_PRIORITY, 0, K_NO_WAIT);

	k_sleep(K_SECONDS(4));
}

static void immediate_work_handler(struct k_work *work)
{
	int count = atomic_get(&fake_irq_count);

	ARG_UNUSED(work);

	printk("[work] handling fake IRQ count=%d in workqueue thread\n", count);
}

static void fake_gpio_isr_event(void)
{
	atomic_inc(&fake_irq_count);
	printk("[fake ISR] submit immediate work\n");
	k_work_submit(&immediate_work);
}

static void run_immediate_work_demo(void)
{
	printk("exercise 4: simulated ISR offloads processing with k_work_submit()\n");

	k_work_init(&immediate_work, immediate_work_handler);

	for (int i = 0; i < 5; i++) {
		fake_gpio_isr_event();
		k_sleep(K_MSEC(500));
	}
}

static void delayable_work_handler(struct k_work *work)
{
	int count = atomic_inc(&delayable_count) + 1;
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);

	printk("[delayable] fired count=%d\n", count);

	if (count < 5) {
		printk("[delayable] reschedule itself after 300 ms\n");
		k_work_reschedule(dwork, K_MSEC(300));
	}
}

static void run_delayable_work_demo(void)
{
	printk("exercise 5: delayable work reschedules itself\n");

	atomic_set(&delayable_count, 0);
	k_work_init_delayable(&delayable_work, delayable_work_handler);
	k_work_reschedule(&delayable_work, K_MSEC(300));

	k_sleep(K_SECONDS(3));
}

static void timer_work_handler(struct k_work *work)
{
	int count = atomic_get(&timer_count);

	ARG_UNUSED(work);

	printk("[timer work] safe thread-context processing, timer_count=%d\n", count);
}

static void timer_expiry_handler(struct k_timer *timer)
{
	int count = atomic_inc(&timer_count) + 1;

	ARG_UNUSED(timer);

	printk("[timer expiry] count=%d, offload to workqueue\n", count);
	k_work_submit(&timer_work);
}

static void run_timer_demo(void)
{
	printk("exercise 6: timer expiry callback offloads real work\n");

	atomic_set(&timer_count, 0);
	k_work_init(&timer_work, timer_work_handler);
	k_timer_init(&practice_timer, timer_expiry_handler, NULL);
	k_timer_start(&practice_timer, K_MSEC(500), K_MSEC(500));

	k_sleep(K_SECONDS(4));
	k_timer_stop(&practice_timer);
	printk("[main] timer stopped\n");
}

static void mutex_counter_thread(void *name_ptr, void *use_mutex_ptr, void *unused)
{
	const char *name = name_ptr;
	bool use_mutex = POINTER_TO_INT(use_mutex_ptr);

	ARG_UNUSED(unused);

	for (int i = 0; i < 5; i++) {
		if (use_mutex) {
			k_mutex_lock(&shared_counter_mutex, K_FOREVER);
		}

		int before = shared_counter;

		printk("[%s] read shared_counter=%d\n", name, before);
		k_sleep(K_MSEC(100));
		shared_counter = before + 1;
		printk("[%s] wrote shared_counter=%d\n", name, shared_counter);

		if (use_mutex) {
			k_mutex_unlock(&shared_counter_mutex);
		}

		k_sleep(K_MSEC(100));
	}
}

static void run_mutex_demo(void)
{
	bool use_mutex = true;

	printk("exercise 7: protect shared_counter with k_mutex\n");
	printk("[main] use_mutex=%d\n", use_mutex);

	shared_counter = 0;

	k_thread_create(&thread_a, thread_a_stack, STACK_SIZE,
			mutex_counter_thread, "thread A", INT_TO_POINTER(use_mutex), NULL,
			THREAD_PRIORITY, 0, K_NO_WAIT);

	k_thread_create(&thread_b, thread_b_stack, STACK_SIZE,
			mutex_counter_thread, "thread B", INT_TO_POINTER(use_mutex), NULL,
			THREAD_PRIORITY, 0, K_NO_WAIT);

	k_sleep(K_SECONDS(3));
	printk("[main] final shared_counter=%d\n", shared_counter);
}

int main(void)
{
	printk("\nZephyr kernel practice app started\n");
	printk("PRACTICE_EXERCISE=%d\n\n", PRACTICE_EXERCISE);

#if PRACTICE_EXERCISE == 1
	run_thread_demo();
#elif PRACTICE_EXERCISE == 2
	run_semaphore_demo();
#elif PRACTICE_EXERCISE == 3
	run_msgq_demo();
#elif PRACTICE_EXERCISE == 4
	run_immediate_work_demo();
#elif PRACTICE_EXERCISE == 5
	run_delayable_work_demo();
#elif PRACTICE_EXERCISE == 6
	run_timer_demo();
#elif PRACTICE_EXERCISE == 7
	run_mutex_demo();
#else
#error "Set PRACTICE_EXERCISE to a value from 1 to 7"
#endif

	printk("\nExercise finished. Change PRACTICE_EXERCISE and rebuild for the next one.\n");

	return 0;
}
