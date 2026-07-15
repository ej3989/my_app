# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES
    ${CMAKE_CURRENT_LIST_DIR}/../modules/ej3989_drivers
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lvgl_practice)

target_sources(app PRIVATE
    src/main.c
    src/lvgl_ej.c
    src/led_service.c
    src/app_controller.c
    src/app_state.c
    src/app_settings.c
    src/aht10_service.c
)
