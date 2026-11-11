/* Device/OS-independent C99 control algorithm for AE350 or a host.
 * 1 Euro equations: https://gery.casiez.net/1euro/ ; independently implemented.
 * Default raw endpoints are an experimental fit of the2026-10-29 CSV.
 */
#include "theremin_control.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

static float bounded(float value, float low, float high)
{
    return value<low ? low : (value>high ? high : value);
}

static bool positive(float value)
{
    return isfinite(value) && value>0.0f;
}

static bool valid_calibration(const theremin_calibration_t *c)
{
    if (c->raw_far==0 || c->raw_near==0 || c->raw_far>=THEREMIN_RAW_LIMIT
        || c->raw_near>=THEREMIN_RAW_LIMIT || !isfinite(c->noise_sigma) || c->noise_sigma<0.0f)
        return false;
    int64_t span=(int64_t)c->raw_near-(int64_t)c->raw_far;
    float width=(float)(span<0 ? -span : span);
    return width>=64.0f && width>=20.0f*c->noise_sigma;
}

static bool valid_config(const theremin_config_t *c)
{
    return valid_calibration(&c->pitch) && valid_calibration(&c->volume)
        && positive(c->min_cutoff_hz) && positive(c->derivative_cutoff_hz)
        && isfinite(c->beta) && c->beta>=0.0f
        && isfinite(c->deadband_sigma) && c->deadband_sigma>=0.0f
        && c->stale_timeout_us>0 && positive(c->pitch_slew_octaves_per_s)
        && positive(c->gain_slew_per_s)
        && isfinite(c->mute_open) && isfinite(c->mute_close)
        && c->mute_open>=0.0f && c->mute_open<c->mute_close && c->mute_close<=1.0f
        && c->pitch_min<c->pitch_max && c->pitch_max<=16383 && c->reference<=16383;
}

theremin_config_t theremin_default_config(void)
{
    theremin_config_t c={0};
    c.pitch=(theremin_calibration_t){UINT32_C(13360095),UINT32_C(13309071),320.7971f};
    c.volume=(theremin_calibration_t){UINT32_C(13246586),UINT32_C(13174639),985.4551f};
    c.min_cutoff_hz=2.0f;
    c.beta=4.0f;
    c.derivative_cutoff_hz=1.0f;
    c.deadband_sigma=1.5f;
    c.stale_timeout_us=343000;
    c.pitch_slew_octaves_per_s=24.0f;
    c.gain_slew_per_s=10.0f;
    c.mute_close=.985f;
    c.mute_open=.95f;
    c.pitch_min=4864;
    c.pitch_max=6912;
    c.reference=5888;
    c.accept_amplitude_warning=true;
    return c;
}

void theremin_reset(theremin_t *state)
{
    if (state==NULL || !state->configured) return;
    memset(&state->pitch,0,sizeof(state->pitch));
    memset(&state->volume,0,sizeof(state->volume));
    state->pitch.position=.5f;
    state->volume.position=.5f;
    state->update_at_us=0;
    state->pitch_value=(float)state->config.reference;
    state->gain=0.0f;
    state->have_time=false;
    state->have_pitch=false;
    state->muted=false;
}

bool theremin_init(theremin_t *state, const theremin_config_t *config)
{
    if (state==NULL || config==NULL || !valid_config(config)) return false;
    /* Copy before clearing: config may point to state->config. */
    theremin_config_t copy=*config;
    memset(state,0,sizeof(*state));
    state->config=copy;
    state->configured=true;
    theremin_reset(state);
    return true;
}

bool theremin_rebaseline(theremin_t *state, uint32_t pitch_far, uint32_t volume_far)
{
    if (state==NULL || !state->configured) return false;
    theremin_config_t c=state->config;
    int64_t pitch_near=(int64_t)c.pitch.raw_near+(int64_t)pitch_far-(int64_t)c.pitch.raw_far;
    int64_t volume_near=(int64_t)c.volume.raw_near+(int64_t)volume_far-(int64_t)c.volume.raw_far;
    if (pitch_near<=0 || pitch_near>=THEREMIN_RAW_LIMIT || volume_near<=0 || volume_near>=THEREMIN_RAW_LIMIT)
        return false;
    c.pitch.raw_far=pitch_far;
    c.pitch.raw_near=(uint32_t)pitch_near;
    c.volume.raw_far=volume_far;
    c.volume.raw_near=(uint32_t)volume_near;
    return theremin_init(state,&c);
}

static theremin_quality_t quality(const theremin_sample_t *sample, bool accept_warning)
{
    if (sample==NULL) return THEREMIN_MISSING;
    if (sample->hard_error) return THEREMIN_HARD_ERROR;
    if (!sample->fresh) return THEREMIN_MISSING;
    if (sample->raw==0 || sample->raw>=THEREMIN_RAW_LIMIT) return THEREMIN_INVALID_RAW;
    if (sample->flags>3) return THEREMIN_INVALID_FLAGS;
    if ((sample->flags&2u)!=0) return THEREMIN_WATCHDOG;
    if ((sample->flags&1u)!=0)
        return accept_warning ? THEREMIN_AMPLITUDE_WARNING : THEREMIN_WARNING_REJECTED;
    return THEREMIN_OK;
}

static bool accepted(theremin_quality_t q)
{
    return q==THEREMIN_OK || q==THEREMIN_AMPLITUDE_WARNING;
}

static uint32_t median3(uint32_t a, uint32_t b, uint32_t c)
{
    if (a>b) {uint32_t t=a;a=b;b=t;}
    if (b>c) {uint32_t t=b;b=c;c=t;}
    return a>b ? a:b;
}

