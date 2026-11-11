#include "b_engine.h"
#include "b_pinmap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static b_request_t request(const char *line)
{
    b_request_t r;
    b_code_t code=b_protocol_parse_line(line,strlen(line),&r);
    assert(code==B_OK);
    return r;
}

static void put32(uint8_t *out,uint32_t v)
{
    for(unsigned i=0;i<4;i++) out[i]=(uint8_t)(v>>(i*8));
}

static void test_config(void)
{
    b_config_t c,changed,sentinel;
    b_config_defaults(&c);
    assert(b_config_valid(&c));
    assert(c.channel==2 && c.notes[0]==60 && c.notes[11]==71);
    assert(c.velocity[0]==20 && c.velocity[1]==500 && c.velocity[2]==127 && c.velocity[3]==16);
    assert(c.gate==200 && c.volume==100 && c.polarity==1 && c.debounce==2 && c.enc_edges==2);
    for(unsigned i=0;i<12;i++) assert(c.gains[i]==100);
    b_request_t r=request("rpc 1 -1 apply volume=90 transpose=2 velocity=20,500,16,127");
    assert(b_config_patch(&c,&r.patch,&changed));
    assert(changed.volume==90 && changed.transpose==2 && changed.velocity[2]==16 && c.velocity[2]==127);
    assert(c.volume==100 && !b_config_needs_maintenance(&c,&changed));
    sentinel=c;
    r=request("rpc 2 -1 apply volume=10 notes=60,60,62,63,64,65,66,67,68,69,70,71");
    assert(!b_config_patch(&c,&r.patch,&sentinel));
    assert(b_config_equal(&sentinel,&c));
    r=request("rpc 2 -1 apply volume=10 pins=13,14,12,40,39,41,47,21,38,2,42,13");
    assert(!b_config_patch(&c,&r.patch,&sentinel));
    assert(b_config_equal(&sentinel,&c));
    r=request("harp encpins 2 8 12 13");
    assert(b_config_patch(&c,&r.patch,&changed));
    assert(changed.pins[1][0]==8 && changed.pins[0][0]==-1);
    assert(b_config_needs_maintenance(&c,&changed));
    r=request("rpc 2 -1 apply channel=16 polarity=0 enc_edges=4 enc_reverse=15 debounce=50 cutoff=0 resonance=127");
    assert(b_config_patch(&c,&r.patch,&c));
    assert(c.channel==16 && c.polarity==0 && c.enc_edges==4 && c.enc_reverse==15);
    b_config_defaults(&c);
    const char *invalid[]={
        "channel=0","channel=17","volume=128","fine=-101","transpose=25",
        "gate=9","gate=5001","polarity=2","debounce=0","debounce=51",
        "enc_edges=3","enc_reverse=16","velocity=20,20,1,127",
        "velocity=20,60001,1,127","velocity=20,500,0,127",
        "gains=24,100,100,100,100,100,100,100,100,100,100,100"
    };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        char line[160];snprintf(line,sizeof(line),"rpc 1 -1 apply %s",invalid[i]);
        r=request(line);sentinel=c;
        assert(!b_config_patch(&c,&r.patch,&sentinel));
        assert(b_config_equal(&sentinel,&c));
    }
    puts("PASS config defaults, atomic invalid patches, pins and reversed velocity");
}

