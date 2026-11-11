// Bounded raw-byte ingress; only engine_link calls the SPI service.
#include "raw_midi.h"
#include "spi_regs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

#define PACKET_BYTES 256u
#define PACKETS 4u
#define CONFIRM_US 100000
#define PROBE_US 20000

typedef struct {uint8_t bytes[PACKET_BYTES];unsigned length,offset;int64_t expires;} packet_t;
static struct {packet_t packets[PACKETS];unsigned head,count;uint32_t revision;int64_t lease;bool flush;} streams[4];
static raw_midi_state_t observed;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static bool desired_enable,enable_dirty,flush_all,input_paused;
static uint32_t flush_revision;
static bool stop_raw_done,stop_usb_done;
static unsigned cursor;
static int64_t probe_at,idle_at;
static struct {
    bool active;
    uint8_t op,source,payload,scope;
    uint32_t accept,reject,revision[4],flush_revision,stop_ticket;
    int64_t at;
} flight;

static void update_ready(void)
{
    observed.ready=observed.available && observed.initialized && observed.enabled &&
        !enable_dirty && !flush_all && !observed.stop_pending && !input_paused;
}
// Called with lock held (or by the single SPI owner while already locked).
static bool source_supported(unsigned source)
{
    return source<4 && (source!=MIDI_RAW_SOURCE_SENSOR || !observed.native_sensor_source3);
}

