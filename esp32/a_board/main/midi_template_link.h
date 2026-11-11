#pragma once
#include "midi_templates.h"
#include "control_source.h"

typedef enum {
    MIDI_TEMPLATE_LINK_OK = 0,
    MIDI_TEMPLATE_LINK_UNAVAILABLE,
    MIDI_TEMPLATE_LINK_IO,
    MIDI_TEMPLATE_LINK_TIMEOUT,
    MIDI_TEMPLATE_LINK_RESET,
    MIDI_TEMPLATE_LINK_REJECTED,
    MIDI_TEMPLATE_LINK_OVERFLOW
} midi_template_link_fault_t;
typedef struct {
    bool available, ready, busy;
    bool sub_patch_supported,sub_patch_valid;
    /* Production generation identifies a transport batch. applied_generation
     * advances only with a native DDS/KS ACK, never for a b400-only send.
     * Independent DDS/KS native COMMIT generations need not match. */
    uint32_t fault, generation, applied_generation;
    uint32_t dds_applied_generation, ks_applied_generation;
    /* Production b400 is write-only: this records dispatch, never native ACK.
     * FM_ONLY continues to require its real data + generation readback. */
    uint32_t fm_sent_revision;
    bool timbre_available, timbre_valid;
    uint32_t timbre_abi, timbre_capabilities, timbre_snapshot;
    uint32_t timbre_state, timbre_requested, timbre_applied, timbre_id;
    uint32_t timbre_version, timbre_factory, timbre_crc, timbre_image_crc;
    uint32_t timbre_template, timbre_completed_template, timbre_result, timbre_requested_template;
    uint32_t ks_upload_generation,ks_upload_model_generation,ks_upload_crc,ks_upload_image_crc;
    uint32_t ks_applied_model_generation,ks_applied_crc,ks_applied_image_crc;
    uint32_t ks_canceled_model_generation,ks_desired_revision,ks_applied_revision;
} midi_template_link_state_t;

void midi_template_link_init(void);
/* SPI-owner-only reset fence: drop desired/ready state, preserve any unknown
 * COMMIT fence, and require a fresh publish. RAM only; never accesses SPI. */
void midi_template_link_invalidate(void);
/* Copies the newest desired image. No SPI; safe from the event owner.
 * Unchanged production images are coalesced. Each changed native domain is
 * uploaded in full; unrelated domains and their COMMITs are left untouched. */
void midi_template_link_publish(const midi_template_image_t *image);
/* Only engine_link's SPI owner calls service; control_idle fences the old
 * static element/producer transaction before beginning a template upload.
 * Explicit FM_ONLY builds upload b400 only and require the validation image's
 * actual applied expression and generation; no DDS/KS ACK is synthesized. */
void midi_template_link_service(int64_t deadline_us, bool control_idle, bool available);
void midi_template_link_state(midi_template_link_state_t *out);

/* Enable read-only KST1 discovery. No SPI outside the existing service owner. */
void midi_template_link_timbre_enable(void);
/* Retire a model before native COMMIT; committed writes retain their fence. */
void midi_template_link_cancel_timbre(uint32_t model_generation);
/* Bind before publishing a network-origin KS candidate. Native adoption
 * retires the binding; shared refreshes of an adopted model remain local. */
void midi_template_link_bind_timbre_source(uint32_t model_generation,control_source_ref_t source,int64_t expires);
