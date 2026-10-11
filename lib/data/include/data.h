/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RSC_DATA_H
#define RSC_DATA_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	const char *device_id;
	const char *firmware_version;
	uint32_t timestamp;
	int8_t rssi;
} data_signal_meta_t;

int data_build_signal_payload(char *buffer, size_t buffer_size,
			      const data_signal_meta_t *meta);
int data_wind_store_init(void);
int data_wind_accumulate(uint16_t direction, uint32_t timestamp);

#endif
