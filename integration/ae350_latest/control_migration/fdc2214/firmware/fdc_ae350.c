/* SPDX-License-Identifier: MIT
 * FPGA owns FDC I2C. This caller only reads immutable raw-frame snapshots. */
#include "fdc_ae350.h"
#include <stddef.h>

static void clear_bytes(void *target, unsigned size)
{
    volatile unsigned char *p=(volatile unsigned char *)target;
    while(size-- != 0u)*p++=0;
}

#if ENABLE_AE350_FDC
enum { P_ID, P_ABI, P_CAP, P_DONE };
enum { S_IDLE, S_REQUEST, S_WAIT, S_WORDS, S_RELEASE };
enum { C_IDLE, C_PUBLISH, C_SHADOW, C_COMMIT, C_WAIT_BUSY, C_ACK, C_STATUS };
static const uint8_t raw_offsets[13]={
    FDC_REG_EPOCH,FDC_REG_SEQUENCE,FDC_REG_CAPTURE_LO,FDC_REG_CAPTURE_HI,
    FDC_REG_RAW0,FDC_REG_RAW1,FDC_REG_QUALITY,FDC_REG_DIAGNOSTIC,
    FDC_REG_OVERWRITTEN,FDC_REG_ERRORS,FDC_REG_FRAME_PRESENT,
    FDC_REG_FREEZE_LO,FDC_REG_FREEZE_HI
};

static int read_reg(fdc_ae350_t *s,unsigned offset,uint32_t *value)
{
    if(s->io.read32) {
        if(s->io.read32(s->io.context,offset,value))return 1;
    } else {
        *value=*(volatile uint32_t *)(uintptr_t)(FDC_AE350_BASE+offset);
#ifdef __riscv
        __asm__ volatile("fence iorw,iorw" ::: "memory");
#endif
        return 1;
    }
    s->stats.faults|=FDC_FAULT_IO;return 0;
}

static int write_reg(fdc_ae350_t *s,unsigned offset,uint32_t value)
{
    if(s->io.write32) {
        if(s->io.write32(s->io.context,offset,value))return 1;
    } else {
        *(volatile uint32_t *)(uintptr_t)(FDC_AE350_BASE+offset)=value;
#ifdef __riscv
        __asm__ volatile("fence iorw,iorw" ::: "memory");
#endif
        return 1;
    }
    s->stats.faults|=FDC_FAULT_IO;return 0;
}

static void queue_output(fdc_ae350_t *s,bool fresh_frame)
{
    fdc_ae350_control_t c;
    c.epoch=s->epoch;c.sequence=0;c.pitch_code=s->output.pitch_code;
    c.active=(uint8_t)(s->stats.enabled && !s->stats.faults &&
        s->algorithm.have_pitch && s->output.gain_q16!=0);
    c.gain_q16=c.active?s->output.gain_q16:0;
    c.pitch_quality=s->output.pitch_quality;
    c.volume_quality=s->output.volume_quality;
    if(!s->latest_known || s->latest.pitch_code!=c.pitch_code ||
       s->latest.gain_q16!=c.gain_q16 || s->latest.active!=c.active ||
       s->latest.epoch!=c.epoch || fresh_frame) {
        s->latest=c;s->latest_valid=1;s->latest_known=1;
    }
}

static void reset_algorithm(fdc_ae350_t *s)
{
    theremin_reset(&s->algorithm);
    s->stats.telemetry_present=s->stats.telemetry_fresh=0;
    clear_bytes(&s->output,sizeof(s->output));
    s->output.pitch_code=s->algorithm.config.reference;
    s->output.pitch_stale=s->output.volume_stale=true;
    s->last_update=0;
    s->latest_known=0;queue_output(s,false);
}

/* Returns true only for a new, sufficiently recent complete frame. The CPU
 * read duration is added conservatively to source age, so cached samples can
 * never acquire a fresh timestamp merely by being read now. */
