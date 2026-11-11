#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_additive.h"
#include "freertos/FreeRTOS.h"

// Replace only the host SDK's no-op critical section with a real mutex. The
// production parser and queue below are unmodified and get true concurrency.
static pthread_mutex_t test_mutex=PTHREAD_MUTEX_INITIALIZER;
void test_additive_enter(void){assert(!pthread_mutex_lock(&test_mutex));}
void test_additive_leave(void){assert(!pthread_mutex_unlock(&test_mutex));}
#undef portENTER_CRITICAL
#undef portEXIT_CRITICAL
#define portENTER_CRITICAL(p) ((void)(p),test_additive_enter())
#define portEXIT_CRITICAL(p) ((void)(p),test_additive_leave())
#include "web_additive.c"

static char valid[WEB_ADDITIVE_MAX_BODY_BYTES+1];
static void append(char *body,size_t *used,const char *format,...)
{
    va_list args;va_start(args,format);
    int n=vsnprintf(body+*used,WEB_ADDITIVE_MAX_BODY_BYTES+1-*used,format,args);
    va_end(args);assert(n>=0 && (size_t)n<=WEB_ADDITIVE_MAX_BODY_BYTES-*used);
    *used+=(size_t)n;
}
static void document(char *body,unsigned envelopes,unsigned partials)
{
    size_t used=0;
    append(body,&used,"{\"context_epoch\":4294967295,\"patch\":{\"bypass_lowpass\":true,\"centroid_q8\":8191,\"slope\":65535,\"comb_period\":31,\"comb_depth\":65535,\"master_gain\":65535,\"master_pan\":65535,\"bend_range\":127,\"envelopes\":[");
    for(unsigned i=0;i<envelopes;i++)append(body,&used,"%s{\"attack_inc\":0,\"decay_inc\":65535,\"sustain\":65535,\"release_inc\":0}",i?",":"");
    append(body,&used,"],\"partials\":[");
    for(unsigned i=0;i<partials;i++)append(body,&used,"%s{\"ratio_q16\":16777215,\"weight\":65535,\"group\":3}",i?",":"");
    append(body,&used,"]},\"sequence\":2,\"token\":1,\"contract_version\":1}");
}
static void rejected_span(const char *body,size_t length)
{
    web_additive_request_t request;memset(&request,0xa5,sizeof(request));
    assert(!web_additive_parse(body,length,&request));
    const unsigned char *p=(const unsigned char *)&request;
    for(size_t i=0;i<sizeof(request);i++)assert(!p[i]);
}
static void replaced(const char *from,const char *to,bool accept)
{
    const char *position=strstr(valid,from);assert(position);
    char body[8192];size_t prefix=(size_t)(position-valid);
    memcpy(body,valid,prefix);
    int n=snprintf(body+prefix,sizeof(body)-prefix,"%s%s",to,position+strlen(from));
    assert(n>=0 && (size_t)n<sizeof(body)-prefix);
    if(accept) {web_additive_request_t request;assert(web_additive_parse(body,strlen(body),&request));}
    else rejected_span(body,strlen(body));
}
static void complete_and_strict(void)
{
    document(valid,4,32);
    web_additive_request_t request;
    assert(web_additive_parse(valid,strlen(valid),&request));
    assert(request.token==1 && request.sequence==2 && request.context_epoch==UINT32_MAX);
    assert(request.patch.bypass_lowpass && request.patch.centroid_q8==8191);
    assert(request.patch.slope==65535 && request.patch.comb_period==31 && request.patch.comb_depth==65535);
    assert(request.patch.master_gain==65535 && request.patch.master_pan==65535 && request.patch.bend_range==127);
    for(unsigned i=0;i<4;i++) {
        assert(!request.patch.env[i].attack_inc && request.patch.env[i].decay_inc==65535);
        assert(request.patch.env[i].sustain==65535 && !request.patch.env[i].release_inc);
    }
    for(unsigned i=0;i<32;i++)assert(request.patch.partials[i].ratio_q16==16777215 && request.patch.partials[i].weight==65535 && request.patch.partials[i].group==3);
    replaced("\"token\":1","\"tok\\u0065n\" : 1",true);
    replaced("\"bypass_lowpass\":true","\"bypass_lowpass\" : false",true);
    replaced("\"master_gain\":65535","\"master_gain\":0",true);
    replaced("\"master_pan\":65535","\"master_pan\":0",true);
    replaced("\"bend_range\":127","\"bend_range\":0",true);
    const char *bad_numbers[]={"-1","+1","01","1.0","1e0","true","null","\"1\"","[]","{}","4294967296","999999999999999999999999999999"};
    for(size_t i=0;i<sizeof(bad_numbers)/sizeof(bad_numbers[0]);i++) {
        char replacement[128];snprintf(replacement,sizeof(replacement),"\"token\":%s",bad_numbers[i]);
        replaced("\"token\":1",replacement,false);
    }
    const char *from[]={"\"contract_version\":1","\"token\":1","\"sequence\":2","\"context_epoch\":4294967295",
        "\"centroid_q8\":8191","\"slope\":65535","\"comb_period\":31","\"comb_depth\":65535",
        "\"master_gain\":65535","\"master_pan\":65535","\"bend_range\":127","\"attack_inc\":0",
        "\"decay_inc\":65535","\"sustain\":65535","\"release_inc\":0","\"ratio_q16\":16777215","\"weight\":65535","\"group\":3"};
    const char *to[]={"\"contract_version\":2","\"token\":0","\"sequence\":0","\"context_epoch\":0",
        "\"centroid_q8\":8192","\"slope\":65536","\"comb_period\":32","\"comb_depth\":65536",
        "\"master_gain\":65536","\"master_pan\":65536","\"bend_range\":128","\"attack_inc\":65536",
        "\"decay_inc\":65536","\"sustain\":65536","\"release_inc\":65536","\"ratio_q16\":16777216","\"weight\":65536","\"group\":4"};
    for(size_t i=0;i<sizeof(from)/sizeof(from[0]);i++)replaced(from[i],to[i],false);
    replaced("\"bypass_lowpass\":true","\"bypass_lowpass\":1",false);
    replaced("\"bypass_lowpass\":true","\"bypass_lowpass\":truefalse",false);
    replaced("\"bypass_lowpass\":true","\"bypass_lowpass\":\"true\"",false);
    replaced("\"token\":1","\"token\":1,\"token\":1",false);
    replaced("\"token\":1","\"token\":1,\"tok\\u0065n\":1",false);
    replaced("\"patch\":{","\"patch\":{\"selected\":true,",false);
    replaced("\"patch\":{","\"patch\":{\"address\":40960,",false);
    replaced("\"patch\":{","\"patch\":{\"unknown\":0,",false);
    replaced("\"slope\":65535,","",false);
    replaced("\"master_gain\":65535","\"master_gain\":65535,\"master_gain\":65535",false);
    replaced("\"attack_inc\":0","\"attack_inc\":0,\"attack_inc\":0",false);
    replaced("\"attack_inc\":0,","",false);
    replaced("\"ratio_q16\":16777215","\"ratio_q16\":16777215,\"ratio_q16\":1",false);
    replaced("\"ratio_q16\":16777215,","",false);
    replaced("\"weight\":65535","\"weight\":65535,\"address\":1",false);
    replaced("\"release_inc\":0","\"release_inc\":0,\"selected\":true",false);
    replaced("\"contract_version\":1","\"contract_version\":1,\"selected\":true",false);
    replaced("\"token\":1,","",false);
    replaced("\"token\":1","\"tok\\u0000en\":1",false);
    replaced("\"token\":1","\"tok\\uD800en\":1",false);
    replaced("\"token\":1","\"tok\\qen\":1",false);
    replaced("\"token\":1","\"tok\nen\":1",false);
    char body[WEB_ADDITIVE_MAX_BODY_BYTES+1];
    document(body,3,32);rejected_span(body,strlen(body));
    document(body,5,32);rejected_span(body,strlen(body));
    document(body,4,31);rejected_span(body,strlen(body));
    document(body,4,33);rejected_span(body,strlen(body));
    rejected_span("{}",2);rejected_span("[]",2);rejected_span(NULL,0);
    assert(!web_additive_parse(valid,strlen(valid),NULL));
}
static void span_boundaries(void)
{
    size_t length=strlen(valid);
    for(size_t n=0;n<length;n++) {
        char *span=malloc(n?n:1);assert(span);memcpy(span,valid,n);
        rejected_span(span,n);free(span);
    }
    char *span=malloc(WEB_ADDITIVE_MAX_BODY_BYTES+1);assert(span);
    memcpy(span,valid,length);memset(span+length,' ',WEB_ADDITIVE_MAX_BODY_BYTES+1-length);
    web_additive_request_t request;
    assert(web_additive_parse(span,WEB_ADDITIVE_MAX_BODY_BYTES,&request));
    rejected_span(span,WEB_ADDITIVE_MAX_BODY_BYTES+1);
    span[length]=0;rejected_span(span,length+1);
    span[length]='x';rejected_span(span,length+1);
    span[length]='\v';rejected_span(span,length+1);
    free(span);
}
static void fixture_presets(const char *path)
{
    FILE *file=fopen(path,"rb");assert(file);
    assert(!fseek(file,0,SEEK_END));long length=ftell(file);assert(length>0);
    rewind(file);char *text=malloc((size_t)length+1);assert(text);
    assert(fread(text,1,(size_t)length,file)==(size_t)length);assert(!fclose(file));text[length]=0;
    const char *names[]={"organ","soft-pad","bell"};
    const uint16_t weights[3][32]={
        {65535,45874,22937,16384,0,10486,0,6554,0,3932,0,2621},
        {65535,28917,17013,11260,7950,5846,4422,3415,2679,2128,1707,1381,1125,922,759,628,522,435,364,305,256,216,182,154,131,111,94,80,68,58,50,43},
        {65535,47185,34078,22937,15073,9830,5898,3932}};
    const uint32_t bell_ratios[]={65536,180617,354157,585433,874512,1221591,1625293,2044723};
    const uint16_t envelopes[3][4][4]={
        {{342,7,65535,9},{342,7,65535,9},{342,7,65535,9},{342,7,65535,9}},
        {{3,3,52428,3},{4,3,39321,3},{4,3,26214,4},{5,3,13107,5}},
        {{683,2,0,5},{683,3,0,7},{683,4,0,10},{683,7,0,14}}};
    for(unsigned preset=0;preset<3;preset++) {
        char needle[32];snprintf(needle,sizeof(needle),"\"%s\"",names[preset]);
        const char *start=strstr(text,needle);assert(start);start=strchr(start,'{');assert(start);
        char body[WEB_ADDITIVE_MAX_BODY_BYTES+1];size_t used=0;
        append(body,&used,"{\"contract_version\":1,\"token\":100,\"sequence\":3,\"context_epoch\":10,\"patch\":");
        unsigned depth=0;bool quoted=false,escape=false;
        const char *p=start;
        do {
            char c=*p++;assert(c);
            if(c=='"' && !escape)quoted=!quoted;
            if(!quoted && !isspace((unsigned char)c)) {
                if(c=='{')depth++;
                if(c=='}')depth--;
            }
            if(quoted || !isspace((unsigned char)c))append(body,&used,"%c",c);
            escape=quoted && c=='\\' && !escape;
        }while(depth);
        append(body,&used,"}");
        web_additive_request_t request;assert(web_additive_parse(body,used,&request));
        assert(request.token==100 && request.sequence==3 && request.context_epoch==10);
        assert(request.patch.bypass_lowpass && !request.patch.centroid_q8 && !request.patch.slope);
        assert(!request.patch.comb_period && !request.patch.comb_depth && request.patch.master_gain==16384);
        assert(request.patch.master_pan==32768 && request.patch.bend_range==2);
        for(unsigned g=0;g<4;g++) {
            assert(request.patch.env[g].attack_inc==envelopes[preset][g][0]);
            assert(request.patch.env[g].decay_inc==envelopes[preset][g][1]);
            assert(request.patch.env[g].sustain==envelopes[preset][g][2]);
            assert(request.patch.env[g].release_inc==envelopes[preset][g][3]);
        }
        for(unsigned k=0;k<32;k++) {
            uint32_t ratio=preset==2 && k<8?bell_ratios[k]:65536u*(k+1);
            unsigned group=preset==2?(k<8?k/2:3):k/8;
            assert(request.patch.partials[k].ratio_q16==ratio);
            assert(request.patch.partials[k].weight==weights[preset][k]);
            assert(request.patch.partials[k].group==group);
        }
    }
    free(text);
}
static pthread_barrier_t barrier;
typedef struct {const web_additive_request_t *request;uint32_t id;bool accepted;} contender_t;
static void *contend(void *argument)
{
    contender_t *candidate=argument;
    int wait=pthread_barrier_wait(&barrier);assert(!wait || wait==PTHREAD_BARRIER_SERIAL_THREAD);
    candidate->accepted=web_additive_queue_push(candidate->request,2,12345,&candidate->id);
    return NULL;
}
static void immutable_single_flight(void)
{
    web_additive_queue_cancel_all();
    web_additive_request_t request;assert(web_additive_parse(valid,strlen(valid),&request));
    pthread_t threads[8];contender_t candidates[8]={0};
    assert(!pthread_barrier_init(&barrier,NULL,8));
    for(unsigned i=0;i<8;i++){candidates[i].request=&request;assert(!pthread_create(&threads[i],NULL,contend,&candidates[i]));}
    unsigned accepted=0;uint32_t id=0;
    for(unsigned i=0;i<8;i++) {
        assert(!pthread_join(threads[i],NULL));
        if(candidates[i].accepted){accepted++;id=candidates[i].id;assert(id);}
        else assert(!candidates[i].id);
    }
    assert(!pthread_barrier_destroy(&barrier));assert(accepted==1);
    request.patch.master_gain=7;
    web_additive_job_t job;
    assert(web_additive_queue_take(id,&job));
    assert(job.id==id && job.client==2 && job.expires==12345 && job.request.patch.master_gain==65535);
    assert(job.request.token==1 && job.request.context_epoch==UINT32_MAX);
    assert(!web_additive_queue_take(id,&job) && !job.id);
    uint32_t next=99;assert(!web_additive_queue_push(&request,1,777,&next) && !next);
    assert(!web_additive_queue_cancel(id+1));
    assert(web_additive_queue_cancel(id) && !web_additive_queue_cancel(id));
    assert(web_additive_queue_push(&request,1,777,&next) && next!=id);
    assert(!web_additive_queue_take(id,&job));
    web_additive_queue_cancel_all();
    assert(!web_additive_queue_take(next,&job));
    uint32_t fresh;assert(web_additive_queue_push(&request,0,1,&fresh));
    assert(fresh!=next && fresh!=id);assert(!web_additive_queue_cancel(next));
    assert(web_additive_queue_take(fresh,&job) && job.request.patch.master_gain==7);
    assert(web_additive_queue_cancel(fresh));
    assert(!web_additive_queue_push(NULL,0,1,&id) && !id);
    assert(!web_additive_queue_push(&request,4,1,&id));
    assert(!web_additive_queue_push(&request,0,0,&id));
    assert(!web_additive_queue_push(&request,0,1,NULL));
    request.token=0;assert(!web_additive_queue_push(&request,0,1,&id));request.token=1;
    request.patch.partials[31].group=4;assert(!web_additive_queue_push(&request,0,1,&id));
    assert(!web_additive_queue_take(0,&job));assert(!web_additive_queue_take(1,NULL));
}
typedef struct {uint32_t id;bool accepted;web_additive_job_t job;} lifecycle_t;
static void *take_racing(void *argument)
{
    lifecycle_t *state=argument;
    int wait=pthread_barrier_wait(&barrier);assert(!wait || wait==PTHREAD_BARRIER_SERIAL_THREAD);
    state->accepted=web_additive_queue_take(state->id,&state->job);return NULL;
}
static void *cancel_racing(void *argument)
{
    lifecycle_t *state=argument;
    int wait=pthread_barrier_wait(&barrier);assert(!wait || wait==PTHREAD_BARRIER_SERIAL_THREAD);
    state->accepted=web_additive_queue_cancel(state->id);return NULL;
}
static void concurrent_lifecycle(void)
{
    web_additive_queue_cancel_all();
    web_additive_request_t request;assert(web_additive_parse(valid,strlen(valid),&request));
    uint32_t id;assert(web_additive_queue_push(&request,0,999,&id));
    lifecycle_t take_state={.id=id},cancel_state={.id=id};pthread_t taking,cancelling;
    assert(!pthread_barrier_init(&barrier,NULL,2));
    assert(!pthread_create(&taking,NULL,take_racing,&take_state));
    assert(!pthread_create(&cancelling,NULL,cancel_racing,&cancel_state));
    assert(!pthread_join(taking,NULL) && !pthread_join(cancelling,NULL));
    assert(!pthread_barrier_destroy(&barrier));assert(cancel_state.accepted);
    if(take_state.accepted)assert(take_state.job.id==id && take_state.job.request.token==1);
    else assert(!take_state.job.id && !take_state.job.request.token);
    uint32_t fresh;assert(web_additive_queue_push(&request,1,888,&fresh) && fresh!=id);
    assert(!web_additive_queue_cancel(id));
    web_additive_job_t job;assert(web_additive_queue_take(fresh,&job) && job.id==fresh);
    assert(web_additive_queue_cancel(fresh));
}
int main(int argc,char **argv)
{
    assert(argc==2);
    complete_and_strict();span_boundaries();fixture_presets(argv[1]);immutable_single_flight();
    concurrent_lifecycle();
    puts("additive JSON: strict complete v1, three exact presets, bounds, immutable single-flight and concurrent admission PASS");
    return 0;
}
