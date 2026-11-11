#include <stdio.h>
#include "ks_params.h"
int main(void)
{
    for(unsigned n=0;n<128;n++) {
        engine_ks_note_t p={0};
        bool valid=ks_params_note(n,127,3000,207,0,1,&p);
        if(valid!=(n>=19))return 1;
        printf("%u,%u,%d,%u,%u,%u\n",n,p.length,p.tuning,p.loss,p.gain_left,p.gain_right);
    }
    return 0;
}