static bool frame_samples(fdc_ae350_t *s,uint64_t now,
                           theremin_sample_t sample[2])
{
    uint32_t *w=s->words,epoch=w[0],seq=w[1];
    uint64_t capture=(uint64_t)w[2]|((uint64_t)w[3]<<32);
    uint64_t freeze=(uint64_t)w[11]|((uint64_t)w[12]<<32);
    s->stats.diagnostic=w[7];s->stats.quality_status=w[6];
    s->stats.source_overwritten=w[8];
    s->stats.source_errors=w[9];
    if(epoch==0 || (s->have_epoch && epoch<s->epoch)) {
        ++s->stats.old_frames;return false;
    }
    if(!s->have_epoch || epoch!=s->epoch) {
        s->epoch=epoch;s->frame_sequence=0;s->control_sequence=0;
        s->have_epoch=1;s->source_errors=w[9];s->await_epoch=0;
        ++s->stats.epoch_changes;reset_algorithm(s);
        /* No other control job is started during restart. Unsolicited FPGA
         * epoch changes cancel private old work; hardware rejects old epoch. */
        s->control_phase=C_IDLE;s->latest_valid=1;
    }
    bool transport_changed=w[9]!=s->source_errors;
    s->source_errors=w[9];
    if(!w[10] || seq==0)return false;
    if(seq==s->frame_sequence){++s->stats.duplicates;return false;}
    if(seq<s->frame_sequence){++s->stats.old_frames;return false;}
    s->frame_sequence=seq;
    /* Publish only a completed immutable snapshot, never an in-progress MMIO
     * words[] array. Raw values are unfiltered, with quality kept separate. */
    s->stats.telemetry_raw0=w[4]&UINT32_C(0x0fffffff);
    s->stats.telemetry_raw1=w[5]&UINT32_C(0x0fffffff);
    s->stats.telemetry_flags0=(uint8_t)((w[6]>>16)&3u);
    s->stats.telemetry_flags1=(uint8_t)((w[6]>>18)&3u);
    s->stats.telemetry_sequence=seq;s->stats.telemetry_present=1;
    s->stats.telemetry_fresh=0;
    if(freeze<capture || now<s->snapshot_started ||
       freeze-capture>s->algorithm.config.stale_timeout_us ||
       now-s->snapshot_started>s->algorithm.config.stale_timeout_us ||
       freeze-capture+now-s->snapshot_started>s->algorithm.config.stale_timeout_us) {
        ++s->stats.aged_frames;return false;
    }
    uint64_t age=freeze-capture+now-s->snapshot_started;
    if(age>now){++s->stats.aged_frames;return false;}
    s->sample_at=now-age;
    s->stats.telemetry_at_us=s->sample_at;s->stats.telemetry_fresh=1;
    /* Conservative age bounds can make a new frame's mapped timestamp fall
     * behind one already consumed. It is safe to skip that frame; never
     * force time forwards and silently grant it a new lifetime. */
    if((s->algorithm.pitch.primed && s->sample_at<=s->algorithm.pitch.accepted_at_us) ||
       (s->algorithm.volume.primed && s->sample_at<=s->algorithm.volume.accepted_at_us)) {
        ++s->stats.old_frames;return false;
    }
    /* STATUS error-channel attribution can be ambiguous. Preserve it in
     * diagnostic words; do not turn AW status into a global hard error.
     * A device/transport fault rejects both lanes; WD remains per lane. */
    bool hard=transport_changed || (w[7]&3u)!=1u || (w[6]&0x0800u)!=0;
    for(unsigned lane=0;lane<2;lane++) {
        sample[lane].raw=w[4+lane];
        sample[lane].flags=(uint8_t)((w[6]>>(16+lane*2))&3u);
        /* The FPGA accumulates STATUS errors across polls. ERR_CHAN then
         * cannot reliably attribute every error. WD conservatively rejects
         * both, while AH/AL remain AW-quality previews (or warning rejection
         * if the user disables AW previews), never transport faults. */
        if(w[6]&0x0600u)sample[lane].flags|=1u;
        sample[lane].fresh=true;sample[lane].hard_error=hard;
    }
    ++s->stats.frames;return true;
}

static unsigned snapshot_step(fdc_ae350_t *s,uint64_t now)
{
    uint32_t value;
    if(s->frame_pending)return 0;
    if(s->snapshot_phase!=S_IDLE &&
       now-s->snapshot_started>FDC_AE350_TIMEOUT_US) {
        s->stats.faults|=FDC_FAULT_SNAPSHOT_TIMEOUT;return 0;
    }
    switch(s->snapshot_phase) {
    case S_IDLE:
        if(now<s->next_snapshot)return 0;
        if(!read_reg(s,FDC_REG_STATE,&value))return 1;
        if(value&3u) {
            /* Leftover PCLK snapshot after a CPU-only reboot is released
             * before requesting another; this never marks it fresh. */
            if(value&1u)s->snapshot_phase=S_RELEASE;
            s->snapshot_started=now;
        } else {s->snapshot_phase=S_REQUEST;s->snapshot_started=now;}
        return 1;
    case S_REQUEST:
        if(write_reg(s,FDC_REG_COMMAND,1))s->snapshot_phase=S_WAIT;
        return 1;
    case S_WAIT:
        if(!read_reg(s,FDC_REG_STATE,&value))return 1;
        if(value&1u){s->snapshot_index=0;s->snapshot_phase=S_WORDS;}
        return 1;
    case S_WORDS:
        if(read_reg(s,raw_offsets[s->snapshot_index],&value)) {
            s->words[s->snapshot_index++]=value;
            if(s->snapshot_index==13)s->snapshot_phase=S_RELEASE;
        }
        return 1;
    case S_RELEASE:
        if(write_reg(s,FDC_REG_COMMAND,2)) {
            if(s->snapshot_index==13){s->frame_pending=1;++s->stats.snapshots;}
            s->snapshot_index=0;s->snapshot_phase=S_IDLE;
            s->next_snapshot=now+FDC_AE350_POLL_US;
        }
        return 1;
    default:return 0;
    }
}

