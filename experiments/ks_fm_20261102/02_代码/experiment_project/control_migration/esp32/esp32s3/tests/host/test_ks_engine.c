#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine_link.h"
#include "spi_regs.h"
extern int64_t fake_time;
static bool caps=true,id_ok=true,foreign_busy,mailbox,swap_ack=true,swap_pending;
static bool reject_next,cancel_shadow,hold_mailbox,drop_accept,drop_mode_ack,slow_init;
static bool drop_all_accepts,record_rebuild;
static unsigned write_cost=7;
static bool rebuilt_gates[2][ENGINE_NUM_ELEMENTS];
static unsigned active_bank,pass,commits,on_commits,mode_commits;
static uint32_t shadow[6],stable[6],stable_seq,accepted,rejects,mode_seq,started[16],status,frames;
static uint16_t active_mask,init_mask,release_mask;
static uint32_t version=0x00020010u;
static unsigned init_delay[16],release_delay[16];
static uint32_t note_seq[16];
void fpga_word_write(uint16_t a,uint32_t value)
{
    fake_time+=write_cost;
    if(record_rebuild && a>=0x2000 && (a-0x2000)%64==4)
        rebuilt_gates[active_bank^1][(a-0x2000)/64]=value==0;
    if(a==2)swap_pending=true;
    if(a>=0x510&&a<0x516)shadow[a-0x510]=value;
    if(a==0x516) {
        commits++;
        if(mailbox || foreign_busy || reject_next) {rejects++;reject_next=false;return;}
        memcpy(stable,shadow,sizeof(stable));stable_seq=value;mailbox=true;
        if((stable[0]&7)==KS_NOTE_ON)on_commits++;
        if((stable[0]&7)==KS_MODE)mode_commits++;
    }
}
void fpga_word_write_burst(uint16_t a,const uint32_t *words,size_t n)
{
    for(size_t i=0;i<n;i++)fpga_word_write(a+i,words[i]);
    if(a==0x510 && cancel_shadow && (words[0]&7)==KS_NOTE_ON) {
        cancel_shadow=false;engine_link_ks_note_off((words[0]>>8)&15);
    }
}
bool fpga_word_read_burst(uint16_t a,uint32_t *words,size_t n)
{
    // spi_regs.c WORDS_MAX is 12; never hide an overlong caller request.
    assert(n<=12 && "real SPI word bursts are limited to 12 words");
    fake_time+=30;
    if(!id_ok)return false;
    memset(words,0,n*sizeof(*words));
    if(a==0x500) {words[0]=caps?ENGINE_KS_CAPABILITY:0;words[1]=version;}
    else if(a==0x502) {
        uint32_t values[6]={status|((mailbox||foreign_busy)?2:0),accepted,rejects,mode_seq,frames,0};
        assert(n==6);memcpy(words,values,sizeof(values));
    } else if(a==0x508) {assert(n==3);words[0]=active_mask;words[1]=init_mask;words[2]=release_mask;}
    else if(a==0x540 || a==0x548) {assert(n==8);memcpy(words,started+(a-0x540),n*sizeof(*words));}
    else if(a==0x520) {assert(n==4);memcpy(words,started,4*sizeof(*words));}
    else if(a==0x524) {assert(n==1);words[0]=0xdeadbeefu;}
    return true;
}
bool fpga_swap_checked(void){active_bank^=1;return true;}
bool fpga_control_status(uint32_t *out)
{
    pass++;fake_time+=8;
    if(swap_ack&&swap_pending) {active_bank^=1;swap_pending=false;}
    *out=(pass<<16)|active_bank|(swap_pending?2:0);return id_ok;
}
bool fpga_live_write(uint16_t bus,uint32_t value){(void)bus;(void)value;fake_time+=7;return id_ok;}
#include "engine_link.c"
static void consume(void)
{
    frames++;
    for(unsigned v=0;v<16;v++) {
        if(init_delay[v] && --init_delay[v]==0) {init_mask&=~(1u<<v);active_mask|=1u<<v;started[v]=note_seq[v];}
        if(release_delay[v] && --release_delay[v]==0)active_mask&=~(1u<<v),release_mask&=~(1u<<v);
    }
    if(!mailbox || hold_mailbox)return;
    unsigned op=stable[0]&7,v=(stable[0]>>8)&15;
    if(drop_accept)drop_accept=false;else if(!drop_all_accepts)accepted=stable_seq;
    mailbox=false;
    if(op==KS_NOTE_ON) {
        note_seq[v]=stable_seq;started[v]=0;init_delay[v]=slow_init?200:4;release_delay[v]=0;
        active_mask&=~(1u<<v);release_mask&=~(1u<<v);init_mask|=1u<<v;
    } else if(op==KS_NOTE_OFF) {
        init_delay[v]=0;started[v]=0;init_mask&=~(1u<<v);
        if(active_mask&(1u<<v)) {release_delay[v]=3;release_mask|=1u<<v;}
    } else if(op==KS_MODE || op==KS_PANIC) {
        memset(init_delay,0,sizeof(init_delay));memset(release_delay,0,sizeof(release_delay));memset(started,0,sizeof(started));
        status&=1;active_mask=init_mask=release_mask=0;
        if(op==KS_MODE) {
            status=(stable[0]>>16)&1;
            if(drop_mode_ack)drop_mode_ack=false;else mode_seq=stable_seq;
        }
    }
}
static void tick(void){consume();fake_time+=1000;engine_service();}
static void ticks(unsigned n){for(unsigned i=0;i<n;i++)tick();}
static engine_ks_note_t note={.length=217,.tuning=2000,.loss=32760,.amplitude=8192,.gain_left=4096,.gain_right=4096,.seed=42};
static int panic_sub;
static void acknowledge_owner(void)
{
    evt_t event;assert(event_bus_receive(panic_sub,&event,0)==pdTRUE&&event.kind==EVT_PANIC);
    engine_link_ks_panic();engine_link_owner_panic_ack();
}
static void safe_dds(void)
{
    ticks(350);engine_ks_state_t state;engine_link_ks_state(&state);
    assert(state.valid&&!state.recovering&&!state.switching&&!state.requested_mode&&!state.applied_mode);
    assert(!state.active_mask&&!state.init_mask);
    for(unsigned v=0;v<16;v++)assert(!state.started_sequence[v]&&!s_ks_desired[v].on);
}
static void select_ks(void)
{assert(engine_link_ks_mode(true));ticks(15);assert(s_ks.applied_mode&&!s_ks.switching);}
int main(void)
{
    event_bus_init();panic_sub=event_bus_subscribe_reliable(xQueueCreate(64,sizeof(evt_t)));
    accepted=500;mode_seq=400;frames=1000; // ESP reboot, FPGA was already running.
    engine_link_init();ticks(15);engine_ks_state_t state;engine_link_ks_state(&state);
    assert(state.available&&state.valid&&!state.applied_mode&&!state.switching&&!state.recovery_count&&state.accepted_sequence>500);
    assert(engine_link_ks_mode(true));ticks(15);engine_link_ks_state(&state);
    assert(state.requested_mode&&state.applied_mode&&!state.switching&&state.mode_sequence);
    assert(ENGINE_KS_VOICES==16 && ENGINE_KS_VERSION==0x00020010u);
    engine_ks_note_t limit=note;limit.length=4097;
    assert(!engine_link_ks_note_on(15,&limit));limit.length=4096;
    assert(engine_link_ks_note_on(15,&limit));engine_link_ks_state(&state);assert(state.pending_mask==0x8000);
    engine_service();tick();engine_link_ks_state(&state);assert(state.init_mask==0x8000 && !state.active_mask);
    ticks(15);engine_link_ks_state(&state);
    assert(state.active_mask==0x8000 && state.started_sequence[15]==state.note_sequence[15] && state.started_sequence[15]);
    assert(!state.started_sequence[3]);
    engine_link_ks_note_off(15);engine_service();tick();engine_link_ks_state(&state);assert(state.release_mask==0x8000);ticks(15);engine_link_ks_state(&state);assert(!state.active_mask);
    for(unsigned v=0;v<16;v++)assert(engine_link_ks_note_on(v,&note));
    ticks(80);engine_link_ks_state(&state);assert(state.active_mask==0xffff);
    for(unsigned v=0;v<16;v++)assert(state.started_sequence[v] && state.started_sequence[v]==state.note_sequence[v]);
    engine_link_ks_panic();ticks(15);engine_link_ks_state(&state);assert(!state.active_mask && !state.init_mask);
    for(unsigned v=0;v<16;v++)assert(!state.started_sequence[v]);
    unsigned before=on_commits;
    foreign_busy=true;assert(engine_link_ks_note_on(0,&note));ticks(10);assert(on_commits==before);
    engine_link_ks_note_off(0);foreign_busy=false;ticks(10);assert(on_commits==before);
    cancel_shadow=true;assert(engine_link_ks_note_on(0,&note));ticks(10);assert(on_commits==before);
    assert(engine_link_ks_note_on(0,&note));engine_service();assert(on_commits==before+1&&mailbox);
    engine_link_ks_note_off(0);tick();engine_link_ks_state(&state);
    assert(state.init_mask==1&&state.note_sequence[0]&&!state.started_sequence[0]);
    ticks(10);engine_link_ks_state(&state);assert(!state.init_mask&&!state.active_mask&&!state.started_sequence[0]);
    reject_next=true;assert(engine_link_ks_note_on(1,&note));ticks(15);engine_link_ks_state(&state);
    assert(state.reject_count==1&&state.started_sequence[1]==state.note_sequence[1]&&(state.active_mask&2));
    // Idle link health is bounded by the 20 ms cache; command failures below
    // still require immediate detection while their ACK is unknown.
    id_ok=false;ticks(21);engine_link_ks_state(&state);
    assert(!state.valid&&state.recovering&&state.fault==KS_FAULT_READBACK&&!engine_link_ks_note_on(2,&note));
    id_ok=true;ticks(100);assert(s_ks.switching&&s_ks_owner_pending); // Transport alone cannot rearm owners.
    // A second real reset while already recovering has no inflight command.
    // It must still invalidate the completed bank rebuild, unlike an ACK retry.
    assert(!s_ks_inflight.sequence);record_rebuild=true;
    memset(rebuilt_gates,0,sizeof(rebuilt_gates));
    accepted=mode_seq=frames=rejects=status=0;active_mask=init_mask=release_mask=0;swap_pending=false;active_bank=0;
    memset(started,0,sizeof(started));memset(init_delay,0,sizeof(init_delay));memset(release_delay,0,sizeof(release_delay));
    tick();acknowledge_owner();safe_dds();
    for(unsigned b=0;b<2;b++)for(unsigned e=0;e<ENGINE_NUM_ELEMENTS;e++)
        assert(rebuilt_gates[b][e] && "a new FPGA reset must rebuild both banks even within an existing recovery");
    record_rebuild=false;select_ks();
    assert(engine_link_ks_note_on(2,&note));assert(engine_link_ks_note_on(3,&note));engine_link_ks_panic();ticks(15);
    engine_link_ks_state(&state);assert(!state.active_mask&&!state.init_mask&&!state.started_sequence[1]);
    // Mode waits for confirmed bank hard mute, not only live GateOff ack.
    engine_cmd_t c={.elem=0,.word=4,.value=1};assert(engine_link_send(&c));ticks(20);
    c.value=0;assert(engine_link_send(&c));swap_ack=false;before=mode_commits;
    assert(engine_link_ks_mode(false));ticks(20);assert(mode_commits==before);
    engine_link_ks_state(&state);
    assert(state.wait_reason==KS_WAIT_BANK && !state.inflight_sequence &&
           state.control_status_valid && (state.control_status&2) && state.wait_ms>=20);
    swap_ack=true;ticks(30);engine_link_ks_state(&state);
    assert(!state.applied_mode&&!state.requested_mode&&!state.switching&&mode_commits==before+1);
    // Accepted sequence lost with no reject must recover after 50 ms.
    select_ks();before=on_commits;unsigned recoveries=s_ks.recovery_count;
    drop_accept=true;assert(engine_link_ks_note_on(0,&note));engine_service();ticks(20);
    assert(!s_ks.recovering&&s_ks_inflight.sequence);ticks(30);
    assert(s_ks.recovering&&s_ks.fault==KS_FAULT_TIMEOUT&&s_ks.recovery_count==recoveries+1);
    acknowledge_owner();safe_dds();assert(on_commits==before+1); // Never retry the old On.
    // MODE acceptance is insufficient if its applied sequence never arrives.
    drop_mode_ack=true;assert(engine_link_ks_mode(true));ticks(70);
    assert(s_ks.recovering&&s_ks.fault==KS_FAULT_TIMEOUT);
    acknowledge_owner();safe_dds();
    // FPGA/PLL reset between COMMIT and accepted clears all counters without rejection.
    select_ks();before=on_commits;assert(engine_link_ks_note_on(15,&note));engine_service();assert(mailbox);
    mailbox=false;accepted=mode_seq=frames=rejects=status=0;active_mask=init_mask=release_mask=0;swap_pending=false;active_bank=0;
    memset(started,0,sizeof(started));memset(init_delay,0,sizeof(init_delay));memset(release_delay,0,sizeof(release_delay));
    tick();assert(s_ks.recovering&&s_ks.fault==KS_FAULT_RESET&&s_ks_inflight.op==KS_PANIC);
    acknowledge_owner();safe_dds();assert(on_commits==before+1);
    // Read ID is lost after COMMIT; a late acceptance on reconnect cannot
    // restore the cleared desired On or bypass the owner panic fence.
    select_ks();before=on_commits;hold_mailbox=true;assert(engine_link_ks_note_on(0,&note));engine_service();
    id_ok=false;tick();assert(s_ks.recovering&&!s_ks_inflight.sequence&&s_ks.fault==KS_FAULT_READBACK);
    id_ok=true;hold_mailbox=false;ticks(10);assert(s_ks.recovering&&!s_ks_desired[0].on);
    acknowledge_owner();safe_dds();assert(on_commits==before+1);
    // A late On accepted after timeout is canceled by priority Panic; no held replay.
    select_ks();before=on_commits;hold_mailbox=true;assert(engine_link_ks_note_on(15,&note));engine_service();ticks(60);
    assert(s_ks.recovering&&s_ks.fault==KS_FAULT_TIMEOUT);acknowledge_owner();hold_mailbox=false;
    safe_dds();assert(on_commits==before+1);
    // A >50 ms initialization is legal once mailbox acceptance has arrived.
    select_ks();slow_init=true;assert(engine_link_ks_note_on(15,&note));ticks(60);
    assert(!s_ks.recovering&&!s_ks_inflight.sequence&&(s_ks.init_mask&0x8000));
    engine_link_ks_note_off(15);ticks(15);slow_init=false;assert(!(s_ks.active_mask|s_ks.init_mask));
    // Counter wrap is not a reset. One forward frame across UINT32_MAX is valid.
    s_ks.frame_count=UINT32_MAX;frames=0;tick();assert(!s_ks.recovering);
    // Lost recovery Panic acknowledgements must not repeatedly discard the
    // independent muted-bank rebuild. A slower full render exceeds 50 ms.
    record_rebuild=true;write_cost=20;drop_all_accepts=true;
    id_ok=false;ticks(21);id_ok=true;acknowledge_owner();ticks(350);
    assert(s_ks.recovering && s_ks.timeout_count);
    for(unsigned b=0;b<2;b++)for(unsigned e=0;e<ENGINE_NUM_ELEMENTS;e++)
        assert(rebuilt_gates[b][e] && "recovery retries must preserve bank mute progress");
    drop_all_accepts=record_rebuild=false;write_cost=7;safe_dds();
    // A pending swap can block MODE before COMMIT; this phase needs its own
    // timeout and must remain muted until a real swap acknowledgement returns.
    select_ks();c.word=4;c.value=1;assert(engine_link_send(&c));ticks(20);
    c.value=0;assert(engine_link_send(&c));swap_ack=false;before=mode_commits;
    assert(engine_link_ks_mode(false));recoveries=s_ks.recovery_count;ticks(1100);
    engine_link_ks_state(&state);
    assert(state.recovering && state.fault==4 && state.recovery_count>recoveries &&
           "mode preparation must time out before an unsent COMMIT can hang forever");
    assert(mode_commits==before && !engine_link_ks_note_on(0,&note));
    acknowledge_owner();swap_ack=true;safe_dds();
    // A wrong capability/version cannot opt into KS; DDS service survives.
    memset(&s_ks,0,sizeof(s_ks));version=0x00010004u;ks_probe();assert(!s_ks.available && !engine_link_ks_mode(true));version=0x00020010u;caps=false;ks_probe();assert(!engine_link_ks_mode(true));
    c.word=0;c.value=123;assert(engine_link_send(&c));ticks(20);assert(s_image[0][0]==123);
    puts("KS engine: ABI2/old ABI disabled, 16 concurrent voices, high masks/started15, 4096/4097, caps/default DDS, ack vs init, busy non-overwrite, shadow cancel, inflight Off, reject retry, owner-fenced ID recovery, lost accepted/mode ack timeout, FPGA reset, late-On cancel, long init, counter wrap, bank-fenced mode, legacy DDS PASS");
    return 0;
}
