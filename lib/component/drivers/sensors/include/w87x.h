/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RSC_W87X_H
#define RSC_W87X_H

#include <stdint.h>

#define W87X_DEFAULT_SLAVE_ADDR 0x08

struct w87x_device {
	uint8_t slave_addr;
};

typedef struct {
	int16_t temperature;
	uint16_t humidity;
	uint16_t pressure;
	uint16_t wind_speed;
	uint16_t wind_dir;
} w87x_data_t;

int w87x_init(struct w87x_device *dev, uint8_t slave_addr);
int w87x_read(const struct w87x_device *dev, w87x_data_t *data);

#endif