static void test_protocol(void)
{
    b_request_t r=request("rpc 4294967295 4294967295 get\r");
    assert(r.id==UINT32_MAX && r.expected_revision==UINT32_MAX && r.check_revision);
    const char *invalid[]={
        "rpc 0 -1 get","rpc 4294967296 -1 get","rpc -1 -1 get",
        "rpc 1 4294967296 get","rpc 1 -2 get","rpc 1 -1 get extra",
        "rpc 1 -1 apply","rpc 1 -1 apply volume=2 volume=3",
        "rpc 1 -1 apply mystery=2","rpc 1 -1 apply volume=",
        "rpc 1 -1 apply volume=2147483648","rpc 1 -1 apply volume=-2147483649",
        "rpc 1 -1 apply notes=1,2,3","rpc 1 -1 apply velocity=20,500,16,127,",
        "rpc 1 -1 apply velocity=20,,16,127","rpc 1 -1 apply volume=2=3",
        "rpc 1 -1 stream 199","rpc 1 -1 stream 2001",
        "rpc 1 -1 strike 0 100","rpc 1 -1 strike 13 100","rpc 1 -1 strike 1 60001",
        "harp route wireless","harp velocity 20 500 16","harp encpins 0 8 9 10"
    };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)
        assert(b_protocol_parse_line(invalid[i],strlen(invalid[i]),&r)!=B_OK);
    assert(b_protocol_parse_line("rpc 7 -1 strange",16,&r)==B_UNSUPPORTED && r.id==7);
    char oversized[B_PROTOCOL_MAX_LINE+2];memset(oversized,' ',sizeof(oversized));
    memcpy(oversized,"rpc 1 -1 get",12);
    assert(b_protocol_parse_line(oversized,B_PROTOCOL_MAX_LINE,&r)==B_OK);
    assert(b_protocol_parse_line(oversized,B_PROTOCOL_MAX_LINE+1,&r)==B_BAD_REQUEST);
    memset(oversized,' ',sizeof(oversized));
    const char *partial="rpc 21 -1 apply volume=7 mystery=2";
    memcpy(oversized,partial,strlen(partial));
    assert(b_protocol_parse_line(oversized,B_PROTOCOL_MAX_LINE,&r)==B_BAD_REQUEST);
    assert(r.id==21 && r.patch.fields==0 && r.patch.values.volume==0);
    const char embedded[]={"rpc 1 -1 get\0 apply volume=2"};
    assert(b_protocol_parse_line(embedded,sizeof(embedded)-1,&r)==B_BAD_REQUEST);
    assert(b_protocol_parse_line("rpc 1 -1 get\n",13,&r)==B_BAD_REQUEST);
    const char *commands[]={"harp status","harp panic","harp save","harp route both",
        "harp transpose -24","harp volume 127","harp fine -100","harp polarity 0",
        "harp gate 5000","harp velocity 20 500 127 16","harp encpins 1 -1 -1 -1"};
    for(unsigned i=0;i<sizeof(commands)/sizeof(*commands);i++) {
        r=request(commands[i]);assert(r.id==0 && !r.check_revision);
    }
    const char *argv[]={"rpc","3","-1","apply","volume=9","cutoff=8"};
    assert(b_protocol_parse_argv(6,argv,&r)==B_OK && r.patch.values.volume==9);
    const char *bad_argv[]={"rpc","3","-1","get "};
    assert(b_protocol_parse_argv(4,bad_argv,&r)==B_BAD_REQUEST);
    puts("PASS RPC uint32 limits, bounded ASCII, duplicate/unknown keys, arrays and legacy commands");
}

static void test_storage(void)
{
    b_config_t c,decoded,baseline;b_config_defaults(&c);baseline=c;
    c.channel=16;c.transpose=-24;c.fine=-100;c.velocity[2]=127;c.velocity[3]=1;
    uint8_t blob[B_CONFIG_BLOB_SIZE];memset(blob,0xa5,sizeof(blob));
    assert(b_config_encode(&c,blob,sizeof(blob)-1)==0);
    for(unsigned i=0;i<sizeof(blob);i++) assert(blob[i]==0xa5);
    assert(b_config_encode(&c,blob,sizeof(blob))==sizeof(blob));
    b_config_t bad=c;bad.volume=128;
    uint8_t preserved[B_CONFIG_BLOB_SIZE];memcpy(preserved,blob,sizeof(blob));
    assert(b_config_encode(&bad,blob,sizeof(blob))==0 && memcmp(blob,preserved,sizeof(blob))==0);
    bool migrated=true;
    assert(b_config_decode(blob,sizeof(blob),&baseline,&decoded,&migrated));
    assert(!migrated && b_config_equal(&decoded,&c));
    for(unsigned i=0;i<sizeof(blob);i++) {
        uint8_t original=blob[i];blob[i]^=1;decoded=baseline;migrated=true;
        assert(!b_config_decode(blob,sizeof(blob),&baseline,&decoded,&migrated));
        assert(b_config_equal(&decoded,&baseline) && migrated);
        blob[i]=original;
    }
    assert(!b_config_decode(blob,sizeof(blob)-1,&baseline,&decoded,&migrated));
    uint8_t old[B_CONFIG_V2_SIZE]={0};
    const int32_t old_values[]={0x42524832,3,-12,23,90,64,11,0,20,500,16,127,222};
    for(unsigned i=0;i<13;i++) put32(old+4*i,(uint32_t)old_values[i]);
    for(unsigned i=0;i<12;i++) put32(old+52+4*i,UINT32_MAX);
    baseline.channel=9;baseline.notes[0]=80;
    assert(b_config_valid(&baseline));
    assert(b_config_decode(old,sizeof(old),&baseline,&decoded,&migrated));
    assert(migrated && decoded.channel==9 && decoded.notes[0]==80 && decoded.route==3);
    assert(decoded.transpose==-12 && decoded.fine==23 && decoded.volume==90 && decoded.gate==222);
    old[4]=9;decoded=baseline;migrated=false;
    assert(!b_config_decode(old,sizeof(old),&baseline,&decoded,&migrated));
    assert(b_config_equal(&decoded,&baseline) && !migrated);
    puts("PASS NVS v3 size/CRC all-byte corruption and compatible 100-byte v2 migration");
}

