/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/logging/log.h>
#include <zephyr/net/tls_credentials.h>

#include "mqtt_tls.h"

LOG_MODULE_REGISTER(mqtt_tls, LOG_LEVEL_INF);

static const unsigned char fan_ca_certificate[] = {
#include <fan-ca.crt.inc>
	0x00
};

const sec_tag_t mqtt_tls_sec_tags[] = {
	MQTT_CA_CERT_TAG,
};

int mqtt_tls_init(void)
{
	int err;

	err = tls_credential_add(MQTT_CA_CERT_TAG,
				 TLS_CREDENTIAL_CA_CERTIFICATE,
				 fan_ca_certificate,
				 sizeof(fan_ca_certificate));
	if (err != 0) {
		LOG_ERR("Failed to register MQTT CA certificate (err %d)", err);
		return err;
	}

	LOG_INF("MQTT CA certificate registered");
	return 0;
}
