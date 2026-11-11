#define HTTP_V2_TEST_MAIN previous_http_v2_main
#include "test_http_v2.c"
#undef HTTP_V2_TEST_MAIN
int main(void) {
 web_control_state_t state={.ready=true,.context_epoch=9,.ks_decay_ms=3000};patch_default(&state.patch);
 state.cpu_mode=(engine_cpu_mode_state_t){.supported=true,.valid=true,.ready=true,.channel_routes=true};
 state.inputs=(web_input_state_t){.version=1,.available=true,.initialized=true,.enabled=true,.ready=true,.capabilities_known=true,.independent_usb=true};
 for(unsigned i=0;i<WEB_CLIENTS;i++)state.midi_channel[i]=1;
 web_control_store(&state);httpd_req_t req=request("",NULL);assert(v2_capabilities(&req)==ESP_OK);
 assert(strstr(req.response,"\"channel_routes\":true"));
 req=request("",NULL);assert(v2_state(&req)==ESP_OK);assert(strstr(req.response,"\"midi_channel\":1"));
 req=request("","1");assert(v2_session(&req)==ESP_OK && !strcmp(req.status,"201 Created"));
 expect(v2_command,"1001 1 9 5 2 0 0","1","202 Accepted");
 assert(published[publication_count-1].web_v2.a==2);
 // Ordinary global editor changes do not revoke independently routed performance.
 state.mode_switching=true;state.cpu_mode.pending=true;state.cpu_mode.busy=true;state.cpu_mode.ready=false;web_control_store(&state);
 expect(v2_command,"1001 2 9 5 0 0 0","1","202 Accepted");
 expect(v2_command,"1001 3 9 3 1 100 0","1","202 Accepted");
 state.cpu_mode.fault=true;web_control_store(&state);
 expect(v2_command,"1001 4 9 5 2 0 0","1","503 Service Unavailable");
 state.cpu_mode.fault=false;state.cpu_mode.valid=false;web_control_store(&state);
 expect(v2_command,"1001 4 9 5 2 0 0","1","503 Service Unavailable");
 state.cpu_mode.valid=true;state.cpu_mode.channel_routes=false;web_control_store(&state);
 expect(v2_command,"1001 4 9 5 2 0 0","1","503 Service Unavailable");
 puts("PASS channel HTTP: real capability/client serialization, owner queue, independent editor busy, fault/invalid/legacy gating");return 0;
}
