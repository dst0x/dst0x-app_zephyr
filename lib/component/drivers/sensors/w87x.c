/* SPDX-License-Identifier: Apache-2.0 */

#include <w87x.h>
#include <modbus_driver.h>
#include <errno.h>

#define W87X_REG_TEMP     0x0000
#define W87X_REG_HUMI     0x0001
#define W87X_REG_PRESSURE 0x0006
#define W87X_REG_WINDSPD  0x000B
#define W87X_REG_WINDDIR  0x000C

int w87x_init(struct w87x_device *dev, uint8_t slave_addr)
{
	if (dev == NULL || slave_addr == 0 || slave_addr > 247) {
		return -EINVAL;
	}

	dev->slave_addr = slave_addr;
	return 0;
}

int w87x_read(const struct w87x_device *dev, w87x_data_t *data)
{
	uint16_t value;
	int err;

	if (dev == NULL || data == NULL) {
		return -EINVAL;
	}

	err = modbus_driver_read_holding_reg(dev->slave_addr, W87X_REG_TEMP, &value);
	if (err != 0) {
		return err;
	}
	data->temperature = (int16_t)value;

	err = modbus_driver_read_holding_reg(dev->slave_addr, W87X_REG_HUMI, &value);
	if (err != 0) {
		return err;
	}
	data->humidity = value;

	err = modbus_driver_read_holding_reg(dev->slave_addr, W87X_REG_PRESSURE, &value);
	if (err != 0) {
		return err;
	}
	data->pressure = value;

	err = modbus_driver_read_holding_reg(dev->slave_addr, W87X_REG_WINDSPD, &value);
	if (err != 0) {
		return err;
	}
	data->wind_speed = value;

	err = modbus_driver_read_holding_reg(dev->slave_addr, W87X_REG_WINDDIR, &value);
	if (err != 0) {
		return err;
	}
	data->wind_dir = value;

	return 0;
}
