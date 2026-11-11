#include "harp_core.h"
#include "ble_midi_unframer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    const uint32_t times[]={0,127,128,8191,8192,UINT32_MAX};
    const harp_midi_t messages[]={{3,{0x90,60,100}},{3,{0x80,60,0}},{3,{0xb0,7,90}},
        {3,{0xb0,74,120}},{3,{0xb0,71,20}},{3,{0xe0,0,64}},{2,{0xc0,8,0}},
        {3,{0x90,60,16}},{3,{0x90,71,127}}};
    ble_midi_unframer_t state;ble_midi_unframer_init(&state);
    unsigned checked=0;
    for(unsigned t=0;t<sizeof(times)/sizeof(times[0]);t++)
        for(unsigned i=0;i<sizeof(messages)/sizeof(messages[0]);i++) {
            uint8_t packet[5],decoded[8];size_t n=0;
            size_t bytes=harp_ble_encode(&messages[i],times[t],packet);
            assert(bytes);
            assert(ble_midi_unframer_feed_packet(&state,packet,bytes,decoded,sizeof(decoded),&n));
            assert(n==messages[i].length && memcmp(decoded,messages[i].data,n)==0);
            ++checked;
        }
    printf("PASS: %u B packets decoded by the existing A-board unframer\n",checked);
    return 0;
}
