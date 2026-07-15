/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT aosong_aht10

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(aht10, CONFIG_SENSOR_LOG_LEVEL);

#define AHT10_POWER_ON_WAIT_MS       20
#define AHT10_INIT_WAIT_MS           10
#define AHT10_MEASUREMENT_WAIT_MS    80
#define AHT10_MEASUREMENT_FRAME_SIZE 6

#define AHT10_STATUS_BUSY       BIT(7)
#define AHT10_STATUS_CALIBRATED BIT(3)

static const uint8_t aht10_init_command[] = {0xE1, 0x08, 0x00};
static const uint8_t aht10_measure_command[] = {0xAC, 0x33, 0x00};

struct aht10_config {
	struct i2c_dt_spec bus;
};

struct aht10_data {
	uint32_t temperature_raw;
	uint32_t humidity_raw;
};

static int aht10_read_status(const struct aht10_config *config, uint8_t *status)
{
	return i2c_read_dt(&config->bus, status, sizeof(*status));
}

static int aht10_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct aht10_config *config = dev->config;
	struct aht10_data *data = dev->data;
	uint8_t frame[AHT10_MEASUREMENT_FRAME_SIZE];
	int ret;

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_AMBIENT_TEMP &&
	    chan != SENSOR_CHAN_HUMIDITY) {
		return -ENOTSUP;
	}

	ret = i2c_write_dt(&config->bus, aht10_measure_command,
			   sizeof(aht10_measure_command));
	if (ret < 0) {
		LOG_ERR("Failed to trigger measurement: %d", ret);
		return ret;
	}

	k_msleep(AHT10_MEASUREMENT_WAIT_MS);

	ret = i2c_read_dt(&config->bus, frame, sizeof(frame));
	if (ret < 0) {
		LOG_ERR("Failed to read measurement: %d", ret);
		return ret;
	}

	if ((frame[0] & AHT10_STATUS_BUSY) != 0U) {
		LOG_WRN("Measurement is still busy");
		return -EBUSY;
	}

	data->humidity_raw = sys_get_be24(&frame[1]) >> 4;
	data->temperature_raw = sys_get_be24(&frame[3]) & 0x0FFFFF;

	return 0;
}

static int aht10_channel_get(const struct device *dev, enum sensor_channel chan,
			     struct sensor_value *value)
{
	const struct aht10_data *data = dev->data;
	int64_t micro_value;

	if (chan == SENSOR_CHAN_AMBIENT_TEMP) {
		micro_value = ((int64_t)data->temperature_raw * 200000000LL) / BIT(20) -
			      50000000LL;
	} else if (chan == SENSOR_CHAN_HUMIDITY) {
		micro_value = ((uint64_t)data->humidity_raw * 100000000ULL) / BIT(20);
	} else {
		return -ENOTSUP;
	}

	value->val1 = micro_value / 1000000LL;
	value->val2 = micro_value % 1000000LL;

	return 0;
}

static int aht10_init(const struct device *dev)
{
	const struct aht10_config *config = dev->config;
	uint8_t status;
	int ret;

	if (!i2c_is_ready_dt(&config->bus)) {
		LOG_ERR_DEVICE_NOT_READY(config->bus.bus);
		return -ENODEV;
	}

	k_msleep(AHT10_POWER_ON_WAIT_MS);

	ret = i2c_write_dt(&config->bus, aht10_init_command,
			   sizeof(aht10_init_command));
	if (ret < 0) {
		LOG_ERR("Failed to send initialization command: %d", ret);
		return ret;
	}

	k_msleep(AHT10_INIT_WAIT_MS);

	ret = aht10_read_status(config, &status);
	if (ret < 0) {
		LOG_ERR("Failed to read status: %d", ret);
		return ret;
	}

	if ((status & AHT10_STATUS_CALIBRATED) == 0U) {
		LOG_ERR("Sensor calibration is not enabled: status=0x%02x", status);
		return -EIO;
	}

	LOG_INF("AHT10 initialized, status=0x%02x", status);
	return 0;
}

static DEVICE_API(sensor, aht10_driver_api) = {
	.sample_fetch = aht10_sample_fetch,
	.channel_get = aht10_channel_get,
};

#define AHT10_DEFINE(inst)                                                                         \
	static struct aht10_data aht10_data_##inst;                                                  \
	static const struct aht10_config aht10_config_##inst = {                                     \
		.bus = I2C_DT_SPEC_INST_GET(inst),                                                     \
	};                                                                                             \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, aht10_init, NULL, &aht10_data_##inst,                       \
				     &aht10_config_##inst, POST_KERNEL,                               \
				     CONFIG_SENSOR_INIT_PRIORITY, &aht10_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AHT10_DEFINE)
