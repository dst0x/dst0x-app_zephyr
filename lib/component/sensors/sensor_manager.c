/* SPDX-License-Identifier: Apache-2.0 */

#include <sensor_manager.h>
#include <modbus_driver.h>
#include <errno.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

static struct w87x_device weather_sensor;
static weather_data_t latest_weather;
K_MUTEX_DEFINE(weather_lock);

static uint8_t initialized;
static bool weather_valid;

int sensor_manager_init(void)
{
	int err = modbus_driver_init();

	if (err != 0) {
		return err;
	}

	err = w87x_init(&weather_sensor, W87X_DEFAULT_SLAVE_ADDR);
	if (err != 0) {
		printk("[SENSOR] W87X init failed: %d\r", err);
		return err;
	}

	initialized = 1;
	printk("[INFO]Sensor Ready\r");
	return 0;
}

int sensor_manager_sample(void)
{
	weather_data_t sample;
	int err;

	if (initialized == 0) {
		return -ENODEV;
	}

	err = w87x_read(&weather_sensor, &sample);
	if (err != 0) {
		return err;
	}

	k_mutex_lock(&weather_lock, K_FOREVER);
	latest_weather = sample;
	weather_valid = true;
	k_mutex_unlock(&weather_lock);
	return 0;
}

int sensor_manager_get_weather(weather_data_t *data)
{
	if (data == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&weather_lock, K_FOREVER);
	if (!weather_valid) {
		k_mutex_unlock(&weather_lock);
		return -ENODATA;
	}
	*data = latest_weather;
	k_mutex_unlock(&weather_lock);
	return 0;
}

void sensor_manager_print_weather(const weather_data_t *data)
{
	int32_t temperature = data->temperature;
	uint32_t temperature_abs = temperature < 0 ? -temperature : temperature;

	printk("[W87X] Temp: %s%u.%u C\r", temperature < 0 ? "-" : "",
	       temperature_abs / 10, temperature_abs % 10);
	printk("[W87X] Humi: %u.%u %%\r", data->humidity / 10,
	       data->humidity % 10);
	printk("[W87X] Pres: %u.%u hPa\r", data->pressure / 10,
	       data->pressure % 10);
	printk("[W87X] Wind: %u.%02u m/s\r", data->wind_speed / 100,
	       data->wind_speed % 100);
	printk("[W87X] Dir : %u deg\r", data->wind_dir);
}
