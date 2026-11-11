/* Actual spi_regs.c framing, with only ESP-IDF wire IO replaced. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/spi_master.h"
#include "spi_regs.h"
static unsigned frames,words_read;
static uint32_t expected(unsigned voice) {return 0xa1234500u+voice;}
int spi_bus_initialize(int host,const spi_bus_config_t *cfg,int dma)
{(void)host;(void)dma;assert(cfg->max_transfer_sz==64);return 0;}
int spi_bus_add_device(int host,const spi_device_interface_config_t *cfg,spi_device_handle_t *device)
{(void)host;(void)cfg;*device=(void*)1;return 0;}
int spi_device_polling_transmit(spi_device_handle_t device,spi_transaction_t *transaction)
{
    (void)device;
    assert(transaction->rx_buffer && transaction->length%8==0);
    unsigned bytes=transaction->length/8;
    assert(bytes>=5 && bytes<=53 && (bytes-5)%4==0);
    const uint8_t *tx=transaction->tx_buffer;uint8_t *rx=transaction->rx_buffer;
    unsigned address=((unsigned)tx[1]<<8)|tx[2],count=(bytes-5)/4;
    assert(tx[0]==0xc0 && address>=0x0540 && address+count<=0x0550 && count<=12);
    frames++;words_read+=count;memset(rx,0,bytes);rx[0]=0xa5;
    for(unsigned i=0;i<count;i++)for(unsigned byte=0;byte<4;byte++)
        rx[5+4*i+byte]=(uint8_t)(expected(address-0x0540+i)>>(24-8*byte));
    return 0;
}
int spi_device_transmit(spi_device_handle_t device,spi_transaction_t *transaction)
{return spi_device_polling_transmit(device,transaction);}
int main(void)
{
    fpga_spi_init(45,48,47,21,10000000);
    uint32_t started[16];for(unsigned i=0;i<16;i++)started[i]=0xdeadbeefu;
    // Reproduce the real API's silent cap: a successful 16 request fills only12.
    assert(fpga_word_read_burst(0x0540,started,16) && frames==1 && words_read==12);
    for(unsigned i=0;i<16;i++)assert(started[i]==(i<12?expected(i):0xdeadbeefu));
    frames=words_read=0;
    assert(fpga_word_read_burst(0x0540,started,8));
    assert(fpga_word_read_burst(0x0548,started+8,8));
    assert(frames==2 && words_read==16);
    for(unsigned i=0;i<16;i++)assert(started[i]==expected(i));
    puts("KS16 real SPI: 12-word cap reproduced; two 8-word bursts preserve all16 including15 PASS");
    return 0;
}
