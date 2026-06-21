# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_WAVESHARE_35C_INPUT_XPT2046 input_xpt2046.c)
