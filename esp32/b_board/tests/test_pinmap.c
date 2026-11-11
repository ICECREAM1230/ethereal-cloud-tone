#include "b_pinmap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    /* User-confirmed A/B/SW order, revision 4, 2026-10-30. */
    int32_t good[4][3]={{13,14,12},{40,39,41},{47,21,38},{2,42,1}},pins[4][3];
    assert(b_encoder_pins_valid(good));
    memcpy(pins,good,sizeof(pins));pins[1][0]=pins[0][0];assert(!b_encoder_pins_valid(pins));
    const int rejected[]={-2,0,3,4,5,6,7,9,10,11,15,16,17,18,19,20,26,35,36,37,43,44,45,46,48,49};
    for(unsigned i=0;i<sizeof(rejected)/sizeof(rejected[0]);i++) {
        memcpy(pins,good,sizeof(pins));pins[0][0]=rejected[i];assert(!b_encoder_pins_valid(pins));
    }
    for(unsigned i=0;i<4;i++) for(unsigned j=0;j<3;j++) pins[i][j]=-1;
    assert(b_encoder_pins_valid(pins));pins[0][0]=8;assert(!b_encoder_pins_valid(pins));
    puts("PASS: GPIO duplication, laser/USB/UART/memory/strap reservations and partial configuration");
    return 0;
}
