/* Portable real-protocol bridge for PC interoperability tests. The platform
 * action result is injected; this program never opens devices or writes NVS. */
#include "b_engine.h"
#include <stdio.h>
#include <string.h>
int main(void)
{
    b_engine_t engine;b_config_t defaults;b_config_defaults(&defaults);
    if(!b_engine_init(&engine,&defaults)) return 1;
    char input[B_PROTOCOL_MAX_LINE+2],output[B_PROTOCOL_FRAME_CAP];uint32_t now=0,seq=0;
    while(fgets(input,sizeof(input),stdin)) {
        size_t length=strlen(input);if(length && input[length-1]=='\n') --length;
        b_request_t request;b_reply_t reply;
        b_code_t code=b_protocol_parse_line(input,length,&request);
        if(code!=B_OK) reply=(b_reply_t){.ready=true,.id=request.id,.revision=engine.revision,.code=code};
        else {
            ++seq;b_engine_command(&engine,&request,now,seq,&reply);
            if(!reply.ready) {
                b_action_t action=b_engine_tick(&engine,now,seq,true,&reply);
                if(action!=B_ACTION_NONE) b_engine_complete(&engine,true,&reply);
            }
        }
        b_state_t state={.uptime_ms=now};b_engine_snapshot(&engine,&state);
        size_t bytes=b_protocol_format_reply(output,sizeof(output),&reply,&engine.config,&state);
        if(!bytes || fwrite(output,1,bytes,stdout)!=bytes) return 2;
        ++now;
    }
    return 0;
}
