#include "b_config.h"
#include "b_pinmap.h"
#include <limits.h>
#include <string.h>

#define WORDS 52u
#define HEADER_SIZE 16u
_Static_assert(B_CONFIG_BLOB_SIZE==HEADER_SIZE+WORDS*4u,"v3 blob size");
_Static_assert(sizeof(b_config_t)==WORDS*sizeof(int32_t),"config has no implicit padding");

void b_config_defaults(b_config_t *out)
{
    if(!out) return;
    memset(out,0,sizeof(*out));
    out->channel=2; /* MIDI wire channel ID 1; public config uses 1..16. */
    for(unsigned i=0;i<12;i++) { out->notes[i]=60+(int32_t)i;out->gains[i]=100; }
    out->velocity[0]=20;out->velocity[1]=500;out->velocity[2]=127;out->velocity[3]=16;
    out->gate=200;out->volume=100;out->cutoff=127;out->polarity=1;
    out->debounce=2;out->enc_edges=2;out->route=HARP_ROUTE_AUTO;
    for(unsigned i=0;i<4;i++) for(unsigned p=0;p<3;p++) out->pins[i][p]=-1;
}

static bool range(int32_t value,int32_t low,int32_t high)
{
    return value>=low && value<=high;
}

bool b_config_valid(const b_config_t *c)
{
    if(!c || !range(c->channel,1,16) || !range(c->gate,10,5000) ||
       !range(c->volume,0,127) || !range(c->transpose,-24,24) ||
       !range(c->fine,-100,100) || !range(c->cutoff,0,127) ||
       !range(c->resonance,0,127) || !range(c->polarity,0,1) ||
       !range(c->debounce,1,50) || (c->enc_edges!=2 && c->enc_edges!=4) ||
       !range(c->enc_reverse,0,15) || !range(c->route,HARP_ROUTE_AUTO,HARP_ROUTE_BOTH) ||
       !range(c->velocity[0],1,59999) || !range(c->velocity[1],2,60000) ||
       c->velocity[0]>=c->velocity[1] || !range(c->velocity[2],1,127) ||
       !range(c->velocity[3],1,127) || !b_encoder_pins_valid(c->pins)) return false;
    for(unsigned i=0;i<12;i++) {
        if(!range(c->notes[i],0,127) || !range(c->gains[i],25,200)) return false;
        for(unsigned j=0;j<i;j++) if(c->notes[j]==c->notes[i]) return false;
    }
    return true;
}

bool b_config_equal(const b_config_t *a,const b_config_t *b)
{
    return a && b && memcmp(a,b,sizeof(*a))==0;
}

bool b_config_needs_maintenance(const b_config_t *a,const b_config_t *b)
{
    return a && b && (a->channel!=b->channel || a->polarity!=b->polarity ||
                      memcmp(a->pins,b->pins,sizeof(a->pins))!=0);
}

bool b_config_patch(const b_config_t *c,const b_patch_t *p,b_config_t *out)
{
    if(!c || !p || !out || !p->fields ||
       (p->fields & ~((UINT32_C(1)<<B_FIELD_COUNT)-1u))) return false;
    b_config_t next=*c;
    for(unsigned field=0;field<B_FIELD_COUNT;field++) {
        if(!(p->fields & (UINT32_C(1)<<field))) continue;
        switch((b_field_t)field) {
        case B_FIELD_CHANNEL:next.channel=p->values.channel;break;
        case B_FIELD_NOTES:memcpy(next.notes,p->values.notes,sizeof(next.notes));break;
        case B_FIELD_GAINS:memcpy(next.gains,p->values.gains,sizeof(next.gains));break;
        case B_FIELD_VELOCITY:memcpy(next.velocity,p->values.velocity,sizeof(next.velocity));break;
        case B_FIELD_GATE:next.gate=p->values.gate;break;
        case B_FIELD_VOLUME:next.volume=p->values.volume;break;
        case B_FIELD_TRANSPOSE:next.transpose=p->values.transpose;break;
        case B_FIELD_FINE:next.fine=p->values.fine;break;
        case B_FIELD_CUTOFF:next.cutoff=p->values.cutoff;break;
        case B_FIELD_RESONANCE:next.resonance=p->values.resonance;break;
        case B_FIELD_POLARITY:next.polarity=p->values.polarity;break;
        case B_FIELD_DEBOUNCE:next.debounce=p->values.debounce;break;
        case B_FIELD_ENC_EDGES:next.enc_edges=p->values.enc_edges;break;
        case B_FIELD_ENC_REVERSE:next.enc_reverse=p->values.enc_reverse;break;
        case B_FIELD_ROUTE:next.route=p->values.route;break;
        case B_FIELD_PINS:
            if(p->pins_row==-1) memcpy(next.pins,p->values.pins,sizeof(next.pins));
            else if(p->pins_row>=0 && p->pins_row<4)
                memcpy(next.pins[(unsigned)p->pins_row],p->values.pins[(unsigned)p->pins_row],sizeof(next.pins[0]));
            else return false;
            break;
        default:return false;
        }
    }
    if(!b_config_valid(&next)) return false;
    *out=next;
    return true;
}

