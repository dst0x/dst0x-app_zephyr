/* SPDX-License-Identifier: Apache-2.0 */

#include <core.h>
#include <storage.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/data/json.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>

#define CONFIG_FILE_BUF 1024

static const struct json_obj_descr config_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct config, devid, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config, dev_inv, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct config, server, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config, server_port, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct config, wifi_ssid, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config, wifi_pass, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config, mqtt_topic, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config, mqtt_user, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config, mqtt_pass, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config, ntp_timezone, JSON_TOK_NUMBER),
};

static struct config config_g;
static char config_buf[CONFIG_FILE_BUF];

const struct config *config_get(void)
{
	return &config_g;
}

int config_load(void)
{
	struct fs_file_t f;
	ssize_t br;
	int64_t ret;
	char path[32];

	snprintf(path, sizeof(path), "%s/config.json", storage_mount_point());

	fs_file_t_init(&f);
	if (fs_open(&f, path, FS_O_READ) != 0) {
		printk("[ERROR] Cannot open %s\r", path);
		return -1;
	}
	br = fs_read(&f, config_buf, sizeof(config_buf) - 1);
	fs_close(&f);

	if (br <= 0) {
		printk("[ERROR] Cannot read %s\r", path);
		return -1;
	}
	config_buf[br] = '\0';

	ret = json_obj_parse(config_buf, br,
			     config_descr, ARRAY_SIZE(config_descr),
			     &config_g);
	if (ret < 0) {
		printk("[ERROR] JSON parse failed: %d\r", (int)ret);
		return -1;
	}
	printk("[INFO] JSON parsed\r");
	return 0;
}

uint32_t datetime_to_epoch(uint16_t year, uint8_t month, uint8_t day,
			   uint8_t hour, uint8_t minute, uint8_t second)
{
	static const uint16_t days_before_month[] = {
		0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
	};

	uint32_t y = year - 1970;
	uint32_t leap_years = (year - 1) / 4 - (year - 1) / 100 + (year - 1) / 400 - 477;
	uint32_t days = y * 365 + leap_years + days_before_month[month - 1] + (day - 1);

	if (month <= 2 && (year % 4 == 0) &&
	    ((year % 100 != 0) || (year % 400 == 0))) {
		/* no leap day adjustment needed before March */
	} else if (month > 2 && (year % 4 == 0) &&
		   ((year % 100 != 0) || (year % 400 == 0))) {
		days++;
	}

	return days * 86400u + hour * 3600u + minute * 60u + second;
}

void config_print(void)
{
	printk("[INFO] Device ID        : %s\r", config_g.devid ? config_g.devid : "?");
	printk("[INFO] Device Interval  : %d s\r", config_g.dev_inv);
	printk("[INFO] Server           : %s\r", config_g.server ? config_g.server : "?");
	printk("[INFO] Server Port      : %d\r", config_g.server_port);
	printk("[INFO] WiFi SSID        : %s\r", config_g.wifi_ssid ? config_g.wifi_ssid : "?");
	printk("[INFO] MQTT Topic       : %s\r", config_g.mqtt_topic ? config_g.mqtt_topic : "?");
	printk("[INFO] MQTT User        : %s\r", config_g.mqtt_user ? config_g.mqtt_user : "?");
	printk("[INFO] Timezone         : %d\r", config_g.ntp_timezone);
}
