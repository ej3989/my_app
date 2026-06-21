# Waveshare 3.5C Zephyr driver module

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
- `dts/bindings/input/xptek,xpt2046.yaml`

These bindings are staged in the module, but `zephyr/module.yml` currently does
not enable `settings.dts_root`. The local Zephyr tree still contains identical
bindings, and enabling both roots at the same time causes a duplicate
`compatible` error.

After the local Zephyr copies are restored or removed, enable the module DTS
root again:

```yaml
build:
  cmake: .
  kconfig: Kconfig
  settings:
    dts_root: .
```

## App integration

`EJ_APP/waveshare_35c/CMakeLists.txt` registers this module before
`find_package(Zephyr ...)`:

```cmake
list(APPEND ZEPHYR_EXTRA_MODULES
    ${CMAKE_CURRENT_LIST_DIR}/../modules/waveshare_35c_drivers
)
```

The app disables Zephyr's built-in driver Kconfig symbols and enables the
module-specific symbols:

```conf
CONFIG_ILI9486=n
CONFIG_WAVESHARE_35C_ILI9486=y
CONFIG_INPUT_XPT2046=n
CONFIG_WAVESHARE_35C_INPUT_XPT2046=y
```

## Migration note

During migration, the local Zephyr tree may still contain copied versions of
the same driver and binding files. Once this module is confirmed to build and
run, restore or remove the corresponding local driver changes under:

- `zephyr/drivers/display/`
- `zephyr/drivers/input/`

Then move the DTS binding source of truth into this module by enabling
`settings.dts_root` in `zephyr/module.yml`.
