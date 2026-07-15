# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_EJ3989_SENSOR_AHT10 aht10.c)
