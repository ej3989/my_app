/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "display_ili9486.h"
#include "display_ili9xxx.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(display_ili9486, CONFIG_DISPLAY_LOG_LEVEL);

static int ili9486_transmit_reg(const struct device *dev, uint8_t cmd,
				const void *data, size_t len, const char *name)
{
	int r;

	LOG_HEXDUMP_DBG(data, len, name);
	r = ili9xxx_transmit(dev, cmd, data, len);
	if (r < 0) {
		LOG_ERR("%s command 0x%02x failed: %d", name, cmd, r);
	}

	return r;
}

int ili9486_regs_init(const struct device *dev)
{
	static const uint8_t ifctl1[] = {0x36, 0x04, 0x00, 0x3c, 0x0f, 0x8f};
	static const uint8_t ifctl2[] = {0x18, 0xa3, 0x12, 0x02, 0xb2, 0x12, 0xff, 0x10, 0x00};
	static const uint8_t adjctl4[] = {0x21, 0x04};
	static const uint8_t adjctl5[] = {0x00, 0x08};
	static const uint8_t madctl_pre[] = {0x08};
	static const uint8_t ifmode[] = {0x00};
	static const uint8_t pwctrl2[] = {0x41};
	static const uint8_t vmctrl[] = {0x00, 0x91, 0x80, 0x00};
	static const uint8_t pgamctrl[] = {
		0x0f, 0x1f, 0x1c, 0x0c, 0x0f,
		0x08, 0x48, 0x98, 0x37, 0x0a,
		0x13, 0x04, 0x11, 0x0d, 0x00
	};
	static const uint8_t ngamctrl[] = {
		0x0f, 0x32, 0x2e, 0x0b, 0x0d,
		0x05, 0x47, 0x75, 0x37, 0x06,
		0x10, 0x03, 0x24, 0x20, 0x00
	};
	static const uint8_t pixset[] = {0x55};
	static const uint8_t madctl_post[] = {0x28};
	int r;

	r = ili9486_transmit_reg(dev, ILI9486_IFCTL1, ifctl1,
				 sizeof(ifctl1), "IFCTL1");
	if (r < 0) {
		return r;
	}

	r = ili9486_transmit_reg(dev, ILI9486_IFCTL2, ifctl2,
				 sizeof(ifctl2), "IFCTL2");
	if (r < 0) {
		return r;
	}

	r = ili9486_transmit_reg(dev, ILI9486_ADJCTL4, adjctl4,
				 sizeof(adjctl4), "ADJCTL4");
	if (r < 0) {
		return r;
	}

	r = ili9486_transmit_reg(dev, ILI9486_ADJCTL5, adjctl5,
				 sizeof(adjctl5), "ADJCTL5");
	if (r < 0) {
		return r;
	}

	r = ili9486_transmit_reg(dev, ILI9XXX_MADCTL, madctl_pre,
				 sizeof(madctl_pre), "MADCTL_PRE");
	if (r < 0) {
		return r;
	}

	r = ili9486_transmit_reg(dev, ILI9486_IFMODE, ifmode,
				 sizeof(ifmode), "IFMODE");
	if (r < 0) {
		return r;
	}

	r = ili9486_transmit_reg(dev, ILI9486_PWCTRL2, pwctrl2,
				 sizeof(pwctrl2), "PWCTRL2");
	if (r < 0) {
		return r;
	}

	r = ili9486_transmit_reg(dev, ILI9486_VMCTRL, vmctrl,
				 sizeof(vmctrl), "VMCTRL");
	if (r < 0) {
		return r;
	}

	r = ili9486_transmit_reg(dev, ILI9486_PGAMCTRL, pgamctrl,
				 sizeof(pgamctrl), "PGAMCTRL");
	if (r < 0) {
		return r;
	}

	r = ili9486_transmit_reg(dev, ILI9486_NGAMCTRL, ngamctrl,
				 sizeof(ngamctrl), "NGAMCTRL");
	if (r < 0) {
		return r;
	}

	r = ili9486_transmit_reg(dev, ILI9XXX_PIXSET, pixset,
				 sizeof(pixset), "PIXSET");
	if (r < 0) {
		return r;
	}

	r = ili9xxx_transmit(dev, ILI9XXX_SLPOUT, NULL, 0);
	if (r < 0) {
		LOG_ERR("SLPOUT command 0x%02x failed: %d", ILI9XXX_SLPOUT, r);
		return r;
	}

	r = ili9486_transmit_reg(dev, ILI9XXX_MADCTL, madctl_post,
				 sizeof(madctl_post), "MADCTL_POST");
	if (r < 0) {
		return r;
	}

	k_sleep(K_MSEC(255));

	r = ili9xxx_transmit(dev, ILI9XXX_DISPON, NULL, 0);
	if (r < 0) {
		LOG_ERR("DISPON command 0x%02x failed: %d", ILI9XXX_DISPON, r);
		return r;
	}

	return 0;
}
