/* SPDX-License-Identifier: Apache-2.0 */

#include <modbus_driver.h>
#include <zephyr/kernel.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/sys/crc.h>

#define MODBUS_NODE DT_NODELABEL(modbus0)
#define MODBUS_FC_READ_HOLDING_REGS 0x03

static int s_iface = -1;

static const struct modbus_iface_param client_param = {
	.mode = MODBUS_MODE_RTU,
	.rx_timeout = 100000,
	.serial = {
		.baud = 9600,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
	},
};

static void debug_frame(const char *direction, const uint8_t *frame, size_t length)
{
	printk("[MODBUS %s]", direction);
	for (size_t i = 0; i < length; i++) {
		printk(" %02X", frame[i]);
	}
	printk("\r");
}

static void append_crc(uint8_t *frame, size_t data_length)
{
	uint16_t crc = crc16_ansi(frame, data_length);

	frame[data_length] = (uint8_t)crc;
	frame[data_length + 1] = (uint8_t)(crc >> 8);
}

int modbus_driver_init(void)
{
	const char name[] = {DEVICE_DT_NAME(MODBUS_NODE)};

	s_iface = modbus_iface_get_by_name(name);
	if (s_iface < 0) {
		printk("[MODBUS] iface not found\r");
		return s_iface;
	}

	int err = modbus_init_client(s_iface, client_param);
	if (err != 0) {
		printk("[MODBUS] init failed: %d\r", err);
		return err;
	}

	printk("[MODBUS] init OK\r");
	return 0;
}

int modbus_driver_read_holding_reg(uint8_t slave_addr, uint16_t reg_addr,
				   uint16_t *value)
{
	if (s_iface < 0 || value == NULL) {
		return -EINVAL;
	}

	uint8_t tx_frame[] = {
		slave_addr,
		MODBUS_FC_READ_HOLDING_REGS,
		(uint8_t)(reg_addr >> 8),
		(uint8_t)reg_addr,
		0x00,
		0x01,
		0x00,
		0x00,
	};

	append_crc(tx_frame, sizeof(tx_frame) - 2);
	debug_frame("TX", tx_frame, sizeof(tx_frame));

	int err = modbus_read_holding_regs(s_iface, slave_addr, reg_addr, value, 1);
	if (err != 0) {
		printk("[MODBUS RX] no valid frame, err=%d\r", err);
		return err;
	}

	uint8_t rx_frame[] = {
		slave_addr,
		MODBUS_FC_READ_HOLDING_REGS,
		0x02,
		(uint8_t)(*value >> 8),
		(uint8_t)*value,
		0x00,
		0x00,
	};

	append_crc(rx_frame, sizeof(rx_frame) - 2);
	debug_frame("RX", rx_frame, sizeof(rx_frame));
	return 0;
}
