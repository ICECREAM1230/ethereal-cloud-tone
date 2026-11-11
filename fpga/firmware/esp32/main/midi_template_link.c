/* Performance-template transport; called exclusively by engine_link's SPI
 * owner. Frozen layout: interfaces/midi_control/{dds,ks-fm}-template.md,
 * 2026-10-16. fpga_word_read_burst verifies the frame's 0xA5 identity. */
#include "midi_template_link.h"
#include "spi_regs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "fm_only_contract.h"
#include "ks_timbre_contract.h"
#include "ae350_control.h"
#include "sub_patch1.h"
#include <stddef.h>
#include <string.h>

#define ACK_TIMEOUT_US 100000
#define HEALTH_POLL_US 20000
#define BURST_WORDS 12u
#define SERVICE_TRANSACTIONS 12u

typedef enum { IDLE, UPLOAD, COMMIT_DDS, COMMIT_KS, WAIT_ACK, FAULT } phase_t;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static midi_template_image_t pending, active;
static bool pending_valid;
static uint32_t pending_revision, active_revision,active_ks_revision;
static midi_template_link_state_t observed;
/* Owner-only fields; publish never edits an in-flight transfer. */
static phase_t phase;
static unsigned region, offset;
static uint32_t generation, old_dds, old_ks, fence_generation;
static bool known_ack;
static uint32_t fm_old_expression, fm_fence_expression;
static int64_t committed_at, health_at, recovery_at;
enum { DOMAIN_DDS=1u, DOMAIN_KS=2u, DOMAIN_FM=4u, DOMAIN_NATIVE=3u };
/* active holds the immutable in-flight image, then the settled image. A
 * cache bit is revoked on failure, so desired data can never stand in for
 * native confirmation. FM's cache bit means sent, not acknowledged. */
