/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/poll.h>

#include "fan_controller.h"
#include "mqtt_fan.h"
#include "mqtt_tls.h"
#include "wifi_manager.h"

LOG_MODULE_REGISTER(mqtt_fan, LOG_LEVEL_INF);

#define MQTT_RX_BUFFER_SIZE 1024
#define MQTT_TX_BUFFER_SIZE 2048
#define MQTT_COMMAND_BUFFER_SIZE 32
#define MQTT_CONNECT_TIMEOUT_MS 10000
#define MQTT_POLL_TIMEOUT_MS 1000
#define RSSI_PUBLISH_INTERVAL_MS 30000

#define TOPIC_POWER_COMMAND       "wifi_fan/power/set"
#define TOPIC_POWER_STATE         "wifi_fan/power/state"
#define TOPIC_SPEED_COMMAND       "wifi_fan/speed/set"
#define TOPIC_SPEED_STATE         "wifi_fan/speed/state"
#define TOPIC_OSC_COMMAND         "wifi_fan/oscillation/set"
#define TOPIC_OSC_STATE           "wifi_fan/oscillation/state"
#define TOPIC_AVAILABILITY        "wifi_fan/availability"
#define TOPIC_RSSI_STATE          "wifi_fan/wifi_rssi/state"
#define TOPIC_FAN_DISCOVERY       "homeassistant/fan/wifi_fan_esp32s3/fan/config"
#define TOPIC_RSSI_DISCOVERY      "homeassistant/sensor/wifi_fan_esp32s3/rssi/config"

#define MQTT_CLIENT_ID "wifi_fan_esp32s3"

static const char fan_discovery_payload[] =
	"{"
	"\"name\":\"Bedroom Fan\","
	"\"unique_id\":\"wifi_fan_esp32s3_fan\","
	"\"command_topic\":\"" TOPIC_POWER_COMMAND "\","
	"\"state_topic\":\"" TOPIC_POWER_STATE "\","
	"\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
	"\"percentage_command_topic\":\"" TOPIC_SPEED_COMMAND "\","
	"\"percentage_state_topic\":\"" TOPIC_SPEED_STATE "\","
	"\"speed_range_min\":1,\"speed_range_max\":3,"
	"\"oscillation_command_topic\":\"" TOPIC_OSC_COMMAND "\","
	"\"oscillation_state_topic\":\"" TOPIC_OSC_STATE "\","
	"\"payload_oscillation_on\":\"ON\","
	"\"payload_oscillation_off\":\"OFF\","
	"\"availability_topic\":\"" TOPIC_AVAILABILITY "\","
	"\"payload_available\":\"online\","
	"\"payload_not_available\":\"offline\","
	"\"retain\":false,"
	"\"device\":{"
	"\"identifiers\":[\"wifi_fan_esp32s3\"],"
	"\"name\":\"Bedroom Fan Controller\","
	"\"manufacturer\":\"Zephyr practice\","
	"\"model\":\"ESP32-S3 Wi-Fi relay fan\"}"
	"}";

static const char rssi_discovery_payload[] =
	"{"
	"\"name\":\"Wi-Fi RSSI\","
	"\"unique_id\":\"wifi_fan_esp32s3_rssi\","
	"\"state_topic\":\"" TOPIC_RSSI_STATE "\","
	"\"availability_topic\":\"" TOPIC_AVAILABILITY "\","
	"\"device_class\":\"signal_strength\","
	"\"state_class\":\"measurement\","
	"\"unit_of_measurement\":\"dBm\","
	"\"entity_category\":\"diagnostic\","
	"\"device\":{\"identifiers\":[\"wifi_fan_esp32s3\"]}"
	"}";

static uint8_t mqtt_rx_buffer[MQTT_RX_BUFFER_SIZE];
static uint8_t mqtt_tx_buffer[MQTT_TX_BUFFER_SIZE];
static struct mqtt_client mqtt_client_ctx;
static struct sockaddr_in broker_address;
static struct pollfd mqtt_poll_fd;
static bool mqtt_connected;
static bool fan_state_dirty;
static uint16_t next_message_id = 1U;

