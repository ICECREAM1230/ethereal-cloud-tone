#pragma once
/* Pure C physical parameters. No SDK/runtime IDs in saved presets. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define KS_TIMBRE_MODES 4u
#define KS_TIMBRE_NOTES 128u
#define KS_TIMBRE_WORDS 64u
#define KS_TIMBRE_FS (91015625.0 / 948.0)
typedef struct {
    double frequency_hz;
    double t60_seconds;
    double weight;
} ks_timbre_mode_t;
typedef struct {
    uint32_t instrument_id;   /* 0 Legacy, 1 Harp, 2 Guzheng */
    uint32_t preset_schema;   /* 1 */
    uint32_t model_version;   /* 1 */
    uint32_t factory_revision; /* 1 for shipped engineering candidates */
    double pluck_position;    /* .05 .. .45 */
    double noise_mix;         /* 0 .. 1 */
    double noise_lowpass;     /* 0 .. 1 */
    double velocity_brightness; /* 0 .. 1 */
    double loss_weight;       /* 0 .. .5 */
    double dispersion;        /* -.8 .. .8, zero bypasses */
    double release_ms;        /* 5 .. 2000 */
    double dry;               /* 0 .. 1; dry + weights <= 1 */
    ks_timbre_mode_t modes[KS_TIMBRE_MODES];
} ks_timbre_preset_t;
typedef struct {
    uint32_t notes[KS_TIMBRE_NOTES][2];
    uint32_t pan[KS_TIMBRE_NOTES];
    uint32_t header[KS_TIMBRE_WORDS];
} ks_timbre_image_t;
/* Reject invalid/nonfinite fields, model_gen==0, decay outside 200..8000.
 * On failure *out is unchanged. Legacy ignores physical fields after validation
 * and emits a canonical description and the original numerical note formulas.
 * notes[n][0] valid bit31 | N; notes[n][1] rho high16 | signed tuning low16. */
bool ks_timbre_factory(unsigned id, ks_timbre_preset_t *out);
bool ks_timbre_validate(const ks_timbre_preset_t *preset);
bool ks_timbre_compile(const ks_timbre_preset_t *preset, unsigned decay_ms,
                       uint8_t volume, int8_t pan, uint32_t model_gen,
                       ks_timbre_image_t *out);
/* model CRC exists after compile. finalize computes header[61] including gen. */
void ks_timbre_image_finalize(ks_timbre_image_t *image, uint32_t template_gen);
/* Complete ISO-HDLC CRC, MSB-first bytes within each word. Empty CRC is zero. */
uint32_t ks_timbre_crc32_words(const uint32_t *words, size_t count);
/* Phase-predicted cents and actual quantized linear-loop T60 (seconds).
 * Includes filter magnitude clipping; excludes finite-amplitude truncation. */
bool ks_timbre_note_metrics(const ks_timbre_image_t *image, unsigned note,
                            double *cents, double *t60_seconds);
