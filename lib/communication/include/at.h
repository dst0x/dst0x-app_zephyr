/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RSC_AT_H
#define RSC_AT_H

#include <stdint.h>
#include <zephyr/device.h>

#define AT_RX_BUF_SIZE     512
#define AT_TX_BUF_SIZE     256
#define AT_LINE_MAX        256
#define AT_DMA_BUF_SIZE    128

typedef enum {
	AT_OK = 0,
	AT_ERROR,
	AT_TIMEOUT,
	AT_BUSY,
	AT_SEND_FAIL
} at_result_t;

typedef void (*at_urc_handler_t)(const char *line);

typedef struct {
	const struct device *dev;
	uint8_t              rx_buf[AT_RX_BUF_SIZE];
	uint8_t              dma_buf[2][AT_DMA_BUF_SIZE];
	uint8_t              dma_idx;
	volatile uint16_t    rx_head;
	uint16_t             rx_tail;
	at_urc_handler_t     urc_handler;
} at_dev_t;

int         at_init(at_dev_t *at, const struct device *uart, at_urc_handler_t urc_cb);
void        at_deinit(at_dev_t *at);
at_result_t at_send_cmd(at_dev_t *at, const char *cmd, uint32_t timeout_ms);
at_result_t at_send_cmd_resp(at_dev_t *at, const char *cmd,
			     char *resp, uint16_t resp_size,
			     uint32_t timeout_ms);
at_result_t at_send_raw(at_dev_t *at, const uint8_t *data, uint16_t len, uint32_t timeout_ms);
at_result_t at_wait_match(at_dev_t *at, const char *match, uint32_t timeout_ms);
void        at_process(at_dev_t *at);

#endif