static struct mqtt_utf8 mqtt_username;
static struct mqtt_utf8 mqtt_password;
static struct mqtt_topic mqtt_will_topic;
static struct mqtt_utf8 mqtt_will_message;

static uint16_t allocate_message_id(void)
{
	uint16_t id = next_message_id++;

	if (next_message_id == 0U) {
		next_message_id = 1U;
	}
	return id;
}

static bool topic_equals(const struct mqtt_utf8 *topic, const char *expected)
{
	size_t expected_length = strlen(expected);

	return topic->size == expected_length &&
	       memcmp(topic->utf8, expected, expected_length) == 0;
}

static int publish_text(const char *topic, const char *payload,
			bool retain, enum mqtt_qos qos)
{
	struct mqtt_publish_param param = {0};

	param.message.topic.topic.utf8 = (uint8_t *)topic;
	param.message.topic.topic.size = strlen(topic);
	param.message.topic.qos = qos;
	param.message.payload.data = (uint8_t *)payload;
	param.message.payload.len = strlen(payload);
	param.message_id = qos == MQTT_QOS_0_AT_MOST_ONCE ?
		0U : allocate_message_id();
	param.retain_flag = retain ? 1U : 0U;

	return mqtt_publish(&mqtt_client_ctx, &param);
}

static int publish_fan_state(void)
{
	struct fan_state state = fan_get_state();
	char speed[2];
	int err;

	snprintk(speed, sizeof(speed), "%u", (unsigned int)state.speed);

	err = publish_text(TOPIC_POWER_STATE,
			   state.speed > 0U ? "ON" : "OFF", true,
			   MQTT_QOS_1_AT_LEAST_ONCE);
	if (err == 0) {
		err = publish_text(TOPIC_SPEED_STATE, speed, true,
				   MQTT_QOS_1_AT_LEAST_ONCE);
	}
	if (err == 0) {
		err = publish_text(TOPIC_OSC_STATE,
				   state.oscillating ? "ON" : "OFF", true,
				   MQTT_QOS_1_AT_LEAST_ONCE);
	}

	if (err != 0) {
		LOG_ERR("Failed to publish fan state (err %d)", err);
	}
	return err;
}

static int publish_rssi(void)
{
	char payload[16];
	int rssi;
	int err;

	err = wifi_manager_get_rssi(&rssi);
	if (err != 0) {
		LOG_WRN("Failed to read Wi-Fi RSSI (err %d)", err);
		return err;
	}

	snprintk(payload, sizeof(payload), "%d", rssi);
	err = publish_text(TOPIC_RSSI_STATE, payload, true,
			   MQTT_QOS_1_AT_LEAST_ONCE);
	if (err == 0) {
		LOG_INF("Wi-Fi RSSI: %d dBm", rssi);
	}
	return err;
}

static int publish_discovery(void)
{
	int err;

	err = publish_text(TOPIC_FAN_DISCOVERY, fan_discovery_payload, true,
			   MQTT_QOS_1_AT_LEAST_ONCE);
	if (err == 0) {
		err = publish_text(TOPIC_RSSI_DISCOVERY, rssi_discovery_payload, true,
				   MQTT_QOS_1_AT_LEAST_ONCE);
	}

	return err;
}

static int apply_command(const struct mqtt_utf8 *topic, const char *payload)
{
	int err = -EINVAL;

	if (topic_equals(topic, TOPIC_POWER_COMMAND)) {
		if (strcmp(payload, "ON") == 0) {
			err = fan_turn_on();
		} else if (strcmp(payload, "OFF") == 0) {
			err = fan_turn_off();
		}
	} else if (topic_equals(topic, TOPIC_SPEED_COMMAND)) {
		if (strcmp(payload, "0") == 0) {
			err = fan_set_speed(0U);
		} else if (strcmp(payload, "1") == 0) {
			err = fan_set_speed(1U);
		} else if (strcmp(payload, "2") == 0) {
			err = fan_set_speed(2U);
		} else if (strcmp(payload, "3") == 0) {
			err = fan_set_speed(3U);
		}
	} else if (topic_equals(topic, TOPIC_OSC_COMMAND)) {
		if (strcmp(payload, "ON") == 0) {
			err = fan_set_oscillation(true);
		} else if (strcmp(payload, "OFF") == 0) {
			err = fan_set_oscillation(false);
		}
	}

	if (err == 0) {
		fan_state_dirty = true;
		LOG_INF("Applied MQTT command: %s", payload);
	} else {
		LOG_WRN("Ignored invalid MQTT command payload '%s'", payload);
	}
	return err;
}

