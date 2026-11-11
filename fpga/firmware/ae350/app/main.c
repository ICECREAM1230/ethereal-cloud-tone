#include <stdint.h>
#include "menu.h"
#define BASE ((uintptr_t)0xF8000000u)
static inline uint32_t rd(unsigned a) { return *(volatile uint32_t *)(BASE+a); }
static inline void wr(unsigned a,uint32_t v) {
    *(volatile uint32_t *)(BASE+a)=v;
    __asm__ volatile("fence iorw,iorw" ::: "memory");
}
static uint32_t next(uint32_t v) { return v==0xffffffffu ? 1u : v+1u; }
int main(void) {
    menu_t menu; char text[MENU_CHARS]; uint32_t keys[3];
    uint32_t seq=0,display_seq=0,awaiting=0,submitted_ms=0,last_draw=0;
    unsigned dirty=1,echo_ok=1,command_ok=0,first_publish=1;
    wr(0x1c,0xAE350010u);
    menu_init(&menu);
    if (rd(0)!=0x4D454E55u || rd(4)!=0x00010000u) {
        wr(0x1c,0xAE3500E1u);
        for (;;) { }
    }
    wr(0x1c,0xAE350011u);
    static const uint32_t patterns[4]={0x12345678u,0x80000001u,0u,0xffffffffu};
    for (unsigned i=0;i<4;i++) {
        uint32_t p=patterns[i]; wr(0x1c,p);
        if (rd(0x1c)!=p || rd(0x20)!=(((p<<7)|(p>>25))^0xA535138Cu)) echo_ok=0;
    }
    for (unsigned k=0;k<3;k++) keys[k]=rd(0x10+4*k);
    for (;;) {
        uint32_t now=rd(8);
        for (unsigned k=0;k<3;k++) {
            uint32_t count=rd(0x10+4*k),delta=count-keys[k];
            keys[k]=count;
            /* Bound recovery after a long pause; there is no autorepeat in FPGA. */
            if (delta>32) delta=32;
            while (delta--) { menu_key(&menu,k); dirty=1; command_ok=0; }
        }
        if (awaiting && rd(0x28)==awaiting) {
            command_ok=rd(0x2c)==0;
            awaiting=0;
            if (!dirty) command_ok=command_ok && rd(0x3c)==menu.gain &&
                rd(0x40)==menu.cutoff && rd(0x44)==menu.mode;
        }
        if (awaiting && (uint32_t)(now-submitted_ms)>100u) command_ok=0;
        if (dirty && !awaiting && !(rd(0x0c)&2u)) {
            wr(0x30,menu.gain);wr(0x34,menu.cutoff);wr(0x38,menu.mode);
            command_ok=0;
            seq=next(seq);wr(0x24,seq);awaiting=seq;submitted_ms=now;dirty=0;
        }
        if ((uint32_t)(now-last_draw)>=100u && !(rd(0x0c)&1u)) {
            last_draw=now;menu.heartbeat++;
            unsigned display_ok=display_seq!=0 && rd(0x54)==display_seq;
            menu_render(&menu,text,rd(0x28),rd(0x48),echo_ok&&command_ok&&display_ok);
            for (unsigned i=0;i<MENU_CHARS;i++) wr(0x1000+4*i,(uint8_t)text[i]);
            display_seq=next(display_seq);
            if (first_publish) wr(0x1c,0xAE350020u);
            wr(0x50,display_seq);
            if (first_publish) {
                wr(0x1c,0xAE350021u);
                first_publish=0;
            }
        }
    }
}
