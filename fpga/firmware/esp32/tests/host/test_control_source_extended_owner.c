#define S3_KS_TIMBRE_HOST_TEST 1
#define S3_FDC_HOST_TEST 1
#define main previous_raw_owner_entry
#include "test_raw_web_owner.c"
#undef main
extern engine_local_state_t fake_local_state;
static unsigned ks_panics,candidate_cancels,fdc_cancels;
static engine_fdc_state_t fdc_view;
void engine_link_ks_state(engine_ks_state_t *s){memset(s,0,sizeof(*s));}
bool engine_link_ks_note_on(unsigned v,const engine_ks_note_t *n){(void)v;(void)n;return false;}
void engine_link_ks_note_off(unsigned v){(void)v;}
void engine_link_ks_panic(void){ks_panics++;}
bool engine_link_ks_mode(bool mode){return !mode;}
void engine_link_ks_gain(unsigned v,uint16_t l,uint16_t r){(void)v;(void)l;(void)r;}
void engine_link_ks_loss(unsigned v,uint16_t loss){(void)v;(void)loss;}
void engine_link_owner_panic_ack(void){}
void midi_template_link_timbre_enable(void){}
void midi_template_link_cancel_timbre(uint32_t model){candidate_cancels++;template_state.ks_canceled_model_generation=model;}
void engine_link_fdc_state(engine_fdc_state_t *s){*s=fdc_view;}
bool engine_link_fdc_request(const fdc_request_t *r)
{fdc_view.pending=true;fdc_view.request_id=r->event.c;return true;}
void engine_link_fdc_cancel(uint32_t id,bool disable)
{(void)id;fdc_cancels++;fdc_view.pending=false;fdc_view.disable_pending|=disable;}
void engine_link_fdc_stop(void){assert(!"source revoke must not call FDC STOP");}
static control_source_ref_t setup(void)
{
    memset(&fake_local_state,0,sizeof fake_local_state);s_owner_local_generation=s_owner_local_adopted=0;
    reset_owner();control_source_init();ks_timbre_store_init();
    memset(&s_kst,0,sizeof s_kst);s_kst_initialized=s_kst_job_active=s_kst_model_job=s_kst_store_job=false;
    s_kst_boot_checked=s_kst_cancel_wait=s_kst_boot_legacy_pending=s_kst_refresh_pending=false;
    s_kst_actual_generation=s_kst_expected_revision=0;s_kst_model_generation=s_kst_generation_counter=s_template_kst_generation=0;
    template_state=(midi_template_link_state_t){.ready=true,.available=true,.timbre_available=true,.timbre_valid=true,
        .timbre_abi=65536,.timbre_capabilities=63,.timbre_applied=3,.timbre_requested=3,.timbre_id=0,
        .timbre_version=1,.timbre_factory=1,.timbre_template=4,.ks_applied_generation=4};
    owner_kst_poll(fake_time);ks_panics=candidate_cancels=fdc_cancels=0;
    observed.source[0].session=900;observed.source[2].session=902;observed.source[3].session=903;
    control_source_ref_t source;assert(control_source_claim(CONTROL_SOURCE_WIFI,fake_time,&source));
    s_web[0].source=source;return source;
}
int main(void)
{
    control_source_ref_t source=setup();
    ks_timbre_request_t job={.op=KST_APPLY,.has_instrument=true,.instrument_id=1,.event={.client=0,.token=111,
        .sequence=1,.context_epoch=s_control_epoch,.expires=fake_time+WEB_LEASE_US}};
    assert(ks_timbre_reserve(&job,fake_time));
    assert(control_source_reserve(source,111,1,fake_time,job.event.expires,CONTROL_SOURCE_OUTPUT));
    evt_t event={.kind=EVT_WEB_KS_TIMBRE,.source=source,.web_v2=job.event};
    assert(owner_control_dispatch(&event,fake_time));assert(s_kst_job_active&&s_kst_model_job);
    control_source_revoke(source);assert(owner_control_sync());
    assert(!s_kst_job_active && !ks_panics && candidate_cancels==1 && !global_flushes && !observed.stop_requested);
    assert(observed.source[0].session==900 && observed.source[2].session==902 && observed.source[3].session==903);
    source=setup();fdc_view=(engine_fdc_state_t){.available=true,.valid=true,.fresh=true,.word=FDC_STATUS_TAG|3};
    fdc_request_t request={.op=FDC_ENABLE,.event={.client=0,.token=111,.sequence=1,
        .context_epoch=s_control_epoch,.expires=fake_time+WEB_LEASE_US}};
    assert(fdc_control_reserve(&request,fake_time));
    assert(control_source_reserve(source,111,1,fake_time,request.event.expires,CONTROL_SOURCE_OUTPUT));
    event=(evt_t){.kind=EVT_WEB_FDC,.source=source,.web_v2=request.event};
    assert(owner_control_dispatch(&event,fake_time));assert(s_fdc_owner.active);
    control_source_revoke(source);assert(owner_control_sync());
    assert(!s_fdc_owner.active && fdc_cancels==1 && !fdc_view.disable_pending);
    assert(!global_flushes && !observed.stop_requested && !ks_panics);
    assert(observed.source[0].session==900 && observed.source[2].session==902 && observed.source[3].session==903);
    puts("PASS source extended owner: KST candidate cancel without Panic; FDC retire without disable; USB/BLE/sensor preserved");
    return 0;
}
