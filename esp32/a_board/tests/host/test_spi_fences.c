#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "driver/spi_master.h"
#include "spi_regs.h"
extern int64_t fake_time;
static bool link_ok=true,reject=false,stuck=false;
static unsigned bank,seq,busy_reads,swap_reads;
static bool wide_mode;
static unsigned wide_written;
static int wire_read(spi_device_handle_t d,spi_transaction_t *t);
int spi_bus_initialize(int h,const spi_bus_config_t*c,int d){(void)h;(void)c;(void)d;return 0;}
int spi_bus_add_device(int h,const spi_device_interface_config_t*c,spi_device_handle_t*d){(void)h;(void)c;*d=(void*)1;return 0;}
int spi_device_polling_transmit(spi_device_handle_t d,spi_transaction_t*t){
    if(t->rx_buffer)return wire_read(d,t);
    (void)d;const uint8_t*tx=t->tx_buffer;fake_time+=10;
    unsigned addr=((unsigned)tx[1]<<8)|tx[2];
    if(wide_mode && addr==0x610) {
        wide_written=(t->length/8-3)/4;
        assert(t->length/8==51 && "FM shadow must carry all 12 words");
        for(unsigned j=0;j<12;j++) {
            uint32_t word=((uint32_t)tx[3+4*j]<<24)|((uint32_t)tx[4+4*j]<<16)|((uint32_t)tx[5+4*j]<<8)|tx[6+4*j];
            assert(word==0x10203040u+j);
        }
    }
    if(addr>=0x800 && addr<0xC00){if(reject)seq++;else busy_reads=2;}
    if(addr==2)swap_reads=2;
    return 0;
}
static int wire_read(spi_device_handle_t d,spi_transaction_t*t){
    (void)d;uint8_t*rx=t->rx_buffer;memset(rx,0,t->length/8);rx[0]=link_ok?0xA5:0;
    const uint8_t *tx=t->tx_buffer;unsigned addr=((unsigned)tx[1]<<8)|tx[2];
    if(wide_mode && addr==0x606) {
        assert(t->length/8==45 && "FM readback must carry flags and session");
        rx[3]=0xee;rx[4]=0xbb;
        for(unsigned j=0;j<10;j++)for(unsigned k=0;k<4;k++)rx[5+4*j+k]=(uint8_t)((0x11223300u+j)>>(24-8*k));
        return 0;
    }
    fake_time+=10;unsigned busy=0,swapping=0;
    if(busy_reads){busy=4;if(!stuck)busy_reads--;}
    if(swap_reads){swapping=2;if(!stuck && --swap_reads==0){swapping=0;bank^=1;}}
    uint32_t status=(100u<<16)|(seq<<8)|busy|swapping|bank;
    for(unsigned i=0;i<4;i++)rx[5+i]=(uint8_t)(status>>(24-8*i));
    return 0;
}
int spi_device_transmit(spi_device_handle_t d,spi_transaction_t*t){return wire_read(d,t);}
int main(void){
    fpga_spi_init(45,48,47,21,10000000);
    assert(fpga_live_write(80,0));
    reject=true;assert(!fpga_live_write(80,1));reject=false;
    stuck=true;assert(!fpga_live_write(80,0));stuck=false;busy_reads=0;
    link_ok=false;uint32_t status;assert(!fpga_control_status(&status));
    assert(!fpga_live_write(80,0));link_ok=true;
    assert(fpga_swap_checked() && bank==1);
    stuck=true;assert(!fpga_swap_checked());
    wide_mode=true;uint32_t body[12],core[10];
    for(unsigned j=0;j<12;j++)body[j]=0x10203040u+j;
    fpga_word_write_burst(0x610,body,12);assert(wide_written==12);
    assert(fpga_word_read_burst(0x606,core,10));
    for(unsigned j=0;j<10;j++)assert(core[j]==0x11223300u+j);
    puts("SPI: mailbox ack, rejection sequence, bounded timeout, ID fault, swap bank acknowledgement PASS");
    return 0;
}
