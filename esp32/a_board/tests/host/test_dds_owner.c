#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine_link.h"
extern int64_t fake_time;
static engine_ks_state_t ks_state;
static bool gate_ack;
static unsigned notes,gates_off;
static uint32_t producer_words[ENGINE_NUM_PRODUCERS][4];
static unsigned env_rate_writes;
bool engine_link_send(const engine_cmd_t *c){(void)c;return true;}
bool engine_link_bus_write(uint16_t b,uint32_t v){if(b>=80 && b<112 && !v)gates_off++;return true;}
bool engine_link_prod_write(uint8_t e,uint8_t w,uint32_t v)
{
    producer_words[e][w]=v;
    if((w==1 || w==3) && (e>=32 && e<128))env_rate_writes++;
    return true;
}
uint32_t engine_link_drops(void){return 0;}
uint32_t engine_link_prod_committed(uint8_t e,uint8_t w){(void)e;(void)w;return 0;}
uint32_t engine_link_release_coefficient(uint8_t e,uint32_t *r){(void)e;*r=0;return 1;}
bool engine_link_bus_committed(uint16_t b,uint32_t v){(void)b;(void)v;return gate_ack;}
void engine_link_ks_state(engine_ks_state_t *s){*s=ks_state;}
bool engine_link_ks_note_on(unsigned v,const engine_ks_note_t *n){(void)v;(void)n;return false;}
void engine_link_ks_note_off(unsigned v){(void)v;}
void engine_link_ks_panic(void){}
bool engine_link_ks_mode(bool mode){return !mode;}
void engine_link_ks_gain(unsigned v,uint16_t l,uint16_t r){(void)v;(void)l;(void)r;}
void engine_link_ks_loss(unsigned v,uint16_t loss){(void)v;(void)loss;}
void engine_link_owner_panic_ack(void){}
#include "voice_alloc.c"
#undef TAG
static unsigned worker_stack_bytes;
static BaseType_t dds_test_task_create(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *task)
{
    assert(!strcmp(name,"dds_files"));worker_stack_bytes=stack;
    return xTaskCreate(fn,name,stack,arg,priority,task);
}
#define xTaskCreate dds_test_task_create
#include "dds_presets.c"
#undef xTaskCreate
bool engine_link_note_on(const engine_note_t *n)
{
    // These words enter the engine's desired bank before it can accept On.
    assert(producer_words[n->amp_entry][1]==patch_adsr_rate1(&g_patch.env[0]));
    assert(producer_words[n->amp_entry][3]==patch_adsr_rate2(&g_patch.env[0]));
    assert(producer_words[n->mod_entry][1]==patch_adsr_rate1(&g_patch.env[1]));
    assert(producer_words[n->mod_entry][3]==patch_adsr_rate2(&g_patch.env[1]));
    notes++;return true;
}

