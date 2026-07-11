#ifndef APP_CONTROLLER_H_
#define APP_CONTROLLER_H_


#include <stdint.h>

enum app_event_type {
    APP_EVENT_LED_NEXT,
    APP_EVENT_STATUS_TICK,
};

struct app_event {
    enum app_event_type type;
    uint32_t value;
};

int app_controller_start(void);
int app_controller_send(enum app_event_type type, uint32_t value);


#endif /* APP_CONTROLLER_H_ */
