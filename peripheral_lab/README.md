# Peripheral Lab

This app is a clean C-based Zephyr practice project for ESP32-S3 DevKitC.

The app is intentionally small and staged:

1. Devicetree aliases and project tree
2. `k_timer` periodic timer
3. GPIO interrupt and delayed debounce work
4. Shell commands
5. I2C bus scan
6. SPI transfer command

Build:

```sh
PATH=/Volumes/ej_disk/zephyrproject/.venv/bin:$PATH \
./.venv/bin/west build -b esp32s3_devkitc/esp32s3/procpu peripheral_lab -p always \
    -S espressif-flash-16M \
    -S espressif-psram-8M
```

Useful shell commands:

```text
lab status
lab i2c_scan
lab spi_xfer aa 55 00 ff
```

For SPI transfer practice, connect the board's SPI MOSI pin to MISO if you want
to see transmitted bytes loop back into RX. On this board's default `spi2`
pinctrl, those are:

```text
MOSI GPIO11
MISO GPIO13
SCLK GPIO12
CS   GPIO10
```

For I2C scan practice, connect an I2C device to:

```text
SDA GPIO1
SCL GPIO2
```
