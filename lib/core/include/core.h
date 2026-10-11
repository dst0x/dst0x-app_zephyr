/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RSC_CORE_H
#define RSC_CORE_H

#include <stdint.h>

struct config {
	const char *devid;
	int32_t dev_inv;
	const char *server;
	int32_t server_port;
	const char *wifi_ssid;
	const char *wifi_pass;
	const char *mqtt_topic;
	const char *mqtt_user;
	const char *mqtt_pass;
	int32_t ntp_timezone;
};

const struct config *config_get(void);
int config_load(void);
void config_print(void);
uint32_t datetime_to_epoch(uint16_t year, uint8_t month, uint8_t day,
			   uint8_t hour, uint8_t minute, uint8_t second);

#endif