static unsigned cached_domains,transfer_domains;
static uint32_t native_generation[2],native_fence[2];
static bool timbre_probe_enabled;
static uint32_t canceled_model_generation;
static struct {uint32_t model;control_source_ref_t source;int64_t expires;} timbre_source;
void midi_template_link_bind_timbre_source(uint32_t model,control_source_ref_t source,int64_t expires)
{
    portENTER_CRITICAL(&lock);timbre_source.model=model;timbre_source.source=source;timbre_source.expires=expires;portEXIT_CRITICAL(&lock);
}
void midi_template_link_cancel_timbre(uint32_t generation_i)
{portENTER_CRITICAL(&lock);canceled_model_generation=generation_i;portEXIT_CRITICAL(&lock);}
static unsigned timbre_probe_phase;
static uint32_t timbre_probe_first,timbre_probe_words[16];
static int64_t timbre_health_at;
static int64_t sub_patch_health_at;
static void sub_patch_observe(void)
{
    if(!CONFIG_FPGA_AE350_CONTROL || esp_timer_get_time()<sub_patch_health_at)return;
    sub_patch_health_at=esp_timer_get_time()+20000;
    uint32_t cap=0;bool ok=fpga_word_read_burst_observe(SUB_PATCH1_CAP_REG,&cap,1);
    portENTER_CRITICAL(&lock);
    observed.sub_patch_valid=ok;
    if(ok)observed.sub_patch_supported=cap==SUB_PATCH1_MAGIC;
    portEXIT_CRITICAL(&lock);
}
void midi_template_link_timbre_enable(void) { portENTER_CRITICAL(&lock);timbre_probe_enabled=true;portEXIT_CRITICAL(&lock); }
static void timbre_observe(void)
{
    portENTER_CRITICAL(&lock);bool probe=timbre_probe_enabled;portEXIT_CRITICAL(&lock);
    if(CONFIG_FPGA_AE350_CONTROL) {
        // ABI2 does not implement the KST model. Observe terminal rejection
        // without advertising legacy KST capabilities or inventing success.
        if((!probe && !(native_fence[1] && active.ks_header[0])) || esp_timer_get_time()<timbre_health_at)return;
        uint32_t terminal[3];
        bool ok=fpga_word_read_burst(KST_ADDR_TEMPLATE_GENERATION,terminal,3);
        portENTER_CRITICAL(&lock);
        observed.timbre_available=false;observed.timbre_valid=false;
        if(ok) {
            observed.timbre_template=terminal[0];observed.timbre_completed_template=terminal[1];observed.timbre_result=terminal[2];
            observed.timbre_requested_template=terminal[1];
        }
        portEXIT_CRITICAL(&lock);
        timbre_health_at=esp_timer_get_time()+(phase==WAIT_ACK || phase==FAULT?0:HEALTH_POLL_US);
        return;
    }
    if(!probe || esp_timer_get_time()<timbre_health_at)return;
    bool ok=false;
    if(timbre_probe_phase==0)ok=fpga_word_read_burst(KST_ADDR_SNAPSHOT,&timbre_probe_first,1);
    else if(timbre_probe_phase==1)ok=fpga_word_read_burst(KST_ADDR_CAP,timbre_probe_words,12);
    else if(timbre_probe_phase==2)ok=fpga_word_read_burst(KST_ADDR_TEMPLATE_GENERATION,timbre_probe_words+12,4);
    else {
        uint32_t last;ok=fpga_word_read_burst(KST_ADDR_SNAPSHOT,&last,1);
        if(ok && last!=timbre_probe_first) {timbre_probe_phase=0;return;}
        if(ok) {
            uint32_t *w=timbre_probe_words;
            bool valid=w[0]==KST_MAGIC && w[1]==KST_ABI && w[2]==0x3fu &&
                !(w[4]&~0x10fu) && (w[4]&15u)<=4 && w[7]<=2 && w[8]==1 && w[14]<=2;
            portENTER_CRITICAL(&lock);
            observed.timbre_available=w[0]==KST_MAGIC && w[1]==KST_ABI;
            observed.timbre_valid=valid;observed.timbre_abi=w[1];observed.timbre_capabilities=w[2];
            observed.timbre_snapshot=last;observed.timbre_state=w[4];
            observed.timbre_requested=w[5];observed.timbre_applied=w[6];observed.timbre_id=w[7];
            observed.timbre_version=w[8];observed.timbre_factory=w[9];observed.timbre_crc=w[10];
            observed.timbre_image_crc=w[11];observed.timbre_template=w[12];
            observed.timbre_completed_template=w[13];observed.timbre_result=w[14];
            observed.timbre_requested_template=w[15];portEXIT_CRITICAL(&lock);
        }
        if(!ok) {portENTER_CRITICAL(&lock);observed.timbre_valid=false;portEXIT_CRITICAL(&lock);}
        timbre_probe_phase=0;timbre_health_at=esp_timer_get_time()+(phase==WAIT_ACK?0:HEALTH_POLL_US);
        return;
    }
    if(ok)timbre_probe_phase++;
    else {
        portENTER_CRITICAL(&lock);observed.timbre_valid=false;portEXIT_CRITICAL(&lock);
        timbre_probe_phase=0;timbre_health_at=esp_timer_get_time()+(phase==WAIT_ACK?0:HEALTH_POLL_US);
    }
}
static uint32_t timbre_crc_feed(uint32_t crc,uint32_t w)
{
    for(int byte=24;byte>=0;byte-=8) {crc^=(w>>byte)&255;for(unsigned b=0;b<8;b++)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    return crc;
}
static void timbre_finalize(void)
{
    uint32_t crc=~0u;
    for(unsigned i=0;i<128;i++)for(unsigned j=0;j<2;j++)crc=timbre_crc_feed(crc,active.ks_notes[i][j]);
    for(unsigned i=0;i<128;i++)crc=timbre_crc_feed(crc,active.ks_pan[i]);
    for(unsigned i=0;i<64;i++)crc=timbre_crc_feed(crc,i==61?0:active.ks_header[i]);
    active.ks_header[61]=~timbre_crc_feed(crc,native_generation[1]);
}
static int64_t native_health_at[2];

static bool ks_same(const midi_template_image_t *a,const midi_template_image_t *b)
{
    size_t start=offsetof(midi_template_image_t,ks_notes),end=offsetof(midi_template_image_t,fm_expression);
    size_t crc=offsetof(midi_template_image_t,ks_header)+KST_H_IMAGE_CRC*sizeof(uint32_t);
    return !memcmp((const char *)a+start,(const char *)b+start,crc-start) &&
        !memcmp((const char *)a+crc+4,(const char *)b+crc+4,end-crc-4);
}

/* Called with lock held; uint32_t arrays have no intervening padding. */
static unsigned changed_domains(void)
{
    unsigned changed=0;
    size_t ks=offsetof(midi_template_image_t,ks_notes);
    if(!(cached_domains&DOMAIN_DDS) || memcmp(&pending,&active,ks))changed|=DOMAIN_DDS;
    if(!(cached_domains&DOMAIN_KS) || !ks_same(&pending,&active))changed|=DOMAIN_KS;
    if(!(cached_domains&DOMAIN_FM) || pending.fm_expression!=active.fm_expression)changed|=DOMAIN_FM;
    return changed;
}

static void timbre_cancel_locked(int64_t now)
{
    // The binding belongs only to the unadopted model candidate. Once native
    // adoption is observed, ordinary shared parameter refreshes are unbound.
    if(timbre_source.model && observed.ks_applied_model_generation==timbre_source.model)
        memset(&timbre_source,0,sizeof timbre_source);
    if(timbre_source.model && timbre_source.source.epoch &&
       (now>=timbre_source.expires || !control_source_valid(timbre_source.source)))
        canceled_model_generation=timbre_source.model;
    uint32_t canceled=canceled_model_generation;
    if(canceled && observed.ks_applied_model_generation==canceled) {canceled_model_generation=0;canceled=0;}
    if(canceled && pending_valid && pending.ks_header[KST_H_MODEL_GENERATION]==canceled) {
        pending_valid=false;observed.ks_canceled_model_generation=canceled;
    }
    if(canceled && active.ks_header[KST_H_MODEL_GENERATION]==canceled &&
       (transfer_domains&DOMAIN_KS) && !native_fence[1] &&
       (phase==UPLOAD || phase==COMMIT_DDS || phase==COMMIT_KS)) {
        /* Never discard an unrelated native DDS COMMIT. The normal state
         * machine still waits for its acknowledgement after removing KS. */
        transfer_domains&=~DOMAIN_KS;cached_domains&=~DOMAIN_KS;
        observed.ks_canceled_model_generation=canceled;
    }
    if(canceled && !native_fence[1] &&
       !(pending_valid && pending.ks_header[KST_H_MODEL_GENERATION]==canceled) &&
       !(active.ks_header[KST_H_MODEL_GENERATION]==canceled && (transfer_domains&DOMAIN_KS) && phase!=IDLE && phase!=FAULT))
        observed.ks_canceled_model_generation=canceled;
    if(canceled && phase==IDLE && !pending_valid)observed.busy=false;
}

void midi_template_link_init(void)
{
    portENTER_CRITICAL(&lock);
    memset(&observed,0,sizeof(observed));
    pending_valid=false;pending_revision=active_revision=0;
    timbre_probe_phase=0;timbre_health_at=0;canceled_model_generation=0;memset(&timbre_source,0,sizeof timbre_source);
    sub_patch_health_at=0;
    portEXIT_CRITICAL(&lock);
    phase=IDLE;region=offset=0;generation=old_dds=old_ks=fence_generation=0;
    known_ack=false;committed_at=health_at=recovery_at=0;
    fm_old_expression=fm_fence_expression=0;
    cached_domains=transfer_domains=0;
    memset(native_generation,0,sizeof(native_generation));
    memset(native_fence,0,sizeof(native_fence));
    memset(native_health_at,0,sizeof(native_health_at));
}

void midi_template_link_publish(const midi_template_image_t *image)
{
    if(!image)return;
    portENTER_CRITICAL(&lock);
    if(!CONFIG_FPGA_FM_ONLY && pending_revision && !observed.fault &&
       !memcmp(image,&pending,sizeof(pending))) {
        portEXIT_CRITICAL(&lock);return;
    }
    bool ks_changed=!pending_revision || !ks_same(image,&pending);
    pending=*image;
    if(!++pending_revision)++pending_revision;
    if(ks_changed)observed.ks_desired_revision=pending_revision;
    pending_valid=true;observed.ready=false;observed.busy=true;
    portEXIT_CRITICAL(&lock);
}

void midi_template_link_state(midi_template_link_state_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&lock);*out=observed;portEXIT_CRITICAL(&lock);
}

