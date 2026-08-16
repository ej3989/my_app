/* SPDX-License-Identifier: Apache-2.0 */

#ifndef SYSTEM_WATCHDOG_H_
#define SYSTEM_WATCHDOG_H_

int system_watchdog_init(void);
void system_watchdog_feed(void);
void system_watchdog_network_healthy(void);

#endif /* SYSTEM_WATCHDOG_H_ */

