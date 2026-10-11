/* SPDX-License-Identifier: Apache-2.0 */

#include <at.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

static void uart_async_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
	at_dev_t *at = (at_dev_t *)user_data;

	switch (evt->type) {
	case UART_RX_RDY:
		for (uint16_t i = 0; i < evt->data.rx.len; i++) {
			uint16_t next = (at->rx_head + 1) % AT_RX_BUF_SIZE;
			if (next != at->rx_tail) {
				at->rx_buf[at->rx_head] = evt->data.rx.buf[evt->data.rx.offset + i];
				at->rx_head = next;
			}
		}
		break;
	case UART_RX_BUF_REQUEST:
		at->dma_idx ^= 1;
		uart_rx_buf_rsp(dev, at->dma_buf[at->dma_idx], AT_DMA_BUF_SIZE);
		break;
	case UART_RX_DISABLED:
		at->dma_idx = 0;
		uart_rx_enable(dev, at->dma_buf[0], AT_DMA_BUF_SIZE, 10000);
		break;
	default:
		break;
	}
}

static uint16_t rx_available(at_dev_t *at)
{
	uint16_t head = at->rx_head;
	if (head >= at->rx_tail) {
		return head - at->rx_tail;
	}
	return AT_RX_BUF_SIZE - at->rx_tail + head;
}

static uint8_t rx_read_byte(at_dev_t *at)
{
	uint8_t b = at->rx_buf[at->rx_tail];
	at->rx_tail = (at->rx_tail + 1) % AT_RX_BUF_SIZE;
	return b;
}

static uint8_t get_line(at_dev_t *at, char *line, uint16_t max_len)
{
	uint16_t avail = rx_available(at);
	uint16_t scan = 0;

	while (scan < avail) {
		uint8_t b = at->rx_buf[(at->rx_tail + scan) % AT_RX_BUF_SIZE];
		if (b == '\n') {
			uint16_t out = 0;
			for (uint16_t i = 0; i <= scan; i++) {
				uint8_t c = rx_read_byte(at);
				if (c != '\r' && c != '\n' && out < (max_len - 1)) {
					line[out++] = (char)c;
				}
			}
			line[out] = '\0';
			return (out > 0) ? 1 : 0;
		}
		scan++;
	}
	return 0;
}

static at_result_t parse_final(const char *line)
{
	if (strcmp(line, "OK") == 0) {
		return AT_OK;
	}
	if (strcmp(line, "ERROR") == 0 ||
	    strncmp(line, "+CME ERROR", 10) == 0 ||
	    strncmp(line, "+CMS ERROR", 10) == 0) {
		return AT_ERROR;
	}
	if (strncmp(line, "busy", 4) == 0) {
		return AT_BUSY;
	}
	return (at_result_t)0xFF;
}

int at_init(at_dev_t *at, const struct device *uart, at_urc_handler_t urc_cb)
{
	if (!device_is_ready(uart)) {
		return -1;
	}

	at->dev = uart;
	at->rx_head = 0;
	at->rx_tail = 0;
	at->urc_handler = urc_cb;
	at->dma_idx = 0;
	memset(at->rx_buf, 0, sizeof(at->rx_buf));
	memset(at->dma_buf, 0, sizeof(at->dma_buf));

	uart_callback_set(uart, uart_async_cb, at);
	uart_rx_enable(uart, at->dma_buf[0], AT_DMA_BUF_SIZE, 10000);

	return 0;
}

void at_deinit(at_dev_t *at)
{
	uart_rx_disable(at->dev);
	at->rx_head = 0;
	at->rx_tail = 0;
}

at_result_t at_send_cmd(at_dev_t *at, const char *cmd, uint32_t timeout_ms)
{
	return at_send_cmd_resp(at, cmd, NULL, 0, timeout_ms);
}

at_result_t at_send_cmd_resp(at_dev_t *at, const char *cmd,
			     char *resp, uint16_t resp_size,
			     uint32_t timeout_ms)
{
	char tx[AT_TX_BUF_SIZE];
	char line[AT_LINE_MAX];
	int n;

	n = snprintf(tx, sizeof(tx), "%s\r\n", cmd);
	if (n <= 0 || (uint16_t)n >= sizeof(tx)) {
		return AT_SEND_FAIL;
	}

	if (resp != NULL && resp_size > 0) {
		resp[0] = '\0';
	}

	/* flush stale data */
	at->rx_tail = at->rx_head;

	for (int i = 0; i < n; i++) {
		uart_poll_out(at->dev, tx[i]);
	}

	int64_t start = k_uptime_get();
	while ((k_uptime_get() - start) < timeout_ms) {
		if (get_line(at, line, sizeof(line)) != 0) {
			if (strncmp(line, cmd, strlen(cmd)) == 0) {
				continue;
			}

			at_result_t rc = parse_final(line);
			if (rc != (at_result_t)0xFF) {
				return rc;
			}

			if (resp != NULL && resp_size > 1) {
				uint16_t cur = (uint16_t)strlen(resp);
				if (cur < (resp_size - 2)) {
					uint16_t rem = resp_size - cur - 1;
					if (cur > 0) {
						resp[cur++] = '\n';
						resp[cur] = '\0';
						rem--;
					}
					strncat(resp, line, rem);
				}
			}

			if (at->urc_handler != NULL) {
				at->urc_handler(line);
			}
		}
		k_sleep(K_MSEC(1));
	}
	return AT_TIMEOUT;
}

at_result_t at_send_raw(at_dev_t *at, const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
	ARG_UNUSED(timeout_ms);

	for (uint16_t i = 0; i < len; i++) {
		uart_poll_out(at->dev, data[i]);
	}
	return AT_OK;
}

at_result_t at_wait_match(at_dev_t *at, const char *match, uint32_t timeout_ms)
{
	char line[AT_LINE_MAX];
	int64_t start = k_uptime_get();

	while ((k_uptime_get() - start) < timeout_ms) {
		if (get_line(at, line, sizeof(line)) != 0) {
			if (strstr(line, match) != NULL) {
				return AT_OK;
			}
			at_result_t rc = parse_final(line);
			if (rc == AT_ERROR || rc == AT_BUSY) {
				return rc;
			}
		}
		k_sleep(K_MSEC(1));
	}
	return AT_TIMEOUT;
}

void at_process(at_dev_t *at)
{
	char line[AT_LINE_MAX];

	while (get_line(at, line, sizeof(line)) != 0) {
		if (at->urc_handler != NULL) {
			at->urc_handler(line);
		}
	}
}
