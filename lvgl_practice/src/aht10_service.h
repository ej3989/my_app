#ifndef AHT10_SERVICE_H_
#define AHT10_SERVICE_H_

#include <stdint.h>

int aht10_service_init(void);
int aht10_service_read(int64_t *temperature_milli_c,
			       int64_t *humidity_milli_percent);

#endif /* AHT10_SERVICE_H_ */
