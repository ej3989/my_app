/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <whd.h>

#include "status_led.h"

LOG_MODULE_REGISTER(status_led, LOG_LEVEL_INF);

#define STATUS_LED_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec status_led =
	GPIO_DT_SPEC_GET(STATUS_LED_NODE, gpios);

/* Provided by the AIROC driver and WHD. */
extern whd_interface_t airoc_wifi_get_whd_interface(void);
extern whd_result_t whd_wifi_set_iovar_buffer(whd_interface_t ifp,
					       const char *iovar,
					       void *in_buffer,
					       uint16_t in_buffer_length);

int status_led_turn_on(void)
{
	whd_interface_t ifp;
	uint32_t gpioout[2] = {
		BIT(status_led.pin), /* GPIO mask */
		BIT(status_led.pin), /* GPIO value */
	};
	whd_result_t whd_result;
	int err;

	if (!gpio_is_ready_dt(&status_led)) {
		LOG_ERR("CYW43439 onboard LED GPIO is not ready");
		return -ENODEV;
	}

	/* Configure the Zephyr GPIO object first, but do not trust its write.
	 * Zephyr's current CYW43 GPIO driver sends only one uint32_t to the
	 * "gpioout" IOVAR. CYW43439 requires two uint32_t values: mask and
	 * value. Send the complete payload below as a project-local workaround.
	 */
	err = gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		LOG_ERR("Failed to configure CYW43439 onboard LED (err %d)", err);
		return err;
	}

	ifp = airoc_wifi_get_whd_interface();
	if (ifp == NULL) {
		LOG_ERR("CYW43439 WHD interface is unavailable");
		return -ENODEV;
	}

	whd_result = whd_wifi_set_iovar_buffer(ifp, "gpioout", gpioout,
						 sizeof(gpioout));
	if (whd_result != WHD_SUCCESS) {
		LOG_ERR("CYW43439 LED gpioout mask/value write failed (0x%08x)",
			(unsigned int)whd_result);
		return -EIO;
	}

	LOG_INF("CYW43439 onboard LED ON using gpioout mask/value payload");
	return 0;
}
