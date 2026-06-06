# Learning Steps

## 1. Project Tree

```text
peripheral_lab/
├── CMakeLists.txt
├── prj.conf
├── boards/
│   ├── esp32s3_devkitc_procpu.conf
│   └── esp32s3_devkitc_procpu.overlay
└── src/
    └── main.c
```

`CMakeLists.txt` adds `src/main.c` to the Zephyr app target.

`prj.conf` enables the Zephyr subsystems used by the lab:

```text
GPIO
I2C
SPI
SHELL
LOG
```

`boards/esp32s3_devkitc_procpu.conf` keeps the board-specific PSRAM mode:

```text
CONFIG_SPIRAM_MODE_OCT=y
```

The overlay adds readable aliases:

```dts
lab-button = &button0;
lab-i2c = &i2c0;
lab-spi = &spi2;
```

The C code then uses:

```c
DT_ALIAS(lab_button)
DT_ALIAS(lab_i2c)
DT_ALIAS(lab_spi)
```

## 2. Build And Flash

Build from the Zephyr workspace root:

```sh
PATH=/Volumes/ej_disk/zephyrproject/.venv/bin:$PATH \
./.venv/bin/west build -b esp32s3_devkitc/esp32s3/procpu peripheral_lab -p always \
    -S espressif-flash-16M \
    -S espressif-psram-8M
```

Flash after connecting the board:

```sh
PATH=/Volumes/ej_disk/zephyrproject/.venv/bin:$PATH \
./.venv/bin/west flash -d build
```

Open the serial console with the port for your board:

```sh
screen /dev/tty.usbmodemXXXX 115200
```

## 3. Timer

`k_timer_init()` registers a timer callback.

`k_timer_start()` starts periodic execution.

In this app, the timer fires once per second and increments `tick_count`.

## 4. GPIO Interrupt

Button setup has four steps:

```text
gpio_pin_configure_dt()
gpio_init_callback()
gpio_add_callback()
gpio_pin_interrupt_configure_dt()
```

The GPIO interrupt callback does not process the button immediately. It only
reschedules delayed work.

## 5. Debounce Work

`k_work_init_delayable()` registers a delayed work callback.

`k_work_reschedule(..., K_MSEC(30))` runs it 30 ms after the latest edge.

This ignores short electrical bounce during button press/release.

## 6. Shell Commands

The app registers these shell commands:

```text
lab status
lab i2c_scan
lab spi_xfer aa 55 00 ff
```

## 7. I2C Scan

The shell command:

```text
lab i2c_scan
```

tries all normal 7-bit addresses and reports addresses that ACK.

## 8. SPI Transfer

The shell command:

```text
lab spi_xfer aa 55 00 ff
```

sends bytes over SPI and prints received bytes.

Without a connected SPI device, RX may be all `0xff`, all `0x00`, or board
dependent noise. For a simple loopback, connect MOSI to MISO.
