#include <zephyr/kernel.h>

#include "lvgl_ej.h"
#include "app_controller.h"
#include "app_state.h"


int main(void)
{
	int ret;

	printk("LVGL practice start\n");

	app_state_init();

	ret = app_controller_start();
	if (ret < 0) {
		printk("App controller start failed: %d\n", ret);
		return 0;
	}

	ret = lvgl_ej_start();
	if (ret < 0) {
		printk("LVGL EJ start failed: %d\n", ret);
		return 0;
	}

	while (1) {
		k_msleep(1000);
	}
}
