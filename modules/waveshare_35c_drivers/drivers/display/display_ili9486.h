/*
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_DRIVERS_DISPLAY_DISPLAY_ILI9486_H_
#define ZEPHYR_DRIVERS_DISPLAY_DISPLAY_ILI9486_H_

#include <zephyr/device.h>

#define ILI9486_PWCTRL1 0xC0
#define ILI9486_PWCTRL2 0xC1
#define ILI9486_VMCTRL  0xC5
#define ILI9486_PGAMCTRL 0xE0
#define ILI9486_NGAMCTRL 0xE1
#define ILI9486_IFMODE 0xB4
#define ILI9486_SETIMAGE 0xE9
#define ILI9486_IFCTL1 0xF1
#define ILI9486_IFCTL2 0xF2
#define ILI9486_ADJCTL3 0xF7
#define ILI9486_ADJCTL4 0xF8
#define ILI9486_ADJCTL5 0xF9

#define ILI9486_PWCTRL1_LEN 2U
#define ILI9486_PWCTRL2_LEN 1U
#define ILI9486_VMCTRL_LEN 3U
#define ILI9486_PGAMCTRL_LEN 15U
#define ILI9486_NGAMCTRL_LEN 15U
#define ILI9486_MADCTL_LEN 1U
#define ILI9486_PIXSET_LEN 1U

#define ILI9486_X_RES 320U
#define ILI9486_Y_RES 480U

struct ili9486_regs {
	uint8_t pgamctrl[ILI9486_PGAMCTRL_LEN];
	uint8_t ngamctrl[ILI9486_NGAMCTRL_LEN];
	uint8_t pwctrl1[ILI9486_PWCTRL1_LEN];
	uint8_t pwctrl2[ILI9486_PWCTRL2_LEN];
	uint8_t vmctrl[ILI9486_VMCTRL_LEN];
	uint8_t madctl[ILI9486_MADCTL_LEN];
	uint8_t pixset[ILI9486_PIXSET_LEN];
};

#define ILI9486_REGS_INIT(n)                                                   \
	static const struct ili9486_regs ili9486_regs_##n = {                  \
		.pgamctrl = DT_PROP(DT_INST(n, ilitek_ili9486),                \
				    positive_gamma_control),                   \
		.ngamctrl = DT_PROP(DT_INST(n, ilitek_ili9486),                \
				    negative_gamma_control),                   \
		.pwctrl1 = DT_PROP(DT_INST(n, ilitek_ili9486),                 \
				   power_control_1),                            \
		.pwctrl2 = DT_PROP(DT_INST(n, ilitek_ili9486),                 \
				   power_control_2),                            \
		.vmctrl = DT_PROP(DT_INST(n, ilitek_ili9486), vcom_control),   \
		.madctl = DT_PROP(DT_INST(n, ilitek_ili9486),                  \
				  memory_access_control),                      \
		.pixset = DT_PROP(DT_INST(n, ilitek_ili9486),                  \
				  interface_pixel_format),                     \
	}

int ili9486_regs_init(const struct device *dev);

#endif /* ZEPHYR_DRIVERS_DISPLAY_DISPLAY_ILI9486_H_ */
