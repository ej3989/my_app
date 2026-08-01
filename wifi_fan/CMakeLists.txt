# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi_fan)

target_sources(app PRIVATE
  src/main.c
  src/fan_controller.c
  src/wifi_manager.c
  src/mqtt_fan.c
)
