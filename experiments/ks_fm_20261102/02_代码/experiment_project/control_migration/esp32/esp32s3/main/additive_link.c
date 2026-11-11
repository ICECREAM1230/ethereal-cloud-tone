// Copyright (c) 2026 ICECREAM1230
/* Additive SPI mailbox owner. Layout/ACK semantics:
 * additive/rtl/additive_control_bridge.sv, MIT, 2026-10-19.
 * 0709 is {busy,31-bit sequence_spi}; legacy presence requires a fresh disabled
 * COMMIT ACK. ENG1 local completion proves mode only, never patch identity. */
#include "additive_link.h"
#include "spi_regs.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "raw_midi.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

#define ADDITIVE_STATUS 0x0709u
#define ADDITIVE_COMMIT 0xa0ffu
#define ACK_TIMEOUT_US 100000
#define HEALTH_US 20000
#define POLL_US 1000
#define BURST_WORDS 12u
#define SERVICE_TRANSACTIONS 8u

typedef enum { SEEK, UPLOAD, COMMIT, WAIT_ACK, IDLE } phase_t;
typedef enum { PROBE, USER, DISABLE } kind_t;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static additive_link_state_t observed;
static additive_patch_t pending,active;
static bool pending_valid,pending_selected,initialize_required,disable_requested,capability_seen;
static bool local_frozen;
static bool quarantined,safe_probe;
static uint32_t pending_id,cancel_epoch;
/* Remaining fields have exactly one owner: engine_link's SPI task. */
static phase_t phase;
static kind_t active_kind;
static bool active_selected;
static uint32_t active_id,active_epoch,counter,flight_commit,last_status;
static unsigned cursor;
static int64_t committed_at,next_read,seek_at;

static uint32_t next_nonzero(uint32_t value)
{
    value=(value+1u)&0x7fffffffu;return value?value:1;
}
static uint32_t new_instance(uint32_t old)
{
    uint32_t instance=esp_random();
    if(!instance || instance==old)instance=old+1u;
    return instance?instance:1;
}

void additive_link_init(void)
{
    uint32_t instance=new_instance(observed.instance_id);
    portENTER_CRITICAL(&lock);
    memset(&observed,0,sizeof(observed));observed.instance_id=instance;observed.busy=true;
    pending_valid=false;initialize_required=true;disable_requested=false;capability_seen=local_frozen=false;
    pending_id=cancel_epoch=0;
    quarantined=safe_probe=false;
    portEXIT_CRITICAL(&lock);
    phase=SEEK;active_kind=PROBE;active_selected=false;
    active_id=active_epoch=counter=flight_commit=last_status=0;cursor=0;
    committed_at=next_read=0;seek_at=esp_timer_get_time();
}

bool additive_link_request(const additive_patch_t *patch,bool selected,uint32_t request_id)
{
    if(!request_id || !additive_patch_validate(patch))return false;
    portENTER_CRITICAL(&lock);
    bool accepted=!local_frozen && observed.available && observed.valid && !observed.busy && !pending_valid &&
        (observed.fault==ADDITIVE_LINK_NONE || observed.fault==ADDITIVE_LINK_CANCELLED);
    if(accepted) {
        safe_probe=false;
        pending=*patch;pending_selected=selected;pending_id=request_id;pending_valid=true;
        observed.busy=true;observed.ready=false;observed.fault=ADDITIVE_LINK_NONE;
        observed.completed_request_id=0;
    }
    portEXIT_CRITICAL(&lock);return accepted;
}

void additive_link_cancel(void)
{
    portENTER_CRITICAL(&lock);
    if(!++cancel_epoch)++cancel_epoch;
    pending_valid=false;disable_requested=!local_frozen;
    quarantined=safe_probe=false;
    observed.valid=false;observed.ready=false;observed.busy=true;
    observed.patch_known=false;
    observed.completed_request_id=0;observed.fault=ADDITIVE_LINK_CANCELLED;
    portEXIT_CRITICAL(&lock);
}

void additive_link_invalidate(void)
{
    uint32_t instance=new_instance(observed.instance_id);
    portENTER_CRITICAL(&lock);
    if(!++cancel_epoch)++cancel_epoch;
    pending_valid=false;initialize_required=disable_requested=!local_frozen;
    quarantined=safe_probe=false;
    memset(&observed,0,sizeof(observed));observed.instance_id=instance;
    observed.busy=true;observed.fault=ADDITIVE_LINK_UNAVAILABLE;
    portEXIT_CRITICAL(&lock);
    // A reset notification never grants permission to overwrite a shadow
    // whose COMMIT may still be crossing the bundled-data mailbox.
    if(phase!=WAIT_ACK)phase=SEEK;
    cursor=0;seek_at=esp_timer_get_time();next_read=0;
}