static void clear_stream(unsigned source,unsigned fault)
{
    observed.source[source].dropped_bytes+=observed.source[source].queued_bytes;
    observed.source[source].queued_bytes=0;observed.source[source].session=0;
    if(fault)observed.source[source].fault=fault;
    streams[source].head=streams[source].count=0;streams[source].revision++;
    streams[source].lease=0;
    streams[source].flush=source_supported(source);
    observed.source[source].flushing=streams[source].flush;
}
// FF never implies an explicit USB request on an independent-input FPGA.
static void clear_remote(void)
{
    for(unsigned i=1;i<4;i++)clear_stream(i,0);
    flush_all=true;flush_revision++;observed.ready=false;
    if(observed.stop_pending) {
        stop_raw_done=false;
        if(!observed.independent_usb)stop_usb_done=false;
    }
}
static void failure(unsigned source,unsigned reason)
{
    portENTER_CRITICAL(&lock);
    observed.fault=reason;
    if(observed.stop_pending) {
        observed.stop_fault=reason;
        if(source==MIDI_RAW_SOURCE_USB)stop_usb_done=false;
        else stop_raw_done=false;
    }
    if(source<4)clear_stream(source,reason);
    else {
        clear_remote();
        for(unsigned i=1;i<4;i++)observed.source[i].fault=reason;
        if(observed.stop_pending){clear_stream(MIDI_RAW_SOURCE_USB,reason);stop_usb_done=false;}
        desired_enable=false;enable_dirty=true;
        observed.initialized=false;observed.ready=false;
        if(reason==RAW_MIDI_RESET || reason==RAW_MIDI_TIMEOUT) {
            // Recovery may follow an FPGA image change without an SPI error.
            // Probe its input ownership again before issuing further commands.
            observed.available=false;observed.input_capabilities_known=false;observed.cpu_control=false;
            observed.independent_usb=false;observed.native_sensor_source3=false;
            probe_at=0;
        }
        if(!++observed.reset_count)++observed.reset_count;
    }
    portEXIT_CRITICAL(&lock);
}
void raw_midi_init(void)
{
    memset(streams,0,sizeof(streams));memset(&observed,0,sizeof(observed));memset(&flight,0,sizeof(flight));
    desired_enable=false;enable_dirty=true;flush_all=input_paused=false;flush_revision=0;
    stop_raw_done=stop_usb_done=false;cursor=0;probe_at=idle_at=0;
}
void raw_midi_request_enable(bool enabled)
{
    portENTER_CRITICAL(&lock);
    if(desired_enable!=enabled) {
        desired_enable=enabled;enable_dirty=true;clear_remote();
    }
    portEXIT_CRITICAL(&lock);
}
void raw_midi_pause(bool paused)
{
    portENTER_CRITICAL(&lock);
    if(input_paused!=paused) {
        input_paused=paused;
        // FF includes USB on legacy images. A local mode switch releases only
        // ESP32 sources, without changing ENABLE or an explicit STOP receipt.
        if(paused)for(unsigned i=1;i<4;i++)clear_stream(i,0);
        update_ready();
    }
    portEXIT_CRITICAL(&lock);
}
void raw_midi_bind(uint8_t source,uint64_t session)
{
    if(source<1||source>3||!session)return;
    portENTER_CRITICAL(&lock);
    if(!source_supported(source)){portEXIT_CRITICAL(&lock);return;}
    if(observed.source[source].session!=session) {clear_stream(source,0);observed.source[source].session=session;}
    portEXIT_CRITICAL(&lock);
}
bool raw_midi_claim(uint8_t source,uint64_t session)
{
    if(source<1||source>3||!session)return false;
    portENTER_CRITICAL(&lock);
    bool ok=source_supported(source)&&observed.ready&&(!observed.source[source].session||observed.source[source].session==session);
    if(ok&&!observed.source[source].session){clear_stream(source,0);observed.source[source].session=session;}
    portEXIT_CRITICAL(&lock);return ok;
}
bool raw_midi_renew(uint8_t source,uint64_t session,int64_t expires)
{
    if(source<1||source>3||!session)return false;
    portENTER_CRITICAL(&lock);
    bool ok=source_supported(source)&&observed.ready&&observed.source[source].session==session;
    if(ok)streams[source].lease=expires;
    portEXIT_CRITICAL(&lock);return ok;
}
void raw_midi_flush(uint8_t source)
{
    if(source>3&&source!=MIDI_RAW_SOURCE_ALL)return;
    portENTER_CRITICAL(&lock);
    if(source==MIDI_RAW_SOURCE_ALL)clear_remote();
    else if(source_supported(source))clear_stream(source,0);
    portEXIT_CRITICAL(&lock);
}
uint32_t raw_midi_request_stop_all(void)
{
    portENTER_CRITICAL(&lock);
    if(!++observed.stop_requested)++observed.stop_requested;
    observed.stop_pending=true;observed.stop_fault=RAW_MIDI_OK;
    stop_raw_done=stop_usb_done=false;
    clear_remote();clear_stream(MIDI_RAW_SOURCE_USB,0);
    uint32_t ticket=observed.stop_requested;
    portEXIT_CRITICAL(&lock);return ticket;
}
bool raw_midi_submit(uint8_t source,uint64_t session,const uint8_t *bytes,size_t length,int64_t expires)
{
    if(source<1||source>3||!session||!bytes||!length||length>PACKET_BYTES*PACKETS)return false;
    portENTER_CRITICAL(&lock);
    if(!source_supported(source)||!observed.ready||observed.source[source].session!=session) {portEXIT_CRITICAL(&lock);return false;}
    unsigned needed=(unsigned)((length+PACKET_BYTES-1)/PACKET_BYTES);
    if(needed>PACKETS-streams[source].count) {
        clear_stream(source,RAW_MIDI_OVERFLOW);observed.source[source].dropped_bytes+=(uint32_t)length;
        portEXIT_CRITICAL(&lock);return false;
    }
    size_t used=0;
    while(used<length) {
        packet_t *p=&streams[source].packets[(streams[source].head+streams[source].count)%PACKETS];
        unsigned n=(unsigned)(length-used);if(n>PACKET_BYTES)n=PACKET_BYTES;
        memcpy(p->bytes,bytes+used,n);p->length=n;p->offset=0;p->expires=expires;
        streams[source].count++;used+=n;
    }
    observed.source[source].queued_bytes+=(uint32_t)length;
    streams[source].lease=expires;
    portEXIT_CRITICAL(&lock);return true;
}
void raw_midi_state(raw_midi_state_t *out)
{
    portENTER_CRITICAL(&lock);*out=observed;portEXIT_CRITICAL(&lock);
}
static bool read_status(uint32_t words[5])
{
    if(fpga_word_read_burst(MIDI_RAW_REG_STATUS,words,5)) {
        portENTER_CRITICAL(&lock);
        observed.enabled=(words[0]&MIDI_RAW_STATUS_ENABLED)!=0;
        observed.accepted=words[1];observed.rejected=words[2];observed.events=words[3];observed.usb_status=words[4];
        update_ready();
        portEXIT_CRITICAL(&lock);return true;
    }
    // A lost link read cannot prove that any cached FPGA session survived.
    failure(MIDI_RAW_SOURCE_ALL,RAW_MIDI_IO);
    portENTER_CRITICAL(&lock);observed.available=false;observed.input_capabilities_known=false;observed.cpu_control=false;
    observed.independent_usb=false;observed.native_sensor_source3=false;observed.initialized=false;observed.ready=false;observed.inflight=false;portEXIT_CRITICAL(&lock);
    flight.active=false;probe_at=esp_timer_get_time()+PROBE_US;return false;
}
void raw_midi_service(int64_t deadline)
{
    while(esp_timer_get_time()<deadline) {
        int64_t now=esp_timer_get_time();
        if(!observed.available) {
            if(now<probe_at)return;
            uint32_t magic=0,capabilities=0;probe_at=now+PROBE_US;
            bool valid=fpga_word_read_burst(MIDI_RAW_REG_MAGIC,&magic,1) && magic==MIDI_RAW_ABI_MAGIC;
            bool known=valid && fpga_word_read_burst(MIDI_RAW_REG_INPUT_CAPABILITIES,&capabilities,1);
            if(known && CONFIG_FPGA_AE350_CONTROL)known=(capabilities&AE350_CPU_CAP) && ae350_control_identity();
            if(!known) {
                // MID1 was identified even if its extension read failed. The
                // persistent IO/reset fence keeps engine_link from treating
                // this as an unseen legacy device and sending static writes.
                if(valid)failure(MIDI_RAW_SOURCE_ALL,RAW_MIDI_IO);
                portENTER_CRITICAL(&lock);
                observed.fault=valid?RAW_MIDI_IO:RAW_MIDI_UNAVAILABLE;observed.ready=false;
                observed.input_capabilities_known=false;observed.cpu_control=false;observed.independent_usb=false;observed.native_sensor_source3=false;
                if(observed.stop_pending)observed.stop_fault=observed.fault;
                portEXIT_CRITICAL(&lock);return;
            }
            portENTER_CRITICAL(&lock);observed.available=true;observed.fault=0;
            observed.input_capabilities_known=true;
            observed.cpu_control=(capabilities&AE350_CPU_CAP)!=0;
            observed.independent_usb=(capabilities&MIDI_RAW_CAP_INDEPENDENT_USB)!=0;
            observed.native_sensor_source3=(capabilities&MIDI_RAW_CAP_NATIVE_SENSOR3)!=0;
            if(observed.native_sensor_source3)clear_stream(MIDI_RAW_SOURCE_SENSOR,0);
            portEXIT_CRITICAL(&lock);
        }
        portENTER_CRITICAL(&lock);
        bool work=flight.active||enable_dirty||flush_all;
        for(unsigned i=1;i<4;i++) {
            if(streams[i].lease&&now>=streams[i].lease)clear_stream(i,RAW_MIDI_EXPIRED);
            work|=streams[i].flush||streams[i].count;
        }
        work|=streams[0].flush;
        observed.inflight=flight.active;
        portEXIT_CRITICAL(&lock);
        if(!work&&now<idle_at)return;
        uint32_t words[5];if(!read_status(words))return;idle_at=now+PROBE_US;
        if(flight.active) {
            if((uint32_t)(words[2]-flight.reject)!=0) {
                unsigned source=flight.source;flight.active=false;failure(source,RAW_MIDI_REJECTED);continue;
            }
            uint32_t delta=words[1]-flight.accept;
            if(!(words[0]&MIDI_RAW_STATUS_BUSY)&&delta==1) {
                portENTER_CRITICAL(&lock);
                if(flight.op==MIDI_RAW_OP_ENABLE) {
                    observed.initialized=true;
                    if(!flight.payload)observed.fault=RAW_MIDI_OK;
                    if(desired_enable==(flight.payload!=0))enable_dirty=false;
                    if(observed.enabled!=(flight.payload!=0)){portEXIT_CRITICAL(&lock);flight.active=false;failure(MIDI_RAW_SOURCE_ALL,RAW_MIDI_RESET);continue;}
                } else if(flight.op==MIDI_RAW_OP_FLUSH) {
                    if(flight.source==MIDI_RAW_SOURCE_ALL) {
                        if(flush_revision==flight.flush_revision)flush_all=false;
                    }
                    for(unsigned i=0;i<4;i++)if((flight.scope&(1u<<i)) && streams[i].revision==flight.revision[i]) {
                        streams[i].flush=false;observed.source[i].flushing=false;observed.source[i].flushes++;
                    }
                    if(observed.stop_pending && flight.stop_ticket==observed.stop_requested) {
                        if(flight.source==MIDI_RAW_SOURCE_ALL && flush_revision==flight.flush_revision)
                            stop_raw_done=true;
                        if((flight.scope&1u) && streams[0].revision==flight.revision[0])stop_usb_done=true;
                        if(stop_raw_done && stop_usb_done) {
                            observed.stop_completed=observed.stop_requested;
                            observed.stop_pending=false;observed.stop_fault=RAW_MIDI_OK;
                        }
                    }
                } else observed.source[flight.source].sent_bytes++;
                update_ready();
                observed.inflight=false;portEXIT_CRITICAL(&lock);flight.active=false;continue;
            }
            if(delta>1||now-flight.at>=CONFIRM_US) {
                unsigned source=flight.source;flight.active=false;failure(source,delta>1?RAW_MIDI_RESET:RAW_MIDI_TIMEOUT);continue;
            }
            return;
        }
        if(words[0]&MIDI_RAW_STATUS_BUSY)return;
        portENTER_CRITICAL(&lock);
        if(!enable_dirty&&desired_enable&&!observed.enabled) {
            portEXIT_CRITICAL(&lock);failure(MIDI_RAW_SOURCE_ALL,RAW_MIDI_RESET);continue;
        }
        uint8_t op=0,source=0,payload=0;
        if(enable_dirty){op=MIDI_RAW_OP_ENABLE;source=MIDI_RAW_SOURCE_ALL;payload=desired_enable;}
        else if(flush_all){op=MIDI_RAW_OP_FLUSH;source=MIDI_RAW_SOURCE_ALL;}
        else {
            for(unsigned i=0;i<4;i++)if(source_supported(i)&&streams[i].flush){op=MIDI_RAW_OP_FLUSH;source=(uint8_t)i;break;}
            if(!op&&observed.ready)for(unsigned n=0;n<3;n++) {
                source=(uint8_t)(1+cursor++%3);if(!source_supported(source)||!streams[source].count)continue;
                packet_t *p=&streams[source].packets[streams[source].head];
                if(p->expires&&now>=p->expires){clear_stream(source,RAW_MIDI_EXPIRED);continue;}
                op=MIDI_RAW_OP_BYTE;payload=p->bytes[p->offset++];observed.source[source].queued_bytes--;
                if(p->offset==p->length){streams[source].head=(streams[source].head+1)%PACKETS;streams[source].count--;}
                break;
            }
        }
        if(op) {
            flight.active=true;flight.op=op;flight.source=source;flight.payload=payload;
            for(unsigned i=0;i<4;i++)flight.revision[i]=streams[i].revision;
            unsigned raw_scope=observed.native_sensor_source3?6u:14u;
            flight.scope=source==MIDI_RAW_SOURCE_ALL?(raw_scope|(observed.independent_usb?0u:1u)):(1u<<source);
            flight.flush_revision=flush_revision;
            flight.stop_ticket=observed.stop_pending?observed.stop_requested:0;
            flight.accept=words[1];flight.reject=words[2];flight.at=now;observed.inflight=true;
        }
        portEXIT_CRITICAL(&lock);
        if(!op)return;
        fpga_word_write(MIDI_RAW_REG_COMMAND,midi_raw_command(op,source,payload));
    }
}
