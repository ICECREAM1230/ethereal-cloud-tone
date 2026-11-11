#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ae350_control.h"
#include "raw_midi.h"
#include "midi_template_link.h"
#include "spi_regs.h"
extern int64_t fake_time;
static uint32_t regs[65536];
static bool io_ok=true,authorized,auto_ack=true,hold_flush;
static bool audio_uncertain,audio_drop;static unsigned audio_writes;
static uint32_t raw_pending;
static unsigned writes,raw_bytes,bulk_frames,template_commits,max_burst,first_write,commands;
static uint32_t pending_dds,pending_ks;
void fpga_cpu_link_authorize(bool good){authorized=good;}
uint32_t fpga_cpu_rejected_writes(void){return 0;}
bool fpga_word_read_burst(uint16_t a,uint32_t *out,size_t n)
{
    fake_time+=5;assert(n && n<=12);
    if(!io_ok)return false;
    for(size_t i=0;i<n;i++)out[i]=regs[a+i];
    return true;
}
bool fpga_word_read_burst_observe(uint16_t a,uint32_t *out,size_t n)
{return fpga_word_read_burst(a,out,n);}
bool fpga_control_status(uint32_t *s){(void)s;assert(!"CPU path must not poll legacy CSP");return false;}
bool fpga_live_write(uint16_t b,uint32_t v){(void)b;(void)v;assert(!"CPU legacy BUS write");return false;}
bool fpga_swap_checked(void){assert(!"CPU legacy bank swap");return false;}
static void accept_raw(uint32_t v)
{
    unsigned op=v>>24;
    if(op==3)regs[0x702]=(v&1)?2:0;
    if(op==1){raw_bytes++;regs[0x705]++;
#ifdef AE350_RAW_TEST_OBSERVE
        AE350_RAW_TEST_OBSERVE(v);
#endif
    }
    regs[0x703]++;
}
void fpga_word_write(uint16_t a,uint32_t value)
{
    assert(authorized && ae350_control_write_allowed(a,1));
    if(!first_write)first_write=a;
    fake_time+=5;writes++;regs[a]=value;
    if(a==0x07b5) {
        audio_writes++;
        if(!audio_drop){regs[0x07b3]=value&15u;regs[0x07b4]=value>>4;regs[0x07b7]++;}
    }
    if(a==0x701) {
        commands++;
        if(hold_flush && value>>24==2){raw_pending=value;regs[0x702]|=1;return;}
        accept_raw(value);return;
    }
    if(a==0x80ff){pending_dds=value;template_commits++;if(auto_ack)regs[0x707]=value;}
    if(a==0xb3ff){pending_ks=value;template_commits++;if(auto_ack)regs[0x708]=value;}
}
void fpga_word_write_burst(uint16_t a,const uint32_t *w,size_t n)
{
    assert(n && n<=12 && ae350_control_write_allowed(a,n));bulk_frames++;
    if(n>max_burst)max_burst=(unsigned)n;
    for(size_t i=0;i<n;i++)fpga_word_write((uint16_t)(a+i),w[i]);
}
fpga_template_write_result_t fpga_template_write_burst(uint16_t a,const uint32_t *w,size_t n)
{
    uint32_t credit;
    assert(authorized&&ae350_control_write_allowed(a,n));
    if(a==0x07b5){fpga_word_write_burst(a,w,n);return audio_uncertain?FPGA_TEMPLATE_UNCERTAIN:FPGA_TEMPLATE_SENT;}
    if(!fpga_word_read_burst_observe(AE350_TEMPLATE_CREDIT_REG,&credit,1))return FPGA_TEMPLATE_IO;
    if(credit&0x10000u)return FPGA_TEMPLATE_OVERFLOW;
    if(credit<n+AE350_TEMPLATE_CREDIT_RESERVE)return FPGA_TEMPLATE_DEFERRED;
    fpga_word_write_burst(a,w,n);return FPGA_TEMPLATE_SENT;
}
#include "engine_link.c"
static void ticks(unsigned n){while(n--){fake_time+=1000;first_write=0;engine_service();}}
static raw_midi_state_t raw_state(void){raw_midi_state_t s;raw_midi_state(&s);return s;}
static midi_template_link_state_t image_state(void){midi_template_link_state_t s;midi_template_link_state(&s);return s;}
static void complete_flush(void)
{
    assert(raw_pending);regs[0x702]&=~1u;accept_raw(raw_pending);raw_pending=0;ticks(1);
}
int main(void)
{
    regs[0x700]=MIDI_RAW_ABI_MAGIC;regs[0x70a]=7;regs[0x718]=AE350_CPU_MAGIC;regs[0x719]=2;regs[0x71c]=64;
    raw_midi_init();engine_link_init();assert(s_ready && !writes);
    engine_cmd_t old={.elem=1,.word=1,.value=42};
    assert(!engine_link_send(&old) && !engine_link_prod_write(1,1,42) && !engine_link_bus_write(80,1));
    engine_note_t note={.voice=0};assert(!engine_link_note_on(&note));
    // Missing CPU1, unknown ABI, missing capability and IO all fail closed.
    regs[0x718]=0;ticks(2);assert(!writes && !authorized);
    regs[0x718]=AE350_CPU_MAGIC;regs[0x719]=1;ticks(2);assert(!writes && !authorized);
    regs[0x719]=2;regs[0x70a]=3;ticks(2);assert(!writes && !authorized);
    regs[0x70a]=7;io_ok=false;ticks(2);assert(!writes && !authorized);
    io_ok=true;ticks(8);assert(raw_state().available && raw_state().initialized && !raw_state().enabled);
    midi_template_image_t image={0};image.elements[0][0]=123;image.ks_notes[0][0]=456;image.fm_expression=0x407f;
    midi_template_link_publish(&image);engine_link_performance_ready(true);ticks(40);
    assert(image_state().ready && raw_state().ready && raw_state().cpu_control);
    assert(regs[0x707] && regs[0x708] && template_commits==2 && max_burst==12);
    printf("CPU1 init/service: no legacy writes; capability/ABI/IO fail closed; real native template ACKs; writes=%u\n",writes);
    // A hot upload must coexist with RAW; uploaded/COMMIT is not activation.
    uint32_t old_generation=image_state().applied_generation;
    auto_ack=false;image.elements[0][0]++;image.ks_notes[0][0]++;midi_template_link_publish(&image);
    uint8_t bytes[256];memset(bytes,0xf8,sizeof bytes);
    assert(raw_midi_claim(1,123));assert(raw_midi_submit(1,123,bytes,sizeof bytes,fake_time+500000));
    unsigned before_raw=raw_bytes,before_bulk=bulk_frames;
    regs[0x71c]=0;
    for(unsigned i=0;i<8;i++) {
        bool queued=raw_state().source[1].queued_bytes!=0;ticks(1);
        if(queued && first_write)assert(first_write==0x701);
    }
    assert(raw_bytes>before_raw&&bulk_frames==before_bulk&&raw_state().enabled&&!image_state().fault);
    regs[0x71c]=13;ticks(3);assert(bulk_frames==before_bulk); // 12-word burst plus two spare credits.
    regs[0x71c]=64;
    for(unsigned i=0;i<40;i++) {
        bool queued=raw_state().source[1].queued_bytes!=0;
        ticks(1);
        if(queued && first_write)assert(first_write==0x701);
    }
    assert(raw_bytes>before_raw && bulk_frames>before_bulk && raw_state().enabled);
    assert(!image_state().ready && image_state().applied_generation==old_generation);
    regs[0x707]=pending_dds;ticks(2);assert(!image_state().ready);
    regs[0x708]=pending_ks;ticks(4);assert(image_state().ready && image_state().applied_generation!=old_generation);
    printf("RAW priority and bulk: 200us first slice, <=12 words/burst, RAW bytes=%u, bulk frames=%u; two-domain ACK barrier\n",raw_bytes,bulk_frames);
    // Per-source flush and explicit STOP remain pending until real ACK.
    hold_flush=true;raw_midi_flush(1);ticks(1);assert(raw_pending && raw_state().source[1].flushing);
    uint32_t accepted=raw_state().accepted;ticks(5);assert(raw_state().accepted==accepted && raw_state().source[1].flushing);
    complete_flush();assert(!raw_state().source[1].flushing);
    uint32_t ticket=raw_midi_request_stop_all();ticks(1);assert(raw_state().stop_pending && raw_state().stop_completed!=ticket);
    complete_flush();assert(raw_state().stop_pending && raw_pending==0x02000000u);
    complete_flush();assert(!raw_state().stop_pending && raw_state().stop_completed==ticket);
    // Hardware global recovery revokes RAW even with an idle mailbox. The
    // real S3 status/reset path must discard its not-yet-sent packet too.
    assert(raw_midi_claim(1,456));uint8_t old_note[]={0x90,60,100};
    assert(raw_midi_submit(1,456,old_note,sizeof old_note,fake_time+500000));
    unsigned old_bytes=raw_bytes;uint32_t resets=raw_state().reset_count;
    regs[0x702]&=~2u;ticks(1);
    assert(raw_bytes==old_bytes && raw_state().reset_count>resets);
    assert(!raw_state().source[1].queued_bytes && raw_state().source[1].dropped_bytes>=3);
    io_ok=false;fake_time+=25000;unsigned before=writes;ticks(3);assert(!authorized && writes==before);
    puts("PASS ae350_engine: real engine/raw/template modules, startup/service owner fence, RAW/bulk fairness, delayed flush/STOP and link-loss no fallback");
    return 0;
}