void additive_link_local_fence(bool fault)
{
    uint32_t instance=new_instance(observed.instance_id);
    portENTER_CRITICAL(&lock);
    if(!++cancel_epoch)++cancel_epoch;
    local_frozen=true;pending_valid=initialize_required=disable_requested=false;
    quarantined=safe_probe=false;
    bool available=observed.available,selected=observed.selected;
    memset(&observed,0,sizeof(observed));observed.instance_id=instance;
    observed.available=available;observed.selected=selected;observed.busy=true;
    observed.fault=fault?ADDITIVE_LINK_CANCELLED:ADDITIVE_LINK_NONE;
    portEXIT_CRITICAL(&lock);
    if(phase!=WAIT_ACK)phase=IDLE;
    cursor=0;next_read=0;
}

void additive_link_adopt_local(bool available,bool selected)
{
    portENTER_CRITICAL(&lock);
    if(!++cancel_epoch)++cancel_epoch;
    local_frozen=true;pending_valid=initialize_required=disable_requested=false;
    quarantined=safe_probe=false;
    capability_seen=available;
    observed.available=observed.valid=observed.ready=available;
    observed.selected=available && selected;observed.busy=phase==WAIT_ACK;
    observed.fault=ADDITIVE_LINK_NONE;observed.patch_known=false;
    observed.revision=observed.commit_id=observed.completed_request_id=0;
    memset(&observed.confirmed_patch,0,sizeof(observed.confirmed_patch));
    portEXIT_CRITICAL(&lock);
    if(phase!=WAIT_ACK)phase=IDLE;
    cursor=0;
}

void additive_link_remote_resume(void)
{
    portENTER_CRITICAL(&lock);
    if(!local_frozen){portEXIT_CRITICAL(&lock);return;}
    if(!++cancel_epoch)++cancel_epoch;
    local_frozen=false;pending_valid=disable_requested=false;initialize_required=true;
    quarantined=safe_probe=false;
    observed.available=observed.valid=observed.ready=observed.patch_known=false;
    observed.busy=true;observed.fault=ADDITIVE_LINK_NONE;
    observed.revision=observed.commit_id=observed.completed_request_id=0;
    memset(&observed.confirmed_patch,0,sizeof(observed.confirmed_patch));
    portEXIT_CRITICAL(&lock);
    if(phase!=WAIT_ACK)phase=SEEK;
    cursor=0;seek_at=esp_timer_get_time();next_read=0;
}

void additive_link_state(additive_link_state_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&lock);*out=observed;portEXIT_CRITICAL(&lock);
}

void additive_link_status(additive_link_status_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&lock);
    *out=(additive_link_status_t){
        .available=observed.available,.valid=observed.valid,
        .selected=observed.selected,.ready=observed.ready,.busy=observed.busy,.patch_known=observed.patch_known,
        .fault=observed.fault,.instance_id=observed.instance_id,
        .revision=observed.revision,.commit_id=observed.commit_id,
        .completed_request_id=observed.completed_request_id,
    };
    portEXIT_CRITICAL(&lock);
}

static bool inactive_idle_locked(void)
{
    return !local_frozen && phase==IDLE && capability_seen && last_status &&
        !active_selected && !observed.selected && !flight_commit && active_epoch==cancel_epoch &&
        !pending_valid && !initialize_required && !disable_requested && (observed.valid || quarantined);
}
static bool quarantine(unsigned fault)
{
    portENTER_CRITICAL(&lock);bool eligible=inactive_idle_locked();portEXIT_CRITICAL(&lock);
    if(!eligible)return false;
    uint32_t magic;
    if(!fpga_word_read_burst(MIDI_RAW_REG_MAGIC,&magic,1) || magic!=MIDI_RAW_ABI_MAGIC)return false;
    portENTER_CRITICAL(&lock);
    eligible=inactive_idle_locked();
    if(eligible) {
        // No transaction can activate this backend: the last matched ACK
        // disabled it, with no later shadow/COMMIT or local owner change.
        quarantined=true;observed.available=observed.valid=observed.ready=observed.patch_known=false;
        observed.busy=false;observed.completed_request_id=0;observed.fault=fault;
    }
    portEXIT_CRITICAL(&lock);
    if(eligible)next_read=esp_timer_get_time()+HEALTH_US;
    return eligible;
}
static void io_fault(void)
{
    if(quarantine(ADDITIVE_LINK_IO))return;
    portENTER_CRITICAL(&lock);
    quarantined=safe_probe=false;
    observed.available=false;observed.valid=false;observed.ready=false;observed.patch_known=false;
    observed.busy=capability_seen;observed.fault=ADDITIVE_LINK_IO;
    portEXIT_CRITICAL(&lock);
    next_read=esp_timer_get_time()+HEALTH_US;
}

