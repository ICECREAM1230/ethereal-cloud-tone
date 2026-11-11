// Public API invariants against the real control core and RAM owner snapshot.
#define main previous_http_api_main
#include "test_http_api.c"
#undef main
#include "control_api.h"
#include <unistd.h>

typedef struct {char bytes[32768];size_t length;unsigned calls;} capture_t;
static esp_err_t capture_write(void *context,const char *bytes,size_t length)
{
    capture_t *capture=context;
    assert(capture->length+length<sizeof(capture->bytes));
    memcpy(capture->bytes+capture->length,bytes,length);
    capture->length+=length;capture->bytes[capture->length]=0;capture->calls++;
    return ESP_OK;
}
static void emit_capture(const control_api_response_t *response,capture_t *capture)
{
    memset(capture,0,sizeof(*capture));
    control_api_sink_t sink={capture,capture_write};
    assert(control_api_emit(response,&sink)==ESP_OK);
}
static control_api_request_t api_request(const char *path,const char *body)
{
    return (control_api_request_t){.path=path,.body=body,.body_length=body?strlen(body):0,
        .peer={CONTROL_API_WIFI,0},.control_authorized=true};
}
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool blocked,resume;
    const control_api_response_t *response;
    capture_t capture;
} slow_sink_t;
static esp_err_t slow_write(void *context,const char *bytes,size_t length)
{
    slow_sink_t *slow=context;
    (void)pthread_mutex_lock(&slow->mutex);
    slow->blocked=true;(void)pthread_cond_broadcast(&slow->condition);
    while(!slow->resume)(void)pthread_cond_wait(&slow->condition,&slow->mutex);
    (void)pthread_mutex_unlock(&slow->mutex);
    return capture_write(&slow->capture,bytes,length);
}
static void *emit_slow(void *context)
{
    slow_sink_t *slow=context;control_api_sink_t sink={slow,slow_write};
    assert(control_api_emit(slow->response,&sink)==ESP_OK);return NULL;
}
static void slow_response_does_not_block_control(web_control_state_t *owner)
{
    owner->ready=true;owner->context_epoch=9;web_control_store(owner);
    control_api_request_t query=api_request("/api/v2/state",NULL);
    control_api_response_t state_response={0};
    assert(control_api_prepare(&query,&state_response)==ESP_OK && state_response.code==200);
    slow_sink_t slow={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER,
        .response=&state_response};
    pthread_t thread;assert(!pthread_create(&thread,NULL,emit_slow,&slow));
    (void)pthread_mutex_lock(&slow.mutex);
    while(!slow.blocked)(void)pthread_cond_wait(&slow.condition,&slow.mutex);
    (void)pthread_mutex_unlock(&slow.mutex);
    // A five-second process deadline turns a regression into a bounded failure.
    alarm(5);
    control_api_request_t session_request=api_request("/api/v2/session","");
    session_request.peer=(control_api_peer_t){CONTROL_API_BLE_CONTROL,100};
    control_api_response_t response={0};unsigned before=publication_count;
    assert(control_api_prepare(&session_request,&response)==ESP_OK && response.code==201);
    uint32_t token=published[before].web.token;control_api_release(&response);
    char body[96];snprintf(body,sizeof(body),"%" PRIu32 " 1 9 4 0 0 0",token);
    control_api_request_t stop=api_request("/api/v2/command",body);stop.peer=session_request.peer;
    assert(control_api_prepare(&stop,&response)==ESP_OK && response.code==202);
    assert(publication_count==before+2 && published[before+1].web_v2.op==FM_HTTP_OPCODE_STOP);
    control_api_release(&response);alarm(0);
    (void)pthread_mutex_lock(&slow.mutex);slow.resume=true;
    (void)pthread_cond_broadcast(&slow.condition);(void)pthread_mutex_unlock(&slow.mutex);
    assert(!pthread_join(thread,NULL));control_api_release(&state_response);
    assert(!pthread_cond_destroy(&slow.condition));assert(!pthread_mutex_destroy(&slow.mutex));
}
static void reconnect_and_busy_fences(web_control_state_t *owner)
{
    owner->ready=true;owner->context_epoch=9;web_control_store(owner);
    control_api_request_t req=api_request("/api/v2/session","");
    req.peer=(control_api_peer_t){CONTROL_API_BLE_CONTROL,20};
    control_api_response_t response={0};unsigned before=publication_count;
    assert(control_api_prepare(&req,&response)==ESP_OK && response.code==201);
    control_source_ref_t old_source=published[before].source;control_api_release(&response);
    // New connection arrives before old worker cleanup. Its fence must differ.
    req.peer.connection_epoch=21;before=publication_count;
    assert(control_api_prepare(&req,&response)==ESP_OK && response.code==201);
    uint32_t token=published[before].web.token;unsigned client=published[before].web.client;
    control_source_ref_t current=published[before].source;control_api_release(&response);
    assert(!control_source_valid(old_source) && control_source_valid(current));
    control_api_disconnect((control_api_peer_t){CONTROL_API_BLE_CONTROL,20});
    assert(control_source_valid(current));
    char body[96];snprintf(body,sizeof(body),"%" PRIu32 " 1 9 3 1 100 0",token);
    req=api_request("/api/v2/command",body);req.peer=(control_api_peer_t){CONTROL_API_BLE_CONTROL,21};
    assert(control_api_prepare(&req,&response)==ESP_OK && response.code==202);control_api_release(&response);
    before=publication_count;
    req=api_request("/api/v2/session","");
    assert(control_api_prepare(&req,&response)==ESP_OK && response.code==409);
    control_api_release(&response);assert(publication_count==before && control_source_valid(current));
    int64_t seen=s_sessions[client].seen;now+=1000;
    req=api_request("/api/v2/state",NULL);
    assert(control_api_prepare(&req,&response)==ESP_OK && response.code==200);
    assert(s_sessions[client].seen==seen && publication_count==before && control_source_valid(current));
    control_api_release(&response);
}
int main(void)
{
    web_control_state_t owner={.ready=true,.context_epoch=9,.revision=1,.ks_decay_ms=3000};
    patch_default(&owner.patch);owner.engine.available=owner.engine.valid=true;
    web_control_store(&owner);
    control_api_response_t response={0};
    control_api_request_t req=api_request("/api/v2/session","");
    unsigned before=publication_count;
    assert(control_api_prepare(&req,&response)==ESP_OK && response.code==201);
    assert(publication_count==before+1);
    static capture_t first,second;
    emit_capture(&response,&first);emit_capture(&response,&second);
    assert(!strcmp(first.bytes,second.bytes) && publication_count==before+1);
    uint32_t token=published[before].web.token;
    control_api_release(&response);control_api_release(&response);
    assert(!response.private_data);

    req=api_request("/api/v2/state",NULL);
    assert(control_api_prepare(&req,&response)==ESP_OK && response.code==200);
    emit_capture(&response,&first);
    owner.context_epoch=10;owner.revision=2;owner.ready=false;
    web_control_store(&owner);
    emit_capture(&response,&second);
    assert(!strcmp(first.bytes,second.bytes));
    assert(strstr(second.bytes,"\"context_epoch\":9") && strstr(second.bytes,"\"revision\":1"));
    assert(publication_count==before+1);
    control_api_release(&response);

    char command_text[96];
    snprintf(command_text,sizeof(command_text),"%" PRIu32 " 1 9 3 0 100 0",token);
    req=api_request("/api/v2/command",command_text);
    req.peer=(control_api_peer_t){CONTROL_API_BLE_CONTROL,1};
    assert(control_api_prepare(&req,&response)==ESP_OK && response.code==410);
    emit_capture(&response,&first);
    assert(strstr(first.bytes,"unknown_session") && publication_count==before+1);
    control_api_release(&response);

    req=api_request("/api/v2/session","");
    req.peer=(control_api_peer_t){CONTROL_API_BLE_CONTROL,2};
    control_api_peer_invalidate(req.peer);
    assert(control_api_prepare(&req,&response)==ESP_OK && response.code==410);
    assert(publication_count==before+1);
    control_api_release(&response);
    reconnect_and_busy_fences(&owner);slow_response_does_not_block_control(&owner);
    puts("control API: once-only dispatch, immutable repeatable emission, cross-peer token and closed-epoch fences PASS");
    puts("control API concurrency: blocked status sink permits BLE session and STOP before send resumes PASS");
    puts("control API lifecycle: new connection fences queued old source; delayed cleanup preserves new peer; busy Wi-Fi rejected; GET never renews lease PASS");
}
