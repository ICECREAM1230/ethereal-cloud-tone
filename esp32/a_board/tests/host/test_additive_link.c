#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "additive_link.h"
#include "spi_regs.h"

static int64_t now;
static bool io_ok, old_window, wire_busy, cancel_on_commit;
static uint32_t wire_sequence,last_commit;
static unsigned writes,reads,commits,logged;
static struct {uint16_t address;uint32_t word;} log_words[4096];
int64_t esp_timer_get_time(void) { return now; }
uint32_t esp_random(void) { static uint32_t random=100;return ++random; }
bool fpga_word_read_burst(uint16_t address,uint32_t *words,size_t n)
{
    assert(address==0x0709 && n==1);now+=5;reads++;
    if(!io_ok)return false;
    words[0]=old_window?wire_sequence:wire_sequence | (wire_busy?0x80000000u:0);return true;
}
void fpga_word_write_burst(uint16_t address,const uint32_t *words,size_t n)
{
    assert(n && n<=12 && 4+4*n<=64);assert(!wire_busy || old_window);
    now+=10;writes++;
    for(unsigned i=0;i<n;i++) {
        assert(logged<4096);log_words[logged].address=address+i;log_words[logged++].word=words[i];
        if(address+i==0xa0ff) {
            assert(n==1 && words[i] && !(words[i]&0x80000000u));
            last_commit=words[i];commits++;
            if(cancel_on_commit) {cancel_on_commit=false;additive_link_cancel();}
            if(!old_window){wire_sequence=words[i];wire_busy=true;}
        }
    }
}
void fpga_word_write(uint16_t address,uint32_t word) { fpga_word_write_burst(address,&word,1); }
static additive_link_state_t state(void)
{
    additive_link_state_t s;additive_link_state(&s);
    additive_link_status_t status;additive_link_status(&status);
    assert(status.available==s.available && status.valid==s.valid &&
        status.selected==s.selected && status.ready==s.ready && status.busy==s.busy &&
        status.patch_known==s.patch_known);
    assert(status.fault==s.fault && status.instance_id==s.instance_id &&
        status.revision==s.revision && status.commit_id==s.commit_id &&
        status.completed_request_id==s.completed_request_id);
    additive_link_status(NULL);
    return s;
}
static void clean(void)
{
    now=0;io_ok=true;old_window=wire_busy=cancel_on_commit=false;wire_sequence=last_commit=0;
    writes=reads=commits=logged=0;additive_link_init();
}
static void step(bool disabled) { now+=1000;additive_link_service(now+200,disabled); }
static void until_commit(unsigned expected)
{
    for(unsigned i=0;commits<expected && i<100;i++)step(true);
    assert(commits==expected);
}
static void ack(void) { assert(!old_window);wire_busy=false;wire_sequence=last_commit;step(true); }
static uint32_t expected_word(const additive_patch_t *p,bool selected,unsigned index)
{
    if(index==0)return selected | ((uint32_t)p->bypass_lowpass<<1);
    if(index==1)return p->centroid_q8 | ((uint32_t)p->slope<<16);
    if(index==2)return p->comb_period | ((uint32_t)p->comb_depth<<16);
    if(index==3)return p->master_gain | ((uint32_t)p->master_pan<<16);
    if(index==4)return p->bend_range;
    if(index>=16 && index<20)return p->env[index-16].attack_inc | ((uint32_t)p->env[index-16].decay_inc<<16);
    if(index>=20 && index<24)return p->env[index-20].sustain | ((uint32_t)p->env[index-20].release_inc<<16);
    if(index>=32) {
        unsigned k=(index-32)/4,w=(index-32)%4;
        if(w==0)return p->partials[k].ratio_q16;
        if(w==1)return p->partials[k].weight | ((uint32_t)p->partials[k].group<<16);
    }
    return 0;
}
static void check_upload(unsigned start,const additive_patch_t *p,bool selected)
{
    for(unsigned i=0;i<ADDITIVE_SHADOW_WORDS;i++) {
        uint16_t address=i<32?0xa000+i:0xa100+i-32;
        assert(log_words[start+i].address==address);
        assert(log_words[start+i].word==expected_word(p,selected,i));
    }
    assert(log_words[start+160].address==0xa0ff);
}
static void probe(void)
{
    until_commit(1);assert(!state().available && !state().valid);ack();
    assert(state().available && state().valid && state().patch_known && !state().selected && !state().busy);
    assert(state().revision==1 && state().commit_id==last_commit && !state().completed_request_id);
}
static void test_patch_and_full_upload(void)
{
    clean();additive_patch_t p;additive_patch_default(&p);
    assert(additive_patch_validate(&p) && p.bypass_lowpass);
    assert(p.master_gain==16384 && p.master_pan==32768 && p.bend_range==2);
    const unsigned sustain[]={49152,32768,16384,8192},release[]={8,12,20,32};
    for(unsigned g=0;g<4;g++)assert(p.env[g].attack_inc==2048 && p.env[g].decay_inc==8 &&
        p.env[g].sustain==sustain[g] && p.env[g].release_inc==release[g]);
    for(unsigned k=0;k<32;k++)assert(p.partials[k].ratio_q16==(k+1)*65536 &&
        p.partials[k].weight==65535/(k+1) && p.partials[k].group==k/8);
    assert(!additive_patch_validate(NULL));
    p.centroid_q8=8192;assert(!additive_patch_validate(&p));p.centroid_q8=8191;
    p.comb_period=32;assert(!additive_patch_validate(&p));p.comb_period=31;
    p.bend_range=128;assert(!additive_patch_validate(&p));p.bend_range=127;
    p.partials[31].ratio_q16=0x1000000;assert(!additive_patch_validate(&p));p.partials[31].ratio_q16=0xffffff;
    p.partials[31].group=4;assert(!additive_patch_validate(&p));p.partials[31].group=3;
    assert(additive_patch_validate(&p));
    uint16_t address;uint32_t word;
    assert(!additive_patch_word(&p,false,160,&address,&word));
    assert(!additive_patch_word(&p,false,0,NULL,&word));
    assert(state().instance_id && state().busy && !state().available);
    step(false);assert(!writes);probe();
    additive_patch_t defaults;additive_patch_default(&defaults);check_upload(0,&defaults,false);
    p.slope=65535;p.comb_depth=65535;p.master_gain=65535;p.master_pan=0;
    for(unsigned g=0;g<4;g++)p.env[g]=(additive_envelope_t){g,65535-g,1234+g,5678+g};
    for(unsigned k=0;k<32;k++)p.partials[k]=(additive_partial_t){0xffffff-k,65535-k,k%4};
    additive_patch_t wanted=p;
    assert(!additive_link_request(&p,true,0));assert(additive_link_request(&p,true,42));
    assert(!additive_link_request(&p,true,43));memset(&p,0,sizeof(p));
    unsigned sent=writes;step(false);assert(writes==sent);
    until_commit(2);check_upload(161,&wanted,true);
    for(unsigned i=0;i<10;i++)step(true);
    assert(!state().selected && state().busy && state().completed_request_id==0);
    ack();additive_link_state_t s=state();
    assert(s.valid && s.selected && s.ready && s.completed_request_id==42 && s.revision==2);
    assert(!memcmp(&wanted,&s.confirmed_patch,sizeof(wanted)));
    assert(additive_link_request(&wanted,false,77));until_commit(3);check_upload(322,&wanted,false);ack();
    assert(!state().selected && state().completed_request_id==77 && state().revision==3);
}
static void test_busy_missing_and_io(void)
{
    clean();wire_sequence=99;wire_busy=true;
    for(unsigned i=0;i<120;i++)step(true);
    assert(!writes && !state().available);
    wire_busy=false;until_commit(1);assert(last_commit==100);ack();assert(state().available);
    clean();old_window=true;until_commit(1);unsigned sent=writes;
    for(unsigned i=0;i<250;i++)step(true);
    assert(!state().available && !state().valid && !state().busy && state().fault==ADDITIVE_LINK_UNAVAILABLE);
    unsigned polled=reads;
    for(unsigned i=0;i<250;i++)step(false);
    assert(writes==sent && reads>polled && reads-polled<=14);
    additive_patch_t p;additive_patch_default(&p);assert(!additive_link_request(&p,true,8));
    clean();io_ok=false;step(true);assert(!writes && !state().available && state().fault==ADDITIVE_LINK_IO);
    io_ok=true;probe();
    assert(additive_link_request(&p,true,9));until_commit(2);sent=writes;
    for(unsigned i=0;i<250;i++)step(true);
    assert(state().busy && !state().valid && state().fault==ADDITIVE_LINK_TIMEOUT && writes==sent);
    clean();old_window=true;wire_sequence=37;until_commit(1);assert(last_commit==38);
    for(unsigned i=0;i<250;i++)step(true);
    assert(!state().available && state().fault==ADDITIVE_LINK_UNAVAILABLE);
}
static void test_cancel_and_invalidate(void)
{
    clean();probe();additive_patch_t p;additive_patch_default(&p);p.master_gain=50000;
    assert(additive_link_request(&p,true,55));until_commit(2);
    additive_link_cancel();unsigned sent=writes;
    for(unsigned i=0;i<150;i++)step(true);
    assert(writes==sent && !state().completed_request_id);
    wire_busy=false;wire_sequence=last_commit;step(false);
    assert(writes==sent && !state().completed_request_id && !state().valid);
    until_commit(3);additive_patch_t defaults;additive_patch_default(&defaults);check_upload(322,&defaults,false);ack();
    assert(state().valid && !state().selected && !state().completed_request_id && state().fault==ADDITIVE_LINK_CANCELLED);
    assert(additive_link_request(&p,true,56));
    now+=1000;additive_link_service(now+20,true);assert(commits==3); // interrupted shadow, no COMMIT
    additive_link_cancel();until_commit(4);assert(!(log_words[logged-161].word&1));ack();
    assert(!state().selected && !state().completed_request_id);
    assert(additive_link_request(&p,true,57));until_commit(5);
    uint32_t old_instance=state().instance_id,old_id=last_commit;
    additive_link_invalidate();sent=writes;
    assert(state().instance_id!=old_instance && !state().available && !state().valid && !state().revision);
    assert(!state().confirmed_patch.master_gain);
    for(unsigned i=0;i<100;i++)step(true);assert(writes==sent);
    wire_busy=false;wire_sequence=old_id;step(false);assert(!state().completed_request_id);
    until_commit(6);assert(last_commit!=old_id);check_upload(logged-161,&defaults,false);ack();
    assert(state().available && state().valid && !state().selected && state().revision==1);
}
static void test_wrap_and_deadline(void)
{
    clean();wire_sequence=0x7fffffffu;
    additive_link_service(now,false);assert(!reads && !writes);
    until_commit(1);assert(last_commit==1);ack();assert(state().valid);
}
static void test_cancel_at_commit_serialization(void)
{
    clean();probe();additive_patch_t p;additive_patch_default(&p);p.master_gain=12345;
    assert(additive_link_request(&p,true,88));cancel_on_commit=true;until_commit(2);
    assert(!state().completed_request_id && !state().valid);
    ack();assert(!state().completed_request_id && !state().selected);
    until_commit(3);assert(!(log_words[logged-161].word&1));ack();
    assert(state().valid && !state().selected && !state().completed_request_id);
}
static void test_local_boot_and_remote_resume(void)
{
    clean();additive_link_local_fence(false);
    unsigned sent=writes;
    for(unsigned i=0;i<150;i++)step(true);
    assert(writes==sent && !state().patch_known);
    additive_link_adopt_local(true,true);
    additive_link_state_t s=state();
    assert(s.available && s.valid && s.selected && s.ready && !s.busy && !s.patch_known);
    assert(!s.revision && !s.commit_id && !s.completed_request_id && !s.confirmed_patch.master_gain);
    additive_patch_t p;additive_patch_default(&p);
    assert(!additive_link_request(&p,true,99));
    for(unsigned i=0;i<150;i++)step(true);
    assert(writes==sent && !reads); // Frozen ownership needs no startup probe or idle health read.
    additive_link_remote_resume();
    assert(!state().valid && state().busy && !state().patch_known);
    assert(!additive_link_request(&p,true,100));
    until_commit(1);check_upload(0,&p,false);ack();
    assert(state().valid && state().patch_known && !state().selected);
    assert(!state().completed_request_id);
}
static void test_local_cancels_shadow_and_late_ack(void)
{
    clean();probe();additive_patch_t p;additive_patch_default(&p);p.master_gain=45678;
    assert(additive_link_request(&p,true,101));
    now+=1000;additive_link_service(now+20,true);assert(commits==1);
    unsigned sent=writes;additive_link_local_fence(false);additive_link_adopt_local(true,true);
    for(unsigned i=0;i<150;i++)step(true);
    assert(writes==sent && !state().patch_known && state().selected && !state().completed_request_id);
    additive_link_remote_resume();until_commit(2);ack();
    assert(state().valid && state().patch_known && !state().selected);

    assert(additive_link_request(&p,true,102));until_commit(3);sent=writes;
    additive_link_local_fence(false);additive_link_adopt_local(true,true);
    for(unsigned i=0;i<150;i++)step(true);
    assert(writes==sent && !state().patch_known && !state().completed_request_id);
    ack();
    assert(state().selected && state().valid && state().ready && !state().busy);
    assert(!state().patch_known && !state().revision && !state().commit_id && !state().completed_request_id);
    for(unsigned i=0;i<150;i++)step(true);
    assert(writes==sent); // Late transport ACK never schedules compensation under local ownership.
    additive_link_remote_resume();until_commit(4);ack();
    assert(state().patch_known && !state().selected && !state().completed_request_id);
}
static void test_local_fault_preserves_unknown_commit(void)
{
    clean();probe();additive_patch_t p;additive_patch_default(&p);
    assert(additive_link_request(&p,true,103));until_commit(2);
    additive_link_local_fence(true);unsigned sent=writes;
    assert(!state().valid && !state().ready && !state().patch_known && state().fault);
    additive_link_remote_resume();
    for(unsigned i=0;i<150;i++)step(true);
    assert(writes==sent && !state().completed_request_id && !state().patch_known);
    ack();assert(writes==sent && !state().valid && !state().completed_request_id);
    until_commit(3);ack();
    assert(state().valid && state().patch_known && !state().selected);
    additive_link_local_fence(false);additive_link_adopt_local(false,false);
    assert(!state().available && !state().valid && !state().ready && !state().selected);
}
static void test_cancel_and_invalidate_stay_local(void)
{
    clean();probe();additive_patch_t p;additive_patch_default(&p);
    assert(additive_link_request(&p,true,104));
    additive_link_local_fence(false);additive_link_adopt_local(true,true);
    unsigned sent=writes;
    additive_link_cancel();
    for(unsigned i=0;i<150;i++)step(true);
    assert(writes==sent && !state().patch_known && !state().completed_request_id);
    additive_link_invalidate();
    for(unsigned i=0;i<150;i++)step(true);
    assert(writes==sent && !state().patch_known && !state().completed_request_id);
    additive_link_remote_resume();until_commit(2);ack();
    assert(state().valid && state().patch_known && !state().selected && !state().completed_request_id);
}
int main(void)
{
    test_patch_and_full_upload();test_busy_missing_and_io();test_cancel_and_invalidate();test_wrap_and_deadline();test_cancel_at_commit_serialization();
    test_local_boot_and_remote_resume();test_local_cancels_shadow_and_late_ack();test_local_fault_preserves_unknown_commit();
    test_cancel_and_invalidate_stay_local();
    puts("additive: exact image, ACK fences, local noninterference/unknown patch, late ACK cancellation, remote reprobe PASS");
    return 0;
}
