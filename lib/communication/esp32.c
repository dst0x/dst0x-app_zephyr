/* SPDX-License-Identifier: Apache-2.0 */

#include <esp32.h>
#include <at.h>
#include <core.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>

static at_dev_t       s_at;
static esp32_status_t s_status;

static const struct device *esp32_uart = DEVICE_DT_GET(DT_NODELABEL(usart6));

static void urc_handler(const char *line)
{
	if (strcmp(line, "WIFI CONNECTED") == 0) {
		s_status.wifi_state = ESP32_WIFI_CONNECTED;
	} else if (strcmp(line, "WIFI GOT IP") == 0) {
		s_status.wifi_state = ESP32_WIFI_GOT_IP;
	} else if (strcmp(line, "WIFI DISCONNECT") == 0) {
		s_status.wifi_state = ESP32_WIFI_DISCONNECTED;
		s_status.ip_addr[0] = '\0';
		s_status.rssi = 0;
	}
}

static esp32_result_t at_to_esp(at_result_t rc)
{
	switch (rc) {
	case AT_OK:      return ESP32_OK;
	case AT_TIMEOUT: return ESP32_TIMEOUT;
	default:         return ESP32_ERROR;
	}
}

static esp32_result_t send_check(const char *cmd, uint32_t timeout_ms)
{
	return at_to_esp(at_send_cmd(&s_at, cmd, timeout_ms));
}

static esp32_result_t send_resp(const char *cmd, char *resp, uint16_t sz, uint32_t timeout_ms)
{
	return at_to_esp(at_send_cmd_resp(&s_at, cmd, resp, sz, timeout_ms));
}

esp32_result_t esp32_init(void)
{
	memset(&s_status, 0, sizeof(s_status));

	if (at_init(&s_at, esp32_uart, urc_handler) != 0) {
		return ESP32_ERROR;
	}

	k_sleep(K_MSEC(3000));

	for (uint8_t i = 0; i < 10; i++) {
		if (at_send_cmd(&s_at, "AT", 1000) == AT_OK) {
			s_status.init_ok = 1;
			(void)at_send_cmd(&s_at, "ATE0", 1000);
			printk("[ESP32] init OK\r");
			return ESP32_OK;
		}
		k_sleep(K_MSEC(500));
	}

	printk("[ESP32] init failed\r");
	return ESP32_TIMEOUT;
}

esp32_result_t esp32_reset(void)
{
	esp32_result_t rc = send_check("AT+RST", ESP32_UART_TIMEOUT);
	if (rc == ESP32_OK) {
		k_sleep(K_MSEC(2000));
		(void)at_send_cmd(&s_at, "ATE0", 1000);
	}
	return rc;
}

esp32_result_t esp32_wifi_set_mode(uint8_t mode)
{
	char cmd[24];
	(void)snprintf(cmd, sizeof(cmd), "AT+CWMODE=%u", (unsigned)mode);
	return send_check(cmd, ESP32_UART_TIMEOUT);
}

esp32_result_t esp32_wifi_connect(const char *ssid, const char *pass)
{
	char cmd[AT_TX_BUF_SIZE];
	char resp[AT_LINE_MAX];
	esp32_result_t rc;

	if (s_status.init_ok == 0) {
		return ESP32_NOT_INIT;
	}

	rc = esp32_wifi_set_mode(1);
	if (rc != ESP32_OK) {
		return rc;
	}

	(void)snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pass);
	printk("[ESP32] connecting to %s...\r", ssid);

	at_result_t ar = at_send_cmd_resp(&s_at, cmd, resp, sizeof(resp), ESP32_WIFI_TIMEOUT);

	if (ar == AT_OK) {
		s_status.wifi_state = ESP32_WIFI_GOT_IP;
		for (uint8_t retry = 0; retry < 5; retry++) {
			k_sleep(K_MSEC(500));
			if (esp32_wifi_get_ip(s_status.ip_addr, sizeof(s_status.ip_addr)) == ESP32_OK) {
				break;
			}
		}
		(void)esp32_wifi_get_rssi(&s_status.rssi);
		printk("[ESP32] WiFi connected, IP: %s, RSSI: %d\r",
		       s_status.ip_addr, (int)s_status.rssi);
		return ESP32_OK;
	}

	if (strstr(resp, "+CWJAP:3") != NULL) {
		printk("[ESP32] WiFi AP not found\r");
		return ESP32_WIFI_NO_AP;
	}
	if (strstr(resp, "+CWJAP:4") != NULL) {
		printk("[ESP32] WiFi wrong password\r");
		return ESP32_WIFI_WRONG_PASS;
	}

	printk("[ESP32] WiFi connect failed\r");
	return ESP32_WIFI_FAIL;
}

esp32_result_t esp32_wifi_disconnect(void)
{
	esp32_result_t rc = send_check("AT+CWQAP", ESP32_UART_TIMEOUT);
	if (rc == ESP32_OK) {
		s_status.wifi_state = ESP32_WIFI_DISCONNECTED;
		s_status.ip_addr[0] = '\0';
	}
	return rc;
}

