#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "web_raw_queue.h"

int main(void)
{
    web_raw_queue_reset();
    web_raw_job_t input = {.request={.token=12,.sequence=3,.context_epoch=4,
        .bytes={144,60,100},.length=3,.has_binding=true},.client=2,.expires=12345};
    web_raw_job_t output;
    uint32_t ids[4];
    for (unsigned i=0;i<4;i++) {
        input.request.sequence=3+i;
        assert(web_raw_queue_push(&input,&ids[i]) && ids[i]);
        for (unsigned j=0;j<i;j++) assert(ids[i]!=ids[j]);
    }
    uint32_t full_id=99;
    assert(!web_raw_queue_push(&input,&full_id) && !full_id);
    input.request.bytes[0]=0; // Enqueued data is an owned copy.
    assert(web_raw_queue_take(ids[1],&output));
    assert(output.request.sequence==4 && output.request.bytes[0]==144);
    assert(output.request.bytes[1]==60 && output.request.bytes[2]==100);
    assert(output.client==2 && output.expires==12345 && output.request.token==12);
    assert(!web_raw_queue_take(ids[1],&output));
    assert(!output.request.length && !output.expires);
    assert(web_raw_queue_push(&input,&full_id) && full_id!=ids[1]);
    web_raw_queue_reset();
    assert(!web_raw_queue_take(ids[0],&output));
    uint32_t fresh_id;
    assert(web_raw_queue_push(&input,&fresh_id));
    assert(fresh_id!=full_id);
    for (unsigned i=0;i<4;i++) assert(fresh_id!=ids[i]);
    assert(!web_raw_queue_take(full_id,&output));
    assert(web_raw_queue_take(fresh_id,&output) && output.request.bytes[0]==0);
    assert(!web_raw_queue_push(NULL,&fresh_id) && !fresh_id);
    assert(!web_raw_queue_push(&input,NULL));
    assert(!web_raw_queue_take(0,&output));
    assert(!web_raw_queue_take(1,NULL));
    input.request.length=257;
    assert(!web_raw_queue_push(&input,&fresh_id));
    input.request.length=0;
    assert(!web_raw_queue_push(&input,&fresh_id));
    puts("raw HTTP queue: bounded copies, full refusal, consume-once and reset ID fences PASS");
    return 0;
}
