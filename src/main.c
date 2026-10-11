/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <string.h>
#include <core.h>
#include <data.h>
#include <esp32.h>
#include <sensor_manager.h>
#include <storage.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#define APP_VERSION "1.0.0"

#define COMMAND_MAX_LEN          32
#define COMMAND_QUEUE_DEPTH      8
#define SENSOR_POLL_INTERVAL_MS  60000
#define NETWORK_RETRY_INTERVAL   K_SECONDS(10)
#define NETWORK_WAKE_LEAD_MS     30000
#define COMMAND_THREAD_STACK     2048
#define SENSOR_THREAD_STACK      2048
#define COMM_THREAD_STACK        6144

struct command_message {
	char text[COMMAND_MAX_LEN];
};

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart2));

K_MSGQ_DEFINE(command_queue, sizeof(struct command_message),
	      COMMAND_QUEUE_DEPTH, 4);
K_SEM_DEFINE(send_request, 0, 1);

K_THREAD_STACK_DEFINE(command_stack, COMMAND_THREAD_STACK);
K_THREAD_STACK_DEFINE(sensor_stack, SENSOR_THREAD_STACK);
K_THREAD_STACK_DEFINE(comm_stack, COMM_THREAD_STACK);

static struct k_thread command_thread_data;
static struct k_thread sensor_thread_data;
static struct k_thread comm_thread_data;

static uint8_t rx_buf[COMMAND_MAX_LEN];
static uint8_t rx_pos;

static uint32_t s_epoch_base;
static int64_t s_epoch_tick;

static int64_t get_timestamp_ms(void)
{
	if (s_epoch_base == 0) {
		return 0;
	}
	return (int64_t)s_epoch_base * MSEC_PER_SEC +
	       (k_uptime_get() - s_epoch_tick);
}

static uint32_t get_timestamp(void)
{
	return (uint32_t)(get_timestamp_ms() / MSEC_PER_SEC);
}

static int sync_network_time(const struct config *cfg)
{
	uint16_t year;
	uint8_t month, day, hour, minute, second;

	if (esp32_ntp_config(cfg->ntp_timezone, NULL) != ESP32_OK) {
		printk("[WARN] NTP config failed\r");
		return -1;
	}

	for (uint8_t retry = 0; retry < 3; retry++) {
		k_sleep(K_SECONDS(2));
		if (esp32_ntp_get_time(&year, &month, &day, &hour, &minute,
				       &second) == ESP32_OK) {
			s_epoch_base = datetime_to_epoch(year, month, day, hour,
						    minute, second);
			s_epoch_tick = k_uptime_get();
			printk("[NTP] %04u-%02u-%02u %02u:%02u:%02u\r",
			       year, month, day, hour, minute, second);
			return 0;
		}
		printk("[WARN] NTP sync retry %u/3\r", (unsigned)(retry + 1));
	}

	return -1;
}

static void publish_signal(uint32_t timestamp)
{
	static char payload[ESP32_MQTT_PAYLOAD_MAX];
	const struct config *cfg = config_get();
	const esp32_status_t *status = esp32_get_status();
	data_signal_meta_t meta = {
		.device_id = cfg->devid,
		.firmware_version = APP_VERSION,
		.timestamp = timestamp,
		.rssi = status->rssi,
	};

	int err = data_build_signal_payload(payload, sizeof(payload), &meta);
	if (err != 0) {
		printk("[DATA] payload build failed: %d\r", err);
		return;
	}

	esp32_result_t result = esp32_send_signal(payload);
	if (result == ESP32_OK) {
		printk("[MQTT] %s\r", payload);
	} else {
		printk("[MQTT] publish failed, rc=%d\r", (int)result);
	}
}

