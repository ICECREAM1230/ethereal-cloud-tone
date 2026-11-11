#include "ae350_control.h"
#include "raw_midi.h"
#include "spi_regs.h"
bool ae350_control_identity(void)
{
    uint32_t magic=0,caps=0,cpu[2]={0};
    fpga_cpu_link_authorize(false);
    bool valid=fpga_word_read_burst_observe(MIDI_RAW_REG_MAGIC,&magic,1) && magic==MIDI_RAW_ABI_MAGIC &&
        fpga_word_read_burst_observe(MIDI_RAW_REG_INPUT_CAPABILITIES,&caps,1) &&
        (caps&UINT32_C(7))==UINT32_C(7) &&
        fpga_word_read_burst_observe(AE350_CPU_MAGIC_REG,cpu,2) && cpu[0]==AE350_CPU_MAGIC && cpu[1]==AE350_CPU_ABI;
    fpga_cpu_link_authorize(valid);
    return valid;
}
bool ae350_control_write_allowed(uint16_t address,size_t words)
{
    if(!words || words>12 || (uint32_t)address+words>UINT32_C(0x10000))return false;
    if(address==MIDI_RAW_REG_COMMAND)return words==1;
    if(address==UINT16_C(0xb401))return words==1;
    if(address==UINT16_C(0x07b5))return words==1;
    if(address==0xb500 || address==0xb501 || address==0xb51e)return words==1;
    if(address>=0xb510 && address<=0xb515)return address+words<=0xb516;
    if(address==UINT16_C(0x0725) || address==UINT16_C(0x0726))return words==1;
    for(size_t i=0;i<words;i++) {
        uint16_t a=(uint16_t)(address+i);
        bool allowed=(a>=0x8000 && a<=0x8041) || a==0x80ff || (a>=0x8100 && a<=0x81ff) ||
            (a>=0xb000 && a<=0xb0ff) || (a>=0xb200 && a<=0xb27f) ||
            (a>=0xb300 && a<=0xb33f) || a==0xb3fe || a==0xb3ff || a==0xb400 ||
            (a>=0xb410 && a<=0xb41c) || a==0xb41f;
        if(!allowed)return false;
    }
    return true;
}
