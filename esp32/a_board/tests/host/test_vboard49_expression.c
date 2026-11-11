#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "midi_templates.h"
#include "dds_document.h"

int main(void)
{
    patch_t p, before;
    midi_template_image_t image;
    patch_default(&p); before=p;
    assert(midi_templates_compile(&p,3000,&image));
    assert(image.elements[0][5]==43 && "missing wheel vibrato range");
    assert(image.elements[0][6]==1024 && "missing XY cutoff range");
    assert(image.elements[0][7]==2048 && "missing XY resonance range");
    assert(!memcmp(&p,&before,sizeof p));
#ifdef VBOARD49_FIELDS
    char text[DDS_TEXT_MAX],error[DDS_ERROR_MAX];
    const char *old="format=1\n[part]\nbend_range=2\n";
    patch_t loaded;
    assert(dds_patch_parse(old,strlen(old),&loaded,NULL,error));
    assert(loaded.wheel_vibrato_depth==43 && loaded.touch_cutoff_depth==1024 && loaded.touch_resonance_depth==2048);
    p.wheel_vibrato_depth=-85;
    p.touch_cutoff_depth=-2048;
    p.touch_resonance_depth=-4096;
    assert(midi_templates_compile(&p,3000,&image));
    assert((int32_t)image.elements[0][5]==-85);
    assert((int32_t)image.elements[0][6]==-2048);
    assert((int32_t)image.elements[0][7]==-4096);
    int length=dds_patch_text(&p,text,sizeof text);
    assert(length>0 && strstr(text,"wheel_vibrato_depth") && strstr(text,"touch_cutoff_depth"));
    assert(dds_patch_parse(text,(size_t)length,&loaded,NULL,error));
    assert(loaded.wheel_vibrato_depth==p.wheel_vibrato_depth);
    assert(loaded.touch_cutoff_depth==p.touch_cutoff_depth);
    assert(loaded.touch_resonance_depth==p.touch_resonance_depth);
    p.wheel_vibrato_depth=p.touch_cutoff_depth=p.touch_resonance_depth=0;
    assert(midi_templates_compile(&p,3000,&image));
    assert(!image.elements[0][5] && !image.elements[0][6] && !image.elements[0][7]);
    for(unsigned u=1;u<8;u++)
        for(unsigned w=5;w<8;w++) assert(!image.elements[u][w]);
#endif
    puts("PASS Vboard49 template defaults, signed ranges, bypass, patch immutability, old TOML and roundtrip");
}
