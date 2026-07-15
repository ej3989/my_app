# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_EJ3989_INPUT_XPT2046 input_xpt2046.c)
