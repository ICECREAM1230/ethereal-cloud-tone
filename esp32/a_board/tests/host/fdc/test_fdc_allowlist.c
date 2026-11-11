#include <assert.h>
#include <stdio.h>
#include "ae350_control.h"
void fpga_cpu_link_authorize(bool value){(void)value;}
bool fpga_word_read_burst_observe(uint16_t address,uint32_t *words,size_t count)
{(void)address;(void)words;(void)count;return false;}
int main(void)
{
    assert(ae350_control_write_allowed(0xb500,1));
    assert(ae350_control_write_allowed(0xb501,1));
    assert(ae350_control_write_allowed(0xb510,6));
    assert(ae350_control_write_allowed(0xb51e,1));
    assert(!ae350_control_write_allowed(0xb500,2));
    assert(!ae350_control_write_allowed(0xb50f,2));
    assert(!ae350_control_write_allowed(0xb515,2));
    assert(!ae350_control_write_allowed(0xb51e,2));
    assert(!ae350_control_write_allowed(0x071f,1));
    assert(ae350_control_write_allowed(0xb401,1));
    assert(ae350_control_write_allowed(0x0725,1));
    puts("PASS FDC exact address allowlist and preserved mode/reverb guards");
}
