/* SPDX-License-Identifier: MIT */
#include "control_latency.h"

static void ordered(void) {
#if defined(__riscv)
    __asm__ volatile("fence iorw,iorw" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}
static int real_read(void *context,uint32_t offset,uint32_t *value) {
    (void)context;*value=*(volatile uint32_t *)(uintptr_t)(CL_APB_BASE+offset);
    ordered();return 1;
}
static int real_write(void *context,uint32_t offset,uint32_t value) {
    (void)context;*(volatile uint32_t *)(uintptr_t)(CL_APB_BASE+offset)=value;
    ordered();return 1;
}
static int read_reg(cl_state_t *s,uint32_t off,uint32_t *value) {
    if(s->io.read32(s->io.context,off,value))return 1;
    s->fault|=CL_FAULT_IO;return 0;
}
static int write_reg(cl_state_t *s,uint32_t off,uint32_t value) {
    if(s->io.write32(s->io.context,off,value))return 1;
    s->fault|=CL_FAULT_IO;return 0;
}
static uint16_t crc_byte(uint16_t crc,uint8_t byte) {
    crc^=(uint16_t)((uint16_t)byte<<8);
    for(unsigned i=0;i<8;i++)crc=(uint16_t)((crc<<1)^((crc&0x8000u)?0x1021u:0u));
    return crc;
}
static uint16_t crc_word(uint16_t crc,uint32_t word) {
    for(unsigned i=0;i<4;i++)crc=crc_byte(crc,(uint8_t)(word>>(i*8u)));
    return crc;
}
static uint64_t tick(const uint32_t *w,unsigned index) {
    return (uint64_t)w[index]|((uint64_t)w[index+1u]<<32);
}
static void derive(uint32_t *w) {
    static const uint8_t start[4]={4,6,8,4},end[4]={6,8,10,10};
    static const uint8_t masks[4]={3,6,12,9};
    for(unsigned i=0;i<4;i++) {
        uint64_t d=(w[3]&masks[i])==masks[i]?tick(w,end[i])-tick(w,start[i]):0;
        w[18u+2u*i]=(uint32_t)d;w[19u+2u*i]=(uint32_t)(d>>32);
    }
}
static void retain(cl_state_t *s) {
    uint32_t id=s->incoming[1];
    if(s->incoming[0]!=CL_ID || id==0 || id>CL_LIMIT) {
        s->fault|=CL_FAULT_PROTOCOL;return;
    }
    unsigned row=(unsigned)id-1u;uint32_t mask=UINT32_C(1)<<row;
    if(s->present&mask) {
        for(unsigned i=0;i<CL_RAW_WORDS;i++)if(s->rows[row][i]!=s->incoming[i]) {
            s->fault|=CL_FAULT_CONFLICT;return;
        }
        return;
    }
    for(unsigned i=0;i<CL_RAW_WORDS;i++)s->rows[row][i]=s->incoming[i];
    derive(s->rows[row]);s->crc[row]=0xffffu;s->crc_words[row]=0;s->present|=mask;
}
void cl_init(cl_state_t *s,const cl_io_t *io) {
    if(!s)return;
    volatile uint8_t *bytes=(volatile uint8_t *)s;
    for(unsigned i=0;i<sizeof(*s);i++)bytes[i]=0;
    if(io && (!io->read32 || !io->write32)){s->fault=CL_FAULT_IO;return;}
    s->io.read32=io?io->read32:real_read;s->io.write32=io?io->write32:real_write;
    s->io.context=io?io->context:0;
    uint32_t id,hz;
    if(!read_reg(s,0,&id) || id==0)return;
    if(id!=CL_ID){s->fault|=CL_FAULT_PROTOCOL;return;}
    if(!read_reg(s,0x10,&hz))return;
    if(hz!=CL_CLOCK_HZ){s->fault|=CL_FAULT_PROTOCOL;return;}
    s->enabled=1;
}
static uint8_t hex(unsigned value) {
    return (uint8_t)(value<10u?'0'+value:'a'+value-10u);
}
static uint8_t row_character(const cl_state_t *s) {
    unsigned at=s->character;
    static const char prefix[5]={'A','T','0','4',' '};
    if(at<5u)return (uint8_t)prefix[at];
    if(at<213u) {
        at-=5u;return hex((s->rows[s->send_row][at/8u]>>(4u*(7u-at%8u)))&15u);
    }
    if(at==213u)return ' ';
    if(at<218u)return hex((s->crc[s->send_row]>>(4u*(217u-at)))&15u);
    return at==218u?'\r':'\n';
}
static uint8_t error_character(const cl_state_t *s) {
    unsigned at=s->character;
    static const char prefix[5]={'A','T','4','E',' '};
    if(at<5u)return (uint8_t)prefix[at];
    if(at<13u)return hex((s->report_fault>>(4u*(12u-at)))&15u);
    if(at==13u)return ' ';
    if(at<18u)return hex((s->error_crc>>(4u*(17u-at)))&15u);
    return at==18u?'\r':'\n';
}
void cl_poll(cl_state_t *s,int idle,uint32_t now_ms) {
    if(!s || !s->enabled)return;
    uint32_t status;
    if(!read_reg(s,4,&status))return;
    if(status&4u)s->fault|=CL_FAULT_LOSS;
    if(status&~7u)s->fault|=CL_FAULT_PROTOCOL;
    if(s->copying && !(status&1u)) {
        s->fault|=CL_FAULT_PROTOCOL;s->copying=0;s->copy_word=0;
    }
    if(status&1u) {
        if(!s->copying){s->copying=1;s->copy_word=0;}
        if(s->copy_word<CL_RAW_WORDS) {
            if(!read_reg(s,0x20u+4u*s->copy_word,&s->incoming[s->copy_word]))return;
            s->copy_word++;
        }
        if(s->copy_word==CL_RAW_WORDS) {
            retain(s);
            if(!write_reg(s,8,1))return;
            s->copying=0;s->copy_word=0;
        }
    }
    if(!idle)return;
    /* Finish an already started frame before a diagnostic, so a newly observed
     * fault never splices two messages together. Every diagnostic is retained
     * by the host as a stream error, without rewriting original row flags. */
    if(!s->frame_kind) {
        if(s->fault!=s->reported_fault) {
            s->report_fault=s->fault;s->error_crc=crc_word(0xffffu,s->report_fault);
            s->frame_kind=2;s->character=0;
        } else {
            const uint32_t all=(UINT32_C(1)<<CL_LIMIT)-1u;
            if(s->present==all && s->sent==all && (uint32_t)(now_ms-s->replay_ms)>=1000u)s->sent=0;
            uint32_t waiting=s->present&~s->sent;
            if(!waiting)return;
            unsigned row=0;while(!(waiting&(UINT32_C(1)<<row)))row++;
            if(s->crc_words[row]<CL_WORDS) {
                unsigned word=s->crc_words[row]++;
                s->crc[row]=crc_word(s->crc[row],s->rows[row][word]);return;
            }
            s->send_row=(uint8_t)row;s->frame_kind=1;s->character=0;
        }
    }
    if(!(status&2u))return;
    uint8_t byte=s->frame_kind==1?row_character(s):error_character(s);
    if(!write_reg(s,0x0c,byte))return;
    if(++s->character==(s->frame_kind==1?CL_FRAME_BYTES:20u)) {
        if(s->frame_kind==1){s->sent|=UINT32_C(1)<<s->send_row;s->replay_ms=now_ms;}
        else s->reported_fault=s->report_fault;
        s->frame_kind=0;s->character=0;
    }
}