static bool status_read(uint32_t *status)
{
    // The SPI primitive verifies response ID 0xA5, unlike unchecked read().
    if(fpga_word_read_burst(ADDITIVE_STATUS,status,1))return true;
    io_fault();return false;
}

static void waiting_fault(int64_t now)
{
    if(now-committed_at<ACK_TIMEOUT_US)return;
    portENTER_CRITICAL(&lock);
    observed.valid=false;observed.ready=false;observed.patch_known=false;
    if(active_kind==PROBE && (!capability_seen || safe_probe)) {
        // A disabled probe cannot activate a backend already proved silent.
        // Keep its unknown COMMIT read-only, without blocking healthy modes.
        observed.available=false;observed.busy=false;observed.fault=ADDITIVE_LINK_UNAVAILABLE;
    } else {
        observed.busy=true;
        observed.fault=active_epoch==cancel_epoch?ADDITIVE_LINK_TIMEOUT:ADDITIVE_LINK_CANCELLED;
    }
    portEXIT_CRITICAL(&lock);
    next_read=esp_timer_get_time()+HEALTH_US;
}

static void complete(void)
{
    last_status=flight_commit;flight_commit=0;
    portENTER_CRITICAL(&lock);
    quarantined=safe_probe=false;
    if(active_epoch!=cancel_epoch) {
        // Late acknowledgement is a transport fence only. It cannot publish
        // a cancelled request, restore its selected mode, or restore a cache
        // from a previous instance. The next complete image disables it.
        observed.completed_request_id=0;observed.valid=false;observed.ready=false;observed.patch_known=false;
        observed.busy=true;phase=SEEK;
    } else {
        capability_seen=true;observed.available=true;observed.valid=true;observed.patch_known=true;
        observed.selected=active_selected;observed.confirmed_patch=active;
        observed.commit_id=last_status;
        if(!++observed.revision)++observed.revision;
        observed.completed_request_id=active_kind==USER?active_id:0;
        observed.fault=active_kind==DISABLE?ADDITIVE_LINK_CANCELLED:ADDITIVE_LINK_NONE;
        observed.busy=false;observed.ready=true;phase=IDLE;
        if(active_kind==PROBE)initialize_required=false;
    }
    portEXIT_CRITICAL(&lock);
    seek_at=esp_timer_get_time();next_read=phase==IDLE?seek_at+HEALTH_US:0;
}