esp32_result_t esp32_wifi_get_ip(char *ip_buf, uint16_t buf_size)
{
	char resp[256];

	if (send_resp("AT+CIPSTA?", resp, sizeof(resp), ESP32_UART_TIMEOUT) != ESP32_OK) {
		return ESP32_ERROR;
	}

	char *p = strstr(resp, "+CIPSTA:ip:\"");
	if (p != NULL) {
		p += 12;
	} else {
		p = strstr(resp, "+CIPSTA:\"");
		if (p != NULL) {
			p += 9;
		}
	}

	if (p == NULL) {
		return ESP32_ERROR;
	}

	char *end = strchr(p, '"');
	if (end == NULL || end == p) {
		return ESP32_ERROR;
	}

	uint16_t len = (uint16_t)(end - p);
	if (len >= buf_size) {
		len = buf_size - 1;
	}
	memcpy(ip_buf, p, len);
	ip_buf[len] = '\0';

	if (strcmp(ip_buf, "0.0.0.0") == 0) {
		ip_buf[0] = '\0';
		return ESP32_ERROR;
	}

	return ESP32_OK;
}

esp32_result_t esp32_wifi_get_rssi(int8_t *rssi)
{
	char resp[128];

	if (send_resp("AT+CWJAP?", resp, sizeof(resp), ESP32_UART_TIMEOUT) != ESP32_OK) {
		return ESP32_ERROR;
	}

	char *p = resp;
	uint8_t comma_count = 0;
	while (*p != '\0' && comma_count < 3) {
		if (*p == ',') {
			comma_count++;
		}
		p++;
	}

	if (comma_count == 3) {
		*rssi = (int8_t)atoi(p);
		s_status.rssi = *rssi;
		return ESP32_OK;
	}
	return ESP32_ERROR;
}

esp32_wifi_state_t esp32_wifi_get_state(void)
{
	return s_status.wifi_state;
}

esp32_result_t esp32_ntp_config(int8_t timezone, const char *server)
{
	char cmd[128];

	if (server == NULL) {
		server = ESP32_NTP_SERVER;
	}

	(void)snprintf(cmd, sizeof(cmd), "AT+CIPSNTPCFG=1,%d,\"%s\"",
		       (int)timezone, server);
	return send_check(cmd, ESP32_NTP_TIMEOUT);
}

esp32_result_t esp32_ntp_get_time(uint16_t *year, uint8_t *month, uint8_t *day,
				  uint8_t *hour, uint8_t *minute, uint8_t *second)
{
	char resp[128];

	if (send_resp("AT+CIPSNTPTIME?", resp, sizeof(resp), ESP32_NTP_TIMEOUT) != ESP32_OK) {
		return ESP32_NTP_FAIL;
	}

	char *p = strstr(resp, "+CIPSNTPTIME:");
	if (p == NULL) {
		return ESP32_NTP_FAIL;
	}
	p += 13;

	if (p[4] == '-' && p[7] == '-') {
		*year   = (uint16_t)atoi(p);
		*month  = (uint8_t)atoi(p + 5);
		*day    = (uint8_t)atoi(p + 8);
		*hour   = (uint8_t)atoi(p + 11);
		*minute = (uint8_t)atoi(p + 14);
		*second = (uint8_t)atoi(p + 17);

		if (*year >= 2020 && *year <= 2099) {
			return ESP32_OK;
		}
	}

	static const char * const months[] = {
		"Jan","Feb","Mar","Apr","May","Jun",
		"Jul","Aug","Sep","Oct","Nov","Dec"
	};

	if (strlen(p) < 20) {
		return ESP32_NTP_FAIL;
	}
	p += 4;

	*month = 0;
	for (uint8_t i = 0; i < 12; i++) {
		if (strncmp(p, months[i], 3) == 0) {
			*month = i + 1;
			break;
		}
	}
	if (*month == 0) {
		return ESP32_NTP_FAIL;
	}

	*day    = (uint8_t)atoi(p + 4);
	*hour   = (uint8_t)atoi(p + 7);
	*minute = (uint8_t)atoi(p + 10);
	*second = (uint8_t)atoi(p + 13);
	*year   = (uint16_t)atoi(p + 16);

	if (*year < 2020 || *year > 2099) {
		return ESP32_NTP_FAIL;
	}

	return ESP32_OK;
}

esp32_result_t esp32_mqtt_config(const char *client_id, const char *username,
				 const char *password)
{
	char cmd[AT_TX_BUF_SIZE];

	(void)snprintf(cmd, sizeof(cmd),
		       "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"",
		       client_id, username, password);
	return send_check(cmd, ESP32_MQTT_TIMEOUT);
}

