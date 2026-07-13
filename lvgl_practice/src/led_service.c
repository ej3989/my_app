#include "led_service.h"

#include <errno.h>
#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define LED_STRIP_NODE DT_ALIAS(led_strip)
#define LED_STRIP_DEV DEVICE_DT_GET(LED_STRIP_NODE)

static const struct led_rgb led_strip_pixels[] = {
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

static struct led_rgb pending_pixel;
static size_t next_color_index;
K_MUTEX_DEFINE(pending_pixel_lock);
static struct k_work update_work;

static void led_service_work_handler(struct k_work *work)
{
	struct led_rgb pixel;

	ARG_UNUSED(work);

	k_mutex_lock(&pending_pixel_lock, K_FOREVER);
	pixel = pending_pixel;
	k_mutex_unlock(&pending_pixel_lock);

	led_strip_update_rgb(LED_STRIP_DEV, &pixel, 1);
}

int led_service_init(void)
{
	if (!device_is_ready(LED_STRIP_DEV)) {
		return -ENODEV;
	}

	k_work_init(&update_work, led_service_work_handler);
	next_color_index = 0;

	return 0;
}

int led_service_restore(uint8_t color_index, bool enabled)
{
	struct led_rgb pixel = { 0 };
	int ret;

	if (color_index >= ARRAY_SIZE(led_strip_pixels)) {
		return -EINVAL;
	}

	if (enabled) {
		pixel = led_strip_pixels[color_index];
	}

	ret = led_strip_update_rgb(LED_STRIP_DEV, &pixel, 1);
	if (ret < 0) {
		return ret;
	}

	next_color_index = (color_index + 1U) % ARRAY_SIZE(led_strip_pixels);

	return 0;
}

int led_service_next(void)
{
	size_t color_index = next_color_index;
	int ret;

	k_mutex_lock(&pending_pixel_lock, K_FOREVER);
	pending_pixel = led_strip_pixels[color_index];
	k_mutex_unlock(&pending_pixel_lock);

	ret = k_work_submit(&update_work);
	if (ret < 0) {
		return ret;
	}

	next_color_index = (color_index + 1) % ARRAY_SIZE(led_strip_pixels);

	return (int)color_index;
}
