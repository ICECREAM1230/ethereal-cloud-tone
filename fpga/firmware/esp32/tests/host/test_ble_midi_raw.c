/* Compiles the actual ble_midi.c ingress region; radio/scheduler are fakes.
 * test_ble_midi_raw.py supplies the unchanged production region as an include.
 */
#include <assert.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "event_bus.h"
#include "raw_midi.h"
#include "ble_midi_unframer.h"

static unsigned critical_depth;
#undef portENTER_CRITICAL
#undef portEXIT_CRITICAL
#define portENTER_CRITICAL(p) do {(void)(p);assert(critical_depth==0);critical_depth++;} while(0)
#define portEXIT_CRITICAL(p) do {(void)(p);assert(critical_depth==1);critical_depth--;} while(0)
#define TAG "ble_midi"
#define BLE_MIDI_MAX_PKT 517
#define BLE_GATT_ACCESS_OP_WRITE_CHR 2
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 13
#define RINGBUF_TYPE_NOSPLIT 0
struct os_mbuf {const uint8_t *data; uint16_t length;};
struct ble_gatt_access_ctxt {unsigned op;struct os_mbuf *om;};
#define OS_MBUF_PKTLEN(om) ((om)->length)
static bool copy_fails;
static int os_mbuf_copydata(struct os_mbuf *om,unsigned start,unsigned n,void *out)
{if(copy_fails)return -1;memcpy(out,om->data+start,n);return 0;}
typedef void *RingbufHandle_t;
static struct {uint8_t data[1024];size_t length;} ring[16];
static unsigned head,tail;
static bool ring_full;
static jmp_buf drained;
static int64_t clock_us;
int64_t esp_timer_get_time(void){return ++clock_us;}
void vTaskDelay(TickType_t ticks){(void)ticks;}
static BaseType_t xRingbufferSend(RingbufHandle_t rb,const void *p,size_t n,TickType_t wait)
{(void)rb;(void)wait;if(ring_full)return pdFALSE;assert(tail<16 && n<=1024);memcpy(ring[tail].data,p,n);ring[tail++].length=n;return pdTRUE;}
static void *xRingbufferReceive(RingbufHandle_t rb,size_t *n,TickType_t wait)
{(void)rb;(void)wait;if(head==tail)longjmp(drained,1);*n=ring[head].length;return ring[head++].data;}
static void vRingbufferReturnItem(RingbufHandle_t rb,void *p){(void)rb;(void)p;}
static unsigned bind_count,flush_count,submit_count,events;
static bool raw_available=true;
static uint64_t session;
static uint8_t delivered[2048];
static size_t delivered_n;
void raw_midi_bind(uint8_t source,uint64_t next)
{assert(critical_depth==1);assert(source==2 && next);session=next;bind_count++;}
void raw_midi_flush(uint8_t source)
{assert(critical_depth==1);assert(source==2);session=0;flush_count++;}
bool raw_midi_submit(uint8_t source,uint64_t wanted,const uint8_t *bytes,size_t n,int64_t expires)
{assert(critical_depth==1);assert(source==2 && wanted==session && expires==0 && n<=1024);submit_count++;if(!raw_available)return false;assert(delivered_n+n<=sizeof(delivered));memcpy(delivered+delivered_n,bytes,n);delivered_n+=n;return true;}
void event_bus_publish(const evt_t *event){(void)event;events++;}
#include "ble_midi_raw_under_test.inc"

