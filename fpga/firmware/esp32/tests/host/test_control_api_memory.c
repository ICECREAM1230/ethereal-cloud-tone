#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
static long fail_after=-1;
static void *allocations[64];
static size_t live_allocations;
static void *checked_malloc(size_t bytes)
{
    if(fail_after==0)return NULL;
    if(fail_after>0)fail_after--;
    void *memory=malloc(bytes);if(!memory)return NULL;
    assert(live_allocations<sizeof(allocations)/sizeof(allocations[0]));
    allocations[live_allocations++]=memory;return memory;
}
static void *checked_calloc(size_t count,size_t bytes)
{
    assert(!bytes || count<=(size_t)-1/bytes);
    void *memory=checked_malloc(count*bytes);if(memory)memset(memory,0,count*bytes);return memory;
}
static void checked_free(void *memory)
{
    if(!memory)return;
    size_t i=0;while(i<live_allocations && allocations[i]!=memory)i++;
    assert(i<live_allocations);allocations[i]=allocations[--live_allocations];free(memory);
}
#define malloc checked_malloc
#define calloc checked_calloc
#define free checked_free
#define main old_http_api_main
#include "test_http_api.c"
#undef main
#undef malloc
#undef calloc
#undef free

static char *expected_document;
static unsigned import_calls;
bool dds_presets_request_owned_source(control_source_ref_t source,dds_operation_t operation,const char *path,
    char **text,size_t length,unsigned argument,uint32_t *id,char *error)
{
    (void)error;(void)argument;assert(operation==DDS_IMPORT);assert(control_source_valid(source));assert(!strcmp(path,"User/test.toml"));
    assert(*text==expected_document && length==DDS_TEXT_MAX);
    import_calls++;*id=17;checked_free(*text);*text=NULL;return true;
}
static esp_err_t count_write(void *context,const char *bytes,size_t length)
{(void)bytes;*(size_t *)context+=length;return ESP_OK;}
static void owned_input_and_prepare_failures(void)
{
    for(long failure=0;failure<5;failure++) {
        fail_after=-1;
        char *document=checked_malloc(DDS_TEXT_MAX+1);assert(document);
        memset(document,'#',DDS_TEXT_MAX);document[DDS_TEXT_MAX]=0;expected_document=document;
        control_api_request_t request={.path="/api/dds/presets?op=import&path=User%2Ftest.toml",
            .body=document,.body_length=DDS_TEXT_MAX,.owned_body=&document,
            .peer={CONTROL_API_WIFI,0},.control_authorized=true};
        control_api_response_t response={0};unsigned before=import_calls;
        fail_after=failure;
        esp_err_t result=control_api_prepare(&request,&response);
        assert(!document); // Success, rejection and allocation failure all consume it.
        fail_after=-1;
        if(result==ESP_OK) {
            assert(response.code==202 && import_calls==before+1);
            size_t first=0,second=0;
            control_api_sink_t sink={&first,count_write};assert(control_api_emit(&response,&sink)==ESP_OK);
            sink.context=&second;assert(control_api_emit(&response,&sink)==ESP_OK);
            assert(first==second && first && import_calls==before+1);
        } else assert(result==ESP_ERR_NO_MEM && import_calls==before);
        control_api_release(&response);control_api_release(&response);assert(!live_allocations);
    }
}
static void snapshot_and_emit_failures(void)
{
    web_control_state_t owner={.ready=true,.context_epoch=9};patch_default(&owner.patch);web_control_store(&owner);
    control_api_request_t request={.path="/api/v2/state",.peer={CONTROL_API_WIFI,0}};
    unsigned successful=0;
    for(long failure=0;failure<5;failure++) {
        control_api_response_t response={0};fail_after=failure;
        esp_err_t result=control_api_prepare(&request,&response);fail_after=-1;
        if(result==ESP_OK) {
            successful++;
            size_t bytes=0;control_api_sink_t sink={&bytes,count_write};
            fail_after=0;assert(control_api_emit(&response,&sink)==ESP_ERR_NO_MEM && !bytes);
            fail_after=-1;assert(control_api_emit(&response,&sink)==ESP_OK && bytes);
        } else assert(result==ESP_ERR_NO_MEM);
        control_api_release(&response);assert(!live_allocations);
    }
    assert(successful);
}
int main(void)
{
    owned_input_and_prepare_failures();snapshot_and_emit_failures();
    assert(!publication_count);
    puts("control API memory: consumed16KiB owned input without second document copy, injected prepare/emit OOM, replay and idempotent release PASS");
}
