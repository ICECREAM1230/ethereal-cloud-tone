// Copyright (c) 2026 ICECREAM1230
// BLE MIDI parameter feedback. The transport shape adapts the batching idea
// from Thorwegian's upstream FPGA synth commit
// e72d04e93c7ef27d6d7e56a7e38252ca2b2a8030, app/main/midi_out.cpp
// (MIT). Values and readiness fences follow this project's
// three-engine owner contract; no upstream source text is copied here.
#include "midi_feedback.h"
#include "midi_feedback_packet.h"
#include "midi_feedback_guard.h"
#include "parameter_cc.h"
#include "ble_midi.h"
#include "event_bus.h"

#include <inttypes.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define TAG "midi_feedback"
#define FEEDBACK_STACK 3072
#define FEEDBACK_PRIO 3
#define RETRY_MS 5
#define RETRY_LIMIT 40

typedef struct {
    midi_feedback_owner_guard_t owner;
    parameter_cc_state_t controls;
    uint64_t epoch;
    uint64_t sync_epoch;
    uint32_t revision;
} feedback_job_t;

static QueueHandle_t s_queue;
static uint32_t s_dropped;

static bool eligible(const web_control_state_t *s)
{
    // This is the voice owner's coherent applied image after its bounded
    // render queue drains. The current parameter ABI has no per-value FPGA
    // readback, so this fence must not be described as quantized hardware ACK.
    return s->ready && !s->rendering_pending && !s->mode_switching &&
        !s->mode_recovering && s->requested_engine==FM_ENGINE_DDS &&
        s->applied_engine==FM_ENGINE_DDS &&
        !(s->local.supported && (s->local.local_active || s->local.blocked ||
                                 s->local.remote_pending));
}

static bool job_current(const feedback_job_t *job)
{
    // One worker owns this BSS snapshot. Keeping it off the 3 KiB task stack
    // also mirrors voice_alloc's snapshot discipline.
    static web_control_state_t current;
    web_control_read(&current);
    return midi_feedback_owner_state_matches(&job->owner, &current);
}

typedef struct { midi_message_t messages[40]; size_t count; } message_list_t;
static void collect(uint8_t cc,uint8_t value,void *user)
{
    message_list_t *list=user;
    list->messages[list->count++]=(midi_message_t){.status=0xb0,.len=2,.data={cc,value}};
}

static bool notify(const midi_feedback_packet_t *packet,
                   const feedback_job_t *job)
{
    for(unsigned attempt=0;attempt<RETRY_LIMIT;attempt++) {
        if(!job_current(job))return false;
        if(ble_midi_feedback_notify(job->epoch,midi_feedback_packet_bytes(packet),
                                    midi_feedback_packet_length(packet),
                                    &job->owner))return true;
        uint64_t current;size_t mtu;
        if(!ble_midi_feedback_link(&current,&mtu) || current!=job->epoch)return false;
        vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
    }
    return false;
}

static bool send_job(const feedback_job_t *job)
{
    uint64_t epoch;size_t mtu;
    if(!job_current(job) || !ble_midi_feedback_link(&epoch,&mtu) || epoch!=job->epoch)return false;
    message_list_t list={0};
    parameter_cc_dump(&job->owner.patch,&job->controls,collect,&list);
    midi_feedback_packet_t packet;
    uint16_t now=(uint16_t)((esp_timer_get_time()/1000)&0x1fff);
    midi_feedback_packet_begin(&packet,mtu,epoch,now);
    for(size_t i=0;i<list.count;i++) {
        if(!midi_feedback_packet_add(&packet,&list.messages[i],epoch,now)) {
            if(midi_feedback_packet_has_messages(&packet) && !notify(&packet,job)){s_dropped++;return false;}
            now=(uint16_t)((esp_timer_get_time()/1000)&0x1fff);
            midi_feedback_packet_begin(&packet,mtu,epoch,now);
            if(!midi_feedback_packet_add(&packet,&list.messages[i],epoch,now)){s_dropped++;return false;}
        }
    }
    if(midi_feedback_packet_has_messages(&packet) && !notify(&packet,job)) {
        s_dropped++;
        return false;
    }
    if(s_dropped){ESP_LOGW(TAG,"%" PRIu32 " state dumps dropped",s_dropped);s_dropped=0;}
    return true;
}

static void feedback_task(void *arg)
{
    (void)arg;feedback_job_t job;
    while(1)if(xQueueReceive(s_queue,&job,portMAX_DELAY)==pdTRUE) {
        feedback_job_t newer;
        while(xQueueReceive(s_queue,&newer,0)==pdTRUE)job=newer;
        if (send_job(&job) && job.sync_epoch && job_current(&job))
            ble_midi_feedback_sync_complete(job.sync_epoch);
    }
}

void midi_feedback_init(void)
{
    s_queue=xQueueCreate(1,sizeof(feedback_job_t));
    if(!s_queue || xTaskCreate(feedback_task,"midi_feedback",FEEDBACK_STACK,NULL,
                               FEEDBACK_PRIO,NULL)!=pdPASS) {
        s_queue=NULL;ESP_LOGE(TAG,"feedback worker unavailable");
    }
}

bool midi_feedback_publish(const web_control_state_t *state,uint8_t except_source,uint64_t epoch)
{
    if(!eligible(state))return false;
    if(except_source==MIDI_SOURCE_BLE)return true;
    uint64_t current;size_t mtu;
    if(!ble_midi_feedback_link(&current,&mtu))return true;
    (void)mtu;
    if(epoch && epoch!=current)return true;
    feedback_job_t job={
        .owner=midi_feedback_owner_guard_capture(&state->patch,
                                                 state->cutoff_offset,
                                                 state->resonance_offset,
                                                 state->context_epoch),
        .controls=parameter_cc_state_from_patch(&state->patch,state->cutoff_offset,
                                                state->resonance_offset),
        .epoch=current,.sync_epoch=epoch,.revision=state->revision};
    if(!s_queue)return true;
    xQueueOverwrite(s_queue,&job);
    return true;
}
