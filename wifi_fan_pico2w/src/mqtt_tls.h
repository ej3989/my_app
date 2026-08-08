/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIFI_FAN_MQTT_TLS_H_
#define WIFI_FAN_MQTT_TLS_H_

#include <zephyr/net/tls_credentials.h>

#define MQTT_CA_CERT_TAG 1
#define MQTT_TLS_SEC_TAG_COUNT 1U

extern const sec_tag_t mqtt_tls_sec_tags[];

int mqtt_tls_init(void);

#endif /* WIFI_FAN_MQTT_TLS_H_ */
