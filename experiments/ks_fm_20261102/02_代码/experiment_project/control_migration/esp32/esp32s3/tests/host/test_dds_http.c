#define _POSIX_C_SOURCE 200809L
#define main legacy_http_test
#include "test_http_api.c"
#undef main
#undef TAG
#include "dds_presets.c"

size_t httpd_req_get_url_query_len(httpd_req_t *r){return r->query?strlen(r->query):0;}
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r,char *out,size_t size)
{if(!r->query || strlen(r->query)>=size)return ESP_FAIL;strcpy(out,r->query);return ESP_OK;}
BaseType_t xTaskCreate(void (*f)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *task)
{(void)f;(void)name;(void)stack;(void)arg;(void)priority;(void)task;return pdPASS;}

static httpd_req_t r;
static uint32_t submit(const char *query,const char *body,const char *expected)
{
    r=request(body,"1");r.query=query;
    assert(dds_command_http(&r)==ESP_OK && !strcmp(r.status,expected));
    if(strcmp(expected,"202 Accepted"))return 0;
    uint32_t id;assert(sscanf(r.response,"{\"dds_api\":1,\"job_id\":%u",&id)==1);return id;
}
static void file_step(uint32_t id)
{
    patch_t snapshot;patch_default(&snapshot);
    dds_presets_owner_begin(id,&snapshot,1,true);
    uint32_t queued;assert(xQueueReceive(s_work,&queued,0)==pdTRUE && queued==id);work(id);
}
int main(void)
{
    char root[]="/tmp/dds-http.XXXXXX";assert(mkdtemp(root));s_root=root;s_mounted=true;dds_presets_init();
    r=request("",NULL);assert(dds_list_http(&r)==ESP_OK && !strcmp(r.status,"403 Forbidden"));
    submit("op=list&op=load","","400 Bad Request");
    submit("op=read&path=system.toml%00bad","","400 Bad Request");
    submit("op=save&path=Patches%2FFactory%2FPads%2FA.toml","","400 Bad Request");
    submit("op=write&path=system.toml","","400 Bad Request");
    uint32_t id=submit("op=list","","202 Accepted");
    assert(s_job.status.phase==DDS_WAIT_OWNER); // HTTP never performed IO
    submit("op=list","","409 Conflict");file_step(id);
    r=request("",NULL);r.query="id=1";assert(dds_status_http(&r)==ESP_OK && strstr(r.response,"\"phase\":\"done\""));
    r=request("",NULL);r.query="id=1";assert(dds_result_http(&r)==ESP_OK && strstr(r.response,"Galaxy.toml") && !strcmp(r.type,"text/plain; charset=utf-8"));
    id=submit("op=save&path=Patches%2FUser%2FPads%2FMy+Tone.toml","","202 Accepted");file_step(id);
    assert(s_job.status.phase==DDS_DONE && !strcmp(s_job.status.path,"Patches/User/Pads/My Tone.toml"));
    id=submit("op=read&path=Patches%2FUser%2FPads%2FMy+Tone.toml","","202 Accepted");file_step(id);
    char query[40];snprintf(query,sizeof(query),"id=%u",id);r=request("",NULL);r.query=query;
    assert(dds_result_http(&r)==ESP_OK && strstr(r.response,"[amp_env]"));
    // Import without final LF normalizes its persisted completion record.
    id=submit("op=write&path=system.toml","format = 1\nstartup_patch = \"Patches/User/Pads/My Tone.toml\"","202 Accepted");file_step(id);
    assert(s_job.status.phase==DDS_DONE);
    id=submit("op=load&path=Patches%2FUser%2FPads%2FMy+Tone.toml","","202 Accepted");file_step(id);
    assert(s_job.status.phase==DDS_READY);r=request("",NULL);r.query="id=5";
    assert(dds_result_http(&r)==ESP_OK && !strcmp(r.status,"409 Conflict"));
    // Completion contains escaped errors from the real TOML parser.
    dds_presets_owner_complete(id,1,"cancelled \"test\"\n");
    r=request("",NULL);r.query="id=5";assert(dds_status_http(&r)==ESP_OK && strstr(r.response,"\\\"test\\\"\\u000a"));
    r=request("",NULL);r.query="id=1";assert(dds_status_http(&r)==ESP_OK && !strcmp(r.status,"404 Not Found"));
    puts("DDS HTTP: actual async handlers, header/query/body boundaries, single-flight IDs, paged list/export, User save and escaped completion PASS");
    return 0;
}
