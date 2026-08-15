/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIFI_FAN_WIFI_MANAGER_H_
#define WIFI_FAN_WIFI_MANAGER_H_

#include <stdbool.h>

int wifi_manager_init(void);
int wifi_manager_connect(void);
bool wifi_manager_is_connected(void);
int wifi_manager_get_rssi(int *rssi);

#endif /* WIFI_FAN_WIFI_MANAGER_H_ */
