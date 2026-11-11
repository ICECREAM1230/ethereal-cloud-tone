/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "control_latency.h"

typedef struct {
    uint32_t id,hz,row[18];
    unsigned valid,ready,loss,reads,word_reads,writes,tx_writes,pops;
    unsigned fail_status,fail_pop,fail_tx;
    char bytes[20000];unsigned count;
} mock_t;
static unsigned checks;
static void check(int condition){checks++;if(!condition){fprintf(stderr,"check %u failed\n",checks);abort();}}
static int read32(void *context,uint32_t off,uint32_t *value) {
    mock_t *m=context;m->reads++;
    if(off==0){*value=m->id;return 1;}
    if(off==0x10){*value=m->hz;return 1;}
    if(off==4){if(m->fail_status){m->fail_status--;return 0;}*value=m->valid|m->ready*2u|m->loss*4u;return 1;}
    check(off>=0x20 && off<=0x64 && off%4==0 && m->valid);
    m->word_reads++;*value=m->row[(off-0x20)/4];return 1;
}
static int write32(void *context,uint32_t off,uint32_t value) {
    mock_t *m=context;m->writes++;
    if(off==8){
        check(value==1 && m->valid);
        if(m->fail_pop){m->fail_pop--;return 0;}
        m->pops++;m->valid=0;return 1;
    }
    check(off==0xc && value<256 && m->ready);
    if(m->fail_tx){m->fail_tx--;return 0;}
    check(m->count<sizeof(m->bytes));m->bytes[m->count++]=(char)value;m->tx_writes++;return 1;
}
static void init(mock_t *m,cl_state_t *s) {
    memset(m,0,sizeof(*m));m->id=CL_ID;m->hz=CL_CLOCK_HZ;m->ready=1;
    cl_io_t io={read32,write32,m};cl_init(s,&io);check(s->enabled && !s->fault);
}
static void poll(mock_t *m,cl_state_t *s,int idle,uint32_t now) {
    unsigned words=m->word_reads,bytes=m->tx_writes;
    cl_poll(s,idle,now);
    check(m->word_reads-words<=1);check(m->tx_writes-bytes<=(idle?1u:0u));
}
static void set64(uint32_t *words,unsigned index,uint64_t value) {
    words[index]=(uint32_t)value;words[index+1]=(uint32_t)(value>>32);
}
static uint64_t get64(const uint32_t *words,unsigned index) {
    return words[index]|((uint64_t)words[index+1]<<32);
}
static void fixture(mock_t *m,unsigned id,unsigned engine) {
    check(!m->valid);memset(m->row,0,sizeof(m->row));
    m->row[0]=CL_ID;m->row[1]=id;
    m->row[2]=engine|((id%16u)<<2)|((60u+(id-1u)%5u)<<6)|(64u<<13);
    m->row[3]=31;m->row[12]=id;m->row[13]=id+100u;m->row[14]=(id-1u)%(engine==0?32u:engine==1?16u:8u);
    m->row[15]=CL_CLOCK_HZ;m->row[16]=id&1u?256u:0;m->row[17]=id&1u?0:UINT32_C(0xffffff00);
    uint64_t t=UINT64_C(0x200000000)+id*1000u;
    set64(m->row,4,t);set64(m->row,6,t+3);set64(m->row,8,t+20);set64(m->row,10,t+30);
    m->valid=1;
}
static void collect(mock_t *m,cl_state_t *s) {
    for(unsigned i=0;i<18;i++)poll(m,s,0,i);
    check(!m->valid);
}
static void save(const char *directory,const char *name,mock_t *m) {
    char path[1024];check(snprintf(path,sizeof(path),"%s/%s",directory,name)>0);
    FILE *f=fopen(path,"wb");check(f!=0);check(fwrite(m->bytes,1,m->count,f)==m->count);check(fclose(f)==0);
}
int main(int argc,char **argv) {
    check(argc==2);static cl_state_t s;mock_t m;
    memset(&m,0,sizeof(m));cl_io_t io={read32,write32,&m};
    cl_init(&s,&io);check(!s.enabled && !s.fault && m.reads==1);poll(&m,&s,1,0);check(m.reads==1);
    m.id=1;cl_init(&s,&io);check(!s.enabled && s.fault==CL_FAULT_PROTOCOL);
    m.id=CL_ID;m.hz=50000000;cl_init(&s,&io);check(!s.enabled && s.fault==CL_FAULT_PROTOCOL);
    cl_io_t invalid={0,write32,&m};cl_init(&s,&invalid);check(!s.enabled && s.fault==CL_FAULT_IO);
    cl_init(0,&io);cl_poll(0,1,0);

    init(&m,&s);fixture(&m,1,0);
    set64(m.row,4,UINT64_C(0x2fffffff0));set64(m.row,6,UINT64_C(0x300000002));
    set64(m.row,8,UINT64_C(0x300000020));set64(m.row,10,UINT64_C(0x300000010));
    uint32_t original[18];memcpy(original,m.row,sizeof(original));collect(&m,&s);
    check(m.count==0 && s.present==1 && !memcmp(original,s.rows[0],sizeof(original)));
    check(get64(s.rows[0],18)==18 && get64(s.rows[0],20)==30);
    check(get64(s.rows[0],22)==UINT64_MAX-15 && get64(s.rows[0],24)==32);
    for(unsigned i=0;i<40;i++)poll(&m,&s,0,i);
    check(m.count==0);
    m.ready=0;for(unsigned i=0;i<40;i++)poll(&m,&s,1,i);
    check(m.count==0);
    m.ready=1;for(unsigned i=0;i<8;i++)poll(&m,&s,1,i);
    check(m.count==8);
    unsigned char_cursor=s.character;
    for(unsigned i=0;i<40;i++)poll(&m,&s,0,i);
    check(m.count==8 && s.character==char_cursor);
    for(unsigned i=0;i<300;i++)poll(&m,&s,1,i);
    check(m.count==220);save(argv[1],"negative-low32-carry.bin",&m);
    fixture(&m,1,0);memcpy(m.row,original,sizeof(original));collect(&m,&s);
    check(!s.fault && s.present==1);check(!memcmp(s.rows[0],original,sizeof(original)));
    fixture(&m,1,0);collect(&m,&s);check(s.fault==CL_FAULT_CONFLICT);
    check(!memcmp(s.rows[0],original,sizeof(original)));
    for(unsigned i=0;i<25;i++)poll(&m,&s,1,i);
    check(m.count==240);
    save(argv[1],"conflict-diagnostic.bin",&m);

    init(&m,&s);fixture(&m,1,2);m.fail_pop=1;
    for(unsigned i=0;i<18;i++)poll(&m,&s,0,i);
    check(m.valid && s.copy_word==18 && s.fault==CL_FAULT_IO);
    poll(&m,&s,0,20);check(!m.valid && s.copy_word==0 && m.word_reads==18 && m.pops==1);
    for(unsigned i=0;i<300;i++)poll(&m,&s,1,i);
    check(m.count==240);
    save(argv[1],"pop-error-diagnostic.bin",&m);

    init(&m,&s);fixture(&m,1,1);m.row[3]=(31u&~6u)|32u|64u;
    set64(m.row,6,0);set64(m.row,8,0);m.row[12]=m.row[13]=m.row[14]=0;collect(&m,&s);
    check(get64(s.rows[0],18)==0 && get64(s.rows[0],20)==0 && get64(s.rows[0],22)==0);
    check(get64(s.rows[0],24)==30);
    for(unsigned i=0;i<300;i++)poll(&m,&s,1,i);
    save(argv[1],"partial-timeout.bin",&m);

    init(&m,&s);fixture(&m,1,0);poll(&m,&s,0,0);m.valid=0;
    poll(&m,&s,0,1);check(s.fault==CL_FAULT_PROTOCOL && !s.copying && !s.present);
    init(&m,&s);fixture(&m,21,0);collect(&m,&s);check(!s.present && s.fault==CL_FAULT_PROTOCOL);
    init(&m,&s);fixture(&m,1,0);m.loss=1;collect(&m,&s);
    check(s.fault==CL_FAULT_LOSS && s.rows[0][3]==31);
    for(unsigned i=0;i<300;i++)poll(&m,&s,1,i);
    check(m.count==240);
    save(argv[1],"loss-with-immutable-row.bin",&m);

    init(&m,&s);m.fail_status=1;poll(&m,&s,0,0);check(s.fault==CL_FAULT_IO && !m.count);
    for(unsigned i=0;i<20;i++)poll(&m,&s,1,i);
    check(m.count==20);
    init(&m,&s);fixture(&m,1,0);collect(&m,&s);
    for(unsigned i=0;i<26;i++)poll(&m,&s,1,i);
    m.fail_tx=1;poll(&m,&s,1,30);check(s.character==0 && s.fault==CL_FAULT_IO);
    for(unsigned i=0;i<250;i++)poll(&m,&s,1,i);
    check(m.count==240);

    for(unsigned engine=0;engine<3;engine++) {
        init(&m,&s);
        for(unsigned id=1;id<=20;id++) {
            fixture(&m,id,engine);collect(&m,&s);
            for(unsigned i=0;i<500;i++) {m.ready=i%3u!=0;poll(&m,&s,(int)(i%7u!=0),100u+id);}
        }
        check(!s.fault && s.present==0xfffffu && s.sent==0xfffffu && m.count==4400);
        uint32_t first[26];memcpy(first,s.rows[0],sizeof(first));
        fixture(&m,21,engine);collect(&m,&s);
        check(s.fault==CL_FAULT_PROTOCOL && !memcmp(first,s.rows[0],sizeof(first)));
        /* The rejected21 diagnostic is not included in the clean20 fixture. */
        char name[32];check(snprintf(name,sizeof(name),"engine-%u-first20.bin",engine)>0);save(argv[1],name,&m);
        s.fault=s.reported_fault=0;m.ready=1;
        for(unsigned i=0;i<230;i++)poll(&m,&s,1,1200u);
        check(m.count==4630u); /* 220B repeated row1 plus10B row2 at this instant. */
        check(!memcmp(first,s.rows[0],sizeof(first)));
    }
    init(&m,&s);fixture(&m,1,0);
    set64(m.row,4,UINT64_MAX-10);set64(m.row,6,UINT64_MAX-5);
    set64(m.row,8,2);set64(m.row,10,9);collect(&m,&s);
    check(get64(s.rows[0],18)==5 && get64(s.rows[0],20)==8 && get64(s.rows[0],22)==7 && get64(s.rows[0],24)==20);
    for(unsigned i=0;i<300;i++)poll(&m,&s,1,i);
    save(argv[1],"u64-wrap.bin",&m);
    printf("PASS control_latency checks=%u sizeof_state=%zu first20_each_engine=20 max_snapshot_words_per_poll=1 max_uart_bytes_per_idle_poll=1 busy_uart_bytes=0\n",checks,sizeof(s));
    return 0;
}
