#include <assert.h>
#include <stdio.h>
#include "midi_source.h"
static midi_source_t source;
static unsigned events,panics;
static bool disconnect_during_publish;
static evt_kind_t last;
void event_bus_publish(const evt_t *e){
    events++;last=e->kind;if(e->kind==EVT_PANIC)panics++;
    if(disconnect_during_publish && e->kind==EVT_MIDI){
        disconnect_during_publish=false;midi_source_close(&source);
        // Model this NoteOn reaching a later subscriber after close's panic.
        last=EVT_MIDI;
    }
}
int main(void){
    midi_message_t on={.status=0x90,.len=2,.data={60,100}};
    midi_source_open(&source);uint32_t old=midi_source_snapshot(&source);
    midi_source_publish(&source,old,&on);assert(last==EVT_MIDI);
    midi_source_close(&source);unsigned closed=events;
    midi_source_publish(&source,old,&on);assert(events==closed && last==EVT_PANIC);
    midi_source_open(&source);uint32_t fresh=midi_source_snapshot(&source);
    midi_source_publish(&source,old,&on);assert(events==closed);
    midi_source_publish(&source,fresh,&on);assert(last==EVT_MIDI);
    midi_source_overflow(&source);unsigned lost=events;
    midi_source_publish(&source,fresh,&on);assert(events==lost && last==EVT_PANIC);
    fresh=midi_source_snapshot(&source);disconnect_during_publish=true;
    midi_source_publish(&source,fresh,&on);assert(last==EVT_PANIC && panics==4);
    puts("MIDI source: disconnect/reconnect stale packets, ingress overflow, concurrent late-On panic PASS");
    return 0;
}
