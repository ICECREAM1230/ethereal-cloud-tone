#include <assert.h>
#include <stdio.h>
#include "midi_template_link.h"
#include "spi_regs.h"
static int64_t now;
static uint32_t expression=0x407f,generation,pending_expression;
static unsigned writes,reads;
static bool io_ok=true;
int64_t esp_timer_get_time(void) { return now++; }
void fpga_word_write_burst(uint16_t a,const uint32_t *w,size_t n)
{ assert(a==0xb400 && n==1);pending_expression=*w;writes++;now+=10; }
void fpga_word_write(uint16_t a,uint32_t v) { fpga_word_write_burst(a,&v,1); }
bool fpga_word_read_burst(uint16_t a,uint32_t *w,size_t n)
{
    assert(a==0x07f2 && n==2);reads++;now+=10;
    if(!io_ok)return false;
    w[0]=expression;w[1]=generation;return true;
}
static midi_template_link_state_t state(void)
{ midi_template_link_state_t s;midi_template_link_state(&s);return s; }
static void service(void) { midi_template_link_service(now+100,true,true); }
static void clean(void)
{ now=0;expression=0x407f;generation=0;writes=reads=0;io_ok=true;midi_template_link_init(); }
static void ack(uint32_t value,uint32_t gen) { expression=value;generation=gen;service(); }
int main(void)
{
    midi_template_image_t image={.fm_expression=0x4055};clean();
    midi_template_link_publish(&image);
    midi_template_link_service(now+100,false,true);assert(!writes && !state().ready);
    service();assert(writes==1 && pending_expression==0x4055 && !state().ready);
    ack(0x4055,0);assert(!state().ready); // data alone is not an ACK
    ack(0x407f,1);assert(!state().ready); // generation alone is not an ACK
    ack(0x4055,1);assert(state().ready && state().applied_generation==1);
    /* The same expression still requires another real apply generation. */
    midi_template_link_publish(&image);service();assert(writes==2 && !state().ready);
    service();assert(!state().ready);ack(0x4055,2);assert(state().ready);
    now+=20000;ack(0x407f,0);assert(state().fault==MIDI_TEMPLATE_LINK_RESET && !state().ready);
    midi_template_link_publish(&image);now+=20000;service();assert(writes==3);
    now+=100001;service();assert(state().fault==MIDI_TEMPLATE_LINK_TIMEOUT);
    midi_template_link_publish(&image);service();assert(writes==3);
    now+=20000;service();assert(writes==3 && state().busy);
    ack(0x4055,1);now+=20000;service();assert(writes==4); // old COMMIT fence resolved
    ack(0x4055,2);assert(state().ready);
    clean();generation=UINT32_MAX;midi_template_link_publish(&image);service();
    ack(0x4055,1);assert(state().ready && state().generation==1);
    clean();midi_template_link_publish(&image);service();midi_template_link_invalidate();
    midi_template_link_publish(&image);service();assert(writes==1);
    ack(0x4055,1);now+=20000;service();assert(writes==2);
    ack(0x4055,2);assert(state().ready);
    clean();io_ok=false;midi_template_link_publish(&image);service();
    assert(!writes && state().fault==MIDI_TEMPLATE_LINK_IO);
    clean();midi_template_link_publish(&image);service();
    midi_template_image_t latest={.fm_expression=0x2033};
    midi_template_link_publish(&latest);service();assert(writes==1);
    ack(0x4055,1);assert(!state().ready && state().busy);
    service();assert(writes==2 && pending_expression==0x2033);
    ack(0x2033,2);assert(state().ready);
    clean();midi_template_link_publish(&image);service();now+=100001;service();
    midi_template_link_publish(&latest);io_ok=false;now+=20000;service();
    assert(writes==1 && state().busy && state().fault==MIDI_TEMPLATE_LINK_IO);
    io_ok=true;expression=0x4055;generation=1;now+=20000;service();
    assert(writes==2 && pending_expression==0x2033);
    ack(0x2033,2);assert(state().ready);
    puts("FM_ONLY template: b400 only, actual expression+generation ACK, repeated value, timeout/reset/unknown-write fence and wrap PASS");
}
