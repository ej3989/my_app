# LVGL Practice

This folder is a small Zephyr/LVGL practice app for the Waveshare 3.5C wiring
used by `EJ_APP/waveshare_35c`.

Current step:

1. Initialize the display and LVGL.
2. Turn on the LCD backlight.
3. Draw one title label, one counter label, and one clickable button.
4. Update the counter label from an LVGL button event callback.

Suggested build command for manual verification:

```sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu EJ_APP/lvgl_practice
```

Suggested next practice steps:

1. Change label text, colors, alignment, and sizes.
2. Add a second button and handle both buttons in one callback.
3. Add a slider and update a label from `LV_EVENT_VALUE_CHANGED`.
4. Split UI creation into small functions.