static void process_command(const char *command)
{
	if (strcmp(command, "on") == 0) {
		gpio_pin_set_dt(&led, 1);
		printk("LED ON\r");
	} else if (strcmp(command, "off") == 0) {
		gpio_pin_set_dt(&led, 0);
		printk("LED OFF\r");
	} else if (strcmp(command, "config") == 0) {
		config_print();
	} else if (strcmp(command, "status") == 0) {
		const esp32_status_t *status = esp32_get_status();
		printk("[INFO] WiFi  : %s\r",
		       status->wifi_state == ESP32_WIFI_GOT_IP ?
		       "connected" : "disconnected");
		printk("[INFO] IP    : %s\r",
		       status->ip_addr[0] ? status->ip_addr : "-");
		printk("[INFO] RSSI  : %d\r", (int)status->rssi);
		printk("[INFO] MQTT  : %s\r",
		       status->mqtt_connected ? "connected" : "disconnected");
	} else if (strcmp(command, "send") == 0) {
		k_sem_give(&send_request);
		printk("[INFO] send requested\r");
	} else if (strcmp(command, "weather") == 0) {
		weather_data_t weather;
		if (sensor_manager_get_weather(&weather) == 0) {
			sensor_manager_print_weather(&weather);
		} else {
			printk("[W87X] data not ready\r");
		}
	} else {
		printk("Unknown: %s\r", command);
	}
}

static void command_thread(void *arg1, void *arg2, void *arg3)
{
	struct command_message message;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		k_msgq_get(&command_queue, &message, K_FOREVER);
		process_command(message.text);
	}
}

static void sensor_thread(void *arg1, void *arg2, void *arg3)
{
	int64_t next_sample = k_uptime_get();

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		int err = sensor_manager_sample();
		if (err != 0) {
			printk("[SENSOR] read failed: %d\r", err);
		} else {
			weather_data_t weather;
			if (sensor_manager_get_weather(&weather) == 0) {
				uint32_t timestamp = get_timestamp();
				if (timestamp != 0) {
					err = data_wind_accumulate(weather.wind_dir,
								   timestamp);
					if (err != 0) {
						printk("[DATA] wind.json write failed: %d\r",
						       err);
					}
				}
			}
		}

		next_sample += SENSOR_POLL_INTERVAL_MS;
		int64_t sleep_ms = next_sample - k_uptime_get();
		if (sleep_ms > 0) {
			k_sleep(K_MSEC(sleep_ms));
		} else {
			next_sample = k_uptime_get();
		}
	}
}

static int communication_prepare(const struct config *cfg)
{
	if (esp32_get_status()->wifi_state != ESP32_WIFI_GOT_IP) {
		printk("[INFO] WiFi connecting...\r");
		if (esp32_wifi_connect(cfg->wifi_ssid, cfg->wifi_pass) != ESP32_OK) {
			return -1;
		}
	}

	if (sync_network_time(cfg) != 0 && s_epoch_base == 0) {
		return -1;
	}

	if (!esp32_get_status()->mqtt_connected) {
		/* ESP-AT rejects MQTTUSERCFG when link 0 still has stale state. */
		(void)esp32_mqtt_disconnect();
		k_sleep(K_MSEC(500));

		if (esp32_mqtt_config(cfg->devid, cfg->mqtt_user,
				       cfg->mqtt_pass) != ESP32_OK) {
			printk("[WARN] MQTT config failed\r");
			return -1;
		}
		if (esp32_mqtt_connect(cfg->server, cfg->server_port, 1) != ESP32_OK) {
			printk("[WARN] MQTT connect failed\r");
			return -1;
		}
	}

	return 0;
}

static void communication_suspend(void)
{
	if (esp32_get_status()->mqtt_connected) {
		(void)esp32_mqtt_disconnect();
	}
	if (esp32_get_status()->wifi_state != ESP32_WIFI_DISCONNECTED) {
		(void)esp32_wifi_disconnect();
	}
	printk("[INFO] Communication suspended\r");
}