static unsigned control_step(fdc_ae350_t *s,uint64_t now)
{
    uint32_t value;
    if(s->control_phase!=C_IDLE && now-s->control_started>FDC_AE350_TIMEOUT_US) {
        s->stats.faults|=FDC_FAULT_CONTROL_TIMEOUT;return 0;
    }
    switch(s->control_phase) {
    case C_IDLE:
        if(!s->latest_valid || !s->have_epoch || s->restart_requested || s->await_epoch)return 0;
        if(!read_reg(s,FDC_REG_STATE,&value))return 1;
        if(value&12u)return 1;
        if(s->control_sequence==UINT32_MAX) {
            s->stats.faults|=FDC_FAULT_SEQUENCE_EXHAUSTED;return 1;
        }
        s->sending=s->latest;s->latest_valid=0;
        s->sending.sequence=++s->control_sequence;
        s->control_started=now;s->control_phase=C_PUBLISH;return 1;
    case C_PUBLISH:
        if(s->io.publish) {
            int result=s->io.publish(s->io.context,&s->sending);
            if(result==FDC_PUBLISH_CANCELLED) {
                ++s->stats.output_cancelled;s->control_phase=C_IDLE;return 0;
            }
            if(result!=FDC_PUBLISH_BUSY && result!=FDC_PUBLISH_APPLIED) {
                s->stats.faults|=FDC_FAULT_CONTROL_REJECTED;return 0;
            }
            if(!result)return 0;
        }
        ++s->stats.output_applied;s->control_index=0;s->control_phase=C_SHADOW;return 0;
    case C_SHADOW: {
        /* snapshot_index is independent of the immutable control payload. */
        unsigned index=s->control_index;
        static const uint8_t off[5]={FDC_REG_CTRL_EPOCH,FDC_REG_CTRL_SEQUENCE,
            FDC_REG_CTRL_PITCH,FDC_REG_CTRL_GAIN,FDC_REG_CTRL_FLAGS};
        uint32_t data=index==0?s->sending.epoch:index==1?s->sending.sequence:
            index==2?s->sending.pitch_code:index==3?s->sending.gain_q16:s->sending.active;
        if(write_reg(s,off[index],data)) {
            ++s->control_index;
            if(index==4)s->control_phase=C_COMMIT;
        }
        return 1;
    }
    case C_COMMIT:
        if(write_reg(s,FDC_REG_CTRL_COMMIT,1)) {
            ++s->stats.control_commits;s->control_phase=C_WAIT_BUSY;
        }
        return 1;
    case C_WAIT_BUSY:
        if(read_reg(s,FDC_REG_STATE,&value) && !(value&8u))s->control_phase=C_ACK;
        return 1;
    case C_ACK:
        if(read_reg(s,FDC_REG_CTRL_ACK,&value)) {
            if(value==s->sending.sequence){++s->stats.control_acks;s->control_phase=C_IDLE;}
            else s->control_phase=C_STATUS;
        }
        return 1;
    case C_STATUS:
        if(read_reg(s,FDC_REG_CTRL_STATUS,&value)) {
            if(value&8u)s->stats.faults|=FDC_FAULT_CONTROL_REJECTED;
            else s->control_phase=C_WAIT_BUSY;
        }
        return 1;
    default:return 0;
    }
}

bool fdc_ae350_init(fdc_ae350_t *s,const fdc_ae350_io_t *io,
                    const theremin_config_t *config)
{
    theremin_t checked;
    if(!s || !io || !io->now_us || !config ||
       !theremin_init(&checked,config))return false;
    clear_bytes(s,sizeof(*s));s->io=*io;s->algorithm=checked;
    s->config_revision=1;reset_algorithm(s);return true;
}

bool fdc_ae350_set_enabled(fdc_ae350_t *s,bool enabled)
{
    if(!s || !s->algorithm.configured || s->stats.faults)return false;
    if(s->stats.enabled!=(uint8_t)enabled) {
        s->stats.enabled=(uint8_t)enabled;reset_algorithm(s);
    }
    return true;
}

bool fdc_ae350_request_restart(fdc_ae350_t *s)
{
    if(!s || !s->algorithm.configured || s->restart_requested || s->stats.faults)
        return false;
    s->restart_requested=1;reset_algorithm(s);return true;
}

bool fdc_ae350_stage_config(fdc_ae350_t *s,const theremin_config_t *config)
{
    theremin_t checked;
    if(!s || !s->algorithm.configured || !config || !theremin_init(&checked,config))
        return false;
    s->staged=*config;s->staged_valid=1;return true;
}