static void fail(unsigned reason)
{
    phase=FAULT;known_ack=false;recovery_at=0;
    portENTER_CRITICAL(&lock);
    cached_domains=0;
    pending_valid=false;observed.ready=false;observed.busy=false;
    observed.applied_generation=0;observed.fault=reason;
    portEXIT_CRITICAL(&lock);
}

static bool template_send(uint16_t address,const uint32_t *words,size_t n,uint32_t *commit_fence)
{
#if CONFIG_FPGA_AE350_CONTROL
    fpga_template_write_result_t result=fpga_template_write_burst(address,words,n);
    if(commit_fence && (result==FPGA_TEMPLATE_SENT || result==FPGA_TEMPLATE_UNCERTAIN))*commit_fence=words[0];
    if(result==FPGA_TEMPLATE_SENT)return true;
    if(result!=FPGA_TEMPLATE_DEFERRED)
        fail(result==FPGA_TEMPLATE_OVERFLOW?MIDI_TEMPLATE_LINK_OVERFLOW:
             result==FPGA_TEMPLATE_FORBIDDEN?MIDI_TEMPLATE_LINK_REJECTED:MIDI_TEMPLATE_LINK_IO);
    return false;
#else
    if(commit_fence)*commit_fence=words[0];
    fpga_word_write_burst(address,words,n);return true;
#endif
}

