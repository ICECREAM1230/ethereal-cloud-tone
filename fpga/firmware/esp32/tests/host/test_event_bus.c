#include <assert.h>
#include <stdio.h>
#include "event_bus.h"
#ifdef RELIABLE_EVENTS
#define subscribe event_bus_subscribe_reliable
#define receive(q,e) event_bus_receive(sub,e,0)
#else
#define subscribe event_bus_subscribe
#define receive(q,e) xQueueReceive(q,e,0)
#endif
static evt_t midi(unsigned status, unsigned a, unsigned b) {
    return (evt_t){.kind=EVT_MIDI,.midi={.status=status,.len=2,.data={a,b}}};
}
int main(void) {
    event_bus_init(); QueueHandle_t q=xQueueCreate(64,sizeof(evt_t));
    int sub=subscribe(q); (void)sub;
    for (unsigned i=0;i<64;i++) { evt_t e=midi(0xB0,74,i); event_bus_publish(&e); }
    evt_t off=midi(0x80,60,0); event_bus_publish(&off);
    evt_t out; bool found=false;
    while(receive(q,&out)==pdTRUE) if(out.kind==EVT_MIDI && out.midi.status==0x80) found=true;
    assert(found && "64 CC events must not lose NoteOff");
#ifdef RELIABLE_EVENTS
    for(unsigned i=0;i<65;i++) { evt_t e=midi(0x90,i,100); event_bus_publish(&e); }
    assert(receive(q,&out)==pdTRUE && out.kind==EVT_PANIC);
    assert(receive(q,&out)==pdTRUE && out.midi.data[0]==64);
    evt_t panic={.kind=EVT_PANIC}; event_bus_publish(&panic);
    assert(receive(q,&out)==pdTRUE && out.kind==EVT_PANIC);
    puts("event bus: CC coalescing, NoteOff overflow, note-only panic, disconnect panic PASS");
#endif
    return 0;
}