static int discard_publish_payload(size_t length)
{
	uint8_t discard[32];
	size_t remaining = length;
	int err;

	while (remaining > 0U) {
		size_t chunk = MIN(remaining, sizeof(discard));

		err = mqtt_readall_publish_payload(&mqtt_client_ctx, discard, chunk);
		if (err != 0) {
			return err;
		}
		remaining -= chunk;
	}

	return 0;
}

static void handle_publish(const struct mqtt_publish_param *publish)
{
	char payload[MQTT_COMMAND_BUFFER_SIZE];
	size_t length = publish->message.payload.len;
	int err;

	if (length >= sizeof(payload)) {
		LOG_WRN("Ignoring oversized MQTT command (%u bytes)",
			(unsigned int)length);
		(void)discard_publish_payload(length);
		return;
	}

	err = mqtt_readall_publish_payload(&mqtt_client_ctx,
					   (uint8_t *)payload, length);
	if (err != 0) {
		LOG_ERR("Failed to read MQTT payload (err %d)", err);
		return;
	}
	payload[length] = '\0';

	/* A retained command could unexpectedly restart the fan after reboot. */
	if (publish->retain_flag != 0U) {
		LOG_WRN("Ignored retained MQTT command");
		return;
	}

	(void)apply_command(&publish->message.topic.topic, payload);
}

static void mqtt_event_handler(struct mqtt_client *const client,
			       const struct mqtt_evt *event)
{
	int err;

	switch (event->type) {
	case MQTT_EVT_CONNACK:
		if (event->result == 0) {
			mqtt_connected = true;
			LOG_INF("Connected to MQTT broker");
		} else {
			LOG_ERR("MQTT connection rejected (result %d)", event->result);
		}
		break;

	case MQTT_EVT_DISCONNECT:
		mqtt_connected = false;
		LOG_WRN("Disconnected from MQTT broker (result %d)", event->result);
		break;

	case MQTT_EVT_PUBLISH:
		handle_publish(&event->param.publish);
		if (event->param.publish.message.topic.qos ==
		    MQTT_QOS_1_AT_LEAST_ONCE) {
			const struct mqtt_puback_param ack = {
				.message_id = event->param.publish.message_id,
			};

			err = mqtt_publish_qos1_ack(client, &ack);
			if (err != 0) {
				LOG_WRN("Failed to acknowledge MQTT command (err %d)", err);
			}
		}
		break;

	case MQTT_EVT_SUBACK:
		LOG_INF("MQTT command topics subscribed");
		break;

	case MQTT_EVT_PUBACK:
	case MQTT_EVT_PINGRESP:
		break;

	default:
		break;
	}
}

static int subscribe_to_commands(void)
{
	struct mqtt_topic topics[] = {
		{
			.topic = {
				.utf8 = (uint8_t *)TOPIC_POWER_COMMAND,
				.size = sizeof(TOPIC_POWER_COMMAND) - 1U,
			},
			.qos = MQTT_QOS_0_AT_MOST_ONCE,
		},
		{
			.topic = {
				.utf8 = (uint8_t *)TOPIC_SPEED_COMMAND,
				.size = sizeof(TOPIC_SPEED_COMMAND) - 1U,
			},
			.qos = MQTT_QOS_0_AT_MOST_ONCE,
		},
		{
			.topic = {
				.utf8 = (uint8_t *)TOPIC_OSC_COMMAND,
				.size = sizeof(TOPIC_OSC_COMMAND) - 1U,
			},
			.qos = MQTT_QOS_0_AT_MOST_ONCE,
		},
	};
	const struct mqtt_subscription_list subscriptions = {
		.list = topics,
		.list_count = ARRAY_SIZE(topics),
		.message_id = allocate_message_id(),
	};

	return mqtt_subscribe(&mqtt_client_ctx, &subscriptions);
}

