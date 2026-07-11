#include "app_state.h"
#include <zephyr/kernel.h>

static struct app_state_snapshot state;
K_MUTEX_DEFINE(state_lock);

void app_state_init(void)
{
    k_mutex_lock(&state_lock, K_FOREVER);
    state = (struct app_state_snapshot){
        .current_screen = APP_SCREEN_MAIN,
        .click_count = 0,
        .led_click_count = 0,
        .led_color_index = 0,
        .led_enabled = true,
    };

    k_mutex_unlock(&state_lock);
}

uint32_t app_state_increment_click_count(void)
{
    uint32_t count;
    k_mutex_lock(&state_lock, K_FOREVER);
    state.click_count++;
    count = state.click_count;
    k_mutex_unlock(&state_lock);

    return count;
}

uint32_t app_state_increment_led_click_count(void)
{
    uint32_t count;
    k_mutex_lock(&state_lock, K_FOREVER);
    state.led_click_count++;
    count = state.led_click_count;
    k_mutex_unlock(&state_lock);

    return count;
}

void app_state_set_screen(enum app_screen_id screen)
{
    k_mutex_lock(&state_lock, K_FOREVER);
    state.current_screen = screen;
    k_mutex_unlock(&state_lock);
}

void app_state_set_led_color_index(uint8_t color_index)
{
    k_mutex_lock(&state_lock, K_FOREVER);
    state.led_color_index = color_index;
    k_mutex_unlock(&state_lock);
}

void app_state_get_snapshot(struct app_state_snapshot *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    k_mutex_lock(&state_lock, K_FOREVER);
    *snapshot = state;
    k_mutex_unlock(&state_lock);
}