void additive_link_service(int64_t deadline_us,bool disabled_and_flushed)
{
    for(unsigned transactions=0;transactions<SERVICE_TRANSACTIONS;transactions++) {
        int64_t now=esp_timer_get_time();if(now>=deadline_us)return;
        portENTER_CRITICAL(&lock);
        bool cancelled=active_epoch!=cancel_epoch;
        bool work=initialize_required || disable_requested || pending_valid;
        bool frozen=local_frozen;
        portEXIT_CRITICAL(&lock);
        if(frozen) {
            // The local controller owns mode/patch. A previous COMMIT can
            // retire its transport fence, but cannot restore a host result.
            if(phase!=WAIT_ACK || now<next_read)return;
            uint32_t status;if(!status_read(&status))return;
            if(!(status&0x80000000u) && status==flight_commit) {
                last_status=flight_commit;flight_commit=0;phase=IDLE;
                portENTER_CRITICAL(&lock);
                observed.completed_request_id=0;
                if(observed.valid)observed.busy=false;
                portEXIT_CRITICAL(&lock);
            }
            next_read=esp_timer_get_time()+POLL_US;return;
        }
        if((phase==UPLOAD || phase==COMMIT) && cancelled) {
            // Incomplete shadow is disposable, but only before COMMIT.
            phase=SEEK;cursor=0;next_read=0;seek_at=now;
        }
        if(phase==IDLE && work) {phase=SEEK;next_read=0;seek_at=now;}
        if(phase==IDLE) {
            if(now<next_read)return;
            uint32_t status;
            if(!status_read(&status)) {
                if(!quarantined) {additive_link_invalidate();io_fault();}
                return;
            }
            next_read=esp_timer_get_time()+HEALTH_US;
            if(quarantined) {
                if(!(status&0x80000000u) && status==last_status) {
                    portENTER_CRITICAL(&lock);
                    if(inactive_idle_locked()) {
                        quarantined=false;observed.available=observed.valid=observed.ready=observed.patch_known=true;
                        observed.busy=false;observed.fault=ADDITIVE_LINK_NONE;observed.completed_request_id=0;
                    }
                    portEXIT_CRITICAL(&lock);
                } else if(status) {
                    // An unknown nonzero sequence/BUSY is a new ownership
                    // uncertainty, not evidence that the old disabled ACK holds.
                    additive_link_invalidate();
                } else if(disabled_and_flushed) {
                    // Reprobe only inside an already established global stop.
                    // A lost disabled-probe ACK retains its own transport
                    // fence; it cannot select this previously silent backend.
                    additive_link_invalidate();safe_probe=true;
                }
                return;
            }
            if((status&0x80000000u) || status!=last_status) {
                // The bridge resets its sequence and selected bit together.
                // Only that zero, non-BUSY reset can retain the quiet proof.
                if(status || !quarantine(ADDITIVE_LINK_UNAVAILABLE))additive_link_invalidate();
            }
            return;
        }
        if(phase==WAIT_ACK) {
            if(now<next_read)return;
            uint32_t status;if(!status_read(&status))return;
            if(!(status&0x80000000u) && status==flight_commit) {complete();return;}
            next_read=esp_timer_get_time()+POLL_US;waiting_fault(now);return;
        }
        if(!disabled_and_flushed)return;
        if(phase==SEEK) {
            if(!work) {phase=IDLE;return;}
            if(now<next_read)return;
            uint32_t status;if(!status_read(&status))return;
            if(status&0x80000000u) {
                next_read=esp_timer_get_time()+POLL_US;
                if(now-seek_at>=ACK_TIMEOUT_US) {
                    portENTER_CRITICAL(&lock);
                    observed.valid=false;observed.ready=false;observed.busy=capability_seen;
                    observed.fault=capability_seen?ADDITIVE_LINK_TIMEOUT:ADDITIVE_LINK_UNAVAILABLE;
                    portEXIT_CRITICAL(&lock);next_read=esp_timer_get_time()+HEALTH_US;
                }
                return;
            }
            if(!counter)counter=status&0x7fffffffu;
            counter=next_nonzero(counter);
            portENTER_CRITICAL(&lock);
            active_epoch=cancel_epoch;active_id=0;active_selected=false;
            if(initialize_required) {additive_patch_default(&active);active_kind=PROBE;disable_requested=false;}
            else if(disable_requested) {
                if(observed.revision)active=observed.confirmed_patch;else additive_patch_default(&active);
                active_kind=DISABLE;disable_requested=false;
            } else if(pending_valid) {
                active=pending;active_kind=USER;active_selected=pending_selected;active_id=pending_id;pending_valid=false;
            } else {portEXIT_CRITICAL(&lock);return;}
            observed.busy=true;observed.ready=false;
            observed.fault=active_kind==DISABLE?ADDITIVE_LINK_CANCELLED:ADDITIVE_LINK_NONE;
            portEXIT_CRITICAL(&lock);
            cursor=0;phase=UPLOAD;continue;
        }
        if(phase==UPLOAD) {
            unsigned limit=cursor<32?32:ADDITIVE_SHADOW_WORDS;
            unsigned n=limit-cursor;if(n>BURST_WORDS)n=BURST_WORDS;
            uint32_t words[BURST_WORDS];uint16_t address=0,unused;
            for(unsigned i=0;i<n;i++)
                additive_patch_word(&active,active_selected,cursor+i,i?&unused:&address,&words[i]);
            fpga_word_write_burst(address,words,n);cursor+=n;
            if(cursor==ADDITIVE_SHADOW_WORDS)phase=COMMIT;
            continue;
        }
        if(phase==COMMIT) {
            portENTER_CRITICAL(&lock);
            bool current=active_epoch==cancel_epoch;
            if(current) {flight_commit=counter;phase=WAIT_ACK;}
            portEXIT_CRITICAL(&lock);
            if(!current)continue;
            fpga_word_write(ADDITIVE_COMMIT,flight_commit);
            committed_at=esp_timer_get_time();next_read=committed_at+POLL_US;return;
        }
    }
}
