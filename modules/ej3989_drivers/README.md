# EJ3989 Zephyr driver module

This is an out-of-tree Zephyr module for the Waveshare/MHS 3.5 inch
Raspberry Pi style SPI LCD used by `EJ_APP/waveshare_35c`.

It keeps the custom display and touch drivers under `EJ_APP` so the app can be
tracked without modifying the upstream Zephyr tree.

## Included drivers

- `drivers/display/display_ili9486.c`
- `drivers/display/display_ili9xxx.c`
- `drivers/input/input_xpt2046.c`

## Included bindings

- `dts/bindings/display/ilitek,ili9486.yaml`
- `dts/bindings/input/xptek,xpt2046-waveshare-35c.yaml`

These bindings are provided by the module through `settings.dts_root` in
`zephyr/module.yml`. The touch binding intentionally uses
`compatible: "xptek,xpt2046-waveshare-35c"` so it can coexist with Zephyr's
built-in `xptek,xpt2046` binding while keeping the extra calibration properties
used by this board.

## App integration

`EJ_APP/lvgl_practice/CMakeLists.txt` registers this module before
`find_package(Zephyr ...)`:

```cmake
list(APPEND ZEPHYR_EXTRA_MODULES
    ${CMAKE_CURRENT_LIST_DIR}/../modules/ej3989_drivers
)
```

The app disables Zephyr's built-in driver Kconfig symbols and enables the
module-specific symbols:

```conf
CONFIG_ILI9486=n
CONFIG_EJ3989_ILI9486=y
CONFIG_INPUT_XPT2046=n
CONFIG_EJ3989_INPUT_XPT2046=y
```

## Migration note

The local Zephyr tree used during bring-up contained copied versions of these
driver and binding files. Those local driver changes and duplicate bindings
should stay restored/removed so this module remains the source of truth:

- `zephyr/drivers/display/`
- `zephyr/drivers/input/`
- `zephyr/dts/bindings/display/`
- `zephyr/dts/bindings/input/`
