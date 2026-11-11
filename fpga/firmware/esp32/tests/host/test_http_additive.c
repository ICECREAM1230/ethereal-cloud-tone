#define HTTP_SHARED_QUERY_IMPL
// Real wifi_web.c handlers and SDK boundary shared with the existing v1 test.
// Keep this focused harness independent of the old v2 fixture-file main.
#define main http_additive_old_v1_main
#include "test_http_api.c"
#undef main
#include "fake_additive_link.c"

size_t httpd_req_get_url_query_len(httpd_req_t *r)
{return r->query?strlen(r->query):0;}
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r,char *out,size_t size)
{
    if(!r->query || strlen(r->query)>=size)return ESP_FAIL;
    strcpy(out,r->query);return ESP_OK;
}

static web_control_state_t snapshot;
static additive_patch_t target;
static httpd_req_t captured;
static char patch_body[WEB_ADDITIVE_MAX_BODY_BYTES+1];

static void write_optional_fixture(const char *name)
{
    const char *directory=getenv("S3_ADDITIVE_FIXTURE_DIR");
    if(!directory || !*directory)return;
    char path[4096];int n=snprintf(path,sizeof(path),"%s/%s",directory,name);
    assert(n>0 && (size_t)n<sizeof(path));
    FILE *file=fopen(path,"wb");assert(file);
    assert(fwrite(captured.response,1,captured.response_len,file)==captured.response_len);
    assert(!fclose(file));
}

