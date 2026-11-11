#pragma once
#include "ks_timbre_params.h"
#include "event_bus.h"
#include "midi_template_link.h"
#define KS_TIMBRE_SLOTS 8u
#define KS_TIMBRE_NAME 65u
#define KS_TIMBRE_DOCUMENT_MAX 4096u
#define KS_TIMBRE_HTTP_MAX 25344u /* JSON escaping may expand every UTF-8 byte. */
typedef enum {KST_APPLY,KST_SAVE,KST_LOAD,KST_IMPORT,KST_STARTUP} ks_timbre_op_t;
typedef struct {
    ks_timbre_preset_t preset;
    uint32_t decay_ms;
    char name[KS_TIMBRE_NAME];
} ks_timbre_setting_t;
typedef struct {
    ks_timbre_op_t op;
    web_v2_event_t event;
    ks_timbre_setting_t setting;
    uint32_t slot,instrument_id;
    bool has_slot,has_instrument,has_preset,enabled;
} ks_timbre_request_t;
typedef struct {
    bool occupied;
    ks_timbre_setting_t setting;
} ks_timbre_slot_t;
typedef struct {
    bool enabled,has_slot;
    uint32_t slot,instrument_id;
} ks_timbre_startup_t;
typedef struct {
    uint32_t token,sequence,context_epoch,request_id,code;
} ks_timbre_result_t;
typedef enum {KST_UNAVAILABLE,KST_IDLE,KST_QUEUED,KST_UPLOADING,KST_SWITCHING,KST_APPLIED,KST_CANCELED,KST_FAILED,KST_UNKNOWN} ks_timbre_phase_t;
typedef struct {
    midi_template_link_state_t link;
    ks_timbre_setting_t setting;
    uint32_t requested_generation,requested_instrument;
    ks_timbre_phase_t phase;
    bool dirty,initializing;
    ks_timbre_result_t result;
} ks_timbre_state_t;
bool ks_timbre_name_valid(const char *name);
bool ks_timbre_document_parse(const char *text,size_t len,ks_timbre_setting_t *out);
bool ks_timbre_document_emit(const ks_timbre_setting_t *setting,char *out,size_t size);
bool ks_timbre_request_parse(const char *text,size_t len,ks_timbre_op_t op,ks_timbre_request_t *out);
/* One immutable HTTP reservation; sound adoption is voice-owner-only. */
bool ks_timbre_reserve(ks_timbre_request_t *request,int64_t now);
bool ks_timbre_claim(const web_v2_event_t *event,ks_timbre_request_t *out);
void ks_timbre_release(uint32_t request_id);
bool ks_timbre_reserved(ks_timbre_request_t *out);
void ks_timbre_state_read(ks_timbre_state_t *out);
void ks_timbre_state_publish(const ks_timbre_state_t *state);
void ks_timbre_directory(ks_timbre_slot_t slots[KS_TIMBRE_SLOTS],ks_timbre_startup_t *startup);
bool ks_timbre_slot_read(unsigned slot,ks_timbre_setting_t *out);
bool ks_timbre_setting_saved(const ks_timbre_setting_t *setting);
/* Dedicated NVS worker, never the MIDI/SPI/HTTP task, performs writes. */
void ks_timbre_store_init(void);
bool ks_timbre_store_request(const ks_timbre_request_t *request);
bool ks_timbre_store_result(uint32_t request_id,bool *success);
bool ks_timbre_startup_setting(ks_timbre_setting_t *out);
#ifndef ESP_PLATFORM
void ks_timbre_store_host_step(void);
void ks_timbre_store_host_fail(bool fail);
#endif
