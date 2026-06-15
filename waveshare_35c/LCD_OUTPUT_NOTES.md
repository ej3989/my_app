# LCD Output Notes

This project targets the MHS-3.5inch RPi Display.

The current bring-up configuration tests the LCD as `ilitek,ili9486` through
Zephyr's display API and MIPI DBI SPI wrapper.

## Display Path

```dts
mipi_dbi {
    compatible = "zephyr,mipi-dbi-spi";
    spi-dev = <&spi2>;

    lcd0: ili9486@0 {
        compatible = "ilitek,ili9486";
    };
};
```

The application talks to the generic Zephyr display API:

```c
#define DISPLAY_NODE DT_CHOSEN(zephyr_display)
#define DISPLAY_DEV DEVICE_DT_GET(DISPLAY_NODE)
```

The application does not call ILI9486 functions directly. It calls the generic
Zephyr `display_write()` / `display_blanking_off()` API, and the device selected
by `zephyr,display` routes those calls to the ILI9486 driver.

## Current LCD Geometry

```dts
width = <320>;
height = <480>;
rotation = <0>;
pixel-format = <PANEL_PIXEL_FORMAT_RGB_888>;
```

The ILI9486 register defaults added in `ilitek,ili9486.yaml` follow the
Linux `.bin` initialization sequence previously used with this panel. That
sequence sets `COLMOD` / command `0x3A` to `0x66`, so the current bring-up uses
Zephyr `PANEL_PIXEL_FORMAT_RGB_888` and sends three bytes per pixel in the
direct test pattern.

The LCDWiki module page lists SPI input up to 125MHz and Raspberry Pi operation
around 50Hz refresh. The overlay currently uses 10MHz for safe bring-up:

```dts
mipi-max-frequency = <10000000>;
```

After the direct `display_write()` color-band test is stable, increase this in
steps instead of jumping directly to the maximum.

## Color Byte Order

The app keeps:

```conf
CONFIG_LV_COLOR_16_SWAP=y
```

This was required on the previous LCD path to make LVGL RGB565 colors appear
correctly. Keep it enabled unless the Waveshare panel shows swapped colors.

## Backlight

The copied app still defines:

```dts
lcd_bl: lcd_bl {
    compatible = "gpio-leds";
    backlight {
        gpios = <&gpio0 7 GPIO_ACTIVE_HIGH>;
    };
};
```

If this LCD board's backlight is always on or is not wired to GPIO7, this node
does not control brightness. It can be removed later after hardware behavior is
confirmed.
