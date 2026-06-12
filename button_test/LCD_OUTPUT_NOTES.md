# LCD output notes

This project uses a Waveshare Pico-LCD-2 style ST7789V LCD through Zephyr's
display API.

## Hardware connection

| LCD signal | Meaning | ESP32-S3 GPIO |
| --- | --- | --- |
| VCC | Power | 3V3 |
| GND | Ground | GND |
| DIN | SPI MOSI | GPIO11 |
| CLK | SPI SCK | GPIO12 |
| CS | SPI chip select | GPIO10 |
| DC | Data/command | GPIO9 |
| RST | Reset | GPIO8 |
| BL | Backlight | GPIO7 |

## Devicetree path

The LCD is not placed directly under `&spi2`. Instead:

```dts
mipi_dbi {
    compatible = "zephyr,mipi-dbi-spi";
    spi-dev = <&spi2>;

    lcd0: st7789v@0 {
        compatible = "sitronix,st7789v";
    };
};
```

`zephyr,mipi-dbi-spi` is a MIPI DBI wrapper that uses `spi2` for transfers.
The ST7789V display driver talks to this MIPI DBI device, not directly to
`spi2` from the application.

## Current selected orientation: output 2

Current display settings:

```dts
width = <240>;
height = <320>;
pixel-format = <PANEL_PIXEL_FORMAT_RGB_565>;
x-offset = <0>;
y-offset = <0>;
mdac = <0xc0>;
```

Application constants must match:

```c
#define LCD_WIDTH 240
#define LCD_HEIGHT 320
```

This is the selected orientation setting for output 2. The current application
does not draw pixels to the LCD. It only initializes the display, turns on the
backlight, and unblanks the panel.

## Output 1

Output 1 was the desired landscape output:

```dts
width = <320>;
height = <240>;
mdac = <0xa0>;
```

Application constants:

```c
#define LCD_WIDTH 320
#define LCD_HEIGHT 240
```

Use this when the display should be treated as a 320 x 240 landscape screen.

## What `mdac` changes

`mdac` is sent to the ST7789V `MADCTL` register. It controls how LCD RAM
coordinates are mapped to the physical panel direction.

Common values observed in this project:

| mdac | Effect in this project |
| --- | --- |
| `0x00` | 240 x 320 portrait, but appeared 180 degrees rotated on this wiring/panel placement |
| `0xc0` | 240 x 320 portrait with the 180-degree rotation corrected |
| `0xa0` | 320 x 240 landscape output, saved as output 1 |
| `0x60` | Alternate landscape direction to try if `0xa0` is mirrored or upside down |

## Pixel format

The working pixel format is:

```dts
pixel-format = <PANEL_PIXEL_FORMAT_RGB_565>;
```

The application writes RGB565 pixels as little-endian values:

```c
lcd_line[x] = sys_cpu_to_le16(color);
```

`PANEL_PIXEL_FORMAT_RGB_565X` was tested, but it produced incorrect colors for
this setup and was reverted.

## Application display flow

The application-side flow is:

1. Get the display device:

   ```c
   #define DISPLAY_NODE DT_CHOSEN(zephyr_display)
   #define DISPLAY_DEV DEVICE_DT_GET(DISPLAY_NODE)
   ```

2. Check device readiness:

   ```c
   device_is_ready(DISPLAY_DEV)
   ```

3. Turn on the backlight:

   ```c
   led_on(LCD_BL_DEV, 0);
   ```

4. Unblank the display:

   ```c
   display_blanking_off(DISPLAY_DEV);
   ```

5. Write pixels when needed:

   ```c
   display_write(DISPLAY_DEV, x, y, &desc, lcd_line);
   ```

The current application intentionally stops before step 5.

## Step-by-step LCD output practice

### Step 1: Turn on only the panel

Goal: prove that the LCD device and backlight are ready.

Relevant code:

```c
led_on(LCD_BL_DEV, 0);
display_blanking_off(DISPLAY_DEV);
```

Expected result: backlight turns on, but the LCD content may be blank or retain
old/random pixels depending on the controller state.

### Step 2: Fill the whole screen

Goal: learn one full-screen `display_write()` pattern.

Concept:

1. Prepare one line buffer.
2. Fill it with one RGB565 color.
3. Write the line repeatedly for every y coordinate.

Example color values:

```c
#define RGB565_RED   0xf800
#define RGB565_GREEN 0x07e0
#define RGB565_BLUE  0x001f
#define RGB565_WHITE 0xffff
#define RGB565_BLACK 0x0000
```

For this setup, write each RGB565 value as:

```c
line[x] = sys_cpu_to_le16(color);
```

### Step 3: Draw a rectangle

Goal: understand `x`, `y`, `width`, and `height`.

`display_write()` writes a rectangular buffer starting at `(x, y)`.

For a simple rectangle, create one line of `width` pixels and write it `height`
times:

```c
display_write(DISPLAY_DEV, x, row, &desc, line);
```

### Step 4: Draw orientation markers

Goal: verify rotation and coordinate mapping.

Use four colored rectangles:

| Logical position | Color |
| --- | --- |
| Top-left | Red |
| Top-right | Green |
| Bottom-left | Blue |
| Bottom-right | White |

If these appear in the wrong physical locations, adjust `mdac` and keep
`width` / `height` synchronized with the application constants.

### Step 5: Draw text

Zephyr's basic display API does not provide a universal built-in text drawing
function. Text is usually handled in one of these ways:

1. Use a graphics/UI library such as LVGL.
2. Draw text manually with a bitmap font.
3. Convert text to pixels in your own framebuffer and call `display_write()`.

Manual bitmap-font text is useful for learning, but LVGL is usually better for
real UI work.

## Learning checklist

When changing LCD direction, change these together:

1. `width` and `height` in the overlay.
2. `LCD_WIDTH` and `LCD_HEIGHT` in `main.c`.
3. `mdac` in the overlay.

Do not change only one of them. If `width/height` and `mdac` do not match, the
display can appear repeated, cropped, mirrored, or rotated unexpectedly.
