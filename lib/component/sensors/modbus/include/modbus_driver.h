/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RSC_MODBUS_DRIVER_H
#define RSC_MODBUS_DRIVER_H

#include <stdint.h>

int modbus_driver_init(void);
int modbus_driver_read_holding_reg(uint8_t slave_addr, uint16_t reg_addr,
				   uint16_t *value);

#endif
