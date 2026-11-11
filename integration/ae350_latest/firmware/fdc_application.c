/* SPDX-License-Identifier: MIT */
#include "fdc_application.h"
#include "../control_migration/fdc2214/firmware/fdc_ae350.h"
#include "../control_migration/fdc2214/integration/fdc_native_owner.h"

#if ENABLE_AE350_FDC
enum { APP_NONE, APP_STOP, APP_APPLY, APP_RESTART, APP_RESTART_WAIT };
static struct {
    cs_service_t *service;
    fdc_ae350_t sensor;
    fdc_native_owner_t owner;
    theremin_config_t staged;
    fdc_application_stats_t stats;
    uint64_t now_us;
    uint32_t last_ms, observed_cancellations;
} app;

static uint64_t now_us(void *context)
{ (void)context;return app.now_us; }
static int publish(void *context,const fdc_ae350_control_t *control)
{ (void)context;return fdc_native_publish(&app.owner,control); }
static int eligible(void)
{
    const cs_service_t *s=app.service;
    return s && (s->engines&(1u<<VM_SUBTRACTIVE)) && !s->fatal && !s->stats.fault && s->stats.ready &&
        (s->parallel_sources || (s->stats.state==CS_RUNNING && s->stats.mode==VM_SUBTRACTIVE &&
        s->stats.requested_mode==VM_SUBTRACTIVE && !s->mode_phase));
}
static int reject(uint32_t error)
{ ++app.stats.rejected;app.stats.last_error=error;return 0; }
static int accept(void)
{ ++app.stats.accepted;app.stats.last_error=0;return 1; }

int fdc_application_init(cs_service_t *service,uint32_t now_ms)
{
    /* Cold initialization only, matching cs_init and coordinated FPGA reset. */
    volatile unsigned char *p=(volatile unsigned char *)&app;
    for(unsigned i=0;i<sizeof(app);i++)p[i]=0;
    app.service=service;app.last_ms=now_ms;app.now_us=(uint64_t)now_ms*1000u;
    if(!service)return 0;
    fdc_native_owner_init(&app.owner,service);
    fdc_ae350_io_t io={0};io.now_us=now_us;io.publish=publish;
    app.staged=theremin_default_config();
    app.stats.initialized=(uint8_t)fdc_ae350_init(&app.sensor,&io,&app.staged);
    return app.stats.initialized;
}

int fdc_application_record(void *context,uint16_t address,uint32_t data)
{
    (void)context;
    if(!app.stats.initialized)return reject(FDC_APP_NOT_READY);
    if(address==0xb500) {
        if(data>1)return reject(FDC_APP_BAD_VALUE);
        if(!data) {
            app.stats.requested_enable=0;
            if(app.stats.operation==APP_NONE)app.stats.operation=APP_STOP;
            return accept();
        }
        if(app.stats.operation!=APP_NONE)return reject(FDC_APP_BUSY);
        if(!eligible() || !app.sensor.stats.probed || !app.sensor.stats.available)
            return reject(FDC_APP_NOT_READY);
        if(app.sensor.stats.faults)return reject(FDC_APP_DRIVER_FAULT);
        app.stats.requested_enable=1;return accept();
    }
    if(address==0xb501) {
        if(data!=1)return reject(FDC_APP_BAD_VALUE);
        if(app.stats.operation!=APP_NONE && app.stats.operation!=APP_STOP)
            return reject(FDC_APP_BUSY);
        if(!app.sensor.stats.probed || !app.sensor.stats.available)
            return reject(FDC_APP_NOT_READY);
        if(app.sensor.stats.faults)return reject(FDC_APP_DRIVER_FAULT);
        app.stats.requested_enable=0;app.stats.operation=APP_RESTART;
        return accept();
    }
    if(address>=0xb510 && address<=0xb515) {
        if(app.stats.operation!=APP_NONE && app.stats.operation!=APP_STOP)
            return reject(FDC_APP_BUSY);
        unsigned field=address-0xb510;
        if(field<4) {
            if(!data || data>=THEREMIN_RAW_LIMIT)return reject(FDC_APP_BAD_VALUE);
            if(field==0)app.staged.pitch.raw_far=data;
            else if(field==1)app.staged.pitch.raw_near=data;
            else if(field==2)app.staged.volume.raw_far=data;
            else app.staged.volume.raw_near=data;
        } else {
            union { uint32_t bits;float value; } word;word.bits=data;
            /* Validation is integer-only here; full span/noise validation
             * occurs atomically at B51E. Reject NaN/Inf/negative sigma. */
            if((data&UINT32_C(0x7f800000))==UINT32_C(0x7f800000) ||
                (data&UINT32_C(0x80000000)))return reject(FDC_APP_BAD_VALUE);
            if(field==4)app.staged.pitch.noise_sigma=word.value;
            else app.staged.volume.noise_sigma=word.value;
        }
        app.stats.staging_mask|=(uint8_t)(1u<<field);return accept();
    }
    if(address==0xb51e) {
        if(data!=1)return reject(FDC_APP_BAD_VALUE);
        if(app.stats.operation!=APP_NONE && app.stats.operation!=APP_STOP)
            return reject(FDC_APP_BUSY);
        if(app.stats.staging_mask!=63 ||
           !fdc_ae350_stage_config(&app.sensor,&app.staged))
            return reject(FDC_APP_BAD_CONFIG);
        app.stats.requested_enable=0;app.stats.operation=APP_APPLY;
        app.stats.staging_mask=0;return accept();
    }
    return reject(FDC_APP_BAD_VALUE);
}