static int mqtt_socket_poll(int timeout_ms)
{
	mqtt_poll_fd.fd = mqtt_client_ctx.transport.tls.sock;
	mqtt_poll_fd.events = POLLIN;
	mqtt_poll_fd.revents = 0;

	return poll(&mqtt_poll_fd, 1, timeout_ms);
}

static int resolve_broker_address(void)
{
	char port[6];
	char resolved_address[INET_ADDRSTRLEN];
	struct zsock_addrinfo *result = NULL;
	const struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	int err;

	snprintk(port, sizeof(port), "%d", CONFIG_WIFI_FAN_MQTT_PORT);
	err = zsock_getaddrinfo(CONFIG_WIFI_FAN_MQTT_HOST, port, &hints,
			       &result);
	if (err != 0) {
		LOG_ERR("Failed to resolve MQTT host '%s': %s",
			CONFIG_WIFI_FAN_MQTT_HOST, zsock_gai_strerror(err));
		return -EHOSTUNREACH;
	}
	if (result == NULL || result->ai_addr == NULL ||
	    result->ai_addrlen < sizeof(broker_address)) {
		LOG_ERR("No IPv4 address found for MQTT host '%s'",
			CONFIG_WIFI_FAN_MQTT_HOST);
		if (result != NULL) {
			zsock_freeaddrinfo(result);
		}
		return -ENOENT;
	}

	memcpy(&broker_address, result->ai_addr, sizeof(broker_address));
	zsock_freeaddrinfo(result);

	if (inet_ntop(AF_INET, &broker_address.sin_addr, resolved_address,
		      sizeof(resolved_address)) != NULL) {
		LOG_INF("Resolved MQTT host '%s' to %s",
			CONFIG_WIFI_FAN_MQTT_HOST, resolved_address);
	}

	return 0;
}

static void mqtt_client_prepare(void)
{
	mqtt_client_init(&mqtt_client_ctx);

	mqtt_client_ctx.broker = &broker_address;
	mqtt_client_ctx.evt_cb = mqtt_event_handler;
	mqtt_client_ctx.client_id.utf8 = (uint8_t *)MQTT_CLIENT_ID;
	mqtt_client_ctx.client_id.size = sizeof(MQTT_CLIENT_ID) - 1U;
	mqtt_client_ctx.protocol_version = MQTT_VERSION_3_1_1;
	mqtt_client_ctx.transport.type = MQTT_TRANSPORT_SECURE;
	mqtt_client_ctx.rx_buf = mqtt_rx_buffer;
	mqtt_client_ctx.rx_buf_size = sizeof(mqtt_rx_buffer);
	mqtt_client_ctx.tx_buf = mqtt_tx_buffer;
	mqtt_client_ctx.tx_buf_size = sizeof(mqtt_tx_buffer);
	mqtt_client_ctx.keepalive = 30U;
	mqtt_client_ctx.clean_session = 1U;

	mqtt_username.utf8 = (uint8_t *)CONFIG_WIFI_FAN_MQTT_USERNAME;
	mqtt_username.size = strlen(CONFIG_WIFI_FAN_MQTT_USERNAME);
	mqtt_client_ctx.user_name = &mqtt_username;

	mqtt_password.utf8 = (uint8_t *)CONFIG_WIFI_FAN_MQTT_PASSWORD;
	mqtt_password.size = strlen(CONFIG_WIFI_FAN_MQTT_PASSWORD);
	mqtt_client_ctx.password = &mqtt_password;

	mqtt_client_ctx.transport.tls.config.peer_verify =
		TLS_PEER_VERIFY_REQUIRED;
	mqtt_client_ctx.transport.tls.config.cipher_list = NULL;
	mqtt_client_ctx.transport.tls.config.sec_tag_list = mqtt_tls_sec_tags;
	mqtt_client_ctx.transport.tls.config.sec_tag_count =
		MQTT_TLS_SEC_TAG_COUNT;
	mqtt_client_ctx.transport.tls.config.hostname =
		CONFIG_WIFI_FAN_MQTT_HOST;

	mqtt_will_topic.topic.utf8 = (uint8_t *)TOPIC_AVAILABILITY;
	mqtt_will_topic.topic.size = sizeof(TOPIC_AVAILABILITY) - 1U;
	mqtt_will_topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	mqtt_will_message.utf8 = (uint8_t *)"offline";
	mqtt_will_message.size = sizeof("offline") - 1U;
	mqtt_client_ctx.will_topic = &mqtt_will_topic;
	mqtt_client_ctx.will_message = &mqtt_will_message;
	mqtt_client_ctx.will_retain = 1U;
}

