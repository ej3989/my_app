# MHS-3.5inch RPi Display Wiring Notes

This project is copied from `EJ_APP/button_test` and retargeted for the
MHS-3.5inch RPi Display.

The LCDWiki page for this module lists `ILI9486`, `320x480` physical
resolution, `XPT2046` resistive touch, and SPI input up to 125MHz. The page
also describes Raspberry Pi operation around 50Hz refresh. This ESP32-S3
bring-up keeps SPI at 10MHz first, then the clock can be raised after the
direct color-band test is stable.

## LCD Pins

Current LCD-only wiring uses `spi2`.

| Waveshare signal | Meaning | ESP32-S3 GPIO |
| --- | --- | --- |
| LCD_RS | Data / command | GPIO9 |
| LCD_CS | LCD chip select | GPIO10 |
| LCD_SI / TP_SI | SPI MOSI | GPIO11 |
| LCD_SCK / TP_SCK | SPI SCK | GPIO12 |
| RST | LCD reset | GPIO8 |
| GND | Ground | GND |
| 3.3V | Power | 3V3 |
| 5V | LCD/backlight power | 5V |

The copied application still defines `lcd_bl` on GPIO7. If this LCD board's
backlight is not wired to GPIO7, the GPIO operation is harmless but it will not
control brightness.

The Waveshare board exposes both 3.3V and 5V pins. For bring-up, connect the
documented power pins and GND first and confirm that the panel/backlight is
visibly powered. Software cannot show pixels if the backlight power path is not
active.

Waveshare documents GPIO-based brightness control as an optional hardware
change: the 0R pad must be soldered before the backlight can be PWM-controlled
from a GPIO. Until that hardware change is made, the `lcd_bl` GPIO in this app
is only a local test GPIO and may not control the module's actual backlight.

## Touch Pins

The touch controller is enabled as `xptek,xpt2046` on the same `spi2` bus.
The initial calibration values are broad defaults and may need adjustment after
checking the serial touch logs.

| Waveshare signal | Meaning | ESP32-S3 GPIO |
| --- | --- | --- |
| TP_SI / T_DIN | Touch SPI MOSI | GPIO11 |
| TP_SCK / T_CLK / T_SCL | Touch SPI SCK | GPIO12 |
| TP_DO / T_DO / SDO | Touch SPI MISO | GPIO13 |
| TP_CS | Touch chip select | GPIO16 |
| TP_IRQ | Touch interrupt | GPIO17 |

These avoid the ESP32-S3 in-package Octal PSRAM pins GPIO33 through GPIO37.

## Build Command

From `EJ_APP`:

```sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu waveshare_35c \
  -S espressif-flash-16M \
  -S espressif-psram-8M
```
