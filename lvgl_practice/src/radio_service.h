#ifndef RADIO_SERVICE_H_
#define RADIO_SERVICE_H_

#include <stdbool.h>

int radio_service_init(void);
int radio_service_start(void);
int radio_service_stop(void);
bool radio_service_is_playing(void);

#endif /* RADIO_SERVICE_H_ */