static int wait_for_connack(void)
{
	int64_t deadline = k_uptime_get() + MQTT_CONNECT_TIMEOUT_MS;
	int err;

	while (!mqtt_connected && k_uptime_get() < deadline) {
		err = mqtt_socket_poll(MQTT_POLL_TIMEOUT_MS);
		if (err < 0) {
			return -errno;
		}
		if (err > 0 && (mqtt_poll_fd.revents & POLLIN) != 0) {
			err = mqtt_input(&mqtt_client_ctx);
			if (err != 0) {
				return err;
			}
		}
		if ((mqtt_poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
			return -ENOTCONN;
		}
	}

	return mqtt_connected ? 0 : -ETIMEDOUT;
}

int mqtt_fan_run(void)
{
	int64_t next_rssi_publish;
	int err;

	if (strlen(CONFIG_WIFI_FAN_MQTT_USERNAME) == 0U ||
	    strlen(CONFIG_WIFI_FAN_MQTT_PASSWORD) == 0U) {
		LOG_ERR("MQTT TLS username and password must be configured");
		return -EINVAL;
	}

	err = resolve_broker_address();
	if (err != 0) {
		return err;
	}

	mqtt_connected = false;
	mqtt_client_prepare();

	LOG_INF("Connecting to MQTT broker %s:%d",
		CONFIG_WIFI_FAN_MQTT_HOST, CONFIG_WIFI_FAN_MQTT_PORT);
	err = mqtt_connect(&mqtt_client_ctx);
	if (err != 0) {
		return err;
	}

	err = wait_for_connack();
	if (err != 0) {
		mqtt_abort(&mqtt_client_ctx);
		return err;
	}

	err = subscribe_to_commands();
	if (err == 0) {
		err = publish_discovery();
	}
	if (err == 0) {
		err = publish_text(TOPIC_AVAILABILITY, "online", true,
				   MQTT_QOS_1_AT_LEAST_ONCE);
	}
	if (err == 0) {
		err = publish_fan_state();
	}
	if (err == 0) {
		err = publish_rssi();
	}
	if (err != 0) {
		mqtt_abort(&mqtt_client_ctx);
		return err;
	}

	next_rssi_publish = k_uptime_get() + RSSI_PUBLISH_INTERVAL_MS;
	while (wifi_manager_is_connected() && mqtt_connected) {
		err = mqtt_socket_poll(MQTT_POLL_TIMEOUT_MS);
		if (err < 0) {
			err = -errno;
			break;
		}
		if (err > 0 && (mqtt_poll_fd.revents & POLLIN) != 0) {
			err = mqtt_input(&mqtt_client_ctx);
			if (err != 0) {
				break;
			}
		}
		if ((mqtt_poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
			err = -ENOTCONN;
			break;
		}

		err = mqtt_live(&mqtt_client_ctx);
		if (err != 0 && err != -EAGAIN) {
			break;
		}

		if (fan_state_dirty) {
			fan_state_dirty = false;
			err = publish_fan_state();
			if (err != 0) {
				break;
			}
		}

		if (k_uptime_get() >= next_rssi_publish) {
			(void)publish_rssi();
			next_rssi_publish =
				k_uptime_get() + RSSI_PUBLISH_INTERVAL_MS;
		}
	}

	mqtt_connected = false;
	mqtt_abort(&mqtt_client_ctx);
	return err != 0 ? err : -ENOTCONN;
}
