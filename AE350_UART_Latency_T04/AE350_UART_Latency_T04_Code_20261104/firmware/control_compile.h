// Copyright (c) 2026 ICECREAM1230
/* SPDX-License-Identifier: GPL-3.0-or-later
 * CPU parameter compiler. SUB/KS arithmetic derives from MIT RTL;
 * JT51 policy/attenuation derives from GPL-3.0-or-later jt51_adapter_pkg.sv. */
#ifndef CONTROL_COMPILE_H
#define CONTROL_COMPILE_H
#include <stdint.h>
#include "../control_migration/firmware/voice_manager.h"
#include "../control_migration/include/prepared_control.h"
#define CF_MAX_PACKETS 48u
#define CF_TEMPLATE_BUDGET 4u
typedef struct {
    uint32_t element[64],amp[128],mod[128];
    uint8_t keytrack,range;
} cf_sub_template_t;
typedef struct { uint32_t word[384]; } cf_ks_template_t;
typedef struct {
    control_voice_command_t logical;
    int32_t bend;
    uint32_t amp,mod;
    uint16_t cutoff;
    uint8_t keytrack,wheel,x,y,live,released;
} cf_sub_voice_t;
typedef struct {
    control_voice_command_t logical;
    uint8_t live,released;
} cf_ks_voice_t;
typedef struct {
    control_voice_command_t logical;
    /* Native operator register snapshot; future patch APIs must snapshot ON. */
    uint8_t op[4][6],algorithm,feedback,keys,live,released;
} cf_fm_voice_t;
typedef struct {
    cf_sub_template_t sub_stage,sub_active;
    cf_ks_template_t ks_stage,ks_active;
    cf_sub_voice_t sub[32],next_sub;
    cf_ks_voice_t ks[16],next_ks;
    cf_fm_voice_t fm[8],next_fm;
    prepared_packet_t packet[CF_MAX_PACKETS];
    uint32_t revision[3],ack[8],pending_generation,unsupported_templates;
    uint32_t fm_sent,ks_header[64],fault;
    uint8_t count,index,effect_engine,effect_voice,effect_valid;
    uint8_t engines,boot_phase,boot_done,background,pending,pending_engine;
    uint8_t refresh_engine,refresh_voice,refresh_phase,ack_dirty;
    uint8_t fm_volume,fm_pan,fm_stage_volume,fm_stage_pan;
} control_compiler_t;
enum { CF_ERROR=0,CF_OK=1,CF_COMMIT_PENDING=2 };
void cf_init(control_compiler_t *c,uint8_t engines);
/* Every generated packet is immutable until cf_ack. Final ACK installs the
 * accepted voice snapshot. This API never consumes the logical VM queue. */
int cf_begin(control_compiler_t *c,const control_voice_command_t *logical);
const prepared_packet_t *cf_packet(const control_compiler_t *c);
int cf_ack(control_compiler_t *c); /* 1 iff this physical job has completed. */
/* Feed only after actual template FIFO POP. COMMIT holds the compiler until
 * old logical traffic drains and cf_background may activate/refresh it. */
int cf_template_record(control_compiler_t *c,uint16_t address,uint32_t data);
/* Start at most one bounded boot/refresh job. Caller guarantees no logical
 * command or physical packet in flight, and old parser events are drained. */
int cf_background(control_compiler_t *c,const voice_manager_t manager[3]);
void cf_idle(control_compiler_t *c,uint8_t engine,uint8_t voice,uint32_t generation);
int cf_maintenance(const control_compiler_t *c);
/* Exact integer helpers, also exercised by independent expected-value tests. */
int32_t cf_raw_bend(uint16_t raw,uint8_t range);
int32_t cf_q16_bend(uint32_t q,uint8_t note);
int32_t cf_floor64(int32_t value);
uint16_t cf_note_pitch(uint8_t note);
uint16_t cf_keytrack_cutoff(uint16_t pitch,uint8_t keytrack);
uint16_t cf_fm_pitch_codes(uint32_t pitch);
#endif
