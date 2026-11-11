// Compile the real driver; only the synchronous SDK boundary is injected.
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "driver/spi_master.h"
#include "spi_regs.h"
#include "esp_log.h"
static jmp_buf sdk_abort;
static unsigned checks_failed,transactions,read_frames,write_frames;
static int transfer_error;
static bool id_ok=true,expect_abort;
int spi_bus_initialize(int host,const spi_bus_config_t *config,int dma)
{ (void)host;(void)config;(void)dma;return 0; }
int spi_bus_add_device(int host,const spi_device_interface_config_t *config,spi_device_handle_t *device)
{ (void)host;(void)config;*device=(void*)1;return 0; }
int spi_device_polling_transmit(spi_device_handle_t device,spi_transaction_t *t)
{
    (void)device;transactions++;
    const uint8_t *tx=t->tx_buffer;uint8_t *rx=t->rx_buffer;
    if(tx[0]&0x80) {
        read_frames++;assert(tx[0]==0xc0 && rx && tx[1]==7 && tx[2]==0xf0);
        assert(t->length==49*8);memset(rx,0xcc,t->length/8);rx[0]=id_ok?0xa5:0;
        for(unsigned i=0;i<11;i++)for(unsigned j=0;j<4;j++)rx[5+4*i+j]=(uint8_t)((0x12345670u+i)>>(24-8*j));
    } else write_frames++;
    return transfer_error;
}
int spi_device_transmit(spi_device_handle_t device,spi_transaction_t *t)
{ return spi_device_polling_transmit(device,t); }
#undef ESP_ERROR_CHECK
#define ESP_ERROR_CHECK(expression) do {if((expression)!=0){checks_failed++;assert(expect_abort);longjmp(sdk_abort,1);}}while(0)
#include "spi_regs.c"
int main(void)
{
    uint32_t observed[11],sentinel[11];for(unsigned i=0;i<11;i++)sentinel[i]=0xfeed0000u+i;
    memcpy(observed,sentinel,sizeof(observed));transfer_error=-5;
    assert(!fpga_word_read_burst_observe(0x7f0,observed,11));
    assert(!memcmp(observed,sentinel,sizeof(observed)) && !checks_failed && transactions==1);
    transfer_error=0;id_ok=false;
    assert(!fpga_word_read_burst_observe(0x7f0,observed,11));
    assert(!memcmp(observed,sentinel,sizeof(observed)) && !checks_failed);
    id_ok=true;assert(fpga_word_read_burst_observe(0x7f0,observed,11));
    for(unsigned i=0;i<11;i++)assert(observed[i]==0x12345670u+i);
    assert(read_frames==3 && !write_frames && !checks_failed);
    assert(!fpga_word_read_burst_observe(0x7f0,NULL,11));
    assert(!fpga_word_read_burst_observe(0x7f0,observed,0));
    assert(!fpga_word_read_burst_observe(0x7f0,observed,13));assert(transactions==3);
    // Original music reads/writes retain their existing ESP_ERROR_CHECK policy.
    expect_abort=true;transfer_error=-5;
    if(!setjmp(sdk_abort)) {fpga_word_read_burst(0x7f0,observed,11);assert(!"original read must check SDK failure");}
    assert(checks_failed==1);
    if(!setjmp(sdk_abort)) {fpga_word_write(0x7f0,0);assert(!"original write must check SDK failure");}
    assert(checks_failed==2 && read_frames==4 && write_frames==1);
    puts("SPI observation: real driver SDK/A5 failures return false without publishing, abort or write; music check policy preserved PASS");
}
