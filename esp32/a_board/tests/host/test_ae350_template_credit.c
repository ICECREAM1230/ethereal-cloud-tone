#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ae350_control.h"
#include "driver/spi_master.h"
#include "spi_regs.h"
#include "midi_template_link.c"

extern int64_t fake_time;
static uint32_t regs[65536];
static bool auto_drain=true,credit_io=true,stop_after_fm,auto_dds_ack,kst_reject;
static uint16_t fail_write;
static unsigned frames,write_frames,credit_reads,raw_frames,words_written;
static struct {uint16_t address;uint32_t data;} log_words[8192];
int spi_bus_initialize(int h,const spi_bus_config_t*c,int dma){(void)h;(void)c;(void)dma;return 0;}
int spi_bus_add_device(int h,const spi_device_interface_config_t*c,spi_device_handle_t*d)
{(void)h;assert(c->mode==0&&c->clock_speed_hz==10000000);*d=(void*)1;return 0;}
int spi_device_polling_transmit(spi_device_handle_t h,spi_transaction_t*t)
{
    (void)h;const uint8_t *tx=t->tx_buffer;uint8_t *rx=t->rx_buffer;
    uint16_t a=(uint16_t)((uint16_t)tx[1]<<8|tx[2]);
    size_t bytes=t->length/8;frames++;fake_time+=(int64_t)((bytes*8+9)/10)+2;
    if(tx[0]==0xc0) {
        if(a==AE350_TEMPLATE_CREDIT_REG){credit_reads++;if(!credit_io)return -1;}
        assert(rx);memset(rx,0,bytes);rx[0]=0xa5;
        for(size_t i=0;i<(bytes-5)/4;i++)for(unsigned b=0;b<4;b++)
            rx[5+i*4+b]=(uint8_t)(regs[(uint16_t)(a+i)]>>(24-b*8));
        return 0;
    }
    assert(tx[0]==0x40&&bytes>=7);size_t n=(bytes-3)/4;
    assert(ae350_control_write_allowed(a,n));
    if(a==0x701)raw_frames++;
    else {
        assert(regs[AE350_TEMPLATE_CREDIT_REG]>=n+AE350_TEMPLATE_CREDIT_RESERVE);
        if(!auto_drain)regs[AE350_TEMPLATE_CREDIT_REG]-=(uint32_t)n;
    }
    write_frames++;
    for(size_t i=0;i<n;i++) {
        uint32_t w=((uint32_t)tx[3+i*4]<<24)|((uint32_t)tx[4+i*4]<<16)|((uint32_t)tx[5+i*4]<<8)|tx[6+i*4];
        assert(words_written<8192);log_words[words_written].address=(uint16_t)(a+i);log_words[words_written++].data=w;
        regs[(uint16_t)(a+i)]=w;
        if(a+i==0x80ff&&auto_dds_ack)regs[0x707]=w;
        if(a+i==KST_ADDR_COMMIT&&kst_reject) {
            regs[KST_ADDR_TEMPLATE_GENERATION]=0;
            regs[KST_ADDR_COMPLETED_TEMPLATE]=w;regs[KST_ADDR_RESULT]=2;
        }
    }
    if(a==0xb400&&stop_after_fm){regs[AE350_TEMPLATE_CREDIT_REG]=2;auto_drain=false;stop_after_fm=false;}
    if(a==fail_write)return -1;
    return 0;
}
int spi_device_transmit(spi_device_handle_t h,spi_transaction_t*t){return spi_device_polling_transmit(h,t);}
static void step(void){fake_time+=1000;midi_template_link_service(fake_time+700,true,true);}
static void until(phase_t wanted)
{for(unsigned i=0;i<1000&&phase!=wanted;i++)step();assert(phase==wanted);}
static void fresh(void)
{
    memset(regs,0,sizeof regs);auto_drain=true;credit_io=true;stop_after_fm=false;auto_dds_ack=false;kst_reject=false;fail_write=0;
    regs[0x700]=0x4d494431;regs[0x70a]=7;regs[0x718]=AE350_CPU_MAGIC;regs[0x719]=2;regs[AE350_TEMPLATE_CREDIT_REG]=64;
    assert(ae350_control_identity());midi_template_link_init();
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    fpga_spi_init(13,14,47,21,10000000);fresh();
    uint32_t words[12];for(unsigned i=0;i<12;i++)words[i]=0x12340000u+i;
    unsigned before=write_frames;
    regs[0x71c]=0;assert(fpga_template_write_burst(0x8000,words,12)==FPGA_TEMPLATE_DEFERRED);
    regs[0x71c]=13;assert(fpga_template_write_burst(0x8000,words,12)==FPGA_TEMPLATE_DEFERRED);
    assert(write_frames==before);
    auto_drain=false;regs[0x71c]=14;
    assert(fpga_template_write_burst(0x8000,words,12)==FPGA_TEMPLATE_SENT&&regs[0x71c]==2);
    assert(fpga_template_write_burst(0x80ff,words,1)==FPGA_TEMPLATE_DEFERRED);
    regs[0x71c]=3;assert(fpga_template_write_burst(0x80ff,words,1)==FPGA_TEMPLATE_SENT&&regs[0x71c]==2);
    regs[0x71c]=0;unsigned prior_credit_reads=credit_reads;before=raw_frames;
    fpga_word_write(0x701,0x01010090);assert(raw_frames==before+1&&credit_reads==prior_credit_reads);
    before=write_frames;fpga_word_write_burst(0x8100,words,12);assert(write_frames==before);
    regs[0x71c]=0x10040;assert(fpga_template_write_burst(0xb400,words,1)==FPGA_TEMPLATE_OVERFLOW);
    regs[0x71c]=65;assert(fpga_template_write_burst(0xb400,words,1)==FPGA_TEMPLATE_IO);
    regs[0x71c]=64;credit_io=false;assert(fpga_template_write_burst(0xb400,words,1)==FPGA_TEMPLATE_IO);credit_io=true;
    assert(write_frames==before);
    regs[0x719]=1;assert(!ae350_control_identity());
    assert(fpga_template_write_burst(0xb400,words,1)==FPGA_TEMPLATE_FORBIDDEN);
    fpga_word_write(0x701,0x01010090);assert(write_frames==before);
    puts("driver: credits 0/13 defer12, 14 sends12, reserve2 blocks COMMIT, 3 sends1; RAW ignores credit; overflow/IO/ABI1 fail closed");

    fresh();midi_template_image_t image={0};image.elements[0][0]=0x98abcdef;image.ks_notes[0][0]=0x13572468;image.fm_expression=0x407f;
    regs[0x71c]=0;midi_template_link_publish(&image);before=write_frames;
    step();assert(phase==UPLOAD&&region==0&&offset==0&&write_frames==before&&observed.busy&&!observed.fault);
    regs[0x71c]=13;for(unsigned i=0;i<5;i++)step();assert(offset==0&&write_frames==before);
    auto_drain=false;regs[0x71c]=14;step();assert(region==0&&offset==12&&regs[0x71c]==2);
    unsigned next_word=words_written;regs[0x71c]=0;
    for(unsigned i=0;i<5;i++){step();}
    assert(offset==12&&words_written==next_word);
    auto_drain=true;regs[0x71c]=64;stop_after_fm=true;
    until(COMMIT_DDS);assert(log_words[next_word].address==0x800c&&native_fence[0]==0&&native_fence[1]==0);
    assert(observed.fm_sent_revision&&!observed.applied_generation&&!observed.ready);
    before=write_frames;for(unsigned i=0;i<4;i++)step();assert(phase==COMMIT_DDS&&write_frames==before&&native_fence[0]==0);
    regs[0x71c]=3;step();assert(phase==COMMIT_KS&&native_fence[0]&&native_fence[1]==0&&regs[0x71c]==2);
    before=write_frames;step();assert(phase==COMMIT_KS&&write_frames==before);
    regs[0x71c]=3;step();assert(phase==WAIT_ACK&&native_fence[1]&&!observed.ready);
    regs[0x707]=native_fence[0];step();assert(phase==WAIT_ACK&&!observed.ready&&!observed.applied_generation);
    regs[0x708]=native_fence[1];step();assert(phase==IDLE&&observed.ready&&observed.applied_generation);
    puts("scheduler: zero/insufficient credit preserves active image/cursor and deferred COMMIT fences; resumed exact address; native dual ACK barrier, FM sent-only");

    image.elements[0][0]++;midi_template_link_publish(&image);regs[0x71c]=0x10040;
    before=write_frames;step();assert(phase==FAULT&&observed.fault==MIDI_TEMPLATE_LINK_OVERFLOW&&write_frames==before);
    fresh();midi_template_link_publish(&image);regs[0x71c]=0;
    step();credit_io=false;before=write_frames;step();assert(phase==FAULT&&observed.fault==MIDI_TEMPLATE_LINK_IO&&write_frames==before);
    // A failed write completion can have clocked a COMMIT already. Preserve
    // its unknown fence; no software retry may borrow a new image's identity.
    fresh();midi_template_link_publish(&image);stop_after_fm=true;until(COMMIT_DDS);
    regs[0x71c]=3;fail_write=0x80ff;step();assert(phase==FAULT&&native_fence[0]&&observed.fault==MIDI_TEMPLATE_LINK_IO);
    uint32_t unknown=native_fence[0];fail_write=0;regs[0x71c]=64;auto_drain=true;
    midi_template_link_publish(&image);before=write_frames;for(unsigned i=0;i<5;i++)step();assert(native_fence[0]==unknown&&write_frames==before);
    puts("faults: overflow/credit IO visible without cursor advance; uncertain COMMIT retains fence and blocks replay");

    fresh();midi_template_link_timbre_enable();image.ks_header[0]=KST_MAGIC;image.ks_header[KST_H_MODEL_GENERATION]=17;
    auto_dds_ack=true;kst_reject=true;midi_template_link_publish(&image);
    until(WAIT_ACK);until(FAULT);
    assert(phase==FAULT&&observed.fault==MIDI_TEMPLATE_LINK_REJECTED&&observed.timbre_result==2);
    assert(!observed.timbre_available&&!observed.timbre_valid&&observed.timbre_completed_template==1);
    assert(!regs[0x708]&&!observed.timbre_template&&!native_fence[1]&&!observed.ks_applied_model_generation);
    image.ks_notes[0][0]++;midi_template_link_publish(&image);fake_time+=21000;
    until(WAIT_ACK);assert(native_generation[1]==2);until(FAULT);assert(observed.timbre_completed_template==2&&!observed.timbre_template);
    printf("PASS ae350_template_credit: real SPI driver + template scheduler; %u frames/%u writes/%u words; credit backpressure, native ACK, KST unsupported2 terminal without successful generation\n",frames,write_frames,words_written);
}