esp32_result_t esp32_mqtt_connect(const char *host, uint16_t port, uint8_t reconnect)
{
	char cmd[AT_TX_BUF_SIZE];
	esp32_result_t rc;

	(void)snprintf(cmd, sizeof(cmd), "AT+MQTTCONN=0,\"%s\",%u,%u",
		       host, (unsigned)port, (unsigned)reconnect);

	printk("[ESP32] MQTT connecting to %s:%u...\r", host, (unsigned)port);
	rc = send_check(cmd, ESP32_MQTT_TIMEOUT);

	if (rc == ESP32_OK) {
		s_status.mqtt_connected = 1;
		printk("[ESP32] MQTT connected\r");
	} else {
		printk("[ESP32] MQTT connect failed\r");
		rc = ESP32_MQTT_FAIL;
	}
	return rc;
}

esp32_result_t esp32_mqtt_publish(const char *topic, const char *data,
				  uint8_t qos, uint8_t retain)
{
	char cmd[AT_TX_BUF_SIZE];
	uint16_t data_len = (uint16_t)strlen(data);

	(void)snprintf(cmd, sizeof(cmd), "AT+MQTTPUBRAW=0,\"%s\",%u,%u,%u",
		       topic, (unsigned)data_len, (unsigned)qos, (unsigned)retain);

	at_result_t ar = at_send_cmd(&s_at, cmd, ESP32_MQTT_TIMEOUT);
	if (ar != AT_OK) {
		return ESP32_MQTT_FAIL;
	}

	k_sleep(K_MSEC(100));

	ar = at_send_raw(&s_at, (const uint8_t *)data, data_len, ESP32_MQTT_TIMEOUT);
	if (ar != AT_OK) {
		return ESP32_MQTT_FAIL;
	}

	ar = at_wait_match(&s_at, "+MQTTPUB:OK", ESP32_MQTT_TIMEOUT);
	return (ar == AT_OK) ? ESP32_OK : ESP32_MQTT_FAIL;
}

esp32_result_t esp32_mqtt_subscribe(const char *topic, uint8_t qos)
{
	char cmd[AT_TX_BUF_SIZE];

	(void)snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"%s\",%u", topic, (unsigned)qos);
	return send_check(cmd, ESP32_MQTT_TIMEOUT);
}

esp32_result_t esp32_mqtt_disconnect(void)
{
	esp32_result_t rc = send_check("AT+MQTTCLEAN=0", ESP32_MQTT_TIMEOUT);
	s_status.mqtt_connected = 0;
	return rc;
}

esp32_result_t esp32_send_signal(const char *payload)
{
	const struct config *cfg = config_get();
	esp32_result_t rc;

	esp32_process();

	if (s_status.wifi_state != ESP32_WIFI_GOT_IP) {
		printk("[ESP32] signal: WiFi lost, reconnecting...\r");
		rc = esp32_wifi_connect(cfg->wifi_ssid, cfg->wifi_pass);
		if (rc != ESP32_OK) {
			return ESP32_WIFI_FAIL;
		}
	}

	if (s_status.mqtt_connected == 0) {
		(void)send_check("AT+MQTTCLEAN=0", 1000);
		k_sleep(K_MSEC(500));

		rc = esp32_mqtt_config(cfg->devid, cfg->mqtt_user, cfg->mqtt_pass);
		if (rc != ESP32_OK) {
			printk("[ESP32] signal: MQTT config failed, resetting...\r");
			(void)esp32_reset();
			rc = esp32_wifi_connect(cfg->wifi_ssid, cfg->wifi_pass);
			if (rc != ESP32_OK) {
				return ESP32_WIFI_FAIL;
			}
			rc = esp32_mqtt_config(cfg->devid, cfg->mqtt_user, cfg->mqtt_pass);
			if (rc != ESP32_OK) {
				return ESP32_MQTT_FAIL;
			}
		}

		rc = esp32_mqtt_connect(cfg->server, cfg->server_port, 1);
		if (rc != ESP32_OK) {
			return ESP32_MQTT_FAIL;
		}
	}

	static uint8_t rssi_cnt = 0;
	if (++rssi_cnt >= 10) {
		rssi_cnt = 0;
		(void)esp32_wifi_get_rssi(&s_status.rssi);
	}

	rc = esp32_mqtt_publish(cfg->mqtt_topic, payload, 1, 0);
	if (rc != ESP32_OK) {
		printk("[ESP32] signal: publish failed\r");
		s_status.mqtt_connected = 0;
		return ESP32_MQTT_FAIL;
	}

	return ESP32_OK;
}

const esp32_status_t *esp32_get_status(void)
{
	return &s_status;
}

const char *esp32_result_str(esp32_result_t result)
{
	switch (result) {
	case ESP32_OK:              return "OK";
	case ESP32_ERROR:           return "ERROR";
	case ESP32_TIMEOUT:         return "TIMEOUT";
	case ESP32_NOT_INIT:        return "NOT_INIT";
	case ESP32_WIFI_FAIL:       return "WIFI_FAIL";
	case ESP32_WIFI_NO_AP:      return "WIFI_NO_AP";
	case ESP32_WIFI_WRONG_PASS: return "WIFI_WRONG_PASS";
	case ESP32_NTP_FAIL:        return "NTP_FAIL";
	case ESP32_MQTT_FAIL:       return "MQTT_FAIL";
	default:                    return "UNKNOWN";
	}
}

void esp32_process(void)
{
	at_process(&s_at);
}