static uint32_t request(dds_operation_t op,const char *path,const char *body)
{
    uint32_t id;char error[DDS_ERROR_MAX];
    assert(dds_presets_request(op,path,body,body?strlen(body):0,0,&id,error));
    dds_owner_request(id);return id;
}
static void run_worker(void)
{uint32_t id;assert(xQueueReceive(s_work,&id,0)==pdTRUE);work(id);}
static void status_is(dds_phase_t phase)
{dds_job_status_t status;dds_presets_status(&status);assert(status.phase==phase);}
int main(void)
{
    char root[]="/tmp/dds-owner.XXXXXX";assert(mkdtemp(root));s_root=root;s_mounted=true;
    event_bus_init();dds_presets_init();patch_default(&g_patch);
    assert(worker_stack_bytes==16u*1024u);
    assert(!dds_presets_boot_patch(&s_dds_candidate));
    ks_state.control_status_valid=true;s_control_epoch=1;dds_presets_owner_context(1);
    for(unsigned v=0;v<NUM_VOICES;v++){update_amp_env_voice(v);update_mod_env_voice(v);}
    patch_t old=g_patch;
    // Prepared load does not mutate the shared sound or touch SPI from worker.
    assert(note_on_owned(0,60,100,0));unsigned before=notes;
    request(DDS_LOAD,"Patches/Factory/Pads/Galaxy.toml",NULL);run_worker();
    assert(!memcmp(&old,&g_patch,sizeof(old)) && !gates_off);status_is(DDS_READY);
    gate_ack=false;assert(!dds_owner_poll(100));status_is(DDS_CLOSING);
    assert(gates_off==32 && !memcmp(&old,&g_patch,sizeof(old)));
    assert(!note_on_owned(0,64,100,0) && notes==before);
    note_off(60);assert(!dds_owner_poll(200));
    gate_ack=true;ks_state.bank_pending=true;assert(!dds_owner_poll(300));
    ks_state.bank_pending=false;assert(dds_owner_poll(400));status_is(DDS_DONE);
    assert(g_patch.osc[0].wave==WAVE_PULSE && !s_dds_switch_id);
    // A completed old batch leaves the next 1 ms tick inside the 4 ms
    // throttle. The first new note still needs both fresh ADSR rate pairs.
    s_apply_active=0;s_last_apply=fake_time;
    unsigned rates_before=env_rate_writes;
    apply_dirty(fake_time+1000);assert(env_rate_writes==rates_before);
    assert(note_on_owned(0,64,100,0) && notes==before+1);
    assert(env_rate_writes==rates_before+4);
    // Exercise the real CC path, including the destination change which
    // preserves a depth above duty's nominal 2048 range, then save/load it.
    handle_cc(112,32);handle_cc(110,127);handle_cc(112,0);
    assert(g_patch.lfo[1].dest==0 && g_patch.lfo[1].depth==4064);
    // Save captures the owner's effective buses without changing live patch.
    s_cut_off=-1234;s_reso_off=456;
    uint32_t id=request(DDS_SAVE,"Patches/User/Pads/Snapshot.toml",NULL);
    old=g_patch;run_worker();status_is(DDS_DONE);assert(!memcmp(&old,&g_patch,sizeof(old)));
    char *text,error[DDS_ERROR_MAX];size_t len;patch_t snapshot;
    assert(dds_files_read(root,"Patches/User/Pads/Snapshot.toml",&text,&len,error));
    assert(dds_patch_parse(text,len,&snapshot,NULL,error));free(text);
    assert((int16_t)snapshot.filter.cutoff==-1234 && snapshot.filter.resonance==old.filter.resonance+456);
    assert(snapshot.lfo[1].dest==0 && snapshot.lfo[1].depth==4064);
    // Invalid imports never replace an existing valid file or active patch.
    request(DDS_WRITE,"Patches/User/Pads/Snapshot.toml","format=99\n");run_worker();status_is(DDS_FAILED);
    assert(dds_files_read(root,"Patches/User/Pads/Snapshot.toml",&text,&len,error));free(text);
    assert(!memcmp(&old,&g_patch,sizeof(old)));
    // A panic while parsing invalidates the load before its gate-close stage.
    request(DDS_LOAD,"Patches/Factory/Pads/Supersaw.toml",NULL);all_sound_off();run_worker();
    dds_owner_poll(500);status_is(DDS_FAILED);assert(!memcmp(&old,&g_patch,sizeof(old)));
    // Engine recovery after gate-close also retains old patch, no auto replay.
    request(DDS_LOAD,"Patches/Factory/Pads/Supersaw.toml",NULL);run_worker();gate_ack=false;
    dds_owner_poll(600);status_is(DDS_CLOSING);ks_state.recovering=true;
    assert(dds_owner_poll(700));status_is(DDS_FAILED);assert(!memcmp(&old,&g_patch,sizeof(old)));
    ks_state.recovering=false;
    // Missing file and unknown MIDI map leave current sound untouched.
    request(DDS_LOAD,"Patches/User/Pads/Missing.toml",NULL);run_worker();status_is(DDS_FAILED);assert(!memcmp(&old,&g_patch,sizeof(old)));
    const char *banks="format=1\n[bank.257]\nname='Keyboard'\nprograms=['Patches/Factory/Pads/Supersaw.toml','Patches/Factory/Pads/Galaxy.toml']\n";
    request(DDS_WRITE,"banks.toml",banks);run_worker();status_is(DDS_DONE);
    midi_message_t msb={.status=0xB3,.len=2,.data={0,2}},lsb={.status=0xB3,.len=2,.data={32,1}},program={.status=0xC3,.len=1,.data={0}};
    handle_midi_source(&msb,MIDI_SOURCE_PANEL);handle_midi_source(&lsb,MIDI_SOURCE_PANEL);
    assert(s_dds_banks[3]==257 && s_dds_banks[2]==0);
    handle_midi_source(&program,MIDI_SOURCE_PANEL);dds_owner_poll(800);run_worker();gate_ack=true;
    assert(dds_owner_poll(900));status_is(DDS_DONE);assert(g_patch.osc[0].wave==WAVE_SAW);
    // Explicit startup default persists and is checked before owner starts.
    request(DDS_DEFAULT,"Patches/User/Pads/Snapshot.toml",NULL);run_worker();status_is(DDS_DONE);
    s_boot_valid=false;dds_presets_init();assert(dds_presets_boot_patch(&snapshot) && snapshot.osc[0].wave==WAVE_PULSE);
    // Corrupt/newer startup documents and missing targets fall back to init,
    // without mutating a live patch or rewriting the user's bad file.
    char system_path[300];snprintf(system_path,sizeof(system_path),"%s/system.toml",root);
    FILE *file=fopen(system_path,"wb");assert(file);fputs("format=99\nstartup_patch='Patches/Factory/Pads/Galaxy.toml'\n",file);assert(!fclose(file));
    old=g_patch;s_boot_valid=false;dds_presets_init();assert(!dds_presets_boot_patch(&snapshot));assert(!memcmp(&old,&g_patch,sizeof(old)));
    file=fopen(system_path,"wb");assert(file);fputs("format=1\nstartup_patch='Patches/User/Pads/Missing.toml'\n",file);assert(!fclose(file));
    s_boot_valid=false;dds_presets_init();assert(!dds_presets_boot_patch(&snapshot));
    // Exclusive worker acceptance, readonly Factory and no storage fallback.
    id=request(DDS_LOAD,"Patches/Factory/Pads/Galaxy.toml",NULL);
    uint32_t rejected;assert(!dds_presets_request(DDS_LIST,"",NULL,0,0,&rejected,error));run_worker();dds_owner_poll(1000);status_is(DDS_DONE);
    assert(!dds_presets_request(DDS_SAVE,"Patches/Factory/Pads/A.toml",NULL,0,0,&rejected,error));
    s_mounted=false;request(DDS_SAVE,"Patches/User/Pads/NoStorage.toml",NULL);run_worker();status_is(DDS_FAILED);
    request(DDS_LIST,"",NULL);run_worker();status_is(DDS_DONE);char *result=dds_presets_result(s_job.status.id,&len);assert(result && strstr(result,"Galaxy"));free(result);
    (void)id;
    // Bank Select cannot be merged/evicted as an ordinary expression.
    event_bus_init();QueueHandle_t q=xQueueCreate(64,sizeof(evt_t));int sub=event_bus_subscribe_reliable(q);
    evt_t e={.kind=EVT_MIDI,.midi={.status=0xB0,.len=2,.data={0,1}}};event_bus_publish(&e);
    e.midi.data[0]=32;event_bus_publish(&e);e.midi.data[0]=1;event_bus_publish(&e);
    e.midi.data[0]=0;for(unsigned i=0;i<62;i++){e.midi.data[1]=(uint8_t)i;event_bus_publish(&e);}
    assert(event_bus_receive(sub,&e,0)==pdTRUE && e.kind==EVT_MIDI && e.midi.data[0]==0 && e.midi.data[1]==1);
    assert(event_bus_receive(sub,&e,0)==pdTRUE && e.kind==EVT_MIDI && e.midi.data[0]==32);
    puts("DDS owner: 16KiB worker, first-key ADSR inside throttle, CC depth4064 save/load, async snapshot, old-gate confirmation, stale/recovery cancellation, boot default, MIDI Omni banks, Factory/no-storage and overflow ordering PASS");
    return 0;
}
