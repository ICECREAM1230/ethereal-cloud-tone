#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "raw_midi.h"
#include "spi_regs.h"

static int64_t now;
int64_t esp_timer_get_time(void) {return now++;}
static uint32_t magic, status, accepted, rejected, events, capabilities, commands[5000];
static unsigned count, reads, capability_reads;
static bool hold, bad_io, lose_ack, bad_capabilities;
bool fpga_word_read_burst(uint16_t addr,uint32_t *out,size_t n)
{
    reads++;now+=5;if(bad_io || (bad_capabilities && addr==MIDI_RAW_REG_INPUT_CAPABILITIES))return false;
    for(size_t i=0;i<n;i++)switch(addr+i) {
    case MIDI_RAW_REG_MAGIC:out[i]=magic;break;
    case MIDI_RAW_REG_STATUS:out[i]=status;break;
    case MIDI_RAW_REG_ACCEPT_COUNT:out[i]=accepted;break;
    case MIDI_RAW_REG_REJECT_COUNT:out[i]=rejected;break;
    case MIDI_RAW_REG_EVENT_COUNT:out[i]=events;break;
    case MIDI_RAW_REG_INPUT_CAPABILITIES:out[i]=capabilities;capability_reads++;break;
    default:out[i]=0;break;
    }
    return true;
}
void fpga_word_write(uint16_t addr,uint32_t value)
{
    assert(addr==MIDI_RAW_REG_COMMAND);assert(count<5000);commands[count++]=value;now+=4;
    if(hold){status|=MIDI_RAW_STATUS_BUSY;return;}
    if(lose_ack)return;
    unsigned op=value>>24;
    if(op==MIDI_RAW_OP_ENABLE)status=(value&1)?MIDI_RAW_STATUS_ENABLED:0;
    accepted++;
}
static void tick(unsigned n) {while(n--){now+=1000;raw_midi_service(now+250);}}
static raw_midi_state_t state(void) {raw_midi_state_t s;raw_midi_state(&s);return s;}
static void fixture(void)
{now=0;magic=MIDI_RAW_ABI_MAGIC;capabilities=1;status=accepted=rejected=events=count=reads=capability_reads=0;hold=bad_io=lose_ack=bad_capabilities=false;raw_midi_init();tick(2);raw_midi_request_enable(true);tick(3);assert(state().ready);}
static void explicit_usb_flush_survives_remote_flush(void)
{
    fixture();unsigned before=count;
    raw_midi_flush(MIDI_RAW_SOURCE_USB);raw_midi_flush(MIDI_RAW_SOURCE_ALL);tick(4);
    unsigned usb_flushes=0;
    for(unsigned i=before;i<count;i++)usb_flushes+=commands[i]==UINT32_C(0x02000000);
    assert(usb_flushes==1 && "RAW FF ACK cannot discard an explicit USB flush");
}
static void acknowledge(void) {status&=~MIDI_RAW_STATUS_BUSY;accepted++;tick(1);}
static void stop_waits_for_both_inputs(void)
{
    fixture();assert(state().input_capabilities_known && state().independent_usb);
    hold=true;unsigned before=count;
    uint32_t ticket=raw_midi_request_stop_all();
    assert(ticket && state().stop_pending && !state().ready && !state().stop_completed);
    tick(1);assert(count==before+1 && commands[before]==UINT32_C(0x02ff0000));
    acknowledge();
    assert(count==before+2 && commands[before+1]==UINT32_C(0x02000000));
    assert(state().stop_pending && !state().stop_completed);
    acknowledge();
    assert(state().stop_completed==ticket && !state().stop_pending && !state().stop_fault);
    assert(state().ready && count==before+2);
}
static void old_ack_cannot_complete_new_stop(void)
{
    fixture();hold=true;unsigned before=count;
    uint32_t first=raw_midi_request_stop_all();tick(1);
    uint32_t second=raw_midi_request_stop_all();assert(first!=second);
    acknowledge();
    assert(state().stop_pending && !state().stop_completed);
    assert(commands[before+1]==UINT32_C(0x02ff0000));
    acknowledge();assert(state().stop_pending && !state().stop_completed);
    assert(commands[before+2]==UINT32_C(0x02000000));
    // A new STOP while the old USB barrier is pending needs fresh RAW and USB ACKs.
    uint32_t third=raw_midi_request_stop_all();assert(third!=second);
    acknowledge();assert(state().stop_pending && !state().stop_completed);
    assert(commands[before+3]==UINT32_C(0x02ff0000));
    acknowledge();assert(state().stop_pending && !state().stop_completed);
    assert(commands[before+4]==UINT32_C(0x02000000));
    acknowledge();assert(state().stop_completed==third && !state().stop_pending);
}
static void old_flush_ack_preserves_new_request(void)
{
    fixture();hold=true;unsigned before=count;
    raw_midi_flush(MIDI_RAW_SOURCE_ALL);tick(1);
    raw_midi_flush(MIDI_RAW_SOURCE_ALL);raw_midi_flush(MIDI_RAW_SOURCE_USB);
    acknowledge();
    assert(count==before+2 && commands[before+1]==UINT32_C(0x02ff0000));
    assert(state().source[0].flushing && state().source[1].flushing);
    acknowledge();
    assert(count==before+3 && commands[before+2]==UINT32_C(0x02000000));
    assert(state().source[0].flushing && !state().source[1].flushing);
    raw_midi_flush(MIDI_RAW_SOURCE_USB);acknowledge();
    assert(count==before+4 && state().source[0].flushing);
    acknowledge();assert(!state().source[0].flushing);
}
static void legacy_stop_uses_confirmed_global_flush(void)
{
    fixture();capabilities=0;raw_midi_init();tick(2);raw_midi_request_enable(true);tick(3);
    assert(state().input_capabilities_known && !state().independent_usb && state().ready);
    hold=true;unsigned before=count;uint32_t ticket=raw_midi_request_stop_all();tick(1);
    assert(state().stop_pending && count==before+1 && commands[before]==UINT32_C(0x02ff0000));
    acknowledge();assert(state().stop_completed==ticket && !state().stop_pending);
    assert(count==before+1); // Legacy FF really covers USB as well as RAW.
}
static void failed_stop_never_claims_completion(void)
{
    fixture();hold=true;uint32_t ticket=raw_midi_request_stop_all();tick(1);
    bad_io=true;tick(1);
    assert(state().stop_pending && !state().stop_completed && state().stop_fault==RAW_MIDI_IO);
    assert(!state().available && !state().input_capabilities_known);
    bad_io=hold=false;status&=~MIDI_RAW_STATUS_BUSY;tick(35);
    assert(state().stop_completed==ticket && !state().stop_pending && !state().stop_fault);
    fixture();hold=true;ticket=raw_midi_request_stop_all();tick(1);tick(110);
    assert(state().stop_pending && !state().stop_completed && state().stop_fault==RAW_MIDI_TIMEOUT);
    hold=false;status&=~MIDI_RAW_STATUS_BUSY;tick(8);
    assert(state().stop_completed==ticket && !state().stop_pending);
}
static void discovery_and_recovery_do_not_clear_independent_usb(void)
{
    fixture();unsigned before=count;raw_midi_request_enable(false);tick(3);
    for(unsigned i=before;i<count;i++)assert(commands[i]!=UINT32_C(0x02000000));
    assert(!state().source[0].flushing);
    bad_capabilities=true;raw_midi_init();before=count;tick(2);
    assert(!state().available && !state().input_capabilities_known && count==before);
    assert(state().fault==RAW_MIDI_IO && state().reset_count && !state().initialized);
    bad_capabilities=false;tick(25);assert(state().available && state().independent_usb);
}
static void pause_cancels_only_esp32_sources(void)
{
    fixture();uint8_t note[]={0x90,60,100};
    for(unsigned i=1;i<4;i++)raw_midi_bind((uint8_t)i,100+i);
    tick(3);
    for(unsigned i=1;i<4;i++)assert(raw_midi_submit((uint8_t)i,100+i,note,sizeof(note),0));
    unsigned before=count;raw_midi_state_t prior=state();raw_midi_pause(true);
    assert(count==before && "pause must not execute SPI outside its owner");
    assert(!state().ready && state().enabled && state().initialized && state().available);
    assert(!state().source[0].flushing && !state().stop_pending);
    assert(state().stop_requested==prior.stop_requested && state().stop_completed==prior.stop_completed);
    for(unsigned i=1;i<4;i++) {
        assert(!state().source[i].session && !state().source[i].queued_bytes);
        assert(state().source[i].dropped_bytes==sizeof(note) && state().source[i].flushing);
        assert(!raw_midi_claim((uint8_t)i,200+i));
        assert(!raw_midi_renew((uint8_t)i,100+i,now+10000));
        assert(!raw_midi_submit((uint8_t)i,100+i,note,sizeof(note),0));
    }
    hold=true;tick(1);assert(count==before+1 && commands[before]==UINT32_C(0x02010000));
    raw_midi_pause(true);assert(count==before+1); // Repeated owner polls must not renew releases.
    acknowledge();assert(count==before+2 && commands[before+1]==UINT32_C(0x02020000));
    raw_midi_pause(true);
    acknowledge();assert(count==before+3 && commands[before+2]==UINT32_C(0x02030000));
    acknowledge();assert(count==before+3 && !state().ready && state().enabled);
    for(unsigned i=1;i<4;i++)assert(!state().source[i].flushing);
    assert(!state().stop_requested && !state().stop_completed);
    hold=false;raw_midi_pause(false);assert(count==before+3 && state().ready);tick(3);
    assert(count==before+3 && "resume must not replay a pre-switch NoteOn");
    assert(!raw_midi_submit(1,101,note,sizeof(note),0));
    assert(raw_midi_claim(1,301));tick(2);
    assert(raw_midi_submit(1,301,note,sizeof(note),0));tick(3);
    assert(state().source[1].sent_bytes==sizeof(note));
    // Legacy FF covers USB too: pause must still use the three precise scopes.
    fixture();capabilities=0;raw_midi_init();tick(2);raw_midi_request_enable(true);tick(3);
    assert(!state().independent_usb && state().ready);before=count;
    raw_midi_pause(true);tick(3);assert(count==before+3 && state().enabled && !state().ready);
    for(unsigned i=0;i<3;i++)assert(commands[before+i]==midi_raw_command(MIDI_RAW_OP_FLUSH,(uint8_t)(i+1),0));
}
static void pause_preserves_pending_byte_without_replay(void)
{
    fixture();uint8_t note[]={0x90,60,100};raw_midi_bind(1,101);tick(2);
    hold=true;unsigned before=count;assert(raw_midi_submit(1,101,note,sizeof(note),0));tick(1);
    assert(count==before+1 && commands[before]>>24==MIDI_RAW_OP_BYTE && state().inflight);
    raw_midi_pause(true);raw_midi_pause(false);tick(3);
    assert(count==before+1 && state().inflight && !state().source[1].session && !state().source[1].queued_bytes);
    acknowledge();assert(count==before+2 && commands[before+1]==UINT32_C(0x02010000));
    acknowledge();acknowledge();acknowledge();
    assert(count==before+4 && state().ready && state().source[1].sent_bytes==1);
    for(unsigned i=before+1;i<count;i++)assert(commands[i]>>24==MIDI_RAW_OP_FLUSH);
    // An uncertain BYTE may time out, but no part of its old MIDI packet is retried.
    fixture();raw_midi_bind(1,101);tick(2);before=count;lose_ack=true;
    assert(raw_midi_submit(1,101,note,sizeof(note),0));tick(1);raw_midi_pause(true);tick(110);
    assert(!state().ready && !state().source[1].session && !state().source[1].queued_bytes);
    assert(state().source[1].fault==RAW_MIDI_TIMEOUT);
    lose_ack=false;tick(110);raw_midi_pause(false);tick(3);
    unsigned bytes=0;
    for(unsigned i=before;i<count;i++) {
        bytes+=commands[i]>>24==MIDI_RAW_OP_BYTE;
        assert(commands[i]!=UINT32_C(0x01000000) && commands[i]!=UINT32_C(0x02ff0000) && commands[i]!=UINT32_C(0x02000000));
    }
    assert(bytes==1 && state().ready && !state().stop_requested && !state().stop_completed);
}
static void paused_stop_still_requires_real_scope_acks(void)
{
    fixture();raw_midi_pause(true);hold=true;unsigned before=count;
    uint32_t ticket=raw_midi_request_stop_all();tick(1);
    assert(count==before+1 && commands[before]==UINT32_C(0x02ff0000));
    assert(state().stop_pending && !state().stop_completed && !state().ready);
    acknowledge();
    assert(count==before+2 && commands[before+1]==UINT32_C(0x02000000));
    assert(state().stop_pending && !state().stop_completed && !state().ready);
    raw_midi_pause(true);acknowledge();
    assert(state().stop_completed==ticket && !state().stop_pending && !state().stop_fault);
    assert(!state().ready && state().enabled && count==before+2);
    raw_midi_pause(false);assert(state().ready);
    // Pause has no ENABLE side effect; explicit enable/disable keeps its original meaning.
    fixture();raw_midi_pause(true);raw_midi_request_enable(false);tick(3);
    assert(!state().enabled && !state().ready);raw_midi_pause(false);assert(!state().ready);
    raw_midi_request_enable(true);tick(3);assert(state().ready);
    raw_midi_pause(true);fixture(); // Init must clear the software barrier.
}
static void pause_regressions(void)
{
    pause_cancels_only_esp32_sources();pause_preserves_pending_byte_without_replay();
    paused_stop_still_requires_real_scope_acks();
}
static void native_sensor_source3_is_not_spi_owned(void)
{
    fixture();capabilities=3;raw_midi_init();tick(2);raw_midi_request_enable(true);tick(3);
    assert(state().ready);
    const uint8_t note[]={0x90,60,100};
    raw_midi_bind(3,99);
    assert(!raw_midi_claim(3,99));
    assert(!raw_midi_renew(3,99,now+1000000));
    assert(!raw_midi_submit(3,99,note,sizeof(note),now+1000000));
    unsigned before=count;
    raw_midi_flush(3);raw_midi_pause(true);tick(8);raw_midi_pause(false);tick(3);
    assert(state().ready && !state().source[3].flushing && !state().source[3].session);
    for(unsigned i=before;i<count;i++)assert(((commands[i]>>16)&255)!=3);
    raw_midi_bind(1,100);raw_midi_bind(2,200);
    assert(raw_midi_submit(1,100,note,sizeof(note),now+1000000));
    assert(raw_midi_submit(2,200,note,sizeof(note),now+1000000));tick(8);
    assert(state().source[1].sent_bytes==3 && state().source[2].sent_bytes==3);
}
static void reset_reprobes_native_sensor_capability(void)
{
    fixture();assert(!state().native_sensor_source3);
    unsigned probes=capability_reads,before=count;
    // A newly programmed FPGA may reset between successful SPI reads.
    capabilities=MIDI_RAW_CAP_INDEPENDENT_USB|MIDI_RAW_CAP_NATIVE_SENSOR3;
    status=accepted=rejected=events=0;tick(25);
    assert(state().reset_count && capability_reads>probes);
    assert(state().input_capabilities_known && state().native_sensor_source3);
    raw_midi_request_enable(true);tick(3);assert(state().ready);
    raw_midi_pause(true);tick(4);raw_midi_pause(false);tick(3);
    assert(state().ready && !state().source[3].flushing && !raw_midi_claim(3,99));
    for(unsigned i=before;i<count;i++)assert(((commands[i]>>16)&255)!=3);
    uint32_t ticket=raw_midi_request_stop_all();tick(4);
    assert(state().stop_completed==ticket && !state().stop_pending);
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"flush")){explicit_usb_flush_survives_remote_flush();return 0;}
    if(argc==2 && !strcmp(argv[1],"pause")){pause_regressions();return 0;}
    if(argc==2 && !strcmp(argv[1],"sensor-reset")){reset_reprobes_native_sensor_capability();return 0;}
    fixture();assert(capability_reads && "MID1 discovery must probe independent-input capabilities");
    explicit_usb_flush_survives_remote_flush();
    stop_waits_for_both_inputs();old_ack_cannot_complete_new_stop();
    old_flush_ack_preserves_new_request();legacy_stop_uses_confirmed_global_flush();
    failed_stop_never_claims_completion();discovery_and_recovery_do_not_clear_independent_usb();
    pause_regressions();native_sensor_source3_is_not_spi_owned();reset_reprobes_native_sensor_capability();
    fixture();uint8_t a[]={0x90,60,100,61,0,0xf8},b[]={0xf0,1,2,0xf7};
    raw_midi_bind(1,101);raw_midi_bind(2,202);tick(3);unsigned before=count;
    assert(raw_midi_submit(1,101,a,sizeof(a),0));assert(raw_midi_submit(2,202,b,sizeof(b),0));tick(8);
    assert(state().source[1].sent_bytes==sizeof(a)&&state().source[2].sent_bytes==sizeof(b));
    unsigned ia=0,ib=0;for(unsigned i=before;i<count;i++)if(commands[i]>>24==MIDI_RAW_OP_BYTE) {
        unsigned source=(commands[i]>>16)&255;assert(source==1||source==2);
        assert((commands[i]&255)==(source==1?a[ia++]:b[ib++]));
    }
    assert(ia==sizeof(a)&&ib==sizeof(b));assert(!raw_midi_submit(0,1,a,3,0));
    // An old connection cannot publish after a source-specific flush/rebind.
    raw_midi_flush(2);raw_midi_bind(2,203);assert(!raw_midi_submit(2,202,b,4,0));tick(3);
    assert(raw_midi_submit(2,203,b,4,0));tick(3);assert(state().source[1].session==101);
    fixture();raw_midi_bind(1,1);tick(2);before=count;lose_ack=true;
    assert(raw_midi_submit(1,1,a,3,0));tick(140);
    unsigned bytes=0;for(unsigned i=before;i<count;i++)bytes+=(commands[i]>>24)==MIDI_RAW_OP_BYTE;
    assert(bytes==1);assert(state().source[1].session==0);assert(state().source[1].fault==RAW_MIDI_TIMEOUT);
    fixture();raw_midi_bind(1,1);tick(2);hold=true;assert(raw_midi_submit(1,1,a,3,0));tick(2);before=count;
    tick(60);assert(count==before); // No writes may overwrite a busy mailbox.
    fixture();raw_midi_bind(1,1);tick(2);uint8_t large[1024]={0};
    assert(raw_midi_submit(1,1,large,sizeof(large),0));assert(!raw_midi_submit(1,1,a,3,0));
    assert(state().source[1].session==0&&state().source[1].fault==RAW_MIDI_OVERFLOW);
    fixture();raw_midi_bind(1,1);tick(2);before=count;assert(raw_midi_submit(1,1,a,3,now+1));tick(3);
    for(unsigned i=before;i<count;i++)assert(commands[i]>>24!=MIDI_RAW_OP_BYTE);
    assert(state().source[1].fault==RAW_MIDI_EXPIRED);
    // Distinct browser and snapshot namespaces cannot steal/interleave a stream.
    fixture();uint64_t snapshot=(UINT64_C(1)<<32)|5;
    assert(raw_midi_claim(1,snapshot));assert(raw_midi_claim(1,snapshot));
    assert(!raw_midi_claim(1,5));assert(!raw_midi_submit(1,5,a,3,0));tick(2);
    assert(raw_midi_submit(1,snapshot,a,3,now+5000));tick(2);
    assert(state().source[1].queued_bytes==0); // Lease outlives the byte queue.
    assert(raw_midi_renew(1,snapshot,now+10000));tick(5);
    assert(state().source[1].session==snapshot);tick(10);
    assert(state().source[1].session==0&&state().source[1].fault==RAW_MIDI_EXPIRED);
    assert(raw_midi_claim(1,5));
    // USB-wide barriers may take more than 50ms; service returns between polls.
    fixture();hold=true;raw_midi_flush(MIDI_RAW_SOURCE_ALL);tick(1);before=count;
    tick(60);assert(count==before&&state().inflight&&state().fault==0);
    hold=false;status&=~MIDI_RAW_STATUS_BUSY;accepted++;tick(2);
    assert(!state().inflight&&state().ready);
    // Failed read invalidates ownership and never replays the uncertain byte.
    fixture();raw_midi_bind(1,9);tick(2);hold=true;
    assert(raw_midi_submit(1,9,a,3,0));tick(1);before=count;bad_io=true;tick(1);
    assert(!state().available&&!state().ready&&state().source[1].session==0);
    assert(count==before&&!state().inflight&&!state().initialized&&state().reset_count==1);
    bad_io=hold=false;status&=~MIDI_RAW_STATUS_BUSY;tick(30);
    assert(state().available&&state().initialized&&!state().enabled);
    assert(state().reset_count==1); // Recovery generation survives its ACK.
    // Rejection of a source byte releases only that stream, without retry.
    fixture();raw_midi_bind(1,1);raw_midi_bind(2,2);tick(2);hold=true;
    assert(raw_midi_submit(1,1,a,3,0));tick(1);before=count;rejected++;
    tick(1);assert(state().source[1].session==0&&state().source[1].fault==RAW_MIDI_REJECTED);
    assert(state().source[2].session==2&&state().reset_count==0&&count==before);
    fixture();accepted=UINT32_MAX;raw_midi_bind(1,1);tick(3);assert(state().source[1].flushes>0);
    fixture();magic=0;raw_midi_init();tick(3);assert(!state().available&&!state().ready);
    puts("Raw MIDI: exact bytes/source isolation, flush/epoch, bounded queue, expiry, counter wrap and no uncertain NoteOn retry PASS");
}
