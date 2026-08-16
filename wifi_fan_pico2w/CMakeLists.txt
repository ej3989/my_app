# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Zephyr currently selects the generic Murata 1YN NVRAM for CYW43439 by
# default. Raspberry Pi Pico W/2 W uses a different board profile. Keep the
# Zephyr and Infineon trees untouched and provide the matching resources from
# this application instead.
set(FW_IMAGE_NAME
  ${CMAKE_CURRENT_LIST_DIR}/resources/pico2w-cyw43439-firmware.bin
)
set(CLM_IMAGE_NAME
  ${CMAKE_CURRENT_LIST_DIR}/resources/pico2w-cyw43439.clm_blob
)
set(NVRAM_IMAGE_NAME
  ${CMAKE_CURRENT_LIST_DIR}/resources/pico2w-cyw43439-nvram.txt
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi_fan_pico2w)

target_sources(app PRIVATE
  src/main.c
  src/status_led.c
  src/fan_controller.c
  src/wifi_manager.c
  src/time_manager.c
  src/mqtt_tls.c
  src/mqtt_fan.c
  src/system_watchdog.c
)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)

generate_inc_file_for_target(
  app
  ${APPLICATION_SOURCE_DIR}/certs/fan-ca.crt
  ${gen_dir}/fan-ca.crt.inc
)
