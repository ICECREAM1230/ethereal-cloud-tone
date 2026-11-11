#pragma once
#include <stddef.h>
#include <stdint.h>
typedef void *spi_device_handle_t;
typedef struct {int mosi_io_num,miso_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num,max_transfer_sz;} spi_bus_config_t;
typedef struct {int mode,clock_speed_hz,spics_io_num,queue_size;} spi_device_interface_config_t;
typedef struct {size_t length;const void *tx_buffer;void *rx_buffer;} spi_transaction_t;
#define SPI2_HOST 2
#define SPI_DMA_DISABLED 0
int spi_bus_initialize(int host,const spi_bus_config_t *cfg,int dma);
int spi_bus_add_device(int host,const spi_device_interface_config_t *cfg,spi_device_handle_t *dev);
int spi_device_polling_transmit(spi_device_handle_t dev,spi_transaction_t *t);
int spi_device_transmit(spi_device_handle_t dev,spi_transaction_t *t);
