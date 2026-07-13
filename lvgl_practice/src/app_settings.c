#include "app_settings.h"

#include <errno.h>
#include <stddef.h>

#include <zephyr/settings/settings.h>

#define APP_SETTINGS_KEY "ej/state"

_Static_assert(sizeof(struct app_persistent_settings) == 4,
	       "Persistent settings layout changed");

int app_settings_init(void)
{
	return settings_subsys_init();
}

int app_settings_load(struct app_persistent_settings *settings)
{
	ssize_t len;

	if (settings == NULL) {
		return -EINVAL;
	}

	*settings = (struct app_persistent_settings) {
		.version = APP_SETTINGS_VERSION,
		.led_color_index = 0,
		.led_enabled = true,
		.reserved = 0,
	};

	len = settings_load_one(APP_SETTINGS_KEY,
				settings,
				sizeof(*settings));

	if ((len == 0) || (len == -ENOENT)) {
		return 0;
	}

	if (len < 0) {
		return (int)len;
	}

	if (len != (ssize_t)sizeof(*settings)) {
		return -EINVAL;
	}

	if (settings->version != APP_SETTINGS_VERSION) {
		return -EINVAL;
	}

	return 0;
}

int app_settings_save(const struct app_persistent_settings *settings)
{
	if (settings == NULL) {
		return -EINVAL;
	}

	return settings_save_one(APP_SETTINGS_KEY,
				 settings,
				 sizeof(*settings));
}
