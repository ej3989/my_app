#ifndef APP_STATE_H_
#define APP_STATE_H_

#include <stdbool.h>
#include <stdint.h>

enum app_screen_id {
	APP_SCREEN_MAIN,
	APP_SCREEN_SETUP,
};

struct app_state_snapshot {
    enum app_screen_id current_screen;
    uint32_t click_count;
    uint32_t led_click_count;
    uint8_t led_color_index;
    bool led_enabled;
};

void app_state_init(void);

uint32_t app_state_increment_click_count(void);
uint32_t app_state_increment_led_click_count(void);

void app_state_set_screen(enum app_screen_id screen);
void app_state_set_led_color_index(uint8_t color_index);

void app_state_get_snapshot(struct app_state_snapshot *snapshot);

#endif /* APP_STATE_H_ */