static void test_engine(void)
{
    b_config_t c;b_config_defaults(&c);b_engine_t e;
    assert(b_engine_init(&e,&c));
    b_reply_t reply;b_request_t r=request("rpc 1 0 apply volume=90");
    b_engine_command(&e,&r,10,1,&reply);
    assert(reply.ready && reply.code==B_OK && reply.revision==1 && e.model.volume==90);
    r=request("rpc 2 1 apply volume=90");b_engine_command(&e,&r,11,2,&reply);
    assert(reply.ready && reply.revision==1);
    r=request("rpc 3 0 apply volume=20");b_engine_command(&e,&r,10,3,&reply);
    assert(reply.code==B_REVISION_CONFLICT && e.config.volume==90);
    r=request("rpc 4 0 get");b_engine_command(&e,&r,13,4,&reply);
    assert(reply.code==B_OK && reply.revision==1 && reply.include_config);
    r=request("rpc 5 -1 strike 1 100");b_engine_command(&e,&r,20,5,&reply);
    assert(reply.code==B_OK && e.model.playing_beams==1 && e.model.last_synthetic[0]);
    r=request("rpc 6 1 apply channel=3 volume=80");b_engine_command(&e,&r,100,42,&reply);
    assert(!reply.ready && e.pending && e.lifecycle==B_LIFECYCLE_QUIESCING);
    assert(e.config.channel==2 && e.model.config.channel==1 && !e.model.playing_beams);
    assert(b_engine_tick(&e,101,41,true,&reply)==B_ACTION_NONE && !reply.ready);
    assert(b_engine_tick(&e,102,42,false,&reply)==B_ACTION_NONE && !reply.ready);
    r=request("rpc 7 -1 apply volume=10");b_engine_command(&e,&r,103,43,&reply);
    assert(reply.code==B_BUSY && e.config.volume==90);
    r=request("rpc 8 -1 status");b_engine_command(&e,&r,104,44,&reply);
    assert(reply.code==B_OK && reply.include_state && !reply.include_config);
    const uint8_t ab[4]={3,3,3,3};
    b_engine_sample(&e,1,ab,0,0,200);
    assert(!e.model.sampled);
    assert(b_engine_tick(&e,201,42,true,&reply)==B_ACTION_RECONFIGURE && !reply.ready);
    assert(e.lifecycle==B_LIFECYCLE_RECONFIGURING && !e.gpio_changed);
    assert(b_engine_tick(&e,202,42,true,&reply)==B_ACTION_NONE);
    b_engine_complete(&e,true,&reply);
    assert(reply.ready && reply.id==6 && reply.code==B_OK && reply.revision==2);
    assert(e.config.channel==3 && e.model.config.channel==2 && e.config.volume==80);
    assert(e.lifecycle==B_LIFECYCLE_RUNNING && !e.model.sampled);
    r=request("rpc 9 2 apply pins=8,12,13,-1,-1,-1,-1,-1,-1,-1,-1,-1");
    b_engine_command(&e,&r,300,50,&reply);
    assert(e.gpio_changed && b_engine_tick(&e,301,50,true,&reply)==B_ACTION_RECONFIGURE);
    b_engine_complete(&e,false,&reply);
    assert(reply.code==B_IO_ERROR && e.config.pins[0][0]==-1 && e.revision==2);
    r=request("rpc 10 -1 save");b_engine_command(&e,&r,UINT32_MAX-100u,60,&reply);
    assert(b_engine_tick(&e,398,59,true,&reply)==B_ACTION_NONE && !reply.ready);
    assert(b_engine_tick(&e,399,59,true,&reply)==B_ACTION_NONE && reply.ready);
    assert(reply.code==B_DRAIN_TIMEOUT && e.lifecycle==B_LIFECYCLE_RUNNING && !e.pending);
    r=request("rpc 11 -1 save");b_engine_command(&e,&r,1000,70,&reply);
    assert(b_engine_tick(&e,1001,70,true,&reply)==B_ACTION_SAVE);
    b_engine_complete(&e,false,&reply);
    assert(reply.code==B_STORAGE_ERROR && e.lifecycle==B_LIFECYCLE_RUNNING);
    r=request("rpc 20 -1 save");b_engine_command(&e,&r,1005,71,&reply);
    r=request("rpc 21 -1 panic");b_engine_command(&e,&r,1006,72,&reply);
    assert(reply.ready && reply.code==B_OK);
    assert(b_engine_tick(&e,1007,71,true,&reply)==B_ACTION_NONE && !reply.ready);
    assert(b_engine_tick(&e,1008,72,true,&reply)==B_ACTION_SAVE);
    b_engine_complete(&e,true,&reply);
    assert(reply.id==20 && reply.code==B_OK && e.revision==2);
    b_engine_complete(&e,true,&reply);assert(!reply.ready);
    harp_turn(&e.model,0,1);
    assert(b_engine_sync_controls(&e) && e.config.transpose==1 && e.revision==3);
    assert(!b_engine_sync_controls(&e) && e.revision==3);
    e.revision=UINT32_MAX;r=request("rpc 12 4294967295 apply volume=70");
    b_engine_command(&e,&r,1100,80,&reply);
    assert(reply.code==B_OK && e.revision==0);
    b_state_t state={0};state.links.usb_sent=500;
    b_engine_snapshot(&e,&state);
    assert(state.revision==e.revision && state.controls.volume==70 && state.links.usb_sent==500);
    puts("PASS owner revision/idempotence, exact router barrier, drain wrap timeout, GPIO rollback and storage failure");
}

