/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>
#include <zephyr/task_wdt/task_wdt.h>

#include "system_watchdog.h"

LOG_MODULE_REGISTER(system_watchdog, LOG_LEVEL_INF);

#define APPLICATION_STALL_TIMEOUT_MS 120000U
#define NETWORK_OUTAGE_TIMEOUT_MS    180000U

static int application_channel = -1;
static int network_channel = -1;

static void watchdog_timeout(int channel_id, void *user_data)
{
	ARG_UNUSED(user_data);

	if (channel_id == application_channel) {
		printk("Application watchdog expired; rebooting\n");
	} else if (channel_id == network_channel) {
		printk("Network unavailable for 180 seconds; rebooting\n");
	} else {
		printk("Unknown watchdog channel %d expired; rebooting\n", channel_id);
	}

	sys_reboot(SYS_REBOOT_COLD);
}

static void log_reset_cause(void)
{
	uint32_t cause;

	if (hwinfo_get_reset_cause(&cause) != 0) {
		return;
	}

	if ((cause & RESET_WATCHDOG) != 0U) {
		LOG_WRN("Previous reset was caused by the RP2350 hardware watchdog");
	}
	if ((cause & RESET_BROWNOUT) != 0U) {
		LOG_WRN("Previous reset included a brownout condition");
	}

	(void)hwinfo_clear_reset_cause();
}

int system_watchdog_init(void)
{
	const struct device *hardware_watchdog =
		DEVICE_DT_GET_OR_NULL(DT_ALIAS(watchdog0));
	int err;

	log_reset_cause();

	if (hardware_watchdog != NULL && device_is_ready(hardware_watchdog)) {
		err = task_wdt_init(hardware_watchdog);
		if (err != 0) {
			LOG_ERR("Hardware watchdog initialization failed (err %d)", err);
			return err;
		}
	} else {
		LOG_WRN("Hardware watchdog unavailable; using task watchdog only");
		err = task_wdt_init(NULL);
		if (err != 0) {
			return err;
		}
	}

	application_channel = task_wdt_add(APPLICATION_STALL_TIMEOUT_MS,
					   watchdog_timeout, NULL);
	if (application_channel < 0) {
		LOG_ERR("Application watchdog channel setup failed (err %d)",
			application_channel);
		return application_channel;
	}

	network_channel = task_wdt_add(NETWORK_OUTAGE_TIMEOUT_MS,
				       watchdog_timeout, NULL);
	if (network_channel < 0) {
		LOG_ERR("Network watchdog channel setup failed (err %d)",
			network_channel);
		return network_channel;
	}

	LOG_INF("Watchdogs armed: application 120 s, network 180 s, hardware fallback 15 s");
	return 0;
}

void system_watchdog_feed(void)
{
	if (application_channel >= 0) {
		(void)task_wdt_feed(application_channel);
	}
}

void system_watchdog_network_healthy(void)
{
	if (network_channel >= 0) {
		(void)task_wdt_feed(network_channel);
	}
}
