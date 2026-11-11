#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "midi_template_link.h"
#include "ks_timbre_params.h"
#include "ks_timbre_contract.h"
static int64_t now;
static uint32_t dds_ack,ks_ack,kst[16],shadow[448];
static unsigned commits,ks_writes,dds_commits;
static bool auto_ack,io_ok=true,fail_last;
static unsigned snapshot_reads;
int64_t esp_timer_get_time(void){return now;}
void fpga_word_write_burst(uint16_t addr,const uint32_t *words,size_t n)
{
    assert(n&&n<=12);now+=10;
    for(unsigned i=0;i<n;i++) {
        uint16_t a=addr+i;
        if(a>=0xb000&&a<=0xb0ff){shadow[a-0xb000]=words[i];ks_writes++;}
        else if(a>=0xb200&&a<=0xb27f){shadow[256+a-0xb200]=words[i];ks_writes++;}
        else if(a>=0xb300&&a<=0xb33f){shadow[384+a-0xb300]=words[i];ks_writes++;}
        else if(a==0xb3fe){commits++;kst[15]=words[i];kst[5]=shadow[387];kst[4]=0x101;if(auto_ack){ks_ack=words[i];kst[4]=0;kst[6]=shadow[387];kst[7]=shadow[386];kst[10]=shadow[444];kst[11]=shadow[445];kst[12]=kst[13]=words[i];kst[14]=0;}}
        else if(a==0xb3ff){assert(!"extended image used old B3FF");}
        else if(a==0x80ff){dds_ack=words[i];dds_commits++;}
    }
}
void fpga_word_write(uint16_t a,uint32_t w){fpga_word_write_burst(a,&w,1);}
bool fpga_word_read_burst(uint16_t addr,uint32_t *out,size_t n)
{
    now+=10;if(!io_ok)return false;
    if(addr==0x553 && n==1 && (++snapshot_reads%2)==0 && fail_last)return false;
    if(addr>=0x550&&addr+n<=0x560){memcpy(out,kst+(addr-0x550),n*4);return true;}
    if(addr==0x707){out[0]=dds_ack;if(n==2)out[1]=ks_ack;return true;}
    if(addr==0x708&&n==1){out[0]=ks_ack;return true;}
    assert(!"unexpected real SPI address");return false;
}
static midi_template_link_state_t read_state(void){midi_template_link_state_t s;midi_template_link_state(&s);return s;}
static void step(void){midi_template_link_service(now+1000,true,true);}
static void slices(unsigned n){for(unsigned i=0;i<n;i++)step();}
static void fixture(void)
{
    now=0;dds_ack=ks_ack=commits=ks_writes=dds_commits=0;memset(shadow,0,sizeof shadow);
    memset(kst,0,sizeof kst);kst[0]=KST_MAGIC;kst[1]=KST_ABI;kst[2]=63;kst[3]=1;kst[8]=kst[9]=1;auto_ack=true;io_ok=true;fail_last=false;snapshot_reads=0;
    midi_template_link_init();midi_template_link_timbre_enable();slices(4);assert(read_state().timbre_valid);
}
static midi_template_image_t image;
static ks_timbre_image_t physical;
static void publish(unsigned id,unsigned modelgen,unsigned decay)
{
    ks_timbre_preset_t p;assert(ks_timbre_factory(id,&p));assert(ks_timbre_compile(&p,decay,180,0,modelgen,&physical));
    memset(&image,0,sizeof image);memcpy(image.ks_notes,physical.notes,sizeof image.ks_notes);memcpy(image.ks_pan,physical.pan,sizeof image.ks_pan);memcpy(image.ks_header,physical.header,sizeof image.ks_header);
    midi_template_link_publish(&image);
}
static void verified_upload(void)
{
    fixture();publish(2,11,2000);slices(12);midi_template_link_state_t s=read_state();
    assert(s.ready&&!s.fault&&commits==1&&ks_writes==448&&s.ks_applied_generation==1);
    ks_timbre_image_finalize(&physical,1);assert(!memcmp(shadow,&physical,sizeof physical));
    now+=20001;slices(4);s=read_state();assert(s.timbre_applied==11&&s.timbre_template==1&&s.timbre_crc==physical.header[60]);
    assert(s.timbre_image_crc==s.ks_applied_image_crc&&s.ks_applied_revision==s.ks_desired_revision);
    unsigned old=commits,writes=ks_writes;image.elements[0][0]=1;midi_template_link_publish(&image);slices(10);
    assert(commits==old&&ks_writes==writes); /* finalized CRC cannot dirty KS on DDS change */
    publish(2,11,4000);slices(10);assert(commits==2&&read_state().ks_applied_generation==2);
}
static void cancel_before_commit(void)
{
    fixture();midi_template_link_cancel_timbre(42);slices(4);assert(!commits&&read_state().ks_canceled_model_generation==42);
    fixture();publish(1,12,3000);midi_template_link_cancel_timbre(12);slices(12);
    assert(!commits&&read_state().ks_canceled_model_generation==12);
    fixture();publish(1,13,3000);midi_template_link_service(now+40,true,true);unsigned writes=ks_writes;
    assert(!commits);midi_template_link_cancel_timbre(13);slices(12);
    assert(!commits&&read_state().ks_canceled_model_generation==13&&ks_writes==writes);
}
static void unknown_and_late_cancel(void)
{
    fixture();auto_ack=false;publish(1,20,3000);slices(10);assert(commits==1);
    midi_template_link_cancel_timbre(20);now+=100001;slices(4);assert(read_state().fault==MIDI_TEMPLATE_LINK_TIMEOUT);
    unsigned writes=ks_writes;publish(2,21,3000);slices(6);assert(ks_writes==writes&&commits==1);
    /* Native cancellation before header has no model identity, but exact
     * completed template identity safely retires the unknown fence. */
    kst[3]++;kst[4]=0;kst[5]=0;kst[13]=kst[15];kst[14]=1;now+=20001;slices(8);
    auto_ack=true;now+=20001;slices(12);assert(commits==2);
    assert(shadow[387]==21);assert(kst[15]==2); /* failed table token 1 never reused */
}
static void last_fence_failure(void)
{
    fixture();fail_last=true;now+=20001;slices(4);assert(!read_state().timbre_valid);
    fail_last=false;now+=20001;slices(4);assert(read_state().timbre_valid);
}
static void late_adoption_without_republish(void)
{
    fixture();auto_ack=false;publish(2,30,3000);slices(10);assert(commits==1);
    midi_template_link_cancel_timbre(30);
    now+=100001;slices(4);assert(read_state().fault==MIDI_TEMPLATE_LINK_TIMEOUT);
    kst[3]++;kst[4]=0;kst[6]=30;kst[7]=2;kst[10]=shadow[444];kst[11]=shadow[445];
    kst[12]=kst[13]=ks_ack=kst[15];kst[14]=0;now+=20001;slices(8);
    midi_template_link_state_t state=read_state();assert(!state.fault&&state.ks_applied_model_generation==30&&state.ks_applied_generation==1&&state.ks_applied_image_crc==kst[11]);
    assert(commits==1); /* confirmation cannot resend a retired write */
    auto_ack=true;publish(2,30,4000);slices(12);assert(commits==2); /* ordinary refresh after canceled-but-adopted model */
}
int main(void)
{verified_upload();cancel_before_commit();unknown_and_late_cancel();last_fence_failure();late_adoption_without_republish();puts("KS actual template link: 448 canonical words/CRC, exact identity, DDS isolation, precommit cancel, unknown fence and late native cancellation PASS");return 0;}