void midi_template_link_invalidate(void)
{
    // Called only by the SPI owner: cancel queued/ready state without ever
    // dropping the fence for a COMMIT whose CDC acknowledgement is unknown.
    fail(MIDI_TEMPLATE_LINK_RESET);
}

static void recovery_fault(unsigned reason)
{
    // An explicit publish after a failed COMMIT is a durable recovery intent.
    // Further timeouts/read errors report the fault without silently dropping
    // that intent; the old COMMIT still fences every shadow write.
    phase=FAULT;known_ack=false;
    portENTER_CRITICAL(&lock);
    observed.ready=false;observed.busy=pending_valid;
    observed.applied_generation=0;observed.fault=reason;
    portEXIT_CRITICAL(&lock);
}

static bool read_ack(uint32_t ack[2],unsigned domains)
{
    ack[0]=old_dds;ack[1]=old_ks;
    if(!(domains&DOMAIN_NATIVE))return true;
    bool ok=(domains&DOMAIN_NATIVE)==DOMAIN_NATIVE ? fpga_word_read_burst(0x0707,ack,2) :
        domains&DOMAIN_DDS ? fpga_word_read_burst(0x0707,ack,1) : fpga_word_read_burst(0x0708,ack+1,1);
    if(ok)return true;
    fail(MIDI_TEMPLATE_LINK_IO);return false;
}

static uint32_t image_word(unsigned r,unsigned i)
{
    switch(r) {
    case 0:return active.elements[i/8][i%8];
    case 1:return i ? active.bend_range : active.key_track;
    case 2:return active.amp_depth[i];
    case 3:return active.mod_depth[i];
    case 4:return active.ks_notes[i/2][i%2];
    case 5:return active.ks_pan[i];
    case 6:return active.ks_header[i];
    default:return active.fm_expression;
    }
}

