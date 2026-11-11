// TI SNOSCZ5B (Oct 2024): register fields, not the inconsistent scan example.
#include "fdc2214.h"
#include <stdio.h>
#include <string.h>

static bool invalid(char *reason, size_t size, const char *text)
{
    if (reason && size) snprintf(reason,size,"%s",text);
    return false;
}

bool fdc_config_valid(const fdc_config_t *c, char *why, size_t size)
{
    if (!c || !c->wiring_confirmed) return invalid(why,size,"wiring not confirmed");
    if (c->sda_gpio<0 || c->scl_gpio<0) return invalid(why,size,"SDA/SCL unassigned");
    int pins[]={c->sda_gpio,c->scl_gpio,c->sd_gpio,c->addr_gpio};
    for (unsigned i=0;i<4;i++) {
        if (pins[i]<-1 || pins[i]>48) return invalid(why,size,"GPIO outside ESP32-S3 range");
        if (pins[i]<0) continue;
        if (pins[i]==19 || pins[i]==20 || (pins[i]>=26 && pins[i]<=37) ||
            pins[i]==0 || pins[i]==3 || pins[i]==45 || pins[i]==46 ||
            (pins[i]>=22 && pins[i]<=25))
            return invalid(why,size,"GPIO reserved for USB/memory/strapping or unavailable");
        for (unsigned j=0;j<i;j++) if (pins[i]==pins[j]) return invalid(why,size,"GPIO signals overlap");
    }
    if (c->clock!=FDC_CLOCK_EXT && c->clock!=FDC_CLOCK_INT)
        return invalid(why,size,"reference clock not confirmed");
    if (c->addr_gpio>=0 && c->sd_gpio<0)
        return invalid(why,size,"ADDR GPIO requires SD GPIO; otherwise strap ADDR externally");
    if (c->address!=0x2A && c->address!=0x2B) return invalid(why,size,"I2C address must be 0x2A/0x2B");
    if (!c->fref_divider || c->fref_divider>1023 || c->rcount<9 || c->settlecount<4 || !c->data_timeout_ms)
        return invalid(why,size,"invalid measurement timing/divider");
    if (c->clock==FDC_CLOCK_EXT && (c->external_hz<2000000 || c->external_hz>40000000))
        return invalid(why,size,"external CLKIN must be 2..40 MHz");
    uint32_t max_hz=c->clock==FDC_CLOCK_INT ? 55000000u : c->external_hz;
    if (max_hz>UINT64_C(40000000)*c->fref_divider)
        return invalid(why,size,"FREF can exceed 40 MHz (internal clock requires divider >=2)");
    for (unsigned i=0;i<FDC_CHANNELS;i++) if (c->idrive[i]>31) return invalid(why,size,"IDRIVE outside 0..31");
    // Reject a data deadline shorter than even the nominal scan plus bus margin.
    uint32_t min_clock=c->clock==FDC_CLOCK_INT ? 35000000u : c->external_hz;
    uint64_t clocks=4u*(16u*(uint64_t)c->rcount+4u+16u*c->settlecount+32u);
    uint64_t scan_ms=(clocks*c->fref_divider*1000u+min_clock-1u)/min_clock;
    if (c->data_timeout_ms<scan_ms+20u) return invalid(why,size,"data deadline too short for configured scan");
    if (why && size) why[0]=0;
    return true;
}

uint16_t fdc_be16(const uint8_t b[2]) { return (uint16_t)(((uint16_t)b[0]<<8)|b[1]); }
uint32_t fdc_raw28(uint16_t msb, uint16_t lsb) { return ((uint32_t)(msb&0x0FFFu)<<16)|lsb; }

static fdc_result_t fail(fdc_device_t *d, fdc_result_t result)
{
    d->configured=false; d->result=result; return result;
}
static bool read_reg(fdc_device_t *d, uint8_t reg, uint16_t *value)
{
    d->last_reg=reg; d->transport_error=d->io.read(d->io.context,reg,value);
    if (d->transport_error) {fail(d,FDC_IO_ERROR);return false;}
    d->actual=*value; return true;
}
static bool write_verify(fdc_device_t *d, uint8_t reg, uint16_t value, uint16_t mask)
{
    uint16_t readback=0;
    d->last_reg=reg;d->expected=value;
    d->transport_error=d->io.write(d->io.context,reg,value);
    if (d->transport_error) {fail(d,FDC_IO_ERROR);return false;}
    if (!read_reg(d,reg,&readback)) return false;
    if ((readback&mask)!=(value&mask)) {fail(d,FDC_VERIFY_ERROR);return false;}
    return true;
}

