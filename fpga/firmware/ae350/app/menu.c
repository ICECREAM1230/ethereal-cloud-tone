#include "menu.h"
void menu_init(menu_t *m) {
    m->selected=0;m->gain=50;m->cutoff=1000;m->mode=0;m->heartbeat=0;
}
void menu_key(menu_t *m, unsigned key) {
    if (key==0) { m->selected=(m->selected+1u)%3u; return; }
    if (key>2) return;
    if (m->selected==0) {
        if (key==1 && m->gain<100) m->gain++;
        if (key==2 && m->gain>0) m->gain--;
    } else if (m->selected==1) {
        if (key==1) m->cutoff=m->cutoff>19900 ? 20000 : m->cutoff+100;
        else m->cutoff=m->cutoff<120 ? 20 : m->cutoff-100;
    } else m->mode=(m->mode+(key==1 ? 1u : 2u))%3u;
}
static void put(char s[MENU_CHARS],unsigned row,unsigned col,const char *p) {
    while (*p && row<MENU_ROWS && col<MENU_COLS) s[row*MENU_COLS+col++]=*p++;
}
static void decimal(char s[MENU_CHARS],unsigned row,unsigned col,uint32_t v) {
    char buf[11]; unsigned n=0;
    do { buf[n++]=(char)('0'+v%10u); v/=10u; } while (v && n<10);
    while (n && col<MENU_COLS) s[row*MENU_COLS+col++]=buf[--n];
}
void menu_render(const menu_t *m,char s[MENU_CHARS],uint32_t a,uint32_t v,unsigned p) {
    for (unsigned i=0;i<MENU_CHARS;i++) s[i]=' ';
    put(s,2,3,"AE350 FPGA MENU LAB");
    put(s,4,3,"MENU RUNS ON REAL AE350");
    put(s,6,5,"GAIN"); decimal(s,6,23,m->gain);
    put(s,8,5,"CUTOFF HZ"); decimal(s,8,23,m->cutoff);
    put(s,10,5,"MODE"); put(s,10,23,m->mode==0 ? "SUB" : m->mode==1 ? "KS" : "FM");
    s[(6u+2u*m->selected)*MENU_COLS+3]='>';
    put(s,12,3,"ACK"); decimal(s,12,7,a);
    put(s,12,20,"VIRTUAL"); decimal(s,12,28,v);
    put(s,14,3,p ? "LINK PASS" : "LINK CHECK / FAIL");
    put(s,14,23,"CPU"); decimal(s,14,27,m->heartbeat);
    put(s,16,3,"KEY0 NEXT  KEY1 +  KEY2 -");
    put(s,18,3,"FPGA ACK IS A VIRTUAL DEVICE");
}
