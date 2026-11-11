#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "midi_template_link.h"
#include "spi_regs.h"

/* Actual compiler and transport, with only the FPGA wire endpoint modeled.
 * No clocks, serial devices or board resets are accessed by this test. */
static int64_t now;
static uint32_t dds_ack,ks_ack;
static bool hold_dds,hold_ks,fail_dds,fail_ks;
static struct {uint16_t address;uint32_t value;} wire[8192];
static unsigned words,reads,dds_reads,ks_reads;
static midi_template_image_t *publish_during_read;
static bool require_ks_observation;
int64_t esp_timer_get_time(void) {return now;}
void fpga_word_write_burst(uint16_t address,const uint32_t *data,size_t count)
{
    assert(count && count<=12);now+=10;
    for(unsigned i=0;i<count;i++) {
        assert(words<sizeof(wire)/sizeof(*wire));
        if(require_ks_observation && address+i>=0xb000 && address+i<=0xb27f)assert(ks_reads);
        wire[words].address=address+i;wire[words++].value=data[i];
        if(address+i==0x80ff && !hold_dds)dds_ack=data[i];
        if(address+i==0xb3ff && !hold_ks)ks_ack=data[i];
    }
}
void fpga_word_write(uint16_t address,uint32_t value)
{fpga_word_write_burst(address,&value,1);}
bool fpga_word_read_burst(uint16_t address,uint32_t *data,size_t count)
{
    assert((address==0x0707 && (count==1 || count==2)) || (address==0x0708 && count==1));
    reads++;now+=10;
    if(address==0x0707)dds_reads++;
    if(address==0x0708 || count==2)ks_reads++;
    if((address==0x0707 && fail_dds) || ((address==0x0708 || count==2) && fail_ks))return false;
    data[0]=address==0x0707?dds_ack:ks_ack;
    if(count==2)data[1]=ks_ack;
    if(publish_during_read) {
        midi_template_image_t *next=publish_during_read;publish_during_read=NULL;
        midi_template_link_publish(next);
    }
    return true;
}
static midi_template_link_state_t state(void)
{midi_template_link_state_t s;midi_template_link_state(&s);return s;}
static void tick(void) {midi_template_link_service(now+250,true,true);now+=100;}
static void settle(void)
{
    for(unsigned i=0;i<500 && !state().ready;i++)tick();
    assert(state().ready && !state().busy && !state().fault);
}
static void clear_wire(void) {words=reads=dds_reads=ks_reads=0;}
static void prime(patch_t *patch,midi_template_image_t *image,uint32_t dds,uint32_t ks)
{
    now=0;dds_ack=dds;ks_ack=ks;hold_dds=hold_ks=fail_dds=fail_ks=false;
    publish_during_read=NULL;require_ks_observation=false;
    clear_wire();midi_template_link_init();patch_default(patch);
    assert(midi_templates_compile(patch,3000,image));
    midi_template_link_publish(image);settle();clear_wire();
}
static bool dds_address(uint16_t a)
{return (a>=0x8000 && a<=0x8041) || a==0x80ff || (a>=0x8100 && a<=0x81ff);}
static bool ks_address(uint16_t a)
{return (a>=0xb000 && a<=0xb0ff) || (a>=0xb200 && a<=0xb27f) || a==0xb3ff;}
static void only_domains(bool dds,bool ks,bool fm,unsigned expected_words)
{
    assert(words==expected_words);
    for(unsigned i=0;i<words;i++) {
        uint16_t a=wire[i].address;
        assert((dds && dds_address(a)) || (ks && ks_address(a)) || (fm && a==0xb400));
    }
}
static uint32_t last_write(uint16_t address)
{
    for(unsigned i=words;i;i--)if(wire[i-1].address==address)return wire[i-1].value;
    return 0;
}
static void dds_parameter(void)
{
    patch_t p;midi_template_image_t image;prime(&p,&image,0,0);
    p.osc[0].wave=WAVE_PULSE;assert(midi_templates_compile(&p,3000,&image));
    midi_template_link_publish(&image);settle();
    only_domains(true,false,false,323);assert(dds_ack==2 && ks_ack==1);
    now+=20000;tick();assert(state().ready && !state().fault); // unequal ACKs are normal
}
static void ks_parameter(void)
{
    patch_t p;midi_template_image_t image;prime(&p,&image,0,0);
    assert(midi_templates_compile(&p,4000,&image));
    midi_template_link_publish(&image);settle();
    only_domains(false,true,false,385);assert(dds_ack==1 && ks_ack==2);
}
static void fm_expression(void)
{
    patch_t p;midi_template_image_t image;prime(&p,&image,0,0);
    uint32_t native_batch=state().applied_generation;
    image.fm_expression=0x4055;midi_template_link_publish(&image);settle();
    only_domains(false,false,true,1);assert(wire[0].value==0x4055 && !reads);
    assert(dds_ack==1 && ks_ack==1); // b400 has no production native ACK
    assert(state().applied_generation==native_batch && state().fm_sent_revision);
}
static void unchanged_template(void)
{
    patch_t p;midi_template_image_t image;prime(&p,&image,0,0);
    midi_template_link_publish(&image);
    assert(state().ready && !state().busy);settle();assert(!words && !reads);
    p.env[0].attack^=1;p.lfo[0].rate++;
    assert(midi_templates_compile(&p,3000,&image));
    midi_template_link_publish(&image);settle();assert(!words && !reads);
}
static void shared_volume(void)
{
    patch_t p;midi_template_image_t image;prime(&p,&image,0,0);
    p.volume-=2;assert(midi_templates_compile(&p,3000,&image));
    midi_template_link_publish(&image);settle();
    only_domains(false,true,true,386);assert(dds_ack==1 && ks_ack==2);
}
static void independent_generations(void)
{
    patch_t p;midi_template_image_t image;prime(&p,&image,7,40);
    assert(dds_ack==8 && ks_ack==41);
    p.osc[0].wave=WAVE_PULSE;assert(midi_templates_compile(&p,3000,&image));
    midi_template_link_publish(&image);settle();
    only_domains(true,false,false,323);assert(dds_ack==9 && ks_ack==41);
    clear_wire();assert(midi_templates_compile(&p,4000,&image));
    midi_template_link_publish(&image);settle();
    only_domains(false,true,false,385);assert(dds_ack==9 && ks_ack==42);
    prime(&p,&image,UINT32_MAX,20);assert(dds_ack==1 && ks_ack==21);
}
static void latest_after_inflight(void)
{
    patch_t p;midi_template_image_t original,changed;prime(&p,&original,0,0);
    changed=original;changed.elements[0][0]++;hold_dds=true;
    midi_template_link_publish(&changed);
    for(unsigned i=0;i<100 && !last_write(0x80ff);i++)tick();
    assert(last_write(0x80ff)==2);only_domains(true,false,false,323);
    midi_template_link_publish(&original);unsigned sent=words;
    for(unsigned i=0;i<5;i++)tick();
    assert(words==sent && !state().ready);
    dds_ack=2;hold_dds=false;settle();
    only_domains(true,false,false,646);assert(dds_ack==3 && ks_ack==1);
    assert(wire[323].address==0x8000 && wire[323].value==original.elements[0][0]);
}
static void foreign_ack_cannot_clear_fence(void)
{
    patch_t p;midi_template_image_t image;prime(&p,&image,0,0);
    image.elements[0][0]++;hold_dds=true;midi_template_link_publish(&image);
    for(unsigned i=0;i<100 && !last_write(0x80ff);i++)tick();
    assert(last_write(0x80ff)==2);unsigned sent=words;
    midi_template_link_invalidate();midi_template_link_publish(&image);
    ks_ack=2;now+=20000;tick();assert(words==sent && !state().ready);
    dds_ack=2;hold_dds=false;now+=20000;settle();assert(state().ready);
}
static void inactive_ack_io(void)
{
    patch_t p;midi_template_image_t image;prime(&p,&image,0,0);
    fail_ks=true;image.elements[0][0]++;
    midi_template_link_publish(&image);settle();only_domains(true,false,false,323);
    prime(&p,&image,0,0);fail_dds=true;image.ks_notes[60][1]++;
    midi_template_link_publish(&image);settle();only_domains(false,true,false,385);
}
static void native_health_cadence(void)
{
    patch_t p;midi_template_image_t image;
    for(unsigned domain=0;domain<2;domain++) {
        prime(&p,&image,0,0);
        for(unsigned n=0;n<6;n++) {
            now+=19000;
            if(domain==0)image.fm_expression=0x4000u | (30u+n);
            else image.elements[0][0]++;
            midi_template_link_publish(&image);settle();
        }
        assert(ks_reads && "continuous changes must not starve the unrelated native health check");
        if(domain==0)assert(dds_reads);
        ks_ack=0;now+=21000;
        image.fm_expression=0x4057;midi_template_link_publish(&image);
        for(unsigned n=0;n<50 && !state().fault;n++)tick();
        assert(state().fault==MIDI_TEMPLATE_LINK_RESET && !state().ready);
    }
}
static void publication_adds_domain_during_read(void)
{
    patch_t p;midi_template_image_t image,latest;prime(&p,&image,0,14);
    image.elements[0][0]++;latest=image;latest.ks_notes[60][1]++;
    publish_during_read=&latest;require_ks_observation=true;
    midi_template_link_publish(&image);settle();
    only_domains(true,true,false,708);assert(dds_ack==2 && ks_ack==16);
    assert(!publish_during_read && ks_reads);
}
static void first_zero_is_not_reset(void)
{
    patch_t p;midi_template_image_t image;prime(&p,&image,0,0);
    midi_template_link_init();dds_ack=ks_ack=0;clear_wire();hold_dds=true;
    midi_template_link_publish(&image);
    for(unsigned n=0;n<100 && !last_write(0xb3ff);n++)tick();
    assert(last_write(0x80ff)==1 && last_write(0xb3ff)==1 && !dds_ack && ks_ack==1);
    unsigned sent=words;
    midi_template_link_invalidate();midi_template_link_publish(&image);
    now+=20000;tick();assert(words==sent && !state().ready && state().busy);
    now+=100001;tick();assert(words==sent && state().busy);
    dds_ack=1;hold_dds=false;now+=20000;settle();
    assert(dds_ack==2 && ks_ack==2);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"dds"))dds_parameter();
    else if(!strcmp(argv[1],"ks"))ks_parameter();
    else if(!strcmp(argv[1],"fm-expression"))fm_expression();
    else if(!strcmp(argv[1],"unchanged"))unchanged_template();
    else if(!strcmp(argv[1],"shared-volume"))shared_volume();
    else if(!strcmp(argv[1],"generations"))independent_generations();
    else if(!strcmp(argv[1],"latest"))latest_after_inflight();
    else if(!strcmp(argv[1],"fence"))foreign_ack_cannot_clear_fence();
    else if(!strcmp(argv[1],"inactive-io"))inactive_ack_io();
    else if(!strcmp(argv[1],"health-cadence"))native_health_cadence();
    else if(!strcmp(argv[1],"publish-race"))publication_adds_domain_during_read();
    else if(!strcmp(argv[1],"first-fence"))first_zero_is_not_reset();
    else assert(!"unknown case");
    printf("MIDI parameter isolation: %s PASS\n",argv[1]);
}