static void fixture(additive_patch_t *patch)
{
    memset(patch,0,sizeof(*patch));
    patch->bypass_lowpass=false;patch->centroid_q8=8191;patch->slope=1234;
    patch->comb_period=31;patch->comb_depth=22222;patch->master_gain=33333;
    patch->master_pan=44444;patch->bend_range=127;
    for(unsigned g=0;g<4;g++)patch->env[g]=(additive_envelope_t){
        .attack_inc=(uint16_t)(g*101+1),.decay_inc=(uint16_t)(g*107+2),
        .sustain=(uint16_t)(g*109+3),.release_inc=(uint16_t)(g*113+4)};
    for(unsigned k=0;k<32;k++)patch->partials[k]=(additive_partial_t){
        .ratio_q16=65536u*(k+1)+k,.weight=(uint16_t)(65535u-k*1000u),.group=(uint8_t)(k%4)};
}
static void same_patch(const additive_patch_t *a,const additive_patch_t *b)
{
    assert(a->bypass_lowpass==b->bypass_lowpass && a->centroid_q8==b->centroid_q8);
    assert(a->slope==b->slope && a->comb_period==b->comb_period && a->comb_depth==b->comb_depth);
    assert(a->master_gain==b->master_gain && a->master_pan==b->master_pan && a->bend_range==b->bend_range);
    for(unsigned g=0;g<4;g++) {
        assert(a->env[g].attack_inc==b->env[g].attack_inc && a->env[g].decay_inc==b->env[g].decay_inc);
        assert(a->env[g].sustain==b->env[g].sustain && a->env[g].release_inc==b->env[g].release_inc);
    }
    for(unsigned k=0;k<32;k++) {
        assert(a->partials[k].ratio_q16==b->partials[k].ratio_q16);
        assert(a->partials[k].weight==b->partials[k].weight && a->partials[k].group==b->partials[k].group);
    }
}
static void append_body(size_t *used,const char *format,...)
{
    va_list args;va_start(args,format);
    int n=vsnprintf(patch_body+*used,sizeof(patch_body)-*used,format,args);va_end(args);
    assert(n>=0 && (size_t)n<sizeof(patch_body)-*used);*used+=(size_t)n;
}
static const char *body(uint32_t token,uint32_t sequence,uint32_t context,
                        unsigned envelope_count,unsigned partial_count,bool forbidden)
{
    size_t used=0;
    append_body(&used,"{\"contract_version\":1,\"token\":%" PRIu32 ",\"sequence\":%" PRIu32 ",\"context_epoch\":%" PRIu32 ",\"patch\":{",token,sequence,context);
    if(forbidden)append_body(&used,"\"selected\":true,");
    append_body(&used,"\"bypass_lowpass\":false,\"centroid_q8\":%u,\"slope\":%u,\"comb_period\":%u,\"comb_depth\":%u,\"master_gain\":%u,\"master_pan\":%u,\"bend_range\":%u,\"envelopes\":[",
        target.centroid_q8,target.slope,target.comb_period,target.comb_depth,target.master_gain,target.master_pan,target.bend_range);
    for(unsigned g=0;g<envelope_count;g++) {
        const additive_envelope_t *e=&target.env[g%4];
        append_body(&used,"%s{\"attack_inc\":%u,\"decay_inc\":%u,\"sustain\":%u,\"release_inc\":%u}",g?",":"",e->attack_inc,e->decay_inc,e->sustain,e->release_inc);
    }
    append_body(&used,"],\"partials\":[");
    for(unsigned k=0;k<partial_count;k++) {
        const additive_partial_t *p=&target.partials[k%32];
        append_body(&used,"%s{\"ratio_q16\":%" PRIu32 ",\"weight\":%u,\"group\":%u}",k?",":"",p->ratio_q16,p->weight,p->group);
    }
    append_body(&used,"]}}");return patch_body;
}
static void reset(void)
{
    now=100;publication_count=0;random_call=0;
    memset(s_sessions,0,sizeof(s_sessions));memset(&s_http_performance,0,sizeof(s_http_performance));
    web_additive_queue_cancel_all();web_raw_queue_reset();additive_link_init();
    memset(&snapshot,0,sizeof(snapshot));patch_default(&snapshot.patch);fixture(&target);
    patch_default(&g_patch);
    snapshot.ready=true;snapshot.context_epoch=9;snapshot.revision=1;snapshot.ks_decay_ms=3000;
    snapshot.engine.available=snapshot.engine.valid=true;
    snapshot.requested_engine=snapshot.applied_engine=WEB_ENGINE_ADDITIVE;
    fake_additive=(additive_link_state_t){.available=true,.valid=true,.selected=true,
        .ready=true,.instance_id=42,.revision=7,.commit_id=23};
    additive_patch_default(&fake_additive.confirmed_patch);
    fake_additive_request_id=0;fake_additive_cancels=0;
    additive_link_state(&snapshot.additive.link);
    snapshot.token[0]=101;
    web_control_store(&snapshot);
    s_sessions[0].token=101;s_sessions[0].context_epoch=9;
    s_sessions[0].api_version=FM_HTTP_API_MAJOR;s_sessions[0].seen=now;
    http_raw_state=(raw_midi_state_t){.available=true,.initialized=true,.enabled=true,.ready=true};
}
static httpd_req_t *call(esp_err_t (*handler)(httpd_req_t *),const char *input,
                         const char *header,const char *status,const char *code)
{
    unsigned before=publication_count;uint32_t sequence=s_sessions[0].sequence;int64_t seen=s_sessions[0].seen;
    captured=request(input,header);
    assert(handler(&captured)==ESP_OK);
    if(strcmp(captured.status,status))fprintf(stderr,"expected %s got %s: %s\n",status,captured.status,captured.response);
    assert(!strcmp(captured.status,status));
    assert(!strcmp(captured.cache,"no-store") && !strcmp(captured.nosniff,"nosniff"));
    assert(!strcmp(captured.type,"application/json; charset=utf-8"));
    if(code)assert(strstr(captured.response,code));
    if(status[0]!='2')assert(publication_count==before && s_sessions[0].sequence==sequence && s_sessions[0].seen==seen);
    return &captured;
}
static void mode_selection(void)
{
    reset();snapshot.requested_engine=snapshot.applied_engine=0;web_control_store(&snapshot);
    call(v2_command,"101 1 9 5 3 0 0","1","202 Accepted",NULL);
    assert(publication_count==1 && published[0].kind==EVT_WEB_V2);
    assert(published[0].web_v2.op==FM_HTTP_OPCODE_ENGINE_SELECT && published[0].web_v2.a==3);
    assert(published[0].web_v2.context_epoch==9 && s_sessions[0].sequence==1);
    web_control_state_t unchanged;web_control_read(&unchanged);
    assert(!unchanged.applied_engine && !fake_additive_request_id && !fake_additive_cancels);
    call(v2_command,"101 2 9 5 3 1 0","1","400 Bad Request","invalid_command");
    snapshot.additive.link.valid=false;web_control_store(&snapshot);
    call(v2_command,"101 2 9 5 3 0 0","1","503 Service Unavailable","additive_unavailable");
}
static void strict_ingress_and_sessions(void)
{
    reset();
    call(additive_patch_post,body(101,1,9,4,32,false),NULL,"403 Forbidden","header_required");
    call(additive_patch_post,"{}","1","400 Bad Request","invalid_patch");
    call(additive_patch_post,body(101,1,9,3,32,false),"1","400 Bad Request","invalid_patch");
    call(additive_patch_post,body(101,1,9,4,31,false),"1","400 Bad Request","invalid_patch");
    call(additive_patch_post,body(101,1,9,4,32,true),"1","400 Bad Request","invalid_patch");
    body(101,1,9,4,32,false);size_t duplicate_length=strlen(patch_body);
    int duplicate=snprintf(patch_body+duplicate_length-1,sizeof(patch_body)-duplicate_length+1,",\"token\":101}");
    assert(duplicate>0 && (size_t)duplicate<sizeof(patch_body)-duplicate_length+1);
    call(additive_patch_post,patch_body,"1","400 Bad Request","invalid_patch");
    target.partials[31].group=4;
    call(additive_patch_post,body(101,1,9,4,32,false),"1","400 Bad Request","invalid_patch");
    fixture(&target);
    call(additive_patch_post,body(999,1,9,4,32,false),"1","410 Gone","unknown_session");
    s_sessions[0].api_version=1;
    call(additive_patch_post,body(101,1,9,4,32,false),"1","410 Gone","protocol_session");
    s_sessions[0].api_version=FM_HTTP_API_MAJOR;s_sessions[0].seen=now-10000000LL;
    call(additive_patch_post,body(101,1,9,4,32,false),"1","410 Gone","session_expired");
    s_sessions[0].seen=now;
    call(additive_patch_post,body(101,1,8,4,32,false),"1","409 Conflict","stale_context");
    s_sessions[0].context_epoch=8;
    call(additive_patch_post,body(101,1,9,4,32,false),"1","409 Conflict","stale_context");
    s_sessions[0].context_epoch=9;s_sessions[0].sequence=2;
    call(additive_patch_post,body(101,2,9,4,32,false),"1","409 Conflict","stale_sequence");
    s_sessions[0].sequence=0;
    captured=request("","1");assert(additive_patch_post(&captured)==ESP_OK && !strcmp(captured.status,"400 Bad Request"));
    captured=request("x","1");captured.content_len=WEB_ADDITIVE_MAX_BODY_BYTES+1;
    assert(additive_patch_post(&captured)==ESP_OK && !strcmp(captured.status,"400 Bad Request"));
    const char *input=body(101,1,9,4,32,false);size_t length=strlen(input);
    char nul[WEB_ADDITIVE_MAX_BODY_BYTES+1];memcpy(nul,input,length);nul[length]=0;nul[length+1]='x';
    captured=request(nul,"1");captured.content_len=length+2;
    assert(additive_patch_post(&captured)==ESP_OK && !strcmp(captured.status,"400 Bad Request"));
    captured=request(input,"1");captured.receive_fails=true;
    assert(additive_patch_post(&captured)==ESP_FAIL && !publication_count && !s_sessions[0].sequence);
}
static void readiness_and_single_flight(void)
{
    reset();
    bool *ready[]={&snapshot.ready,&snapshot.additive.link.available,&snapshot.additive.link.valid,
        &snapshot.additive.link.ready,&snapshot.additive.link.selected};
    for(unsigned i=0;i<sizeof(ready)/sizeof(ready[0]);i++) {
        *ready[i]=false;web_control_store(&snapshot);
        call(additive_patch_post,body(101,1,9,4,32,false),"1","409 Conflict","additive_not_ready");
        *ready[i]=true;
    }
    bool *busy[]={&snapshot.additive.pending,&snapshot.additive.link.busy,&snapshot.mode_switching,&snapshot.mode_recovering};
    for(unsigned i=0;i<sizeof(busy)/sizeof(busy[0]);i++) {
        *busy[i]=true;web_control_store(&snapshot);
        call(additive_patch_post,body(101,1,9,4,32,false),"1","409 Conflict","additive_not_ready");
        *busy[i]=false;
    }
    snapshot.applied_engine=0;web_control_store(&snapshot);
    call(additive_patch_post,body(101,1,9,4,32,false),"1","409 Conflict","additive_not_ready");
    snapshot.applied_engine=3;web_control_store(&snapshot);
    patch_t patch_before=g_patch;
    call(additive_patch_post,body(101,1,9,4,32,false),"1","202 Accepted","\"stage\":\"received\"");
    assert(strstr(captured.response,"\"accepted\":1") && strstr(captured.response,"\"fpga\":\"unverified\""));
    assert(!strstr(captured.response,"applied") && publication_count==1);
    assert(published[0].kind==EVT_WEB_ADDITIVE && published[0].web_v2.token==101);
    assert(published[0].web_v2.sequence==1 && published[0].web_v2.context_epoch==9);
    assert(published[0].web_v2.client==0 && published[0].web_v2.expires==now+WEB_LEASE_US);
    uint32_t id=published[0].web_v2.a;assert(id);
    assert(!fake_additive_request_id && !fake_additive_cancels);
    assert(!memcmp(&g_patch,&patch_before,sizeof(g_patch)));
    call(additive_patch_post,body(101,2,9,4,32,false),"1","503 Service Unavailable","additive_queue_full");
    web_additive_job_t job;assert(web_additive_queue_take(id,&job));
    same_patch(&job.request.patch,&target);
    assert(job.id==id && job.client==0 && job.expires==now+WEB_LEASE_US);
    assert(job.request.token==101 && job.request.sequence==1 && job.request.context_epoch==9);
    call(additive_patch_post,body(101,2,9,4,32,false),"1","503 Service Unavailable","additive_queue_full");
    call(v2_state,"",NULL,"200 OK","\"applied\":null");
    assert(strstr(captured.response,"\"revision\":7") && !strstr(captured.response,"\"commit_id\":23"));
    web_control_state_t unchanged;web_control_read(&unchanged);
    assert(!unchanged.additive.token && !unchanged.additive.pending && unchanged.additive.link.revision==7);
    same_patch(&unchanged.additive.link.confirmed_patch,&fake_additive.confirmed_patch);
    assert(web_additive_queue_cancel(id));
    call(additive_patch_post,body(101,2,9,4,32,false),"1","202 Accepted",NULL);
    assert(published[1].web_v2.a!=id && s_sessions[0].sequence==2);
}
static const char *object_end(const char *start)
{
    assert(*start=='{');unsigned depth=0;bool quoted=false,escape=false;
    for(const char *p=start;*p;p++) {
        char c=*p;
        if(c=='"' && !escape)quoted=!quoted;
        if(!quoted){if(c=='{')depth++;else if(c=='}' && !--depth)return p+1;}
        escape=quoted && c=='\\' && !escape;
    }
    assert(!"unterminated serialized JSON object");return NULL;
}
static void serialization(void)
{
    reset();snapshot.additive.link.confirmed_patch=target;
    snapshot.additive.token=101;snapshot.additive.sequence=7;
    snapshot.additive.context_epoch=9;snapshot.additive.commit_id=23;
    web_control_store(&snapshot);
    call(v2_capabilities,"",NULL,"200 OK",NULL);
    assert(strstr(captured.response,"\"id\":3,\"available\":true"));
    assert(strstr(captured.response,"\"additive\":{\"contract_version\":1,\"voices\":8,\"partials\":32,\"envelope_groups\":4,\"max_patch_bytes\":4096"));
    assert(strstr(captured.response,"\"patch_path\":\"/api/v2/additive/patch\""));
    assert(strstr(captured.response,"\"sample_rate\":{\"numerator\":91015625,\"denominator\":948}"));
    write_optional_fixture("additive-capabilities.json");
    call(v2_state,"",NULL,"200 OK",NULL);
    assert(strstr(captured.response,"\"mode\":{\"requested\":3,\"applied\":3"));
    assert(strstr(captured.response,"\"applied\":{\"token\":101,\"sequence\":7,\"context_epoch\":9,\"commit_id\":23}"));
    const char *additive=strstr(captured.response,"\"additive\":{");assert(additive);
    assert(strstr(additive,"\"available\":true,\"valid\":true,\"ready\":true,\"busy\":false"));
    assert(strstr(additive,"\"instance_id\":42,\"revision\":7"));
    const char *start=strstr(additive,"\"patch\":");assert(start);start+=8;
    const char *end=object_end(start);size_t length=(size_t)(end-start);
    char roundtrip[WEB_ADDITIVE_MAX_BODY_BYTES+1];
    int n=snprintf(roundtrip,sizeof(roundtrip),"{\"contract_version\":1,\"token\":101,\"sequence\":7,\"context_epoch\":9,\"patch\":%.*s}",(int)length,start);
    assert(n>0 && (size_t)n<sizeof(roundtrip));
    web_additive_request_t parsed;assert(web_additive_parse(roundtrip,(size_t)n,&parsed));
    same_patch(&parsed.patch,&target); // Includes exact cardinality and every integer.
    const char *legacy=strstr(captured.response,"\"legacy\":{");assert(legacy);
    assert(strstr(legacy,"\"ready\":false"));
    assert(strstr(legacy,"\"engine\":{\"available\":true,\"valid\":true,\"requested\":0,\"applied\":0"));
    assert(!strstr(legacy,"\"requested\":3") && !strstr(legacy,"\"applied\":3"));
    write_optional_fixture("additive-state.json");
    call(state,"",NULL,"200 OK","\"ready\":false");
    snapshot.engine.requested_mode=snapshot.engine.applied_mode=1;web_control_store(&snapshot);
    call(v2_state,"",NULL,"200 OK",NULL);
    legacy=strstr(captured.response,"\"legacy\":{");assert(legacy);
    assert(strstr(legacy,"\"engine\":{\"available\":true,\"valid\":true,\"requested\":1,\"applied\":1"));
    assert(strstr(legacy,"\"ready\":false") && !strstr(legacy,"\"requested\":3"));
    snapshot.additive.link.valid=false;web_control_store(&snapshot);
    call(v2_capabilities,"",NULL,"200 OK","\"id\":3,\"available\":false");
    call(v2_state,"",NULL,"200 OK","\"patch\":null,\"applied\":null");
}
static void busy_keeps_release_and_panic(void)
{
    for(unsigned variant=0;variant<2;variant++) {
        reset();
        if(!variant)snapshot.additive.pending=true;else snapshot.additive.link.busy=true;
        web_control_store(&snapshot);
        const char *raw="{\"source\":\"web\",\"bytes\":[144,60,100],\"token\":101,\"sequence\":1,\"context_epoch\":9}";
        call(midi_raw_post,raw,"1","409 Conflict","raw_not_ready");
        call(v2_command,"101 1 9 3 1 100 0","1","409 Conflict","additive_busy");
        call(v2_command,"101 1 9 3 0 100 0","1","202 Accepted",NULL);
        assert(published[0].kind==EVT_WEB_V2 && !published[0].web_v2.a);
        call(v2_command,"101 2 9 4 0 0 0","1","202 Accepted",NULL);
        assert(published[1].web_v2.op==FM_HTTP_OPCODE_STOP && s_sessions[0].sequence==2);
        assert(!fake_additive_request_id && !fake_additive_cancels); // HTTP only publishes.
    }
}
static void physical_handoff(void)
{
    reset();
    snapshot.local=(engine_local_state_t){.supported=true,.valid=true,.local_active=true,
        .blocked=true,.fault=5,.revision=7,.completed=6,.engine_mask=15};
    snapshot.engine.available=false;snapshot.engine.recovering=true;
    snapshot.fm.status.available=false;snapshot.fm.status.needs_select=true;
    snapshot.additive.link.valid=false;snapshot.additive.link.patch_known=false;
    web_control_store(&snapshot);
    call(v2_command,"101 1 9 5 2 1 0","1","202 Accepted",NULL);
    assert(publication_count==1 && published[0].web_v2.a==FM_ENGINE_FM);
    call(v2_command,"101 2 9 5 1 0 0","1","202 Accepted",NULL);
    call(v2_command,"101 3 9 5 3 0 0","1","202 Accepted",NULL);
    call(v2_state,"",NULL,"200 OK","\"local_control\":{");
    assert(strstr(captured.response,"\"patch\":null") && strstr(captured.response,"\"completed\":6"));
    assert(!fake_additive_request_id && !fake_additive_cancels);
}
int main(void)
{
    mode_selection();strict_ingress_and_sessions();readiness_and_single_flight();
    serialization();busy_keeps_release_and_panic();physical_handoff();
    web_additive_queue_cancel_all();web_raw_queue_reset();
    puts("HTTP additive: real mode3/patch handlers, strict sessions, immutable 202-only queue, exact full state and busy-safe release/Panic PASS");
    return 0;
}