void fdc_application_poll(uint32_t now_ms)
{
    app.now_us+=(uint64_t)(uint32_t)(now_ms-app.last_ms)*1000u;
    app.last_ms=now_ms;
    if(!app.stats.initialized)return;
    if(!eligible() || app.sensor.stats.faults ||
       app.owner.cancelled!=app.observed_cancellations) {
        app.stats.requested_enable=0;
        if(app.stats.operation==APP_NONE)app.stats.operation=APP_STOP;
    }
    app.observed_cancellations=app.owner.cancelled;
    if(app.stats.requested_enable && app.stats.operation==APP_NONE) {
        if(!app.owner.suspended || fdc_native_resume(&app.owner))
            (void)fdc_ae350_set_enabled(&app.sensor,1);
    } else {
        (void)fdc_ae350_set_enabled(&app.sensor,0);
        int stopped=fdc_native_stop(&app.owner);
        if(stopped<0)app.stats.last_error=FDC_APP_DRIVER_FAULT;
        if(stopped==1 && fdc_ae350_quiescent(&app.sensor)) {
            switch(app.stats.operation) {
            case APP_APPLY:
                if(fdc_ae350_apply_config(&app.sensor))++app.stats.applied_configs;
                else app.stats.last_error=FDC_APP_DRIVER_FAULT;
                app.stats.operation=APP_NONE;break;
            case APP_RESTART:
                if(fdc_ae350_request_restart(&app.sensor))
                    app.stats.operation=APP_RESTART_WAIT;
                else {app.stats.last_error=FDC_APP_DRIVER_FAULT;app.stats.operation=APP_NONE;}
                break;
            case APP_RESTART_WAIT:
                ++app.stats.restarts;app.stats.operation=APP_NONE;break;
            case APP_STOP:app.stats.operation=APP_NONE;break;
            default:break;
            }
        }
    }
    /* The driver performs at most2 MMIO accesses. No ACK/ready wait loop;
     * cs_poll and the original physical-key processing have already run. */
    (void)fdc_ae350_poll(&app.sensor);
}
void fdc_application_telemetry(uint32_t words[3])
{
    const fdc_ae350_stats_t *t=&app.sensor.stats;
    uint32_t present=t->telemetry_present?UINT32_C(1)<<30:0;
    uint32_t fresh=t->telemetry_present && t->telemetry_fresh &&
        app.now_us>=t->telemetry_at_us &&
        app.now_us-t->telemetry_at_us<=app.sensor.algorithm.config.stale_timeout_us &&
        !(t->diagnostic&2u)?UINT32_C(1)<<31:0;
    words[0]=t->telemetry_raw0|((uint32_t)t->telemetry_flags0<<28)|present|fresh;
    words[1]=t->telemetry_raw1|((uint32_t)t->telemetry_flags1<<28)|present|fresh;
    words[2]=app.sensor.output.gain_q16|((uint32_t)!!t->enabled<<17)|
        ((uint32_t)!!app.sensor.output.muted<<18)|
        ((uint32_t)!!app.sensor.output.pitch_stale<<19)|
        ((uint32_t)!!app.sensor.output.volume_stale<<20)|
        ((t->telemetry_sequence&255u)<<24);
}
uint32_t fdc_application_status_word(void)
{
    if(!app.stats.initialized)return 0;
    uint32_t flags=(app.sensor.stats.probed && app.sensor.stats.available?1u:0u)|
        (eligible()?2u:0u)|(app.stats.requested_enable?4u:0u)|
        (app.sensor.stats.enabled?8u:0u)|(app.stats.operation!=APP_NONE?16u:0u)|
        (app.sensor.stats.faults || (app.sensor.stats.diagnostic&2u)?32u:0u)|
        (app.stats.last_error?64u:0u)|(app.stats.applied_configs?128u:0u);
    return UINT32_C(0xfdc10000)|flags;
}
const fdc_application_stats_t *fdc_application_stats(void)
{ return &app.stats; }
#endif
