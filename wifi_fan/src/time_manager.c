/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/clock.h>

#include "time_manager.h"

LOG_MODULE_REGISTER(time_manager, LOG_LEVEL_INF);

#define SNTP_QUERY_TIMEOUT_MS 5000
#define MIN_VALID_UNIX_TIME 1735689600LL /* 2025-01-01 00:00:00 UTC */

static bool system_time_is_valid(void)
{
	struct timespec now;

	return sys_clock_gettime(SYS_CLOCK_REALTIME, &now) == 0 &&
	       now.tv_sec >= MIN_VALID_UNIX_TIME;
}

static int resolve_sntp_address(struct sockaddr_in *server_address)
{
	char port[6];
	struct zsock_addrinfo *result = NULL;
	const struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_DGRAM,
	};
	int err;

	snprintk(port, sizeof(port), "%d", CONFIG_WIFI_FAN_SNTP_PORT);
	err = zsock_getaddrinfo(CONFIG_WIFI_FAN_SNTP_HOST, port, &hints,
			       &result);
	if (err != 0) {
		LOG_ERR("Failed to resolve SNTP host '%s': %s",
			CONFIG_WIFI_FAN_SNTP_HOST, zsock_gai_strerror(err));
		return -EHOSTUNREACH;
	}
	if (result == NULL || result->ai_addr == NULL ||
	    result->ai_addrlen < sizeof(*server_address)) {
		LOG_ERR("No IPv4 address found for SNTP host '%s'",
			CONFIG_WIFI_FAN_SNTP_HOST);
		if (result != NULL) {
			zsock_freeaddrinfo(result);
		}
		return -ENOENT;
	}

	memcpy(server_address, result->ai_addr, sizeof(*server_address));
	zsock_freeaddrinfo(result);
	return 0;
}

int time_manager_sync(void)
{
	struct sockaddr_in server_address;
	struct sntp_ctx context;
	struct sntp_time sntp_time;
	struct timespec system_time;
	int err;

	if (system_time_is_valid()) {
		return 0;
	}

	memset(&server_address, 0, sizeof(server_address));
	err = resolve_sntp_address(&server_address);
	if (err != 0) {
		return err;
	}

	LOG_INF("Synchronizing time with '%s'", CONFIG_WIFI_FAN_SNTP_HOST);
	err = sntp_init(&context, (struct sockaddr *)&server_address,
			sizeof(server_address));
	if (err != 0) {
		LOG_ERR("Failed to initialize SNTP (err %d)", err);
		return err;
	}

	err = sntp_query(&context, SNTP_QUERY_TIMEOUT_MS, &sntp_time);
	sntp_close(&context);
	if (err != 0) {
		LOG_ERR("SNTP query failed (err %d)", err);
		return err;
	}

	system_time.tv_sec = sntp_time.seconds;
	system_time.tv_nsec =
		(long)(((uint64_t)sntp_time.fraction * 1000000000ULL) >> 32);
	err = sys_clock_settime(SYS_CLOCK_REALTIME, &system_time);
	if (err != 0) {
		LOG_ERR("Failed to set system time (err %d)", err);
		return err;
	}

	LOG_INF("System time synchronized (epoch %lld)",
		(long long)system_time.tv_sec);
	return 0;
}
