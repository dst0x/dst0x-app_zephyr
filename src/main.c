/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart2));

static uint8_t rx_buf[32];
static uint8_t rx_pos;

static void process_command(const char *cmd)
{
	if (strcmp(cmd, "on") == 0) {
		gpio_pin_set_dt(&led, 1);
		printk("LED ON\r\n");
	} else if (strcmp(cmd, "off") == 0) {
		gpio_pin_set_dt(&led, 0);
		printk("LED OFF\r\n");
	} else {
		printk("Unknown: %s\r\n", cmd);
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

	printk("RSC ready. Commands: on, off\r\n");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