static void test_sample_revision(void)
{
    b_engine_t e;assert(b_engine_init(&e,NULL));
    uint8_t ab[4]={3,3,3,3};
    b_engine_sample(&e,0,ab,0,1,10);
    ab[0]=2;b_engine_sample(&e,0,ab,0,1,11);
    ab[0]=0;b_engine_sample(&e,0,ab,0,1,12);
    assert(e.model.transpose==1 && e.config.transpose==1 && e.revision==1);
    b_engine_sample(&e,0,ab,1,1,13);
    b_engine_sample(&e,0,ab,1,1,33);
    assert(e.model.fine_cents==-1 && e.config.fine==-1 && e.revision==2);
    b_request_t r=request("rpc 1 1 apply transpose=0 volume=70");b_reply_t reply;
    b_engine_command(&e,&r,33,1,&reply);
    assert(reply.code==B_REVISION_CONFLICT && e.config.transpose==1 && e.config.volume==100);
    puts("PASS physical quadrature/button changes advance revision before same-tick PC mutation");
}

typedef struct { uint32_t channel_id;unsigned note_on; } selected_channel_t;
static bool capture_selected_channel(void *arg,const harp_midi_t *m,uint32_t epoch)
{
    selected_channel_t *seen=arg;uint8_t usb[4],ble[5];(void)epoch;
    assert((m->data[0]&15)==seen->channel_id);
    assert(harp_usb_encode(m,usb) && harp_ble_encode(m,0,ble)==5);
    assert(memcmp(usb+1,m->data,3)==0 && memcmp(ble+2,m->data,3)==0);
    if((m->data[0]&0xf0)==0x90) ++seen->note_on;
    return true;
}

