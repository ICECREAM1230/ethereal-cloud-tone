#include "b_pinmap.h"

static bool encoder_gpio_allowed(int pin)
{
    /* YD-S3 profile: reserve lasers, USB, UART, memory, straps and RGB. */
    static const int available[] = {1,2,8,12,13,14,21,38,39,40,41,42,47};
    for (unsigned i=0; i<sizeof(available)/sizeof(available[0]); ++i)
        if (pin == available[i]) return true;
    return false;
}

bool b_encoder_pins_valid(const int32_t pins[4][3])
{
    if (!pins) return false;
    uint64_t used=0;
    for (unsigned i=0; i<4; ++i) {
        if (pins[i][0]==-1 && pins[i][1]==-1 && pins[i][2]==-1) continue;
        for (unsigned p=0; p<3; ++p) {
            int pin=pins[i][p];
            if (!encoder_gpio_allowed(pin) || (used & (1ULL<<pin))) return false;
            used |= 1ULL<<pin;
        }
    }
    return true;
}
