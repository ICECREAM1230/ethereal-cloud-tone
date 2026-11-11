// DDS regression fixture: legacy bitstream has no KS capability.
#include "engine_link.h"
#include <string.h>
void engine_link_ks_state(engine_ks_state_t*s){memset(s,0,sizeof(*s));}
bool engine_link_ks_note_on(unsigned v,const engine_ks_note_t*n){(void)v;(void)n;return false;}
void engine_link_ks_note_off(unsigned v){(void)v;}
void engine_link_ks_panic(void){}
bool engine_link_ks_mode(bool mode){return !mode;}
void engine_link_ks_gain(unsigned v,uint16_t l,uint16_t r){(void)v;(void)l;(void)r;}
void engine_link_ks_loss(unsigned v,uint16_t loss){(void)v;(void)loss;}
void engine_link_owner_panic_ack(void){}
