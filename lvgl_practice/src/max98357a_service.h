#ifndef MAX98357A_SERVICE_H_
#define MAX98357A_SERVICE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int max98357a_service_init(void);
int max98357a_service_play_startup_wav(void);
int max98357a_service_stream_start(uint32_t sample_rate);
int max98357a_service_stream_write(const int16_t *pcm,
				   size_t samples_per_channel,
				   uint8_t source_channels);
int max98357a_service_stream_stop(bool drain);

#endif /* MAX98357A_SERVICE_H_ */
