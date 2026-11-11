// Included after the bounded JSON helpers in http_v2.inc.h.
#include "fm_diagnostics.h"
#if CONFIG_FPGA_FM_ONLY
static esp_err_t json_diagnostic_context(api_req_t *req,engine_fm_diagnostic_context_t c)
{
    return json_format(req,"{\"cached\":true,\"valid\":%s,\"session_id\":%" PRIu32 ",\"engine_epoch\":%" PRIu32 ",\"topology_generation\":%" PRIu32 "}",
        c.valid?"true":"false",c.session_id,c.engine_epoch,c.topology_generation);
}
static esp_err_t json_diagnostic_value(api_req_t *req,const engine_fm_diagnostic_block_t *block,unsigned index)
{
    return block->read_ok?json_format(req,"%" PRIu32,block->words[index]):json_format(req,"null");
}
static esp_err_t json_diagnostic_fields(api_req_t *req,const engine_fm_diagnostic_block_t *block,
    const char *const *names,const unsigned *indices,unsigned count)
{
    JSON_TRY(json_format(req,"{"));
    for(unsigned i=0;i<count;i++) {
        JSON_TRY(json_format(req,"%s\"%s\":",i?",":"",names[i]));
        JSON_TRY(json_diagnostic_value(req,block,indices?indices[i]:i));
    }
    return json_format(req,"}");
}
static esp_err_t json_diagnostic_raw_cache(api_req_t *req,const engine_fm_diagnostics_t *d)
{
    if(!d->raw_cache_valid)return json_format(req,"null");
    const raw_midi_state_t *r=&d->raw_cache;
    JSON_TRY(json_format(req,"{\"cached\":true,\"captured_at_us\":%" PRId64 ",\"available\":%s,\"initialized\":%s,\"enabled\":%s,\"ready\":%s,\"inflight\":%s,",
        d->raw_cached_at_us,r->available?"true":"false",r->initialized?"true":"false",r->enabled?"true":"false",r->ready?"true":"false",r->inflight?"true":"false"));
    JSON_TRY(json_format(req,"\"accepted\":%" PRIu32 ",\"rejected\":%" PRIu32 ",\"events\":%" PRIu32 ",\"usb_status\":%" PRIu32 ",\"fault\":%" PRIu32 ",\"reset_count\":%" PRIu32 ",\"inflight_opcode\":null,\"inflight_source\":null,\"inflight_payload\":null,\"source\":[",
        r->accepted,r->rejected,r->events,r->usb_status,r->fault,r->reset_count));
    for(unsigned i=0;i<4;i++) {
        const raw_midi_source_state_t *s=&r->source[i];
        JSON_TRY(json_format(req,"%s{\"id\":%u,\"session\":%" PRIu64 ",\"queued_bytes\":%" PRIu32 ",\"sent_bytes\":%" PRIu32 ",\"dropped_bytes\":%" PRIu32 ",\"flushes\":%" PRIu32 ",\"fault\":%" PRIu32 ",\"flushing\":%s,\"lease_expires_at_us\":null}",
            i?",":"",i,s->session,s->queued_bytes,s->sent_bytes,s->dropped_bytes,s->flushes,s->fault,s->flushing?"true":"false"));
    }
    return json_format(req,"]}");
}
#endif
static esp_err_t core_v2_fm_diagnostics(api_req_t *req)
{
#if !CONFIG_FPGA_FM_ONLY
    return core_v2_error(req,"404 Not Found","fm_diagnostics_unsupported");
#else
    // These are RAM-only calls. The HTTP task never reads/writes FPGA SPI,
    // creates a musical session, renews a lease or enqueues an owner event.
    engine_fm_diagnostics_t d;api_diagnostics(req,&d);
    int64_t now=req->data->prepared_at;
    JSON_TRY(json_begin(req));
    JSON_TRY(json_format(req,"{\"api_major\":2,\"supported\":%s,\"non_atomic\":true,\"request_id\":%" PRIu32 ",\"sample_request_id\":%" PRIu32 ",\"pending\":%s,\"valid\":%s,\"error\":%" PRIu32 ",\"error_count\":%" PRIu32 ",",
        d.supported?"true":"false",d.request_id,d.sample_request_id,d.pending?"true":"false",d.valid?"true":"false",d.error,d.error_count));
    JSON_TRY(json_format(req,"\"requested_at_us\":%" PRId64 ",\"started_at_us\":%" PRId64 ",\"completed_at_us\":%" PRId64 ",\"age_us\":",d.requested_at_us,d.started_at_us,d.completed_at_us));
    if(d.sample_request_id)JSON_TRY(json_format(req,"%" PRId64,now>d.completed_at_us?now-d.completed_at_us:0));
    else JSON_TRY(json_format(req,"null"));
    JSON_TRY(json_format(req,",\"context_before\":"));JSON_TRY(json_diagnostic_context(req,d.context_before));
    JSON_TRY(json_format(req,",\"context_after\":"));JSON_TRY(json_diagnostic_context(req,d.context_after));
    JSON_TRY(json_format(req,",\"blocks\":["));
    static const char *const block_names[]={"validation","local_commands","mid1","identity_after"};
    for(unsigned i=0;i<ENGINE_FM_DIAGNOSTIC_BLOCKS;i++) {
        const engine_fm_diagnostic_block_t *b=&d.blocks[i];
        JSON_TRY(json_format(req,"%s{\"name\":\"%s\",\"word_address\":%u,\"word_count\":%u,\"started_at_us\":%" PRId64 ",\"completed_at_us\":%" PRId64 ",\"read_ok\":%s,\"words\":",
            i?",":"",block_names[i],b->address,b->count,b->started_at_us,b->completed_at_us,b->read_ok?"true":"false"));
        if(b->read_ok) {
            JSON_TRY(json_format(req,"["));
            for(unsigned j=0;j<b->count && j<ENGINE_FM_DIAGNOSTIC_WORDS;j++)JSON_TRY(json_format(req,"%s%" PRIu32,j?",":"",b->words[j]));
            JSON_TRY(json_format(req,"]"));
        } else JSON_TRY(json_format(req,"null"));
        JSON_TRY(json_format(req,"}"));
    }
    JSON_TRY(json_format(req,"],\"validation\":"));
    static const char *const validation[]={"identity","abi_version","expression_applied_data","expression_generation","flags_raw","backend_accepted","backend_completed","nonzero_frames_cumulative","historical_peak","raw_dispatch_count","resampler_underflows"};
    JSON_TRY(json_diagnostic_fields(req,&d.blocks[0],validation,NULL,11));
    JSON_TRY(json_format(req,",\"local_commands\":"));
    static const char *const local[]={"accepted","completed","result"};
    JSON_TRY(json_diagnostic_fields(req,&d.blocks[1],local,NULL,3));
    JSON_TRY(json_format(req,",\"mid1\":"));
    static const char *const mid1[]={"magic","status_raw","accepted","rejected","events","usb_status_raw"};
    static const unsigned mid1_indices[]={0,2,3,4,5,6};
    JSON_TRY(json_diagnostic_fields(req,&d.blocks[2],mid1,mid1_indices,6));
    JSON_TRY(json_format(req,",\"raw_cache\":"));JSON_TRY(json_diagnostic_raw_cache(req,&d));
    JSON_TRY(json_format(req,",\"unavailable_fields\":[\"last_opcode\",\"core_operator_key_on\",\"actual_keyoff_confirmation\",\"lease_deadlines\",\"inflight_command_details\"],"));
    JSON_TRY(json_format(req,"\"semantics\":{\"valid\":\"complete reads with matching FMV1/ABI and cached context; not an atomic snapshot or acoustic proof\",\"local_completed\":\"includes cancellation before backend dispatch; equality does not prove NoteOff\","));
    JSON_TRY(json_format(req,"\"raw_dispatch_count\":\"same counter as MID1 events; includes delivered or drained events, not processed Offs\",\"nonzero_frames_cumulative\":\"compare successive samples; not current sound state\",\"historical_peak\":\"historical maximum, not current amplitude\"}}"));
    return json_end(req);
#endif
}