static void fm_only_service(int64_t deadline_us,bool control_idle)
{
    for(unsigned transactions=0;transactions<SERVICE_TRANSACTIONS;transactions++) {
        int64_t now=esp_timer_get_time();
        if(now>=deadline_us)return;
        if(phase==IDLE || phase==FAULT) {
            bool queued;
            portENTER_CRITICAL(&lock);queued=pending_valid;portEXIT_CRITICAL(&lock);
            if(!queued) {
                if(phase==FAULT || !known_ack || now<health_at)return;
                uint32_t ack[2];
                if(!fpga_word_read_burst(FM_ONLY_REG_EXPRESSION,ack,2)) {fail(MIDI_TEMPLATE_LINK_IO);return;}
                health_at=esp_timer_get_time()+HEALTH_POLL_US;
                if(ack[1]!=old_dds || ack[0]!=fm_old_expression)fail(MIDI_TEMPLATE_LINK_RESET);
                return;
            }
            bool recovering=phase==FAULT;
            if((recovering && now<health_at) || (!recovering && !control_idle))return;
            uint32_t ack[2];
            if(!fpga_word_read_burst(FM_ONLY_REG_EXPRESSION,ack,2)) {
                if(recovering) {
                    health_at=esp_timer_get_time()+HEALTH_POLL_US;
                    recovery_fault(MIDI_TEMPLATE_LINK_IO);
                } else fail(MIDI_TEMPLATE_LINK_IO);
                return;
            }
            if(recovering)health_at=esp_timer_get_time()+HEALTH_POLL_US;
            // A real reset from a previously confirmed nonzero generation
            // proves an old apply cannot still arrive. Zero before the first
            // ACK is ambiguous and must keep an unknown-write fence intact.
            bool reset_ack=old_dds && !ack[1];
            bool fence_ack=ack[1]==fence_generation && ack[0]==fm_fence_expression;
            if(fence_generation && !fence_ack && !reset_ack) {
                if(!recovery_at)recovery_at=now;
                if(now-recovery_at>=ACK_TIMEOUT_US)recovery_fault(MIDI_TEMPLATE_LINK_TIMEOUT);
                return;
            }
            if(known_ack && (ack[1]!=old_dds || ack[0]!=fm_old_expression)) {
                if(reset_ack)fence_generation=0;
                fail(MIDI_TEMPLATE_LINK_RESET);return;
            }
            if(!control_idle)return;
            old_dds=ack[1];fm_old_expression=ack[0];known_ack=true;fence_generation=0;recovery_at=0;
            generation=ack[1]+1;if(!generation)generation=1;
            portENTER_CRITICAL(&lock);
            active=pending;active_revision=pending_revision;pending_valid=false;active_ks_revision=observed.ks_desired_revision;
            observed.generation=generation;observed.ready=false;observed.busy=true;
            observed.fault=MIDI_TEMPLATE_LINK_OK;
            portEXIT_CRITICAL(&lock);
            phase=UPLOAD;
            continue;
        }
        if(phase==UPLOAD) {
            fence_generation=generation;fm_fence_expression=active.fm_expression & 0x7f7fu;
            // b400 is the real FM expression mailbox. DDS/KS shadow regions
            // and their COMMIT/ACK registers do not exist in this image.
            fpga_word_write(0xb400,fm_fence_expression);
            committed_at=esp_timer_get_time();phase=WAIT_ACK;return;
        }
        if(phase==WAIT_ACK) {
            if(now-committed_at>=ACK_TIMEOUT_US) {fail(MIDI_TEMPLATE_LINK_TIMEOUT);return;}
            uint32_t ack[2];
            if(!fpga_word_read_burst(FM_ONLY_REG_EXPRESSION,ack,2)) {fail(MIDI_TEMPLATE_LINK_IO);return;}
            if(ack[1]!=old_dds && ack[1]!=generation) {
                if(old_dds && !ack[1])fence_generation=0;
                fail(MIDI_TEMPLATE_LINK_RESET);return;
            }
            // A burst can straddle the CDC observation; a mismatched data
            // word is retried and can never confirm a generation by itself.
            if(ack[1]!=generation || ack[0]!=fm_fence_expression)return;
            old_dds=generation;fm_old_expression=fm_fence_expression;
            fence_generation=0;known_ack=true;phase=IDLE;
            health_at=esp_timer_get_time()+HEALTH_POLL_US;
            portENTER_CRITICAL(&lock);
            observed.applied_generation=generation;
            observed.ready=!pending_valid && pending_revision==active_revision;
            observed.busy=pending_valid;
            portEXIT_CRITICAL(&lock);
            return;
        }
    }
}

