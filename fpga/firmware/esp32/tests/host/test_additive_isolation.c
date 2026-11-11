/* Drive the actual additive mailbox through its public service boundary. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "additive_link.h"
#include "raw_midi.h"
#include "spi_regs.h"
static int64_t now;
static bool backend_ok=true,common_ok=true,auto_ack=true,busy;
static uint32_t sequence,commit;
static unsigned writes,commits,reads;
int64_t esp_timer_get_time(void) {return now;}
uint32_t esp_random(void) {static uint32_t value=100;return ++value;}
bool fpga_word_read_burst(uint16_t address,uint32_t *words,size_t n)
{
    assert(n==1);now+=5;reads++;
    if(address==MIDI_RAW_REG_MAGIC) {if(!common_ok)return false;*words=MIDI_RAW_ABI_MAGIC;return true;}
    assert(address==0x0709);if(!common_ok || !backend_ok)return false;
    *words=sequence|(busy?0x80000000u:0);return true;
}
void fpga_word_write_burst(uint16_t address,const uint32_t *words,size_t n)
{
    assert(n<=12);now+=5;writes++;
    if(address==0xa0ff) {assert(n==1);commit=words[0];commits++;busy=!auto_ack;if(auto_ack)sequence=commit;}
}
void fpga_word_write(uint16_t address,uint32_t word) {fpga_word_write_burst(address,&word,1);}
static additive_link_status_t state(void) {additive_link_status_t s;additive_link_status(&s);return s;}
static void tick(bool disabled) {now+=1000;additive_link_service(now+150,disabled);}
static void settled(void)
{for(unsigned n=0;n<100 && (!state().valid || state().busy);n++)tick(true);assert(state().valid && !state().busy);}
int main(int argc,char **argv)
{
    assert(argc==2);additive_link_init();settled();assert(!state().selected);
    const char *name=argv[1];unsigned before=writes;
    if(!strcmp(name,"idle-io")) {
        backend_ok=false;now+=21000;tick(false);
        assert(!state().available && !state().valid && !state().busy && state().fault==ADDITIVE_LINK_IO);
        unsigned polled=reads;for(unsigned n=0;n<200;n++)tick(false);
        assert(writes==before && reads-polled<=24);
        backend_ok=true;now+=21000;tick(false);
        assert(state().available && state().valid && !state().selected && !state().busy && !state().completed_request_id);
        assert(writes==before);
    } else if(!strcmp(name,"idle-reset") || !strcmp(name,"idle-reset-no-ack") || !strcmp(name,"idle-reset-cancel")) {
        sequence=0;now+=21000;tick(false);
        assert(!state().valid && !state().busy);
        for(unsigned n=0;n<50;n++)tick(false);
        assert(writes==before);
        if(strcmp(name,"idle-reset")) {
            auto_ack=false;while(commits<2)tick(true);
            now+=101000;tick(false);
            assert(!state().available && !state().valid && !state().busy && !state().completed_request_id);
            before=writes;for(unsigned n=0;n<200;n++)tick(false);assert(writes==before);
            bool cancel=!strcmp(name,"idle-reset-cancel");
            if(cancel) {additive_link_cancel();assert(state().busy);}
            busy=false;sequence=commit;now+=21000;tick(false);
            assert(!state().completed_request_id);
            if(cancel) {
                assert(!state().valid && state().busy);auto_ack=true;settled();
            } else assert(state().available && state().valid && !state().busy && !state().selected);
        } else settled();
        assert(state().valid && !state().selected && !state().completed_request_id);
    } else if(!strcmp(name,"idle-unknown-busy") || !strcmp(name,"idle-unknown-sequence") ||
              !strcmp(name,"quarantine-unknown-busy")) {
        if(!strcmp(name,"quarantine-unknown-busy")) {
            backend_ok=false;now+=21000;tick(false);assert(!state().busy);
            backend_ok=true;
        }
        sequence+=19;busy=strcmp(name,"idle-unknown-sequence")!=0;now+=21000;tick(false);
        assert(!state().valid && state().busy && !state().completed_request_id && writes==before);
    } else if(!strcmp(name,"shared")) {
        common_ok=false;now+=21000;tick(false);assert(!state().valid && state().busy);
        assert(writes==before);
    } else if(!strcmp(name,"selected") || !strcmp(name,"flight")) {
        additive_patch_t patch;additive_patch_default(&patch);
        assert(additive_link_request(&patch,true,91));
        if(!strcmp(name,"flight"))auto_ack=false;
        while(commits<2)tick(true);
        if(auto_ack)settled();
        backend_ok=false;now+=21000;before=writes;tick(false);
        assert(!state().valid && state().busy && !state().completed_request_id && writes==before);
        if(!auto_ack) {
            additive_link_cancel();backend_ok=true;busy=false;sequence=commit;now+=21000;tick(false);
            assert(!state().completed_request_id && !state().valid);
            auto_ack=true;settled();assert(!state().selected && !state().completed_request_id);
        }
    } else assert(!"unknown test case");
    printf("additive isolation: %s PASS\n",name);
}
