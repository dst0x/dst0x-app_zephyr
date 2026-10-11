/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RSC_SENSOR_MANAGER_H
#define RSC_SENSOR_MANAGER_H

#include <w87x.h>

typedef w87x_data_t weather_data_t;

int sensor_manager_init(void);
int sensor_manager_sample(void);
int sensor_manager_get_weather(weather_data_t *data);
void sensor_manager_print_weather(const weather_data_t *data);

#endif
