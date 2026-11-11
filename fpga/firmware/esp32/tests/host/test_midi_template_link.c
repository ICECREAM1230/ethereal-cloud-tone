#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "midi_template_link.h"
#include "spi_regs.h"

static int64_t now;
static uint32_t dds_ack,ks_ack;
static bool io_ok;
static unsigned writes,reads;
static struct {uint16_t addr;uint32_t value;} log_words[4096];
static unsigned logged;
int64_t esp_timer_get_time(void) { return now; }
void fpga_word_write_burst(uint16_t addr,const uint32_t *words,size_t n)
{
    assert(n && n<=12); assert(4+4*n<=64);
    writes++; now+=10;
    for(unsigned i=0;i<n;i++) {
        assert(logged<sizeof(log_words)/sizeof(*log_words));
        log_words[logged].addr=addr+i;log_words[logged++].value=words[i];
    }
}
void fpga_word_write(uint16_t addr,uint32_t value) { fpga_word_write_burst(addr,&value,1); }
bool fpga_word_read_burst(uint16_t addr,uint32_t *words,size_t n)
{
    assert(addr==0x0707 && n==2); reads++; now+=10;
    if(!io_ok)return false;
    words[0]=dds_ack;words[1]=ks_ack;return true;
}
static midi_template_link_state_t state(void)
{ midi_template_link_state_t s;midi_template_link_state(&s);return s; }
static void clean(void)
{
    now=0;dds_ack=ks_ack=0;io_ok=true;writes=reads=logged=0;midi_template_link_init();
}
static void fill(midi_template_image_t *image,uint32_t tag)
{
    memset(image,0,sizeof *image);
    for(unsigned u=0;u<8;u++)for(unsigned w=0;w<8;w++)image->elements[u][w]=tag+u*8+w;
    image->key_track=tag+64;image->bend_range=tag+65;
    for(unsigned n=0;n<128;n++) {
        image->amp_depth[n]=tag+66+n;image->mod_depth[n]=tag+194+n;
        image->ks_notes[n][0]=tag+322+2*n;image->ks_notes[n][1]=tag+323+2*n;
        image->ks_pan[n]=tag+578+n;
    }
    image->fm_expression=tag+706;
}
static unsigned upload(void)
{
    unsigned calls=0;
    while(logged<709 && calls++<200) midi_template_link_service(now+100,true,true);
    assert(logged==709 && calls<200);return writes;
}
static void check_image(unsigned start,uint32_t tag,uint32_t generation)
{
    for(unsigned i=0;i<707;i++) {
        uint16_t addr=i<64?0x8000+i:i<66?0x8040+i-64:i<194?0x8100+i-66:
            i<322?0x8180+i-194:i<578?0xb000+i-322:i<706?0xb200+i-578:0xb400;
        assert(log_words[start+i].addr==addr && log_words[start+i].value==tag+i);
    }
    assert(log_words[start+707].addr==0x80ff && log_words[start+707].value==generation);
    assert(log_words[start+708].addr==0xb3ff && log_words[start+708].value==generation);
}
static void test_upload_and_latest(void)
{
    clean();assert(sizeof(midi_template_image_t)==3084);
    assert(!state().ready && !state().busy);
    midi_template_image_t first,latest;fill(&first,1000);fill(&latest,9000);
    midi_template_link_publish(&first);assert(state().busy && !state().ready);
    midi_template_link_service(now+100,true,false);assert(!writes && !state().ready);
    midi_template_link_service(now+100,false,true);assert(!writes);
    midi_template_link_service(now,true,true);assert(!writes);
    midi_template_link_service(now+35,true,true);assert(writes && logged<707);
    fill(&latest,8000);midi_template_link_publish(&latest);
    fill(&latest,9000);midi_template_link_publish(&latest);memset(&latest,0,sizeof(latest));
    unsigned sent=upload();check_image(0,1000,1);
    assert(!state().ready && state().busy);
    for(unsigned i=0;i<3;i++)midi_template_link_service(now+100,true,true);
    assert(writes==sent); // no shadow writes until BOTH ACKs
    dds_ack=1;midi_template_link_service(now+100,true,true);assert(writes==sent && !state().ready);
    ks_ack=1;midi_template_link_service(now+100,true,true);assert(!state().ready);
    for(unsigned i=0;logged<1418 && i<200;i++)midi_template_link_service(now+100,true,true);
    assert(logged==1418);check_image(709,9000,2);
    dds_ack=ks_ack=2;midi_template_link_service(now+100,true,true);
    assert(state().ready && !state().busy && state().applied_generation==2);
    dds_ack=ks_ack=0;now+=20000;midi_template_link_service(now+100,true,true);
    assert(!state().ready && state().fault==MIDI_TEMPLATE_LINK_RESET);
    sent=writes;midi_template_link_service(now+100,true,true);assert(writes==sent);
}
static void test_faults_and_wrap(void)
{
    midi_template_image_t image;fill(&image,1000);
    clean();midi_template_link_publish(&image);upload();now+=100001;
    midi_template_link_service(now+100,true,true);
    assert(!state().ready && state().fault==MIDI_TEMPLATE_LINK_TIMEOUT);
    unsigned sent=writes;midi_template_link_service(now+100,true,true);assert(writes==sent);
    /* Explicit re-publish still cannot overwrite a bank while old COMMIT is
     * unresolved. Once its ACK arrives, a complete fresh image may begin. */
    midi_template_link_publish(&image);midi_template_link_service(now+100,true,true);assert(writes==sent);
    dds_ack=ks_ack=1;now+=20000;
    for(unsigned i=0;logged<1418 && i<200;i++)midi_template_link_service(now+100,true,true);
    assert(logged==1418);check_image(709,1000,2);
    dds_ack=ks_ack=2;midi_template_link_service(now+100,true,true);assert(state().ready);
    clean();midi_template_link_publish(&image);io_ok=false;
    midi_template_link_service(now+100,true,true);
    assert(!writes && state().fault==MIDI_TEMPLATE_LINK_IO && !state().ready);
    clean();dds_ack=ks_ack=UINT32_MAX;midi_template_link_publish(&image);upload();
    check_image(0,1000,1);dds_ack=ks_ack=1;midi_template_link_service(now+100,true,true);
    assert(state().ready && state().generation==1);
    midi_template_link_service(now+100,true,false);assert(!state().ready);
    sent=writes;midi_template_link_service(now+100,true,true);assert(writes==sent);
    clean();dds_ack=ks_ack=10;midi_template_link_publish(&image);upload();
    dds_ack=ks_ack=9;midi_template_link_service(now+100,true,true);
    assert(state().fault==MIDI_TEMPLATE_LINK_RESET && !state().ready);
    clean();midi_template_link_publish(&image);upload();io_ok=false;
    midi_template_link_service(now+100,true,true);assert(state().fault==MIDI_TEMPLATE_LINK_IO);
    io_ok=true;midi_template_link_publish(&image);sent=writes;
    midi_template_link_service(now+100,true,true);assert(writes==sent);
    clean();midi_template_link_publish(&image);
    midi_template_link_service(now+100000,true,true);
    assert(reads==1 && writes==11 && logged==114); // 12 transactions maximum
}
static void test_explicit_invalidate(void)
{
    clean();midi_template_image_t image;fill(&image,1000);
    midi_template_link_publish(&image);midi_template_link_invalidate();
    assert(!writes && !reads && !state().busy && state().fault==MIDI_TEMPLATE_LINK_RESET);
    midi_template_link_service(now+100,true,true);assert(!writes);
    midi_template_link_publish(&image);upload();unsigned sent=writes;
    midi_template_link_invalidate();
    assert(!state().ready && !state().busy && writes==sent);
    midi_template_link_publish(&image);midi_template_link_service(now+100,true,true);
    assert(writes==sent); // invalidation does not discard an unknown COMMIT
    dds_ack=ks_ack=1;now+=20000;
    for(unsigned i=0;logged<1418 && i<200;i++)midi_template_link_service(now+100,true,true);
    assert(logged==1418);check_image(709,1000,2);
    dds_ack=ks_ack=2;midi_template_link_service(now+100,true,true);assert(state().ready);
}
static void test_late_ack_recovery_intent(void)
{
    clean();midi_template_image_t image;fill(&image,1000);
    midi_template_link_publish(&image);upload();now+=100001;
    midi_template_link_service(now+100,true,true);
    unsigned sent=writes,polled=reads;
    // A failed old image alone never grants automatic recovery permission.
    for(unsigned i=0;i<3;i++) {now+=20000;midi_template_link_service(now+100,true,true);}
    assert(writes==sent && reads==polled && !state().busy);
    midi_template_link_publish(&image);midi_template_link_service(now+100,true,true);
    for(unsigned i=0;i<3;i++) {
        now+=100001;midi_template_link_service(now+100,true,true);
        assert(writes==sent && state().busy && !state().ready);
        assert(state().fault==MIDI_TEMPLATE_LINK_TIMEOUT);
    }
    polled=reads;
    for(unsigned i=0;i<10;i++)midi_template_link_service(now+100,true,true);
    assert(reads==polled && writes==sent); // bounded 20ms fault-health cadence
    // Transient read failures while recovering must retain that same intent.
    io_ok=false;now+=20000;midi_template_link_service(now+100,true,true);
    assert(state().busy && state().fault==MIDI_TEMPLATE_LINK_IO && writes==sent);
    io_ok=true;dds_ack=ks_ack=1;now+=20000;
    for(unsigned i=0;logged<1418 && i<200;i++)midi_template_link_service(now+100,true,true);
    assert(logged==1418);check_image(709,1000,2);
    dds_ack=ks_ack=2;midi_template_link_service(now+100,true,true);
    assert(state().ready && !state().busy);
}
int main(void)
{
    test_upload_and_latest();test_faults_and_wrap();test_explicit_invalidate();test_late_ack_recovery_intent();
    puts("MIDI template link: bounded bursts, static fence, immutable transfer/latest publish, dual ACK, reset/timeout/read failure/wrap PASS");
}