static void test_human_midi_channel_selection(void)
{
    for(uint32_t id=0;id<16;id++) {
        b_engine_t e;assert(b_engine_init(&e,NULL));char line[64];
        snprintf(line,sizeof(line),"harp channel %u",(unsigned)id);
        b_request_t r=request(line);b_reply_t reply;
        b_engine_command(&e,&r,10,42,&reply);
        if(id==1) assert(reply.ready && reply.code==B_OK && e.revision==0);
        else {
            assert(!reply.ready && e.lifecycle==B_LIFECYCLE_QUIESCING && e.config.channel==2);
            assert(b_engine_tick(&e,11,41,true,&reply)==B_ACTION_NONE && !reply.ready);
            assert(b_engine_tick(&e,12,42,false,&reply)==B_ACTION_NONE && !reply.ready);
            assert(b_engine_tick(&e,13,42,true,&reply)==B_ACTION_RECONFIGURE);
            b_engine_complete(&e,true,&reply);assert(reply.ready && reply.code==B_OK);
        }
        assert(e.config.channel==(int32_t)id+1 && e.model.config.channel==id);
        r=request("harp channel");b_engine_command(&e,&r,20,43,&reply);
        char frame[B_PROTOCOL_FRAME_CAP],expected[64];
        assert(reply.include_config && b_protocol_format_reply(frame,sizeof(frame),&reply,&e.config,NULL));
        snprintf(expected,sizeof(expected),"\"midi_channel_id\":%u,",(unsigned)id);
        assert(strstr(frame,expected));
        uint8_t blob[B_CONFIG_BLOB_SIZE];b_config_t decoded;bool migrated;
        assert(b_config_encode(&e.config,blob,sizeof(blob))==sizeof(blob));
        assert(b_config_decode(blob,sizeof(blob),&e.config,&decoded,&migrated) && decoded.channel==(int32_t)id+1);
        assert(harp_debug_strike(&e.model,0,100,30));harp_target_t target;harp_target(&e.model,&target);
        harp_sink_t sink;harp_sink_init(&sink);selected_channel_t seen={.channel_id=id};
        harp_sink_pump(&sink,&target,true,true,1,64,capture_selected_channel,&seen);assert(seen.note_on==1);
    }
    b_request_t query=request("harp get");assert(query.verb==B_VERB_GET);
    const char *invalid[]={"harp channel -1","harp channel 16","harp channel 4294967295",
        "harp channel +1","harp channel x","harp channel 1 2"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)
        assert(b_protocol_parse_line(invalid[i],strlen(invalid[i]),&query)!=B_OK);
    puts("PASS serial MIDI IDs 0..15, query, NVS roundtrip, router drain barrier and USB/BLE output");
}

typedef struct { unsigned velocity, on, off; } shade_midi_t;
static bool capture_shade_midi(void *arg,const harp_midi_t *m,uint32_t epoch)
{
    shade_midi_t *seen=arg;uint8_t usb[4],ble[5];(void)epoch;
    assert((m->data[0]&15)==1 && harp_usb_encode(m,usb));
    assert(harp_ble_encode(m,0,ble)==5);
    assert(memcmp(usb+1,m->data,3)==0 && memcmp(ble+2,m->data,3)==0);
    if((m->data[0]&0xf0)==0x90) {
        assert(m->data[0]==0x91 && m->data[1]>=60 && m->data[1]<=71);
        assert(m->data[2]==seen->velocity);++seen->on;
    } else if((m->data[0]&0xf0)==0x80) {
        assert(m->data[0]==0x81);++seen->off;
    } else if(m->data[0]==0xb1 && m->data[1]==7) assert(m->data[2]==100);
    return true;
}

