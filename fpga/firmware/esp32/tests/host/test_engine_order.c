#include <assert.h>
#include <stdio.h>
#include "engine_link.h"
#include "spi_regs.h"
extern int64_t fake_time;
static bool startup=true, gate_on=false;
static unsigned active_bank, completed_pass, last_low_pass, on_count, wire_us=7;
static bool status_ok=true, live_ok=true, walk=true, swap_ack=true, swap_pending;
static uint32_t depth[2], expected_depth=1234, element[2], gate_on_mask;
static uint32_t amp_rates[2][2],mod_rates[2][2];
static uint32_t expected_amp_rates[2]={0x00112233,0x00334455};
static uint32_t expected_mod_rates[2]={0x00556677,0x00778899};
bool fpga_word_read_burst(uint16_t a,uint32_t*w,size_t n){(void)a;for(size_t i=0;i<n;i++)w[i]=0;return true;}
void fpga_word_write_burst(uint16_t a,const uint32_t*w,size_t n) {for(size_t i=0;i<n;i++)fpga_word_write(a+i,w[i]);}
void fpga_word_write(uint16_t a,uint32_t v) {
    fake_time += wire_us;
    if(a==2)swap_pending=true;
    if(a==0x182)depth[active_bank^1]=v;
    if(a==0x181)amp_rates[active_bank^1][0]=v;
    if(a==0x183)amp_rates[active_bank^1][1]=v;
    if(a==0x201)mod_rates[active_bank^1][0]=v;
    if(a==0x203)mod_rates[active_bank^1][1]=v;
    if(a==0x2000)element[active_bank^1]=v;
    if(a>=0x0850 && a<0x0870 && v==1)gate_on_mask |= 1u<<(a-0x0850);
    if(a==0x0850 && v==0)last_low_pass=completed_pass;
    if(a==0x0850 && v==1) {
        assert(depth[active_bank]==expected_depth && "GateOn must follow committed DEPTH");
#ifdef TRANSACTION_ENGINE
        for(unsigned i=0;i<2;i++) {
            assert(amp_rates[active_bank][i]==expected_amp_rates[i] && "GateOn must follow committed AMP rates");
            assert(mod_rates[active_bank][i]==expected_mod_rates[i] && "GateOn must follow committed MOD rates");
        }
        assert(completed_pass-last_low_pass>=2 && "steal needs a complete low-gate pass");
#endif
        gate_on=true;on_count++;
    }
}
void fpga_swap(void) { active_bank^=1; }
#ifdef TRANSACTION_ENGINE
bool fpga_swap_checked(void) {fpga_swap();return true;}
bool fpga_control_status(uint32_t*s) {
    if(walk)completed_pass++;
    if(swap_ack && swap_pending){active_bank^=1;swap_pending=false;}
    *s=(completed_pass<<16)|active_bank|(swap_pending?2:0);
    fake_time+=8;return status_ok;
}
bool fpga_live_write(uint16_t b,uint32_t v) {if(!live_ok)return false;fpga_word_write(0x800+b,v);return true;}
#endif
#ifndef ENGINE_SOURCE
#define ENGINE_SOURCE "engine_link.c"
#endif
#include ENGINE_SOURCE
int main(void) {
    engine_link_init();startup=false;(void)startup;
#ifdef TRANSACTION_ENGINE
    engine_note_t tx={.voice=0,.amp_entry=32,.mod_entry=64,.amp_depth=1234,.mod_depth=42,
                      .count=1,.words={{.elem=0,.word=0,.value=55}}};
    for(unsigned i=0;i<2;i++) {
        assert(engine_link_prod_write(32,1+2*i,expected_amp_rates[i]));
        assert(engine_link_prod_write(64,1+2*i,expected_mod_rates[i]));
    }
    assert(engine_link_note_on(&tx));
    for(unsigned i=0;i<200 && !gate_on;i++)engine_service();
    assert(gate_on);for(unsigned i=0;i<20;i++)engine_service();
    assert(depth[0]==1234 && depth[1]==1234 && element[0]==55 && element[1]==55);
    // Replacement of an already held voice cannot collapse low/high into a
    // single CSP pass. With a stalled walker the On remains pending.
    gate_on=false; walk=false; tx.amp_depth=2222;expected_depth=2222;
    for(unsigned i=0;i<2;i++) {
        expected_amp_rates[i]+=16;expected_mod_rates[i]+=16;
        assert(engine_link_prod_write(32,1+2*i,expected_amp_rates[i]));
        assert(engine_link_prod_write(64,1+2*i,expected_mod_rates[i]));
    }
    assert(engine_link_note_on(&tx));
    for(unsigned i=0;i<20;i++)engine_service();assert(!gate_on);
    // The pass already in flight may have read gate before the Off. Its
    // late completion (+1) is insufficient; a full later pass (+2) is needed.
    completed_pass++;
    for(unsigned i=0;i<20;i++)engine_service();assert(!gate_on);
    completed_pass++;
    walk=true;swap_ack=false;
    for(unsigned i=0;i<20;i++)engine_service();assert(!gate_on);
    swap_ack=true;
    for(unsigned i=0;i<200 && !gate_on;i++)engine_service();assert(gate_on);
    for(unsigned i=0;i<20;i++)engine_service();
    assert(depth[0]==2222 && depth[1]==2222);
    // A transport failure is retried, and an Off before recovery cancels On.
    gate_on=false;live_ok=false;tx.amp_depth=3333;expected_depth=3333;
    assert(engine_link_note_on(&tx));
    for(unsigned i=0;i<20;i++)engine_service();assert(!gate_on);
    assert(engine_link_bus_write(80,0));live_ok=true;
    for(unsigned i=0;i<100;i++)engine_service();assert(!gate_on);
    // Address coalescing survives a write burst far beyond the old 1024 queue.
    for(unsigned i=0;i<5000;i++) {
        engine_cmd_t c={.elem=0,.word=0,.value=i};assert(engine_link_send(&c));
    }
    for(unsigned i=0;i<100;i++)engine_service();
    assert(element[0]==4999 && element[1]==4999 && engine_link_drops()==0);
    expected_depth=4444;tx.amp_depth=4444;assert(engine_link_note_on(&tx));
    for(unsigned i=0;i<200 && !gate_on;i++)engine_service();assert(gate_on);
    assert(on_count==3);
    // Adversarial traffic replaces voice 0 every service while voice 31 and
    // the rest wait. Fair preparation/commit slices must still let them play.
    gate_on_mask=0;
    for(unsigned v=1;v<32;v++) {
        tx.voice=v;tx.amp_entry=32+v;tx.mod_entry=64+2*v;assert(engine_link_note_on(&tx));
    }
    wire_us=35;
    for(unsigned i=0;i<1000 && !(gate_on_mask&(1u<<31));i++) {
        for(unsigned v=0;v<8;v++) {
            tx.voice=v;tx.amp_entry=32+v;tx.mod_entry=64+2*v;
            assert(engine_link_note_on(&tx));
        }
        assert(engine_link_bus_write(1,i));assert(engine_link_bus_write(1023,i));
        engine_service();
    }
    assert((gate_on_mask&(1u<<31)) && "replacing voices 0..7 must not starve voice 31");
    puts("engine: ADSR rates/DEPTH before GateOn, bank commit ordering, low-gate steal, stalled pass/swap, retry/cancel, 5000 writes, double-bank mirror, voice31 fairness PASS");
#else
    engine_link_prod_write(32,2,1234);engine_link_bus_write(80,1);
    bus_cmd_t bc;
    while(xQueueReceive(s_bus_queue,&bc,0)==pdTRUE)fpga_word_write(0x800+bc.bus,bc.value);
#endif
    assert(gate_on);return 0;
}
