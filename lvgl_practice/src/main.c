#include <zephyr/kernel.h>

#include "lvgl_ej.h"

int main(void)
{
	int ret;

	printk("LVGL practice start\n");

	ret = lvgl_ej_start();
	if (ret < 0) {
		printk("LVGL EJ start failed: %d\n", ret);
		return 0;
	}

	while (1) {
		k_msleep(1000);
	}
}