static float normalized(uint32_t raw, const theremin_calibration_t *c)
{
    /* Subtract as signed integers BEFORE converting to float. */
    int64_t difference=(int64_t)raw-(int64_t)c->raw_far;
    int64_t span=(int64_t)c->raw_near-(int64_t)c->raw_far;
    return bounded((float)difference/(float)span,0.0f,1.0f);
}

static float alpha(float cutoff_hz, float dt_s)
{
    return dt_s/(dt_s+1.0f/(6.283185307179586f*cutoff_hz));
}

static void accept_sample(theremin_lane_t *lane, const theremin_calibration_t *cal,
                          const theremin_config_t *c, uint32_t raw, uint64_t now_us)
{
    if (lane->primed && now_us-lane->accepted_at_us>c->stale_timeout_us)
        memset(lane,0,sizeof(*lane));
    if (!lane->primed) {
        lane->median[0]=raw;lane->median[1]=raw;lane->median[2]=raw;
        lane->next=0;
        lane->held=normalized(raw,cal);
        lane->position=lane->held;
        lane->previous_raw=lane->held;
        lane->derivative=0.0f;
        lane->primed=true;
    } else {
        lane->median[lane->next]=raw;
        lane->next=(uint8_t)((lane->next+1u)%3u);
        float value=normalized(median3(lane->median[0],lane->median[1],lane->median[2]),cal);
        int64_t signed_span=(int64_t)cal->raw_near-(int64_t)cal->raw_far;
        float span=(float)(signed_span<0 ? -signed_span:signed_span);
        float width=bounded(c->deadband_sigma*cal->noise_sigma/span,0.0f,.015f);
        float difference=value-lane->held;
        if (value==0.0f || value==1.0f) lane->held=value;
        else if (fabsf(difference)>width) lane->held=value-(difference>0.0f ? width:-width);
        float dt_s=(float)(now_us-lane->accepted_at_us)*.000001f;
        float derivative=(lane->held-lane->previous_raw)/dt_s;
        lane->derivative+=alpha(c->derivative_cutoff_hz,dt_s)*(derivative-lane->derivative);
        float cutoff=c->min_cutoff_hz+c->beta*fabsf(lane->derivative);
        lane->position+=alpha(cutoff,dt_s)*(lane->held-lane->position);
        lane->position=bounded(lane->position,0.0f,1.0f);
        lane->previous_raw=lane->held;
    }
    lane->accepted_at_us=now_us;
}

bool theremin_update(theremin_t *s, uint64_t now_us,
                     const theremin_sample_t *pitch, const theremin_sample_t *volume,
                     theremin_output_t *out)
{
    if (s==NULL || out==NULL || !s->configured || (s->have_time && now_us<=s->update_at_us))
        return false;
    float dt_s=s->have_time ? (float)(now_us-s->update_at_us)*.000001f:0.0f;
    if (s->have_time && now_us-s->update_at_us>s->config.stale_timeout_us) {
        /* A scheduling gap must not count as time spent fading IN. */
        s->gain=0.0f;
        dt_s=0.0f;
    }
    theremin_quality_t pq=quality(pitch,s->config.accept_amplitude_warning);
    theremin_quality_t vq=quality(volume,s->config.accept_amplitude_warning);
    if (accepted(pq)) accept_sample(&s->pitch,&s->config.pitch,&s->config,pitch->raw,now_us);
    if (accepted(vq)) accept_sample(&s->volume,&s->config.volume,&s->config,volume->raw,now_us);
    uint64_t pa=s->pitch.primed ? now_us-s->pitch.accepted_at_us:THEREMIN_AGE_UNKNOWN;
    uint64_t va=s->volume.primed ? now_us-s->volume.accepted_at_us:THEREMIN_AGE_UNKNOWN;
    bool ps=pa>s->config.stale_timeout_us,vs=va>s->config.stale_timeout_us;
    if (!ps) {
        float target=(float)s->config.pitch_min
            +(float)(s->config.pitch_max-s->config.pitch_min)*s->pitch.position;
        if (!s->have_pitch) {s->pitch_value=target;s->have_pitch=true;}
        else {
            float limit=s->config.pitch_slew_octaves_per_s*1024.0f*dt_s;
            s->pitch_value+=bounded(target-s->pitch_value,-limit,limit);
        }
    }
    if (!vs) {
        if (s->muted && s->volume.position<=s->config.mute_open) s->muted=false;
        else if (!s->muted && s->volume.position>=s->config.mute_close) s->muted=true;
    }
    float open=1.0f-s->volume.position;
    float target_gain=!ps && !vs && !s->muted ? open*open:0.0f;
    float gain_limit=s->config.gain_slew_per_s*dt_s;
    s->gain=bounded(s->gain+bounded(target_gain-s->gain,-gain_limit,gain_limit),0.0f,1.0f);
    s->update_at_us=now_us;
    s->have_time=true;
    out->pitch_code=(uint16_t)(s->pitch_value+.5f);
    out->pitch_delta=(int32_t)out->pitch_code-(int32_t)s->config.reference;
    out->gain_q16=(uint32_t)(s->gain*65536.0f+.5f);
    out->gain_linear=s->gain;
    out->pitch_u=s->pitch.position;
    out->volume_u=s->volume.position;
    out->pitch_age_us=pa;out->volume_age_us=va;
    out->pitch_quality=pq;out->volume_quality=vq;
    out->pitch_accepted=accepted(pq);out->volume_accepted=accepted(vq);
    out->pitch_stale=ps;out->volume_stale=vs;out->muted=s->muted;
    return true;
}

const char *theremin_quality_name(theremin_quality_t q)
{
    static const char *const names[]={"missing","ok","amplitude_warning","hard_error",
        "watchdog","invalid_raw","invalid_flags","warning_rejected"};
    return (unsigned)q<sizeof(names)/sizeof(names[0]) ? names[(unsigned)q]:"unknown";
}
