#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ae350_control.h"
#include "driver/spi_master.h"
#include "spi_regs.h"
static uint32_t regs[65536];
static unsigned frames,write_frames;static bool io_ok=true;
int spi_bus_initialize(int h,const spi_bus_config_t *c,int d){(void)h;(void)c;(void)d;return 0;}
int spi_bus_add_device(int h,const spi_device_interface_config_t*c,spi_device_handle_t*d)
{(void)h;assert(c->mode==0 && c->clock_speed_hz==10000000);*d=(void*)1;return 0;}
int spi_device_polling_transmit(spi_device_handle_t h,spi_transaction_t*t)
{
    (void)h;if(!io_ok)return -1;frames++;
    const uint8_t*tx=t->tx_buffer;uint8_t*rx=t->rx_buffer;
    unsigned addr=(unsigned)tx[1]*256+tx[2];
    if(tx[0]&0x80) {
        assert(tx[0]==0xc0 && rx);memset(rx,0,t->length/8);rx[0]=0xa5;
        for(size_t i=0;i<(t->length/8-5)/4;i++)
            for(unsigned b=0;b<4;b++)rx[5+i*4+b]=(uint8_t)(regs[addr+i]>>(24-b*8));
    } else {
        write_frames++;assert(tx[0]==0x40 && t->length/8>=7);
        size_t n=(t->length/8-3)/4;assert(ae350_control_write_allowed((uint16_t)addr,n));
        for(size_t i=0;i<n;i++)regs[addr+i]=((uint32_t)tx[3+i*4]<<24)|((uint32_t)tx[4+i*4]<<16)|((uint32_t)tx[5+i*4]<<8)|tx[6+i*4];
    }
    return 0;
}
int spi_device_transmit(spi_device_handle_t h,spi_transaction_t*t){return spi_device_polling_transmit(h,t);}
int main(void)
{
    fpga_spi_init(13,14,47,21,10000000);
    uint32_t v[12]={0};unsigned base=frames;
    fpga_word_write(0x701,0x01010090);assert(frames==base && fpga_cpu_rejected_writes()==1);
    regs[0x700]=0x4d494431;regs[0x70a]=7;regs[0x718]=AE350_CPU_MAGIC;regs[0x719]=2;regs[0x71c]=64;
    assert(ae350_control_identity());fpga_word_write(0x701,0x01010090);
    assert(regs[0x701]==0x01010090 && write_frames==1);
    fpga_word_write_burst(0x8100,v,12);assert(write_frames==2);
    base=frames;
    fpga_word_write(0x2004,1);fpga_word_write(0x610,1);fpga_word_write(0x710,1);
    fpga_word_write_burst(0x803f,v,4);fpga_word_write_burst(0xb3ff,v,3);
    fpga_reg_write(1,1);uint8_t b=1;fpga_reg_write_burst(1,&b,1);
    assert(frames==base && fpga_cpu_rejected_writes()==8);
    assert(!fpga_live_write(80,1) && !fpga_swap_checked() && frames==base);
    regs[0x707]=123;uint32_t ack=0;assert(fpga_word_read_burst(0x707,&ack,1) && ack==123);
    regs[0x719]=1;assert(!ae350_control_identity());base=frames;fpga_word_write(0xb400,1);assert(frames==base);
    regs[0x719]=2;io_ok=false;assert(!ae350_control_identity());
    assert(!ae350_control_write_allowed(0x701,2) && !ae350_control_write_allowed(0xffff,2));
    io_ok=true;assert(ae350_control_identity());
    uint32_t mode=0x10000102;
    regs[0x71c]=0;base=write_frames;
    assert(fpga_template_write_burst(0xb401,&mode,1)==FPGA_TEMPLATE_DEFERRED && write_frames==base);
    regs[0x71c]=3;assert(fpga_template_write_burst(0xb401,&mode,1)==FPGA_TEMPLATE_SENT && regs[0xb401]==mode);
    assert(ae350_control_write_allowed(0xb401,1) && !ae350_control_write_allowed(0xb401,2) && !ae350_control_write_allowed(0xb400,2));
    regs[0x71d]=0x100001aau;assert(fpga_word_read_burst_observe(0x71d,&ack,1) && ack==regs[0x71d]);
    puts("PASS ae350_spi: actual driver Mode0 framing/ID/read ACK, CPU1 authorization, mode v1 B401 credit/scalar boundary and 071D read, no byte/voice/owner fallback");
}