static void write32(uint8_t *out,uint32_t v)
{
    for(unsigned i=0;i<4;i++) out[i]=(uint8_t)(v>>(i*8));
}

static uint32_t read32(const uint8_t *p)
{
    uint32_t v=0;
    for(unsigned i=0;i<4;i++) v|=(uint32_t)p[i]<<(i*8);
    return v;
}

static int32_t read_signed(const uint8_t *p)
{
    uint32_t v=read32(p);
    return v<=INT32_MAX?(int32_t)v:(int32_t)(-1-(int64_t)(UINT32_MAX-v));
}

static uint32_t crc32(const uint8_t *p,size_t size)
{
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<size;i++) {
        crc^=p[i];
        for(unsigned bit=0;bit<8;bit++) crc=(crc>>1)^(UINT32_C(0xedb88320)&(0u-(crc&1u)));
    }
    return ~crc;
}

/* Pointer lists explicitly specify the persistent order, independent of ABI. */
static void pack_config(const b_config_t *c,uint8_t *p)
{
    const int32_t *groups[]={&c->channel,c->notes,c->gains,c->velocity,
        &c->gate,&c->volume,&c->transpose,&c->fine,&c->cutoff,&c->resonance,
        &c->polarity,&c->debounce,&c->enc_edges,&c->enc_reverse,&c->route};
    const unsigned sizes[]={1,12,12,4,1,1,1,1,1,1,1,1,1,1,1};
    for(unsigned g=0;g<sizeof(sizes)/sizeof(*sizes);g++) for(unsigned i=0;i<sizes[g];i++,p+=4)
        write32(p,(uint32_t)groups[g][i]);
    for(unsigned i=0;i<4;i++) for(unsigned j=0;j<3;j++,p+=4) write32(p,(uint32_t)c->pins[i][j]);
}

static void unpack_config(b_config_t *c,const uint8_t *p)
{
    int32_t *groups[]={&c->channel,c->notes,c->gains,c->velocity,
        &c->gate,&c->volume,&c->transpose,&c->fine,&c->cutoff,&c->resonance,
        &c->polarity,&c->debounce,&c->enc_edges,&c->enc_reverse,&c->route};
    const unsigned sizes[]={1,12,12,4,1,1,1,1,1,1,1,1,1,1,1};
    for(unsigned g=0;g<sizeof(sizes)/sizeof(*sizes);g++) for(unsigned i=0;i<sizes[g];i++,p+=4)
        groups[g][i]=read_signed(p);
    for(unsigned i=0;i<4;i++) for(unsigned j=0;j<3;j++,p+=4) c->pins[i][j]=read_signed(p);
}

size_t b_config_encode(const b_config_t *c,uint8_t *out,size_t capacity)
{
    if(!out || capacity<B_CONFIG_BLOB_SIZE || !b_config_valid(c)) return 0;
    uint8_t blob[B_CONFIG_BLOB_SIZE];
    write32(blob,B_CONFIG_MAGIC_V3);write32(blob+4,3);write32(blob+8,B_CONFIG_BLOB_SIZE);
    pack_config(c,blob+HEADER_SIZE);
    write32(blob+12,crc32(blob+HEADER_SIZE,sizeof(blob)-HEADER_SIZE));
    memcpy(out,blob,sizeof(blob));
    return sizeof(blob);
}

bool b_config_decode(const uint8_t *p,size_t size,const b_config_t *defaults,b_config_t *out,bool *migrated)
{
    if(!p || !out) return false;
    b_config_t next;
    bool was_v2=false;
    if(size==B_CONFIG_BLOB_SIZE && read32(p)==B_CONFIG_MAGIC_V3 && read32(p+4)==3 &&
       read32(p+8)==B_CONFIG_BLOB_SIZE && read32(p+12)==crc32(p+HEADER_SIZE,size-HEADER_SIZE)) {
        unpack_config(&next,p+HEADER_SIZE);
    } else if(size==B_CONFIG_V2_SIZE && read32(p)==UINT32_C(0x42524832)) {
        if(defaults) next=*defaults;else b_config_defaults(&next);
        next.route=read_signed(p+4);next.transpose=read_signed(p+8);next.fine=read_signed(p+12);
        next.volume=read_signed(p+16);next.cutoff=read_signed(p+20);next.resonance=read_signed(p+24);
        next.polarity=read_signed(p+28);
        for(unsigned i=0;i<4;i++) next.velocity[i]=read_signed(p+32+4*i);
        next.gate=read_signed(p+48);
        for(unsigned i=0;i<4;i++) for(unsigned j=0;j<3;j++) next.pins[i][j]=read_signed(p+52+4*(i*3+j));
        was_v2=true;
    } else return false;
    if(!b_config_valid(&next)) return false;
    *out=next;
    if(migrated) *migrated=was_v2;
    return true;
}