static void test_shorter_shade_is_louder_by_default(void)
{
    const struct { uint32_t ms;uint8_t velocity; } cases[]={
        {5,127},{20,127},{100,109},{260,72},{500,16},{1000,16}
    };
    const uint8_t ab[4]={3,3,3,3};
    for(unsigned n=0;n<sizeof(cases)/sizeof(*cases);n++) {
        b_engine_t e;assert(b_engine_init(&e,NULL));
        b_engine_sample(&e,0,ab,0,0,0);b_engine_sample(&e,0,ab,0,0,2);
        b_engine_sample(&e,0xfff,ab,0,0,10);b_engine_sample(&e,0xfff,ab,0,0,12);
        harp_target_t target;harp_target(&e.model,&target);
        for(unsigned note=60;note<=71;note++) assert(!target.notes[note]);
        b_engine_sample(&e,0,ab,0,0,10+cases[n].ms);
        b_engine_sample(&e,0,ab,0,0,12+cases[n].ms);
        harp_target(&e.model,&target);
        for(unsigned i=0;i<12;i++) {
            assert(target.notes[60+i] && target.velocity[60+i]==cases[n].velocity);
            assert(e.model.last_duration_ms[i]==cases[n].ms && !e.model.last_synthetic[i]);
        }
        harp_sink_t sink;harp_sink_init(&sink);shade_midi_t seen={.velocity=cases[n].velocity};
        harp_sink_pump(&sink,&target,true,true,1,64,capture_shade_midi,&seen);
        assert(seen.on==12 && seen.off==0);
        b_engine_sample(&e,0,ab,0,0,212+cases[n].ms);harp_target(&e.model,&target);
        harp_sink_pump(&sink,&target,true,true,1,64,capture_shade_midi,&seen);
        assert(seen.off==12 && harp_sink_quiet(&sink));
    }
    puts("PASS shorter shade -> louder default on all 12 beams; clamping, release trigger, 200ms gate and USB/BLE channel ID 1");
}

static void frames(void)
{
    b_config_t c;b_config_defaults(&c);
    b_state_t s={.uptime_ms=UINT32_MAX,.revision=UINT32_MAX,.lifecycle=B_LIFECYCLE_RUNNING,
        .event_count=B_PROTOCOL_MAX_EVENTS};
    for(unsigned i=0;i<12;i++) {s.duration[i]=UINT32_MAX;s.velocity[i]=127;s.notes[i]=i==0?-1:(int32_t)i;s.hits[i]=UINT32_MAX;}
    for(unsigned i=0;i<B_PROTOCOL_MAX_EVENTS;i++) s.events[i]=(b_event_t){UINT32_MAX,UINT32_MAX,12,60000,127,-1,true,UINT32_MAX};
    b_reply_t reply={.ready=true,.include_config=true,.include_state=true,.id=UINT32_MAX,
        .revision=UINT32_MAX,.code=B_OK};
    char frame[B_PROTOCOL_FRAME_CAP];
    size_t n=b_protocol_format_reply(frame,sizeof(frame),&reply,&c,&s);
    assert(n>0 && n==strlen(frame));fputs(frame,stdout);
    for(size_t cap=1;cap<=n;cap++) {
        memset(frame,'x',sizeof(frame));
        assert(b_protocol_format_reply(frame,cap,&reply,&c,&s)==0 && frame[0]=='\0');
    }
    n=b_protocol_format_state(frame,sizeof(frame),&s);
    assert(n>0);fputs(frame,stdout);
    for(size_t cap=1;cap<=n;cap++) {
        assert(b_protocol_format_state(frame,cap,&s)==0 && frame[0]=='\0');
    }
    reply.include_state=false;reply.include_config=false;reply.code=B_DRAIN_TIMEOUT;
    assert(b_protocol_format_reply(frame,sizeof(frame),&reply,NULL,NULL));fputs(frame,stdout);
    s.event_count=B_PROTOCOL_MAX_EVENTS+1;
    assert(b_protocol_format_state(frame,sizeof(frame),&s)==0);
}

int main(int argc,char **argv)
{
    if(argc==2 && strcmp(argv[1],"--frames")==0) { frames();return 0; }
    test_human_midi_channel_selection();
    test_shorter_shade_is_louder_by_default();
    test_config();test_protocol();test_storage();test_engine();test_sample_revision();
    puts("PASS b_control tests (AddressSanitizer + UndefinedBehaviorSanitizer)");
    return 0;
}