static bool wait_until_timestamp(int64_t target_ms)
{
	while (get_timestamp_ms() < target_ms) {
		int64_t remaining = target_ms - get_timestamp_ms();
		int64_t wait_ms = remaining > 1000 ? 1000 : remaining;

		if (k_sem_take(&send_request, K_MSEC(wait_ms)) == 0) {
			return true;
		}
		esp32_process();
	}

	return false;
}

static void communication_thread(void *arg1, void *arg2, void *arg3)
{
	const struct config *cfg = config_get();
	bool esp_ready = false;
	int32_t interval_s = cfg->dev_inv > 0 ? cfg->dev_inv : 300;
	int64_t interval_ms = (int64_t)interval_s * MSEC_PER_SEC;
	int64_t wake_lead_ms = interval_ms / 2;

	if (wake_lead_ms > NETWORK_WAKE_LEAD_MS) {
		wake_lead_ms = NETWORK_WAKE_LEAD_MS;
	}

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (!esp_ready) {
		if (esp32_init() == ESP32_OK) {
			esp_ready = true;
			break;
		}
		printk("[WARN] ESP32 init retry\r");
		k_sleep(NETWORK_RETRY_INTERVAL);
	}

	while (communication_prepare(cfg) != 0) {
		printk("[WARN] communication prepare retry\r");
		k_sleep(NETWORK_RETRY_INTERVAL);
	}

	/* Publish once after boot, then align all periodic sends to wall time. */
	publish_signal(get_timestamp());
	communication_suspend();

	while (1) {
		int64_t now_ms = get_timestamp_ms();
		int64_t target_ms = ((now_ms / interval_ms) + 1) * interval_ms;
		int64_t wake_ms = target_ms - wake_lead_ms;

		while (get_timestamp_ms() < wake_ms) {
			if (wait_until_timestamp(wake_ms)) {
				if (communication_prepare(cfg) == 0) {
					publish_signal(get_timestamp());
					communication_suspend();
				}
			}
		}

		while (communication_prepare(cfg) != 0) {
			printk("[WARN] communication prepare retry\r");
			k_sleep(K_SECONDS(2));
			if (get_timestamp_ms() >= target_ms) {
				break;
			}
		}

		/* NTP refresh may correct clock drift; retain the same time boundary. */
		while (get_timestamp_ms() < target_ms) {
			if (wait_until_timestamp(target_ms)) {
				publish_signal(get_timestamp());
			}
		}

		publish_signal((uint32_t)(target_ms / MSEC_PER_SEC));
		communication_suspend();
	}
}

static void uart_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	uart_irq_update(dev);
	if (!uart_irq_rx_ready(dev)) {
		return;
	}

	uint8_t character;
	while (uart_fifo_read(dev, &character, 1) == 1) {
		if (character == '\r' || character == '\n') {
			if (rx_pos > 0) {
				struct command_message message = {0};
				memcpy(message.text, rx_buf, rx_pos);
				message.text[rx_pos] = '\0';
				(void)k_msgq_put(&command_queue, &message, K_NO_WAIT);
				rx_pos = 0;
			}
		} else if (rx_pos < sizeof(rx_buf) - 1) {
			rx_buf[rx_pos++] = character;
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

	if (storage_init() != 0 || config_load() != 0) {
		printk("[ERROR] startup stopped: storage/config unavailable\r");
		return 0;
	}
	config_print();
	(void)data_wind_store_init();

	(void)sensor_manager_init();

	k_thread_create(&command_thread_data, command_stack,
			K_THREAD_STACK_SIZEOF(command_stack), command_thread,
			NULL, NULL, NULL, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	k_thread_create(&sensor_thread_data, sensor_stack,
			K_THREAD_STACK_SIZEOF(sensor_stack), sensor_thread,
			NULL, NULL, NULL, K_PRIO_PREEMPT(6), 0, K_NO_WAIT);
	k_thread_create(&comm_thread_data, comm_stack,
			K_THREAD_STACK_SIZEOF(comm_stack), communication_thread,
			NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);

	return 0;
}
