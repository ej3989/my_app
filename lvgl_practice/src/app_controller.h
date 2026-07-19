#ifndef APP_CONTROLLER_H_
#define APP_CONTROLLER_H_


#include <stdint.h>

struct app_log_message;

enum app_event_type {
	APP_EVENT_LED_NEXT,
	APP_EVENT_AUDIO_PLAY,
	APP_EVENT_RADIO_TOGGLE,
	APP_EVENT_STATUS_TICK,
	APP_EVENT_LOG_MESSAGE,
	APP_EVENT_SAVE_SETTINGS,
};

struct app_event {
	enum app_event_type type;
	uint32_t value;
	struct app_log_message *log_message;
};

int app_controller_start(void);
int app_controller_send(enum app_event_type type, uint32_t value);
int app_controller_send_log(const char *text);


#endif /* APP_CONTROLLER_H_ */
