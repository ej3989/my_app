#ifndef LED_SERVICE_H_
#define LED_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>

int led_service_init(void);
int led_service_restore(uint8_t color_index, bool enabled);
int led_service_next(void);

#endif /* LED_SERVICE_H_ */
