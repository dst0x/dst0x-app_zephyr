/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/data/json.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fs/fs.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>
#include <ff.h>

#define DISK_NAME         DT_PROP(DT_NODELABEL(sdmmc1), disk_name)
#define MOUNT_POINT       "/" DISK_NAME ":"
#define CONFIG_FILE_PATH  MOUNT_POINT "/config.json"
#define CONFIG_FILE_BUF   1024
#define CONFIG_STR_MAX    64

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = MOUNT_POINT,
};

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart2));

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

static struct config g_config;
static char config_buf[CONFIG_FILE_BUF];

static uint8_t rx_buf[32];
static uint8_t rx_pos;

static int sd_mount(void)
{
	if (disk_access_init(DISK_NAME) != 0) {
		printk("[ERROR] disk_access_init failed\r\n");
		return -1;
	}
	mp.mnt_point = MOUNT_POINT;
	if (fs_mount(&mp) != 0) {
		printk("[ERROR] fs_mount failed\r\n");
		return -1;
	}
	printk("[INFO] SD card mounted\r");
	return 0;
}

static int config_load(void)
{
	struct fs_file_t f;
	ssize_t br;
	int64_t ret;

	fs_file_t_init(&f);
	if (fs_open(&f, CONFIG_FILE_PATH, FS_O_READ) != 0) {
		printk("[ERROR] Cannot open %s\r", CONFIG_FILE_PATH);
		return -1;
	}
	br = fs_read(&f, config_buf, sizeof(config_buf) - 1);
	fs_close(&f);

	if (br <= 0) {
		printk("[ERROR] Cannot read %s\r", CONFIG_FILE_PATH);
		return -1;
	}
	config_buf[br] = '\0';

	ret = json_obj_parse(config_buf, br,
			     config_descr, ARRAY_SIZE(config_descr),
			     &g_config);
	if (ret < 0) {
		printk("[ERROR] JSON parse failed: %d\r", (int)ret);
		return -1;
	}
	printk("[INFO] JSON parsed\r");
	return 0;
}

static void config_print(void)
{
	printk("[INFO] Device ID        : %s\r", g_config.devid ? g_config.devid : "?");
	printk("[INFO] Device Interval  : %d s\r", g_config.dev_inv);
	printk("[INFO] Server           : %s\r", g_config.server ? g_config.server : "?");
	printk("[INFO] Server Port      : %d\r", g_config.server_port);
	printk("[INFO] WiFi SSID        : %s\r", g_config.wifi_ssid ? g_config.wifi_ssid : "?");
	printk("[INFO] MQTT Topic       : %s\r", g_config.mqtt_topic ? g_config.mqtt_topic : "?");
	printk("[INFO] MQTT User        : %s\r", g_config.mqtt_user ? g_config.mqtt_user : "?");
	printk("[INFO] Timezone         : %d\r", g_config.ntp_timezone);
}

static void process_command(const char *cmd)
{
	if (strcmp(cmd, "on") == 0) {
		gpio_pin_set_dt(&led, 1);
		printk("LED ON\n", g_config.devid ? g_config.devid : "?");
	} else if (strcmp(cmd, "off") == 0) {
		gpio_pin_set_dt(&led, 0);
		printk("LED OFF\n", g_config.devid ? g_config.devid : "?");
	} else if (strcmp(cmd, "config") == 0) {
		config_print();
	} else {
		printk("Unknown: %s\n", cmd);
	}		
}

static void uart_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	uart_irq_update(dev);
	if (!uart_irq_rx_ready(dev)) {
		return;
	}

	uint8_t c;
	while (uart_fifo_read(dev, &c, 1) == 1) {
		if (c == '\r' || c == '\n') {
			if (rx_pos > 0) {
				rx_buf[rx_pos] = '\0';
				process_command((const char *)rx_buf);
				rx_pos = 0;
			}
		} else if (rx_pos < sizeof(rx_buf) - 1) {
			rx_buf[rx_pos++] = c;
		}
	}
}

int main(void)
{
	if (!gpio_is_ready_dt(&led) ||
	    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) < 0) {
		return 0;
	}

	if (!device_is_ready(uart_dev)) {
		return 0;
	}

	uart_irq_callback_set(uart_dev, uart_cb);
	uart_irq_rx_enable(uart_dev);

	printk("RSC1.0 Running\r");

	if (sd_mount() == 0 && config_load() == 0) {
		config_print();
	}

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
