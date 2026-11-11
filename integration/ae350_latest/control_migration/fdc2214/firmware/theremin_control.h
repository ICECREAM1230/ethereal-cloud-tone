#ifndef THEREMIN_CONTROL_H
#define THEREMIN_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Existing converter fIN/fREF<1/4 guard, distinct from the 28-bit format limit. */
#define THEREMIN_RAW_LIMIT UINT32_C(0x04000000)
#define THEREMIN_AGE_UNKNOWN UINT64_MAX

typedef enum {
    THEREMIN_MISSING = 0,
    THEREMIN_OK,
    THEREMIN_AMPLITUDE_WARNING,
    THEREMIN_HARD_ERROR,
    THEREMIN_WATCHDOG,
    THEREMIN_INVALID_RAW,
    THEREMIN_INVALID_FLAGS,
    THEREMIN_WARNING_REJECTED
} theremin_quality_t;

typedef struct {
    uint32_t raw;
    uint8_t flags; /* FDC DATA_MSB[13:12]: bit0 amplitude, bit1 watchdog. */
    bool fresh;    /* A new channel measurement; never repeat old data as fresh. */
    bool hard_error; /* I2C/transport/status failure, independent of AW. */
} theremin_sample_t;

typedef struct {
    uint32_t raw_far;
    uint32_t raw_near; /* May be greater OR smaller than raw_far. */
    float noise_sigma;
} theremin_calibration_t;

typedef struct {
    theremin_calibration_t pitch;
    theremin_calibration_t volume;
    float min_cutoff_hz; /* Filtering fields below apply only to volume. */
    float beta; /* Units: normalized position, not raw FDC codes. */
    float derivative_cutoff_hz;
    float deadband_sigma;
    uint32_t stale_timeout_us;
    float pitch_slew_octaves_per_s; /* Retained ABI field; direct pitch ignores it. */
    float gain_slew_per_s;
    float mute_close; /* normalized near position */
    float mute_open;  /* must be smaller than mute_close */
    uint16_t pitch_min;
    uint16_t pitch_max;
    uint16_t reference;
    bool accept_amplitude_warning;
} theremin_config_t;

/* Internal state is exposed only to permit static/stack allocation on AE350.
 * Applications must not modify these fields. One caller owns each instance. */
typedef struct {
    uint32_t median[3];
    uint8_t next;
    bool primed;
    uint64_t accepted_at_us;
    float held;
    float position;
    float previous_raw;
    float derivative;
} theremin_lane_t;

typedef struct {
    theremin_config_t config;
    theremin_lane_t pitch;
    theremin_lane_t volume;
    uint64_t update_at_us;
    float pitch_value;
    float gain;
    bool configured;
    bool have_time;
    bool have_pitch;
    bool muted;
} theremin_t;

typedef struct {
    uint16_t pitch_code; /* 1024/octave; default4864..6912, about220..880Hz. */
    int32_t pitch_delta; /* pitch_code - config.reference */
    uint32_t gain_q16;   /* 0..65536, requires17 bits. */
    float gain_linear;
    float pitch_u; /* Direct clamped raw position; never filtered/slew-limited. */
    float volume_u;
    uint64_t pitch_age_us;
    uint64_t volume_age_us;
    theremin_quality_t pitch_quality;
    theremin_quality_t volume_quality;
    bool pitch_accepted;
    bool volume_accepted;
    bool pitch_stale;
    bool volume_stale;
    bool muted;
} theremin_output_t;

/* Experimental defaults fitted to the unlabelled600-row2026-10-29 CSV.
 * Frozen P5/P95 after causal median3, not physical-distance calibration. */
theremin_config_t theremin_default_config(void);

/* Invalid config returns false WITHOUT changing an existing instance. */
bool theremin_init(theremin_t *state, const theremin_config_t *config);
void theremin_reset(theremin_t *state); /* retain config, clear sample history */

/* Explicit far-hand recenter; preserves both spans, then resets the controller.
 * Invoke only when deliberately recalibrating, outside an active performance. */
bool theremin_rebaseline(theremin_t *state, uint32_t pitch_far, uint32_t volume_far);

/* now_us must be a monotonic64-bit MICROSECOND timestamp, not CPU clock ticks.
 * Call periodically even without data (NULL or fresh=false) to handle timeout.
 * Pitch maps directly by rounded integer interpolation in 1024/octave codes.
 * Channels update independently. Bad timestamps return false before mutation.
 * Only the core init/reset/update API is needed by AE350; no OS/BSP required. */
bool theremin_update(theremin_t *state, uint64_t now_us,
                     const theremin_sample_t *pitch, const theremin_sample_t *volume,
                     theremin_output_t *output);
/* FPGA mailbox adaptation: accepted sample ages use acquisition-derived local
 * times; output slew/stale time uses now_us. Future or non-increasing accepted
 * sample timestamps reject the call before mutation. Original update() passes
 * now_us for both timestamps and keeps its original behavior. */
bool theremin_update_timed(theremin_t *state, uint64_t now_us,
                           const theremin_sample_t *pitch, uint64_t pitch_at_us,
                           const theremin_sample_t *volume, uint64_t volume_at_us,
                           theremin_output_t *output);
const char *theremin_quality_name(theremin_quality_t quality);

#ifdef __cplusplus
}
#endif
#endif
