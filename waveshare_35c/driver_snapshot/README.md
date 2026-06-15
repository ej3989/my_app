# Waveshare 3.5C ILI9486 driver snapshot

This folder keeps the Zephyr-side files that were added or touched to bring up
the MHS/Waveshare 3.5 inch RPi-style ILI9486 SPI display.

## New driver files

- `zephyr_drivers_display/display_ili9486.c`
- `zephyr_drivers_display/display_ili9486.h`
- `zephyr_dts_bindings_display/ilitek,ili9486.yaml`

## Zephyr integration files

These files contain the build/Kconfig/instance hookup for the new driver:

- `zephyr_drivers_display/CMakeLists.txt`
- `zephyr_drivers_display/Kconfig.ili9xxx`
- `zephyr_drivers_display/display_ili9xxx.c`

## Touch stability patch

The XPT2046 input driver is not new, but this project uses a patched copy:

- `zephyr_drivers_input/input_xpt2046.c`

The patch clamps scaled touch coordinates to the configured display range. This
prevents values such as `x=505`, `y=336` from being reported to LVGL on a
`480x320` screen.

To reuse this display support in another Zephyr tree, copy the files above back
to the matching paths under `zephyr/`, then enable the display with a devicetree
node using `compatible = "ilitek,ili9486"`. If LVGL touch input is enabled with
XPT2046, also copy the patched `input_xpt2046.c` to
`zephyr/drivers/input/input_xpt2046.c`.

## Known-good app overlay settings

The working `EJ_APP/waveshare_35c` overlay uses:

```dts
xfr-min-bits = "MIPI_DBI_SPI_XFR_16BIT";
mipi-max-frequency = <1000000>;
pixel-format = <PANEL_PIXEL_FORMAT_RGB_565>;
interface-pixel-format = [55];
width = <320>;
height = <480>;
rotation = <90>;
h-mirror;
```

Do not enable `mipi-hold-cs` for the current ESP32-S3 wiring. It caused the
panel to stop updating correctly.
