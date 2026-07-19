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
		.temperature_milli_c = 0,
		.humidity_milli_percent = 0,
        .led_color_index = 0,
        .led_enabled = true,
		.aht10_valid = false,
		.radio_playing = false,
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

void app_state_set_led_enabled(bool enabled)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	state.led_enabled = enabled;
	k_mutex_unlock(&state_lock);
}

void app_state_set_aht10_reading(int64_t temperature_milli_c,
				 int64_t humidity_milli_percent)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	state.temperature_milli_c = temperature_milli_c;
	state.humidity_milli_percent = humidity_milli_percent;
	state.aht10_valid = true;
	k_mutex_unlock(&state_lock);
}

void app_state_set_aht10_unavailable(void)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	state.aht10_valid = false;
	k_mutex_unlock(&state_lock);
}

void app_state_set_radio_playing(bool playing)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	state.radio_playing = playing;
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
