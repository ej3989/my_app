# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_EJ3989_ILI9486 display_ili9486.c)
zephyr_library_sources_ifdef(CONFIG_EJ3989_ILI9XXX display_ili9xxx.c)