void midi_template_link_service(int64_t deadline_us,bool control_idle,bool available)
{
    bool was_available;
    portENTER_CRITICAL(&lock);
    was_available=observed.available;observed.available=available;
    portEXIT_CRITICAL(&lock);
    if(!available) {
        portENTER_CRITICAL(&lock);observed.sub_patch_valid=false;portEXIT_CRITICAL(&lock);
        if(was_available)fail(MIDI_TEMPLATE_LINK_UNAVAILABLE);
        return;
    }
    if(CONFIG_FPGA_FM_ONLY) {fm_only_service(deadline_us,control_idle);return;}
    if(esp_timer_get_time()>=deadline_us)return;
    sub_patch_observe();
    timbre_observe();
    if(phase==FAULT && native_fence[0] && native_fence[1] && active.ks_header[0]) {
        uint32_t ack;
        if(fpga_word_read_burst(0x0707,&ack,1) && ack==native_fence[0]) {
            old_dds=ack;native_fence[0]=0;
            portENTER_CRITICAL(&lock);cached_domains|=DOMAIN_DDS;observed.dds_applied_generation=ack;portEXIT_CRITICAL(&lock);
        }
    }
    /* A timed-out COMMIT is still observed without republishing it. A late
     * native terminal may retire its fence, never a timer or an unrelated ACK. */
    if(phase==FAULT && native_fence[1] && active.ks_header[0] && (observed.timbre_valid || CONFIG_FPGA_AE350_CONTROL) &&
       observed.timbre_completed_template==native_fence[1] && !(observed.timbre_state&0x100)) {
        uint32_t ack;
        bool adopted=observed.timbre_valid && !observed.timbre_result && !observed.timbre_state &&
            observed.timbre_template==native_fence[1] &&
            observed.timbre_applied==active.ks_header[KST_H_MODEL_GENERATION] &&
            observed.timbre_image_crc==active.ks_header[KST_H_IMAGE_CRC] &&
            fpga_word_read_burst(0x0708,&ack,1) && ack==native_fence[1];
        if(adopted || observed.timbre_result) {
            if(adopted)old_ks=native_fence[1];
            native_fence[1]=0;
            portENTER_CRITICAL(&lock);
            if(adopted) {
                cached_domains|=DOMAIN_KS;observed.ks_applied_generation=old_ks;
                observed.ks_applied_model_generation=active.ks_header[KST_H_MODEL_GENERATION];
                observed.ks_applied_crc=active.ks_header[KST_H_MODEL_CRC];
                observed.ks_applied_image_crc=active.ks_header[KST_H_IMAGE_CRC];
                observed.ks_applied_revision=active_ks_revision;
                if(canceled_model_generation==active.ks_header[KST_H_MODEL_GENERATION])canceled_model_generation=0;
                if(!native_fence[0]) {observed.fault=MIDI_TEMPLATE_LINK_OK;phase=IDLE;observed.applied_generation=generation;}
            } else observed.fault=MIDI_TEMPLATE_LINK_REJECTED;
            observed.busy=pending_valid || native_fence[0];
            observed.ready=adopted && !pending_valid && !native_fence[0];
            portEXIT_CRITICAL(&lock);
        }
    }
    portENTER_CRITICAL(&lock);
    timbre_cancel_locked(esp_timer_get_time());
    portEXIT_CRITICAL(&lock);
    for(unsigned transactions=0;transactions<SERVICE_TRANSACTIONS;transactions++) {
        int64_t now=esp_timer_get_time();
        if(now>=deadline_us)return;
        portENTER_CRITICAL(&lock);timbre_cancel_locked(now);portEXIT_CRITICAL(&lock);
        /* One domain's traffic must not keep another domain's acknowledged
         * cache alive indefinitely. Only actual native reads move its clock. */
        if(phase==IDLE && known_ack) {
            unsigned due=0;
            for(unsigned i=0;i<2;i++)if(now>=native_health_at[i])due|=1u<<i;
            if(due) {
                uint32_t ack[2];if(!read_ack(ack,due))return;
                if(((due&DOMAIN_DDS) && ack[0]!=old_dds) ||
                   ((due&DOMAIN_KS) && ack[1]!=old_ks)) {fail(MIDI_TEMPLATE_LINK_RESET);return;}
                for(unsigned i=0;i<2;i++)if(due&(1u<<i))native_health_at[i]=esp_timer_get_time()+HEALTH_POLL_US;
                continue;
            }
        }
        if(phase==IDLE || phase==FAULT) {
            bool queued;
            unsigned changed;
            portENTER_CRITICAL(&lock);
            queued=pending_valid;changed=queued?changed_domains():0;
            if(queued && !changed) {
                pending_valid=false;active_revision=pending_revision;
                observed.ready=true;observed.busy=false;
            }
            portEXIT_CRITICAL(&lock);
            if(queued && !changed)return;
            if(!queued)return;
            bool recovering=phase==FAULT;
            if(recovering && now<health_at)return;
            if(!recovering && !control_idle)return;
            unsigned needed=changed&DOMAIN_NATIVE;
            for(unsigned i=0;i<2;i++)if(native_fence[i])needed|=1u<<i;
            uint32_t ack[2]={old_dds,old_ks};
            bool readable=!(needed&DOMAIN_NATIVE) ||
                ((needed&DOMAIN_NATIVE)==DOMAIN_NATIVE ? fpga_word_read_burst(0x0707,ack,2) :
                 needed&DOMAIN_DDS ? fpga_word_read_burst(0x0707,ack,1) : fpga_word_read_burst(0x0708,ack+1,1));
            if(!readable) {
                if(recovering) {
                    health_at=esp_timer_get_time()+HEALTH_POLL_US;
                    recovery_fault(MIDI_TEMPLATE_LINK_IO);
                } else fail(MIDI_TEMPLATE_LINK_IO);
                return;
            }
            if(recovering)health_at=esp_timer_get_time()+HEALTH_POLL_US;
            /* Each unknown COMMIT fences only its own bank. An unrelated
             * ACK cannot retire it; zero before a first ACK is ambiguous. */
            if(native_fence[1] && (observed.timbre_valid || CONFIG_FPGA_AE350_CONTROL) &&
               observed.timbre_completed_template==native_fence[1] && observed.timbre_result)native_fence[1]=0;
            bool fenced=false;
            uint32_t old[2]={old_dds,old_ks};
            for(unsigned i=0;i<2;i++)if(native_fence[i]) {
                if(ack[i]==native_fence[i] || (old[i] && !ack[i]))native_fence[i]=0;
                else fenced=true;
            }
            if(fenced) {
                if(!recovery_at)recovery_at=now;
                if(now-recovery_at>=ACK_TIMEOUT_US)recovery_fault(MIDI_TEMPLATE_LINK_TIMEOUT);
                return;
            }
            if(known_ack && (((needed&DOMAIN_DDS) && ack[0]!=old_dds) ||
                             ((needed&DOMAIN_KS) && ack[1]!=old_ks))) {
                fail(MIDI_TEMPLATE_LINK_RESET);return;
            }
            if(!control_idle)return;
            portENTER_CRITICAL(&lock);
            /* publish can run during the reads above. Restart if its newest
             * image needs an ACK domain that was not read in this slice. */
            unsigned newest=changed_domains();
            if((newest&DOMAIN_NATIVE)&~needed) {portEXIT_CRITICAL(&lock);continue;}
            transfer_domains=newest;
            active=pending;active_revision=pending_revision;pending_valid=false;active_ks_revision=observed.ks_desired_revision;
            cached_domains&=~transfer_domains;
            if(!transfer_domains) {
                observed.ready=true;observed.busy=false;
                portEXIT_CRITICAL(&lock);return;
            }
            if(!++generation)++generation;
            observed.generation=generation;observed.ready=false;
            observed.busy=true;observed.fault=MIDI_TEMPLATE_LINK_OK;
            portEXIT_CRITICAL(&lock);
            old_dds=ack[0];old_ks=ack[1];known_ack=true;recovery_at=0;
            for(unsigned i=0;i<2;i++) {
                if(needed&(1u<<i))native_health_at[i]=esp_timer_get_time()+HEALTH_POLL_US;
                uint32_t last=ack[i];
                if(i==1 && active.ks_header[0] && (observed.timbre_valid || CONFIG_FPGA_AE350_CONTROL) &&
                   (int32_t)(observed.timbre_requested_template-last)>0)last=observed.timbre_requested_template;
                native_generation[i]=last+1;
                if(!native_generation[i])native_generation[i]=1;
            }
            if((transfer_domains&DOMAIN_KS) && active.ks_header[0])timbre_finalize();
            if(transfer_domains&DOMAIN_KS) {
                portENTER_CRITICAL(&lock);observed.ks_upload_generation=native_generation[1];
                observed.ks_upload_model_generation=active.ks_header[KST_H_MODEL_GENERATION];
                observed.ks_upload_crc=active.ks_header[KST_H_MODEL_CRC];
                observed.ks_upload_image_crc=active.ks_header[KST_H_IMAGE_CRC];portEXIT_CRITICAL(&lock);
            }
            region=offset=0;phase=UPLOAD;
            continue;
        }
        if(phase==UPLOAD) {
            static const uint16_t bases[]={0x8000,0x8040,0x8100,0x8180,0xb000,0xb200,KST_ADDR_HEADER,0xb400};
            static const unsigned counts[]={64,2,128,128,256,128,64,1};
            static const unsigned domains[]={DOMAIN_DDS,DOMAIN_DDS,DOMAIN_DDS,DOMAIN_DDS,DOMAIN_KS,DOMAIN_KS,DOMAIN_KS,DOMAIN_FM};
            while(region<sizeof(counts)/sizeof(*counts) && (!(transfer_domains&domains[region]) ||
                  (region==6 && !active.ks_header[0])))region++;
            if(region==sizeof(counts)/sizeof(*counts)) {phase=COMMIT_DDS;continue;}
            uint32_t words[BURST_WORDS];
            unsigned n=counts[region]-offset;if(n>BURST_WORDS)n=BURST_WORDS;
            for(unsigned i=0;i<n;i++)words[i]=image_word(region,offset+i);
            if(!template_send(bases[region]+offset,words,n,NULL))return;
            if(domains[region]==DOMAIN_FM) {
                portENTER_CRITICAL(&lock);observed.fm_sent_revision=active_revision;portEXIT_CRITICAL(&lock);
            }
            offset+=n;
            if(offset==counts[region]) {
                offset=0;
                if(++region==sizeof(counts)/sizeof(*counts))phase=COMMIT_DDS;
            }
            continue;
        }
        if(phase==COMMIT_DDS) {
            if(transfer_domains&DOMAIN_DDS) {
                if(!template_send(0x80ff,&native_generation[0],1,&native_fence[0]))return;
            }
            phase=COMMIT_KS;continue;
        }
        if(phase==COMMIT_KS) {
            if(transfer_domains&DOMAIN_KS) {
                if(!template_send(active.ks_header[0]?KST_ADDR_COMMIT:KST_ADDR_LEGACY_COMMIT,&native_generation[1],1,&native_fence[1]))return;
            }
            committed_at=esp_timer_get_time();phase=WAIT_ACK;return;
        }
        if(phase==WAIT_ACK) {
            if(now-committed_at>=ACK_TIMEOUT_US) {fail(MIDI_TEMPLATE_LINK_TIMEOUT);return;}
            if((transfer_domains&DOMAIN_KS) && active.ks_header[0]) {
                portENTER_CRITICAL(&lock);
                bool rejected=(observed.timbre_valid || CONFIG_FPGA_AE350_CONTROL) && observed.timbre_completed_template==native_generation[1] && observed.timbre_result;
                portEXIT_CRITICAL(&lock);
                if(rejected) {native_fence[1]=0;fail(MIDI_TEMPLATE_LINK_REJECTED);return;}
            }
            uint32_t ack[2];if(!read_ack(ack,transfer_domains))return;
            bool waiting=false;
            uint32_t old[2]={old_dds,old_ks};
            for(unsigned i=0;i<2;i++)if(transfer_domains&(1u<<i)) {
                if(ack[i]!=old[i] && ack[i]!=native_generation[i]) {
                    if(old[i] && !ack[i])native_fence[i]=0;
                    fail(MIDI_TEMPLATE_LINK_RESET);return;
                }
                if(ack[i]!=native_generation[i])waiting=true;
                native_health_at[i]=esp_timer_get_time()+HEALTH_POLL_US;
            }
            if(waiting)return;
            if(transfer_domains&DOMAIN_DDS){old_dds=ack[0];native_fence[0]=0;}
            if(transfer_domains&DOMAIN_KS){old_ks=ack[1];native_fence[1]=0;}
            known_ack=true;
            phase=IDLE;
            portENTER_CRITICAL(&lock);
            cached_domains|=transfer_domains;
            observed.dds_applied_generation=old_dds;
            observed.ks_applied_generation=old_ks;
            if(transfer_domains&DOMAIN_KS) {
                observed.ks_applied_revision=active_ks_revision;
                if(canceled_model_generation==active.ks_header[KST_H_MODEL_GENERATION])canceled_model_generation=0;
                observed.ks_applied_model_generation=active.ks_header[KST_H_MODEL_GENERATION];
                observed.ks_applied_crc=active.ks_header[KST_H_MODEL_CRC];
                observed.ks_applied_image_crc=active.ks_header[KST_H_IMAGE_CRC];
            }
            if(transfer_domains&DOMAIN_NATIVE)observed.applied_generation=generation;
            observed.ready=!pending_valid && pending_revision==active_revision;
            observed.busy=pending_valid;
            portEXIT_CRITICAL(&lock);
            return;
        }
    }
}
