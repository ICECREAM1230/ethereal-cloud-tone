// Core-owned reservation closes the interval between enqueue and owner
// claim. Snapshot and raw requests share one Web byte stream and one token.
#include "raw_midi.h"
enum { HTTP_PERF_PROBE, HTTP_PERF_CLAIM, HTTP_PERF_RENEW, HTTP_PERF_RELEASE };
static struct { uint32_t token,context;int64_t expires;control_source_ref_t source; } s_http_performance;
static const char *http_performance(uint32_t token,uint32_t context,int64_t now,unsigned action)
{
    if(s_http_performance.context!=context || now>=s_http_performance.expires ||
       (s_http_performance.token && !control_source_valid(s_http_performance.source)))
        memset(&s_http_performance,0,sizeof(s_http_performance));
    if(action==HTTP_PERF_RELEASE) {
        if(s_http_performance.token==token)memset(&s_http_performance,0,sizeof(s_http_performance));
        return NULL;
    }
    if(action==HTTP_PERF_RENEW) {
        if(s_http_performance.token==token)s_http_performance.expires=now+WEB_LEASE_US;
        return NULL;
    }
    raw_midi_state_t raw;raw_midi_state(&raw);
    if(!raw.available || !raw.enabled || !raw.ready)return "raw_not_ready";
    if((raw.source[MIDI_RAW_SOURCE_WEB].session && raw.source[MIDI_RAW_SOURCE_WEB].session!=token) ||
       (s_http_performance.token && s_http_performance.token!=token))return "raw_source_busy";
    if(action==HTTP_PERF_CLAIM) {
        s_http_performance.token=token;s_http_performance.context=context;
        s_http_performance.expires=now+WEB_LEASE_US;
        for(unsigned i=0;i<WEB_CLIENTS;i++)if(s_sessions[i].token==token)
            s_http_performance.source=s_sessions[i].source;
    }
    return NULL;
}
