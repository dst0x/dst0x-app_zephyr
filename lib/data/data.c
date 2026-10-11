/* SPDX-License-Identifier: Apache-2.0 */

#include <data.h>
#include <sensor_manager.h>
#include <storage.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>

#define WIND_FILE_BUFFER_SIZE 192

struct wind_accumulator {
	uint32_t samples;
	uint32_t direction_sum;
	uint16_t last_direction;
	uint32_t timestamp;
};

K_MUTEX_DEFINE(wind_file_lock);
static struct wind_accumulator wind_accumulator;
static bool wind_store_ready;

static int append_format(char *buffer, size_t buffer_size, size_t *used,
			 const char *format, ...)
{
	if (*used >= buffer_size) {
		return -ENOSPC;
	}

	va_list args;
	va_start(args, format);
	int written = vsnprintf(buffer + *used, buffer_size - *used, format, args);
	va_end(args);

	if (written < 0) {
		return -EIO;
	}
	if ((size_t)written >= buffer_size - *used) {
		buffer[buffer_size - 1] = '\0';
		return -ENOSPC;
	}

	*used += (size_t)written;
	return 0;
}

static uint8_t rssi_to_percent(int8_t rssi)
{
	if (rssi <= -100) {
		return 0;
	}
	if (rssi >= -50) {
		return 100;
	}
	return (uint8_t)((rssi + 100) * 2);
}

static uint32_t json_uint_value(const char *json, const char *key)
{
	const char *field = strstr(json, key);
	if (field == NULL) {
		return 0;
	}

	field = strchr(field, ':');
	return field == NULL ? 0 : (uint32_t)strtoul(field + 1, NULL, 10);
}

static int write_wind_file(void)
{
	struct fs_file_t file;
	char path[32];
	char json[WIND_FILE_BUFFER_SIZE];

	snprintf(path, sizeof(path), "%s/wind.json", storage_mount_point());
	int length = snprintf(json, sizeof(json),
			      "{\"samples\":%lu,\"direction_sum\":%lu,"
			      "\"last_direction\":%u,\"timestamp\":%lu}\n",
			      (unsigned long)wind_accumulator.samples,
			      (unsigned long)wind_accumulator.direction_sum,
			      wind_accumulator.last_direction,
			      (unsigned long)wind_accumulator.timestamp);
	if (length <= 0 || length >= sizeof(json)) {
		return -ENOSPC;
	}

	fs_file_t_init(&file);
	int err = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (err != 0) {
		return err;
	}

	ssize_t written = fs_write(&file, json, (size_t)length);
	if (written == length) {
		err = fs_sync(&file);
	} else {
		err = written < 0 ? (int)written : -EIO;
	}
	fs_close(&file);
	return err;
}

int data_wind_store_init(void)
{
	struct fs_file_t file;
	char path[32];
	char json[WIND_FILE_BUFFER_SIZE];

	snprintf(path, sizeof(path), "%s/wind.json", storage_mount_point());
	memset(&wind_accumulator, 0, sizeof(wind_accumulator));

	fs_file_t_init(&file);
	int err = fs_open(&file, path, FS_O_READ);
	if (err == 0) {
		ssize_t length = fs_read(&file, json, sizeof(json) - 1);
		fs_close(&file);
		if (length > 0) {
			json[length] = '\0';
			wind_accumulator.samples = json_uint_value(json, "\"samples\"");
			wind_accumulator.direction_sum =
				json_uint_value(json, "\"direction_sum\"");
			wind_accumulator.last_direction = (uint16_t)
				json_uint_value(json, "\"last_direction\"");
			wind_accumulator.timestamp = json_uint_value(json, "\"timestamp\"");
		}
	}

	wind_store_ready = true;
	if (err != 0) {
		err = write_wind_file();
		if (err != 0) {
			wind_store_ready = false;
			printk("[DATA] wind.json create failed: %d\r", err);
			return err;
		}
		printk("[INFO] wind.json created\r");
	}

	return 0;
}

int data_wind_accumulate(uint16_t direction, uint32_t timestamp)
{
	if (!wind_store_ready) {
		return -ENODEV;
	}

	k_mutex_lock(&wind_file_lock, K_FOREVER);
	wind_accumulator.samples++;
	wind_accumulator.direction_sum += direction;
	wind_accumulator.last_direction = direction;
	wind_accumulator.timestamp = timestamp;
	int err = write_wind_file();
	k_mutex_unlock(&wind_file_lock);

	return err;
}

int data_build_signal_payload(char *buffer, size_t buffer_size,
			      const data_signal_meta_t *meta)
{
	weather_data_t weather;
	size_t used = 0;
	int err;

	if (buffer == NULL || buffer_size == 0 || meta == NULL ||
	    meta->device_id == NULL || meta->firmware_version == NULL) {
		return -EINVAL;
	}

	buffer[0] = '\0';
	err = append_format(buffer, buffer_size, &used,
			    "{\"id\":\"%s\",\"ver\":\"%s\","
			    "\"timestamp\":%lu,\"DST0-sig\":%u",
			    meta->device_id, meta->firmware_version,
			    (unsigned long)meta->timestamp,
			    rssi_to_percent(meta->rssi));
	if (err != 0) {
		return err;
	}

	err = sensor_manager_get_weather(&weather);
	if (err == 0) {
		int32_t temperature = weather.temperature;
		uint32_t temperature_abs = temperature < 0 ? -temperature : temperature;

		err = append_format(buffer, buffer_size, &used,
				    ",\"DST0-tem\":%s%u.%u,\"DST0-hum\":%u.%u,"
				    "\"DST0-pre\":%u.%u,\"DST0-wsp\":%u.%02u,"
				    "\"DST0-dir\":%u",
				    temperature < 0 ? "-" : "",
				    temperature_abs / 10, temperature_abs % 10,
				    weather.humidity / 10, weather.humidity % 10,
				    weather.pressure / 10, weather.pressure % 10,
				    weather.wind_speed / 100, weather.wind_speed % 100,
				    weather.wind_dir);
		if (err != 0) {
			return err;
		}
	} else {
		printk("[DATA] weather unavailable: %d\r", err);
	}

	return append_format(buffer, buffer_size, &used, "}");
}
