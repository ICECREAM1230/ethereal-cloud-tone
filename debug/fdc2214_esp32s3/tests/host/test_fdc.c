#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "fdc2214.h"
#include "fdc_ranges.h"

static unsigned checks;
#define CK(x) do {checks++;assert(x);} while(0)
typedef struct {
    uint16_t reg[128];
    unsigned reads,writes,read_count;
    uint8_t read_order[512];
    int fail_reg;
    bool mismatch;
    uint64_t now;
} fake_t;
static int read_fake(void *context,uint8_t reg,uint16_t *value)
{
    fake_t *f=context;f->reads++;f->read_order[f->read_count++]=reg;
    if (reg==f->fail_reg) return 777;
    *value=f->reg[reg];
    if (f->mismatch && reg==0x1B) *value^=0x2000;
    if (reg==0x18) f->reg[reg]&=(uint16_t)~FDC_STATUS_ERRORS;
    return 0;
}
static int write_fake(void *context,uint8_t reg,uint16_t value)
{
    fake_t *f=context;f->writes++;f->reg[reg]=value;return 0;
}
static uint64_t now_fake(void *context) {return ((fake_t *)context)->now;}
static fdc_config_t config(void)
{
    return (fdc_config_t){.wiring_confirmed=true,.sda_gpio=4,.scl_gpio=5,.sd_gpio=-1,.addr_gpio=-1,
        .address=0x2A,.clock=FDC_CLOCK_EXT,.external_hz=40000000,.fref_divider=1,
        .rcount=0x1000,.settlecount=0x100,.idrive={15,14,13,12},.data_timeout_ms=500};
}
static void fixture(fake_t *f,fdc_device_t *d)
{
    memset(f,0,sizeof(*f));f->fail_reg=-1;f->reg[0x7E]=0x5449;f->reg[0x7F]=0x3055;
    fdc_config_t c=config();fdc_io_t io={f,read_fake,write_fake,now_fake};
    CK(fdc_init(d,&c,&io)==FDC_OK);
    CK(f->writes==20 && f->reads==22 && d->configured);
    for (unsigned ch=0;ch<4;ch++) {
        CK(f->reg[0x08+ch]==0x1000);CK(f->reg[0x10+ch]==0x100);
        CK(f->reg[0x14+ch]==0x2001);CK(f->reg[0x1E + ch]==(uint16_t)(c.idrive[ch]<<11));
        f->reg[2*ch]=(uint16_t)(0x100+ch);f->reg[2*ch+1]=(uint16_t)(0x1200+ch);
    }
    CK(f->reg[0x1B]==0xC20D && f->reg[0x1A]==0x1601);
    f->reg[0x18]=0x40;
}
static void test_configuration_gate(void)
{
    fdc_config_t c=config();fake_t f={.fail_reg=-1};fdc_device_t d;
    fdc_io_t io={&f,read_fake,write_fake,now_fake};char why[128];
    c.wiring_confirmed=false;CK(fdc_init(&d,&c,&io)==FDC_UNCONFIGURED);CK(!f.reads && !f.writes);
    c=config();c.sda_gpio=-1;CK(!fdc_config_valid(&c,why,sizeof(why)));
    CK(fdc_init(&d,&c,&io)==FDC_UNCONFIGURED && !f.reads && !f.writes);
    c=config();c.clock=FDC_CLOCK_UNKNOWN;CK(fdc_init(&d,&c,&io)==FDC_UNCONFIGURED && !f.reads && !f.writes);
    c=config();c.scl_gpio=c.sda_gpio;CK(!fdc_config_valid(&c,why,sizeof(why)));
    c=config();c.sd_gpio=c.sda_gpio;CK(!fdc_config_valid(&c,why,sizeof(why)));
    c=config();c.sda_gpio=19;CK(!fdc_config_valid(&c,why,sizeof(why)));
    c=config();c.sda_gpio=46;CK(!fdc_config_valid(&c,why,sizeof(why)));
    c=config();c.addr_gpio=6;CK(!fdc_config_valid(&c,why,sizeof(why)));
    c=config();c.rcount=8;CK(!fdc_config_valid(&c,why,sizeof(why)));
    c.rcount=9;CK(fdc_config_valid(&c,why,sizeof(why)));
    c.settlecount=3;CK(!fdc_config_valid(&c,why,sizeof(why)));
    c.settlecount=4;CK(fdc_config_valid(&c,why,sizeof(why)));
    c=config();c.clock=FDC_CLOCK_INT;c.fref_divider=1;CK(!fdc_config_valid(&c,why,sizeof(why)));
    c.fref_divider=2;CK(fdc_config_valid(&c,why,sizeof(why)));
    c=config();c.fref_divider=1023;c.rcount=9;c.settlecount=4;c.data_timeout_ms=10000;
    CK(fdc_config_valid(&c,why,sizeof(why))); // Includes 64-bit FREF limit comparison.
    c=config();c.rcount=0xFFFF;c.clock=FDC_CLOCK_INT;c.fref_divider=2;c.data_timeout_ms=100;
    CK(!fdc_config_valid(&c,why,sizeof(why)));
}
static void test_endian_and_scan(void)
{
    const uint8_t data[]={0xAB,0xCD};CK(fdc_be16(data)==0xABCD);
    CK(fdc_raw28(0x3ABC,0x1234)==UINT32_C(0x0ABC1234));
    fake_t f;fdc_device_t d;fixture(&f,&d);fdc_frame_t frame;
    f.read_count=0;CK(fdc_poll(&d,&frame)==FDC_OK);
    CK(frame.read_mask==15 && frame.valid_mask==15 && !frame.ratio_error_mask);
    CK(f.read_count==9 && f.read_order[0]==0x18);
    for (unsigned reg=0;reg<8;reg++) CK(f.read_order[reg+1]==reg);
    for (unsigned ch=0;ch<4;ch++) CK(frame.raw[ch]==(((uint32_t)(0x100+ch)<<16)|(0x1200+ch)));
    fdc_ranges_t ranges={0};CK(fdc_ranges_accept(&ranges,&frame));
    for (unsigned ch=0;ch<4;ch++) CK(ranges.channel[ch].valid_count==1 && ranges.channel[ch].has_range);
    f.reg[1]+=100;f.reg[3]-=20;f.reg[5]+=300;f.reg[7]-=40;
    CK(fdc_poll(&d,&frame)==FDC_OK);CK(fdc_ranges_accept(&ranges,&frame));
    CK(fdc_range_span(&ranges.channel[0])==100);CK(fdc_range_span(&ranges.channel[1])==20);
    CK(fdc_range_span(&ranges.channel[2])==300);CK(fdc_range_span(&ranges.channel[3])==40);
    fdc_ranges_clear(&ranges);CK(ranges.epoch==1 && !ranges.frames && !ranges.transport_errors);
    for (unsigned ch=0;ch<4;ch++) CK(!ranges.channel[ch].has_range && !ranges.channel[ch].valid_count);
    fdc_ranges_clear(&ranges);CK(ranges.epoch==2);
}
static void test_diagnostics_and_bad_ranges(void)
{
    fake_t f;fdc_device_t d;fixture(&f,&d);fdc_frame_t frame;fdc_ranges_t ranges={0};
    CK(fdc_poll(&d,&frame)==FDC_OK);CK(fdc_ranges_accept(&ranges,&frame));
    uint32_t old=ranges.channel[2].maximum;
    f.reg[4]|=0x2000;f.reg[5]=0xFFFF;
    CK(fdc_poll(&d,&frame)==FDC_OK);CK(frame.flags[2]==2 && frame.valid_mask==11);
    CK(fdc_ranges_accept(&ranges,&frame));
    CK(ranges.channel[2].maximum==old && ranges.channel[2].valid_count==1 && ranges.channel[2].error_count==1);
    CK(ranges.channel[3].valid_count==2);
    f.reg[4]&=0x0FFF;f.reg[6]=0x400; // fIN/fREF == 1/4: retain raw but flag it.
    CK(fdc_poll(&d,&frame)==FDC_OK);CK(frame.ratio_error_mask==8 && frame.valid_mask==7);
    CK(fdc_ranges_accept(&ranges,&frame));CK(ranges.channel[3].valid_count==2 && ranges.channel[3].error_count==1);
    f.reg[6]=0x103;
    f.reg[0x18]=0x0800;CK(fdc_poll(&d,&frame)==FDC_NO_DATA);
    f.reg[0x18]=0xC400;CK(fdc_poll(&d,&frame)==FDC_NO_DATA);
    f.reg[0x18]=0x40;CK(fdc_poll(&d,&frame)==FDC_OK);
    CK(frame.status_errors==0x0C00 && frame.status_error_channels==9 && frame.valid_mask==0);
    CK((frame.status&0xC000)==0xC000);CK(fdc_ranges_accept(&ranges,&frame));
    for (unsigned ch=0;ch<4;ch++) CK(ranges.channel[ch].error_count>=1);
    CK(fdc_poll(&d,&frame)==FDC_OK && !frame.status_errors && frame.valid_mask==15);
}
static void test_failure_never_publishes_partial_or_old_data(void)
{
    fake_t f;fdc_device_t d;fixture(&f,&d);fdc_frame_t frame;
    CK(fdc_poll(&d,&frame)==FDC_OK);
    f.fail_reg=7;CK(fdc_poll(&d,&frame)==FDC_IO_ERROR);
    fdc_frame_t empty={0};CK(!memcmp(&empty,&frame,sizeof(frame)) && !d.configured);
    unsigned reads=f.reads;CK(fdc_poll(&d,&frame)==FDC_IO_ERROR && f.reads==reads);
    CK(d.last_reg==7 && d.transport_error==777);
    fdc_ranges_t ranges={0};frame.read_mask=7;frame.valid_mask=15;
    CK(!fdc_ranges_accept(&ranges,&frame) && !ranges.frames && ranges.transport_errors==1);
    for (unsigned ch=0;ch<4;ch++) CK(!ranges.channel[ch].has_range && ranges.channel[ch].error_count==1);
    fixture(&f,&d);f.now=500000;CK(fdc_poll(&d,&frame)==FDC_DATA_TIMEOUT && !frame.read_mask);
    fixture(&f,&d);f.mismatch=true;fdc_config_t c=config();fdc_io_t io={&f,read_fake,write_fake,now_fake};
    CK(fdc_init(&d,&c,&io)==FDC_VERIFY_ERROR && d.last_reg==0x1B);
    f.mismatch=false;f.reg[0x7F]=0x3054;CK(fdc_init(&d,&c,&io)==FDC_ID_ERROR);
    f.reg[0x7F]=0x3055;c.clock=FDC_CLOCK_INT;c.fref_divider=2;
    CK(fdc_init(&d,&c,&io)==FDC_OK && f.reg[0x1A]==0x1401);
    for (unsigned ch=0;ch<4;ch++) CK(f.reg[0x14+ch]==0x2002);
}
int main(void)
{
    test_configuration_gate();test_endian_and_scan();test_diagnostics_and_bad_ranges();
    test_failure_never_publishes_partial_or_old_data();
    printf("PASS fdc2214 raw/debug host checks=%u\n",checks);return 0;
}