fdc_result_t fdc_init(fdc_device_t *d, const fdc_config_t *c, const fdc_io_t *io)
{
    memset(d,0,sizeof(*d));d->result=FDC_UNCONFIGURED;
    // This gate is independently host-tested: unknown pins/clock cause zero I/O.
    if (!fdc_config_valid(c,NULL,0) || !io || !io->read || !io->write || !io->now_us) return d->result;
    d->config=*c;d->io=*io;
    uint16_t id=0;
    d->expected=0x5449;
    if (!read_reg(d,0x7E,&id)) return d->result;
    if (id!=0x5449) return fail(d,FDC_ID_ERROR);
    d->expected=0x3055;
    if (!read_reg(d,0x7F,&id)) return d->result;
    if (id!=0x3055) return fail(d,FDC_ID_ERROR);
    uint16_t active=(uint16_t)(0x1401u|(c->clock==FDC_CLOCK_EXT ? 0x0200u:0));
    if (!write_verify(d,0x1A,(uint16_t)(active|0x2000u),0xEAC0)) return d->result;
    for (unsigned ch=0;ch<FDC_CHANNELS;ch++)
        if (!write_verify(d,(uint8_t)(0x08+ch),c->rcount,0xFFFF)) return d->result;
    for (unsigned ch=0;ch<FDC_CHANNELS;ch++)
        if (!write_verify(d,(uint8_t)(0x10+ch),c->settlecount,0xFFFF)) return d->result;
    for (unsigned ch=0;ch<FDC_CHANNELS;ch++)
        if (!write_verify(d,(uint8_t)(0x14+ch),(uint16_t)(0x2000u|c->fref_divider),0x33FF)) return d->result;
    for (unsigned ch=0;ch<FDC_CHANNELS;ch++)
        if (!write_verify(d,(uint8_t)(0x1E + ch),(uint16_t)(c->idrive[ch]<<11),0xF800)) return d->result;
    if (!write_verify(d,0x19,0x3821,0x3821) ||
        !write_verify(d,0x1B,FDC_MUX_FOUR_CHANNELS,0xE007) ||
        !write_verify(d,0x1A,active,0xEAC0)) return d->result;
    d->configured=true;d->last_frame_us=d->io.now_us(d->io.context);d->result=FDC_OK;
    return FDC_OK;
}

fdc_result_t fdc_poll(fdc_device_t *d, fdc_frame_t *out)
{
    memset(out,0,sizeof(*out));
    if (!d->configured) return d->result;
    uint16_t status=0;
    if (!read_reg(d,0x18,&status)) return d->result;
    if (status&FDC_STATUS_ERRORS) {
        d->pending_errors|=status&FDC_STATUS_ERRORS;
        d->last_error_channel=(uint8_t)(status>>14);
        d->pending_error_channels|=(uint8_t)(1u<<d->last_error_channel);
    }
    if (d->io.now_us(d->io.context)-d->last_frame_us>=1000u*(uint64_t)d->config.data_timeout_ms)
        return fail(d,FDC_DATA_TIMEOUT);
    if (!(status&0x0040)) return FDC_NO_DATA;
    fdc_frame_t f={0};
    f.status=(uint16_t)((status&~FDC_STATUS_ERRORS)|d->pending_errors);
    if (d->pending_errors) f.status=(uint16_t)((f.status&0x3FFFu)|((uint32_t)d->last_error_channel<<14));
    f.status_errors=d->pending_errors;f.status_error_channels=d->pending_error_channels;
    for (unsigned ch=0;ch<FDC_CHANNELS;ch++) {
        uint16_t msb=0,lsb=0;
        if (!read_reg(d,(uint8_t)(ch*2),&msb) || !read_reg(d,(uint8_t)(ch*2+1),&lsb)) return d->result;
        f.raw[ch]=fdc_raw28(msb,lsb);f.flags[ch]=(uint8_t)((msb>>12)&3);
        // FDC digitizes fIN/fREF. TI requires fIN < fREF/4; retain raw but
        // exclude ratios outside the specified converter operating range.
        if (f.raw[ch]>=UINT32_C(0x04000000)) f.ratio_error_mask|=(uint8_t)(1u<<ch);
        f.read_mask|=(uint8_t)(1u<<ch);
        if (!f.status_errors && !f.flags[ch] && !(f.ratio_error_mask&(1u<<ch))) f.valid_mask|=(uint8_t)(1u<<ch);
    }
    f.time_us=d->io.now_us(d->io.context);
    if (f.time_us-d->last_frame_us>=1000u*(uint64_t)d->config.data_timeout_ms) return fail(d,FDC_DATA_TIMEOUT);
    d->last_frame_us=f.time_us;d->pending_errors=0;d->pending_error_channels=0;
    d->result=FDC_OK;*out=f;return FDC_OK;
}

const char *fdc_result_name(fdc_result_t r)
{
    switch (r) {
    case FDC_OK:return "ok";case FDC_NO_DATA:return "waiting";
    case FDC_UNCONFIGURED:return "unconfigured";case FDC_IO_ERROR:return "i2c_error";
    case FDC_ID_ERROR:return "id_mismatch";case FDC_VERIFY_ERROR:return "verify_mismatch";
    case FDC_DATA_TIMEOUT:return "data_timeout";default:return "unknown";
    }
}
