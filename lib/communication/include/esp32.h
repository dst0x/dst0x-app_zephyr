/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RSC_ESP32_H
#define RSC_ESP32_H

#include <stdint.h>

#define ESP32_UART_TIMEOUT     5000
#define ESP32_WIFI_TIMEOUT     15000
#define ESP32_NTP_TIMEOUT      10000
#define ESP32_MQTT_TIMEOUT     10000
#define ESP32_NTP_SERVER       "pool.ntp.org"
#define ESP32_MQTT_PAYLOAD_MAX 512

typedef enum {
	ESP32_OK = 0,
	ESP32_ERROR,
	ESP32_TIMEOUT,
	ESP32_NOT_INIT,
	ESP32_WIFI_FAIL,
	ESP32_WIFI_NO_AP,
	ESP32_WIFI_WRONG_PASS,
	ESP32_NTP_FAIL,
	ESP32_MQTT_FAIL
} esp32_result_t;

typedef enum {
	ESP32_WIFI_DISCONNECTED = 0,
	ESP32_WIFI_CONNECTED,
	ESP32_WIFI_GOT_IP
} esp32_wifi_state_t;

typedef struct {
	uint8_t            init_ok;
	esp32_wifi_state_t wifi_state;
	char               ip_addr[16];
	int8_t             rssi;
	uint8_t            mqtt_connected;
} esp32_status_t;

esp32_result_t     esp32_init(void);
esp32_result_t     esp32_reset(void);

esp32_result_t     esp32_wifi_set_mode(uint8_t mode);
esp32_result_t     esp32_wifi_connect(const char *ssid, const char *pass);
esp32_result_t     esp32_wifi_disconnect(void);
esp32_result_t     esp32_wifi_get_ip(char *ip_buf, uint16_t buf_size);
esp32_result_t     esp32_wifi_get_rssi(int8_t *rssi);
esp32_wifi_state_t esp32_wifi_get_state(void);

esp32_result_t     esp32_ntp_config(int8_t timezone, const char *server);
esp32_result_t     esp32_ntp_get_time(uint16_t *year, uint8_t *month, uint8_t *day,
				      uint8_t *hour, uint8_t *minute, uint8_t *second);

esp32_result_t     esp32_mqtt_config(const char *client_id, const char *username,
				     const char *password);
esp32_result_t     esp32_mqtt_connect(const char *host, uint16_t port, uint8_t reconnect);
esp32_result_t     esp32_mqtt_publish(const char *topic, const char *data,
				      uint8_t qos, uint8_t retain);
esp32_result_t     esp32_mqtt_subscribe(const char *topic, uint8_t qos);
esp32_result_t     esp32_mqtt_disconnect(void);

esp32_result_t     esp32_send_signal(const char *payload);

const esp32_status_t *esp32_get_status(void);
const char           *esp32_result_str(esp32_result_t result);
void                  esp32_process(void);

#endif
