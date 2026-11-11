#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/spi_master.h"
#include "engine_link.h"
#include "fm_engine.h"

extern int64_t fake_time;
static uint32_t regs[0x6100];
static uint32_t gates[2][ENGINE_NUM_ELEMENTS];
static unsigned bank, pass, interrupt_delay;
static unsigned setup_us=5;

int spi_bus_initialize(int h,const spi_bus_config_t *c,int d)
{(void)h;(void)c;(void)d;return 0;}
int spi_bus_add_device(int h,const spi_device_interface_config_t *c,spi_device_handle_t *d)
{(void)h;(void)c;*d=(void *)1;return 0;}
static int transfer(spi_transaction_t *t)
{
    const uint8_t *tx=t->tx_buffer;
    uint8_t *rx=t->rx_buffer;
    unsigned bytes=t->length/8,addr=((unsigned)tx[1]<<8)|tx[2];
    fake_time+=setup_us+(bytes*8+9)/10; // Controlled overhead plus 10 MHz wire time.
    if(tx[0]&0x80) {
        assert(rx && bytes>=9);memset(rx,0,bytes);rx[0]=0xa5;
        for(unsigned i=0;i<(bytes-5)/4;i++) {
            uint32_t value=addr+i==3 ? (++pass<<16)|bank : regs[addr+i];
            for(unsigned k=0;k<4;k++)rx[5+4*i+k]=(uint8_t)(value>>(24-8*k));
        }
    } else {
        for(unsigned i=0;i<(bytes-3)/4;i++) {
            uint32_t value=((uint32_t)tx[3+4*i]<<24)|((uint32_t)tx[4+4*i]<<16)|
                           ((uint32_t)tx[5+4*i]<<8)|tx[6+4*i];
            unsigned a=addr+i;assert(a<0x6100);regs[a]=value;
            if(a>=0x2000 && (a-0x2000)%64==4)gates[bank^1][(a-0x2000)/64]=value;
            if(a==2 && value)bank^=1;
            if(a==0x647) {regs[0x648]=0;regs[0x649]=4;}
            if(a==0x61c) {
                fm_command_t command;
                assert(fm_deserialize_command(&regs[0x610],12,&command));
                regs[0x608]=regs[0x609]=regs[0x60a]=value;regs[0x60b]=FM_RESULT_OK;
                regs[0x620]=regs[0x622]=regs[0x624]=command.session_id;
                regs[0x621]=regs[0x623]=regs[0x625]=command.engine_epoch;
                if(command.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND) {
                    for(unsigned e=0;e<ENGINE_NUM_ELEMENTS;e++)assert(!gates[bank][e]);
                    regs[0x60f]=command.arg0_u32;regs[0x606]++;
                    regs[0x60e]=FM_FLAG_READY|(command.value_u32?FM_FLAG_SELECTED:FM_FLAG_MUTED);
                }
                regs[0x60d]++;
            }
            if(a==0x516) {
                regs[0x503]=value;
                if((regs[0x510]&7)==KS_MODE) {
                    for(unsigned e=0;e<ENGINE_NUM_ELEMENTS;e++)assert(!gates[bank][e]);
                    regs[0x502]=(regs[0x510]>>16)&1;regs[0x505]=value;
                }
            }
        }
    }
    return 0;
}
int spi_device_polling_transmit(spi_device_handle_t d,spi_transaction_t *t)
{(void)d;return transfer(t);}
int spi_device_transmit(spi_device_handle_t d,spi_transaction_t *t)
{
    (void)d;
    // Scheduling contention may delay each queued read independently. This
    // is a controlled failure model, not a measured board latency.
    fake_time+=interrupt_delay;return transfer(t);
}
#include "engine_link.c"
static void ticks(unsigned n)
{
    while(n--) {
        regs[0x506]++;fake_time+=1000;engine_service();
        engine_link_owner_panic_ack();
    }
}
int main(int argc,char **argv)
{
    bool with_fm=argc>1, select_fm=with_fm && !strcmp(argv[1],"fm-select");
    regs[0x500]=ENGINE_KS_CAPABILITY;regs[0x501]=ENGINE_KS_VERSION;
    if(with_fm) {
        regs[0x600]=FM_CAP_MAGIC;regs[0x601]=0x10001;regs[0x602]=1;
        regs[0x603]=8;regs[0x604]=4;regs[0x605]=0x3ff;
        regs[0x606]=1;regs[0x607]=2;regs[0x60c]=1;regs[0x60d]=1;regs[0x60e]=5;
        regs[0x640]=4921875;regs[0x641]=88;regs[0x642]=91015625;regs[0x643]=948;
        regs[0x644]=0x10000;regs[0x645]=13;regs[0x646]=3;
        memcpy(&regs[0x660],fm_profiles_sha256_words,32);
    }
    fpga_spi_init(13,14,47,21,10000000);engine_link_init();ticks(20);
    assert(!s_ks.switching);
    assert(s_fm.status.available==with_fm);
    engine_cmd_t gate={.elem=0,.word=4,.value=1};
    assert(engine_link_send(&gate));ticks(30);assert(gates[bank][0]==1);
    gate.value=0;assert(engine_link_send(&gate));
    if(with_fm) {
        setup_us=argc>2?(unsigned)atoi(argv[2]):60;
        assert(engine_link_bus_write(1023,0));
    } else interrupt_delay=350;
    if(select_fm) assert(engine_link_fm_mode(FM_BACKEND_JT51_OPM));
    else assert(engine_link_ks_mode(true));
    ticks(500);
    engine_ks_state_t state;engine_link_ks_state(&state);
    if(select_fm) {
        engine_fm_status_t fm;engine_link_fm_status(&fm);
        assert(fm.applied_backend==FM_BACKEND_JT51_OPM && !fm.switching && !fm.recovering &&
               "FM SELECT waiting for DDS silence must not starve that silence");
    } else assert(state.applied_mode && !state.switching && !state.recovering &&
                  "status reads must not starve the bank mute needed by MODE");
    assert(!gates[0][0] && !gates[1][0]);
    printf("Real SPI driver + DDS/KS/FM: %s after bank mute, modeled setup=%u us PASS\n",
           select_fm?"FM SELECT":"KS MODE",setup_us);
}
