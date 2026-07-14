#include "aht10_service.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

#define AHT10_NODE DT_NODELABEL(aht10)

static const struct device *const aht10_dev = DEVICE_DT_GET(AHT10_NODE);

int aht10_service_init(void)
{
	if (!device_is_ready(aht10_dev)) {
		return -ENODEV;
	}

	return 0;
}

int aht10_service_read(int64_t *temperature_milli_c,
			       int64_t *humidity_milli_percent)
{
	struct sensor_value temperature;
	struct sensor_value humidity;
	int ret;

	if (temperature_milli_c == NULL || humidity_milli_percent == NULL) {
		return -EINVAL;
	}

	ret = sensor_sample_fetch(aht10_dev);
	if (ret < 0) {
		return ret;
	}

	ret = sensor_channel_get(aht10_dev, SENSOR_CHAN_AMBIENT_TEMP,
				 &temperature);
	if (ret < 0) {
		return ret;
	}

	ret = sensor_channel_get(aht10_dev, SENSOR_CHAN_HUMIDITY, &humidity);
	if (ret < 0) {
		return ret;
	}

	*temperature_milli_c = sensor_value_to_milli(&temperature);
	*humidity_milli_percent = sensor_value_to_milli(&humidity);

	return 0;
}