bool fdc_ae350_apply_config(fdc_ae350_t *s)
{
    if(!s || !s->staged_valid || s->stats.faults || s->config_revision==UINT32_MAX)
        return false;
    if(!theremin_init(&s->algorithm,&s->staged))return false;
    ++s->config_revision;s->staged_valid=0;reset_algorithm(s);return true;
}

unsigned fdc_ae350_poll(fdc_ae350_t *s)
{
    uint32_t value;
    unsigned work=0;
    if(!s || !s->algorithm.configured || s->stats.faults)return 0;
    uint64_t now=s->io.now_us(s->io.context);
    ++s->stats.polls;
    if(s->have_now && now<s->last_now){s->stats.faults|=FDC_FAULT_TIME;return 0;}
    s->last_now=now;s->have_now=1;
    if(s->probe<P_DONE) {
        static const uint8_t off[3]={FDC_REG_ID,FDC_REG_ABI,FDC_REG_CAP};
        if(!read_reg(s,off[s->probe],&value))return 1;
        if(s->probe==P_ID && value!=FDC_AE350_ID)s->stats.faults|=FDC_FAULT_ID;
        if(s->probe==P_ABI && value!=FDC_AE350_ABI)s->stats.faults|=FDC_FAULT_ABI;
        if(s->probe==P_CAP) {
            s->stats.available=(uint8_t)((value&7u)==7u);s->stats.probed=1;
        }
        ++s->probe;return 1;
    }
    if(!s->stats.available)return 0;
    if(s->restart_requested && s->control_phase==C_IDLE &&
       s->snapshot_phase==S_IDLE) {
        if(!s->restart_sent) {
            if(!read_reg(s,FDC_REG_STATE,&value))return 1;
            if(value&15u)return 1;
            if(write_reg(s,FDC_REG_COMMAND,4))s->restart_sent=1;
            return 2;
        }
        if(!read_reg(s,FDC_REG_STATE,&value))return 1;
        if(!(value&4u)) {
            s->restart_requested=s->restart_sent=0;
            s->next_snapshot=now;s->latest_valid=0;s->await_epoch=1;
        }
        return 1;
    }
    work+=snapshot_step(s,now);
    if(s->stats.faults)return work;
    theremin_sample_t sample[2];
    bool fresh=false;
    bool consume=s->frame_pending && (!s->algorithm.have_time || now>s->algorithm.update_at_us);
    if(consume)fresh=frame_samples(s,now,sample);
    if((!s->algorithm.have_time || now>s->algorithm.update_at_us) &&
       (s->frame_pending || !s->algorithm.have_time || now-s->last_update>=FDC_AE350_POLL_US)) {
        if(theremin_update_timed(&s->algorithm,now,
            fresh?&sample[0]:NULL,s->sample_at,
            fresh?&sample[1]:NULL,s->sample_at,&s->output)) {
            s->last_update=now;queue_output(s,fresh);
        }
    }
    if(consume)s->frame_pending=0;
    if(!s->stats.faults)work+=control_step(s,now);
    return work;
}
bool fdc_ae350_quiescent(const fdc_ae350_t *s)
{
    return s && s->algorithm.configured && s->control_phase==C_IDLE &&
        s->snapshot_phase==S_IDLE && !s->frame_pending &&
        !s->restart_requested && !s->restart_sent && !s->await_epoch;
}
#else
bool fdc_ae350_init(fdc_ae350_t *s,const fdc_ae350_io_t *io,
                    const theremin_config_t *config)
{(void)io;(void)config;if(!s)return false;clear_bytes(s,sizeof(*s));return true;}
unsigned fdc_ae350_poll(fdc_ae350_t *s){(void)s;return 0;}
bool fdc_ae350_quiescent(const fdc_ae350_t *s){return s!=NULL;}
bool fdc_ae350_set_enabled(fdc_ae350_t *s,bool enabled){(void)s;return !enabled;}
bool fdc_ae350_request_restart(fdc_ae350_t *s){(void)s;return false;}
bool fdc_ae350_stage_config(fdc_ae350_t *s,const theremin_config_t *c)
{(void)s;(void)c;return false;}
bool fdc_ae350_apply_config(fdc_ae350_t *s){(void)s;return false;}
#endif

const theremin_config_t *fdc_ae350_active_config(const fdc_ae350_t *s)
{return s && s->algorithm.configured?&s->algorithm.config:NULL;}
const theremin_config_t *fdc_ae350_staged_config(const fdc_ae350_t *s)
{return s && s->staged_valid?&s->staged:NULL;}
const fdc_ae350_stats_t *fdc_ae350_stats(const fdc_ae350_t *s)
{return s?&s->stats:NULL;}
