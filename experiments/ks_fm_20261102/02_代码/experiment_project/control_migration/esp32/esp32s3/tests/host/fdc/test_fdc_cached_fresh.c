/* Real engine RAM getter + real HTTP handlers: a recent SPI read cannot
 * prolong an unchanged CPU revision beyond its liveness deadline. */
#define main previous_http_main
#include "../test_http_api.c"
#undef main
#include "spi_regs.h"
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static unsigned status_reads;
bool fpga_word_read_burst_observe(uint16_t address,uint32_t *out,size_t count)
{assert(address==FDC_STATUS_REG && count==1);status_reads++;*out=FDC_STATUS_TAG|3u;return true;}
fpga_template_write_result_t fpga_template_write_burst(uint16_t address,const uint32_t *words,size_t count)
{(void)address;(void)words;(void)count;assert(!"HTTP must not write SPI");return FPGA_TEMPLATE_DEFERRED;}
#include "engine_fdc.inc.h"
int main(void)
{
    web_fdc_state_t state={0};fdc_init();now=100;fdc_service(now+700,true);
    now=480000;fdc_service(now+700,true);engine_link_fdc_state(&state.link);
    assert(state.link.fresh);fdc_control_state_publish(&state);
    now=600001;engine_fdc_state_t current;engine_link_fdc_state(&current);assert(!current.fresh);
    httpd_req_t get=request("",NULL);assert(fdc_get(&get)==ESP_OK);
    assert(strstr(get.response,"\"fresh\":false") && status_reads==2);
    web_control_state_t global={.context_epoch=9};web_control_store(&global);
    s_sessions[0].token=101;s_sessions[0].context_epoch=9;s_sessions[0].api_version=2;s_sessions[0].seen=now;
    httpd_req_t post=request("101 1 9 1 0 0 0 0 0 0","1");assert(fdc_post(&post)==ESP_OK);
    assert(!strcmp(post.status,"503 Service Unavailable") && !publication_count && status_reads==2);
    fdc_unavailable();assert(!s_fdc.valid);
    puts("PASS real engine+HTTP: expired CPU revision cannot be refreshed by cached owner or recent SPI read; GET false/POST503; HTTP0SPI");
}