static void drain(void){if(setjmp(drained)==0)ble_rx_task(NULL);head=tail=0;}
static int send_packet(const uint8_t *bytes,size_t n)
{struct os_mbuf om={bytes,(uint16_t)n};struct ble_gatt_access_ctxt c={BLE_GATT_ACCESS_OP_WRITE_CHR,&om};return gatt_svr_chr_access(0,0,&c,NULL);}
static void fixture(void)
{
 head=tail=0;ring_full=copy_fails=false;raw_available=true;
 bind_count=flush_count=submit_count=events=0;session=0;delivered_n=0;
 s_ble_epoch=1;s_ble_live=false;s_bound_epoch=s_unframer_epoch=0;
 s_rx_drops=0;ble_midi_unframer_init(&s_unframer);
 ble_raw_set_live(true);
 assert(flush_count==1 && !bind_count && !submit_count);
}
#define SEND(...) do {const uint8_t bytes[]={__VA_ARGS__};assert(send_packet(bytes,sizeof(bytes))==0);} while(0)
static void raw_bytes_and_callback_boundary(void)
{
 fixture();
 SEND(0x80,0x80,0x90,60,0,61,1,0x81,0xf8,0x82,62,2,0x83,0xf1,3,0x84,63,4);
 assert(!bind_count && !submit_count && !events); /* Callback only enqueues. */
 drain();
 const uint8_t expected[]={0x90,60,0,61,1,0xf8,62,2,0xf1,3,0x90,63,4};
 assert(bind_count==1 && submit_count==1 && delivered_n==sizeof(expected));
 assert(!memcmp(delivered,expected,sizeof(expected)) && !events);
 SEND(0x80,0x80,0xf0,0x7d,1);drain();
 SEND(0x80,2,0xf7,0xf8,3,0x81,0xf7);drain();
 const uint8_t sysex[]={0xf0,0x7d,1,2,0xf8,3,0xf7};
 assert(delivered_n==sizeof(expected)+sizeof(sysex));
 assert(!memcmp(delivered+sizeof(expected),sysex,sizeof(sysex)) && bind_count==1);
}
static void disconnect_and_old_epochs(void)
{
 fixture();uint64_t old=ble_raw_snapshot();
 SEND(0x80,0x80,0x90,60,127);
 ble_raw_set_live(false);assert(!ble_raw_snapshot());
 ble_raw_set_live(true);assert(ble_raw_snapshot()!=old);
 drain();assert(!submit_count && !bind_count && !delivered_n);
 SEND(0x80,0x80,0x90,64,0);drain();
 assert(bind_count==1 && submit_count==1 && delivered[1]==64 && delivered[2]==0);
 /* A late failure from an old RX epoch cannot flush the new stream. */
 unsigned before=flush_count;ble_raw_loss(old);assert(flush_count==before);
 ble_raw_set_live(false);SEND(0x80,0x80,0x90,65,1);drain();
 assert(submit_count==1 && !events);
}
static void overflow_and_submit_failure(void)
{
 fixture();uint64_t old=ble_raw_snapshot();
 SEND(0x80,0x80,0x90,60,127);
 ring_full=true;SEND(0x80,0x80,0x80,60,0);
 assert(ble_raw_snapshot()!=old);ring_full=false;drain();
 assert(!submit_count && !delivered_n && !bind_count && flush_count==2);
 SEND(0x80,0x80,0x90,62,1);drain();assert(delivered[1]==62);
 fixture();raw_available=false;old=ble_raw_snapshot();
 SEND(0x80,0x80,0x90,60,127);SEND(0x80,0x80,0x80,60,0);drain();
 assert(submit_count==1 && !delivered_n && ble_raw_snapshot()!=old);
 raw_available=true;drain();assert(submit_count==1); /* No replay on ready. */
 SEND(0x80,0x80,0x90,64,0);drain();
 assert(submit_count==2 && bind_count==2 && delivered_n==3 && delivered[1]==64);
 assert(!events);
}
static void malformed_flush_and_reset(void)
{
 fixture();uint64_t old=ble_raw_snapshot();
 SEND(0x80,0x80,0xf0,1);drain();
 /* Invalid status in SysEx invalidates the following old-epoch Note On. */
 SEND(0x80,2,0x81,0x90,1,2);SEND(0x80,0x80,0x90,60,127);drain();
 assert(submit_count==1 && ble_raw_snapshot()!=old && flush_count==2);
 SEND(0x80,2,3);drain();assert(submit_count==1); /* SysEx context was reset. */
 SEND(0x80,0x80,0x90,64,0);drain();assert(submit_count==2 && delivered_n==5);
 fixture();SEND(0x80,0x80,0x90,60,127);
 const uint8_t short_packet[]={0x80};
 assert(send_packet(short_packet,sizeof(short_packet))==BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
 drain();assert(!submit_count && flush_count==2);
 fixture();copy_fails=true;
 const uint8_t valid[]={0x80,0x80,0x90,60,1};
 assert(send_packet(valid,sizeof(valid))==BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
 assert(!tail && flush_count==2 && !events);
 fixture();s_ble_epoch=UINT32_MAX;ble_raw_set_live(true);
 SEND(0x80,0x80,0x90,60,0);drain();
 assert(session==UINT64_C(0x100000000) && submit_count==1);
}
int main(void)
{
 raw_bytes_and_callback_boundary();
 disconnect_and_old_epochs();
 overflow_and_submit_failure();
 malformed_flush_and_reset();
 puts("BLE raw ingress: preserved bytes/SysEx; callback enqueue; source-only loss/epoch/reset; no old replay; uint64 sessions PASS");
 return 0;
}
