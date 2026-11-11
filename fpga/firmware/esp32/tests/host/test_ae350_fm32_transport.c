#include <stdint.h>
static void observe_fm32_raw(uint32_t word);
#define AE350_RAW_TEST_OBSERVE observe_fm32_raw
#define main original_ae350_engine_main
#include "test_ae350_engine.c"
#undef main

static uint8_t captured[256];
static unsigned captured_count,expected_source;
static void observe_fm32_raw(uint32_t word)
{
    assert(((word>>16)&255u)==expected_source);
    assert(captured_count<sizeof captured);
    captured[captured_count++]=(uint8_t)word;
}
static void forward(unsigned source,uint64_t token,const uint8_t *bytes,size_t length)
{
    captured_count=0;expected_source=source;
    assert(raw_midi_claim(source,token));
    assert(raw_midi_submit(source,token,bytes,length,fake_time+500000));
    for(unsigned i=0;i<1000&&captured_count<length;i++)ticks(1);
    assert(captured_count==length&&!memcmp(captured,bytes,length));
    assert(raw_state().ready&&!raw_state().fault);
}
int main(void)
{
    assert(ENGINE_CPU_FM_VOICES==32&&ENGINE_CPU_FM_OPERATORS==4);
    regs[0x700]=MIDI_RAW_ABI_MAGIC;regs[0x70a]=7;
    regs[0x718]=AE350_CPU_MAGIC;regs[0x719]=2;regs[0x71c]=64;
    raw_midi_init();engine_link_init();ticks(8);
    midi_template_image_t image={0};image.fm_expression=0x407f;
    midi_template_link_publish(&image);engine_link_performance_ready(true);ticks(40);
    assert(image_state().ready&&raw_state().ready&&raw_state().cpu_control);
    uint8_t on[99],off[99];
    for(unsigned n=0;n<33;n++){
        on[3*n]=0x92;on[3*n+1]=(uint8_t)(36+n);on[3*n+2]=100;
        off[3*n]=0x82;off[3*n+1]=(uint8_t)(36+n);off[3*n+2]=0;
    }
    forward(MIDI_RAW_SOURCE_WEB,123,on,sizeof on);
    forward(MIDI_RAW_SOURCE_WEB,123,off,sizeof off);
    forward(MIDI_RAW_SOURCE_BLE,456,on,sizeof on);
    forward(MIDI_RAW_SOURCE_BLE,456,off,sizeof off);
    const uint8_t controls[]={0xb2,64,127,0xe2,0,96,0xb2,7,0,0xb2,7,127,0xb2,64,0};
    forward(MIDI_RAW_SOURCE_WEB,123,controls,sizeof controls);
    engine_note_t legacy={.voice=31};assert(!engine_link_note_on(&legacy));
    assert(!engine_link_fm_note_on(31,0,100,127,64));
    puts("PASS FM32 ESP transport: actual RAW/SPI owner forwards33 NoteOn+33 NoteOff byte-exact forWeb/BLE onchannel3, controls unchanged, no local8voice allocation or legacy fallback; target32x4");
    return 0;
}
