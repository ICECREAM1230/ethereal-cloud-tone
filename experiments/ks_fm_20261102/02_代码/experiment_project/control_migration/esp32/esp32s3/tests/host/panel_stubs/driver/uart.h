#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
typedef int uart_port_t;
#define UART_NUM_0 0
#define UART_NUM_1 1
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE (-1)
#define UART_SIGNAL_RXD_INV 1
typedef struct { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; } uart_config_t;
esp_err_t uart_driver_install(uart_port_t port, int rx, int tx, int queue_size, void *queue, int flags);
esp_err_t uart_param_config(uart_port_t port, const uart_config_t *config);
esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts);
esp_err_t uart_set_line_inverse(uart_port_t port, uint32_t flags);
esp_err_t uart_flush_input(uart_port_t port);
int uart_read_bytes(uart_port_t port, void *buffer, uint32_t size, TickType_t wait);
