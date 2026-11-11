#ifndef VOLUME_AUDIO_H
#define VOLUME_AUDIO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VC_CAPTURE_COUNT 128u
#define VC_RAW_LIMIT UINT32_C(0x04000000)
#define VC_SAMPLE_RATE 48000u

typedef enum { VC_OFF, VC_REFERENCE, VC_SENSOR } vc_mode_t;
typedef enum { VC_NO_SAMPLE, VC_OK, VC_AW, VC_WATCHDOG, VC_INVALID, VC_FAULT, VC_STALE } vc_quality_t;
typedef enum { VC_CAPTURE_IDLE, VC_CAPTURE_ACTIVE, VC_CAPTURE_DONE, VC_CAPTURE_FAILED } vc_capture_result_t;
typedef enum { VC_SHOW, VC_EXPORT, VC_MODE, VC_LEVEL, VC_TONE, VC_CHANNEL,
               VC_LOUD, VC_MUTE, VC_RANGE, VC_CURVE, VC_APPLY } vc_command_kind_t;
typedef struct { vc_command_kind_t kind; uint32_t a,b; } vc_command_t;
typedef struct {
    uint32_t raw_loud,raw_mute,timeout_us;
    float noise_sigma,smoothing_ms;
    uint16_t tone_hz;
    uint8_t channel,curve,level_percent;
} vc_config_t;
typedef struct { uint64_t time_us; uint32_t raw; uint8_t flags; bool hard_error; } vc_sample_t;
/* One owner; wrappers provide synchronization. No OS, heap, I2C or I2S here. */
typedef struct {
    vc_config_t config;
    vc_mode_t mode;
    vc_quality_t quality;
    uint32_t raw,median[3],pending[2],capture[VC_CAPTURE_COUNT];
    float pending_noise[2],position,gain;
    uint64_t update_us,sample_us,seen_us,capture_deadline;
    uint16_t capture_count,capture_aw;
    uint8_t median_next,pending_mask,capture_target;
    vc_capture_result_t capture_result;
    bool have_time,have_sample,have_seen,primed,calibrated,transport_fault,volume_muted;
} vc_state_t;
typedef struct {
    vc_config_t config;
    vc_mode_t mode;
    vc_quality_t quality;
    vc_capture_result_t capture_result;
    uint32_t raw,pending_loud,pending_mute,gain_q16;
    uint64_t age_us;
    float position,signal_gain;
    uint16_t capture_count,capture_aw;
    uint8_t pending_mask;
    bool calibrated;
} vc_status_t;
typedef struct { uint32_t phase; float gain; } vc_oscillator_t;

vc_config_t vc_default_config(void);
bool vc_config_valid(const vc_config_t *config);
bool vc_init(vc_state_t *state,const vc_config_t *config);
bool vc_update(vc_state_t *state,uint64_t now_us,const vc_sample_t *sample);
bool vc_execute(vc_state_t *state,const vc_command_t *command,uint64_t now_us);
bool vc_parse_command(int argc,const char *const argv[],vc_command_t *command);
vc_status_t vc_status(const vc_state_t *state,uint64_t now_us);
const char *vc_quality_name(vc_quality_t quality);
bool vc_render(vc_oscillator_t *osc,int16_t *stereo,size_t frames,uint32_t sample_rate,
               uint16_t tone_hz,float signal_gain);
bool vc_s3_pins_valid(const int pins[4],const int fdc_pins[4],int uart_tx,int uart_rx);
#endif
