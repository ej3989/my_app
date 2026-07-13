#ifndef APP_SETTINGS_H_
#define APP_SETTINGS_H_

#include <stdint.h>

#define APP_SETTINGS_VERSION 1U

struct app_persistent_settings {
	uint8_t version;
	uint8_t led_color_index;
	uint8_t led_enabled;
	uint8_t reserved;
};

int app_settings_init(void);
int app_settings_load(struct app_persistent_settings *settings);
int app_settings_save(const struct app_persistent_settings *settings);

#endif /* APP_SETTINGS_H_ */
