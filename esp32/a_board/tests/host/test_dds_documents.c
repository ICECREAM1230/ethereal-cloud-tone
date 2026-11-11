#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "dds_document.h"
#include "dds_patch_units.h"
#include "dds_files.h"

static char text[DDS_TEXT_MAX+1],error[DDS_ERROR_MAX];
static void same_fields(const patch_t *a,const patch_t *b)
{
    size_t n;const patch_field_t *f=patch_fields(&n);
    for(size_t k=0;k<n;k++)for(int i=0;i<f[k].count;i++)
        assert(patch_field_canonical(f+k,i,a,f[k].get(a,i))==f[k].get(b,i));
}
static void file_copy(const char *source,const char *target)
{
    FILE *in=fopen(source,"rb"),*out=fopen(target,"wb");assert(in&&out);
    int c;while((c=fgetc(in))!=EOF)assert(fputc(c,out)!=EOF);assert(!fclose(in)&&!fclose(out));
}
int main(void)
{
    patch_t a,b;patch_default(&a);unsigned warnings;
    a.filter.cutoff=(uint16_t)(int16_t)-8000;a.filter.resonance=15000;
    a.osc[0].coarse=5;a.osc[0].fine=-41;a.osc[1].duty=7345678;
    a.env[0].sustain=255;a.env[1].sustain=49;
    int n=dds_patch_text(&a,text,sizeof(text));assert(n>0);
    assert(dds_patch_parse(text,(size_t)n,&b,&warnings,error));same_fields(&a,&b);
    int m=dds_patch_text(&b,text,sizeof(text));assert(m>0);
    patch_t c;assert(dds_patch_parse(text,(size_t)m,&c,NULL,error));same_fields(&b,&c);
    const char *bad[]={"format=2","format=1\n[amp_env]\nsustain=\"bad\"","format=1\n[part]\nbend_range=2.5", "format=1\n[[osc]]\nwave=3", "format=1\n[voice]\nunison_detune=nan", "format=1\n[part]\nvolume=inf", "format=1\n[filter]\ncutoff=inf", "format=1\n[voice]\nosc_mix=0\nosc_mix=1", "format=1\n[voice\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){c=a;assert(!dds_patch_parse(bad[i],strlen(bad[i]),&c,NULL,error));assert(!memcmp(&a,&c,sizeof(a)));}
    const char *bom="\xEF\xBB\xBF" "format=1\r\n[voice]\r\nosc_mix=0\r\n";
    assert(dds_patch_parse(bom,strlen(bom),&b,NULL,error));
    const char *adapt="format=1\n[[lfo]]\ndestination='cutoff'\ndepth=2\n[mod_env]\ndestination='unsupported'\n[future]\nx=1\n";
    assert(dds_patch_parse(adapt,strlen(adapt),&b,&warnings,error) && warnings==2 && b.lfo[0].dest==2 && b.env1_dest==0);
    const char *units="format=1\n[[osc]]\ntune=-12\npulse_width=25\n[amp_env]\nsustain=-30.102999566\n[mod_env]\nsustain=50\n[[lfo]]\nrate=1\n";
    assert(dds_patch_parse(units,strlen(units),&b,NULL,error));
    assert(b.osc[0].coarse*1024/12+b.osc[0].fine==-1024 && b.osc[0].duty==-4194304);
    assert(b.env[0].sustain==128 && b.env[1].sustain==128 && b.lfo[0].rate==350);
    size_t count;const patch_field_t *fields=patch_fields(&count);
    // Changing LFO2 destination via CC keeps its raw depth. Units must not
    // clamp that valid engine state to a destination's nominal knob range.
    const int depths[]={0,1,2048,4064,8192,11264,22016,32767};
    for(unsigned dest=0;dest<4;dest++)for(unsigned d=0;d<sizeof(depths)/sizeof(*depths);d++) {
        patch_default(&a);a.lfo[0].depth=depths[d];a.lfo[1].dest=dest;a.lfo[1].depth=depths[d];
        n=dds_patch_text(&a,text,sizeof(text));assert(n>0);
        assert(dds_patch_parse(text,(size_t)n,&b,NULL,error));
        assert(b.lfo[0].depth==depths[d] && b.lfo[1].dest==dest && b.lfo[1].depth==depths[d]);
    }
    // Exercise every ADSR code, sustain value and LFO rate; roundtrip must
    // preserve the canonical engine value, not just approximate milliseconds.
    for(size_t k=0;k<count;k++) {
        const patch_field_t *f=fields+k;
        bool env=strstr(f->table,"env")!=NULL;
        if(!env && strcmp(f->key,"rate"))continue;
        if(f->kind!=PF_NUMBER)continue;
        if(strcmp(f->key,"attack") && strcmp(f->key,"decay") && strcmp(f->key,"release") && strcmp(f->key,"sustain") && strcmp(f->key,"rate"))continue;
        unsigned limit=!strcmp(f->key,"rate")?65536:256;
        for(unsigned raw=0;raw<limit;raw++) {
            f->set(&a,0,(int32_t)raw);char value[64];patch_field_format(f,0,&a,value,sizeof(value));
            int32_t round=f->to_raw(&a,0,strtod(value,NULL));
            assert(round==patch_field_canonical(f,0,&a,(int32_t)raw));
        }
    }
    uint16_t banks[16]={0};dds_bank_select(banks,3,0,12);dds_bank_select(banks,3,32,127);
    assert(banks[3]==1663 && banks[2]==0);dds_bank_select(banks,3,0,0);assert(banks[3]==127);
    char path[DDS_PATH_MAX];const char *bank="format=1\n[bank.1663]\nname='custom'\nprograms=['Patches/User/Pads/My Tone.toml','Patches/Factory/Pads/Galaxy.toml']\n";
    assert(dds_bank_path(bank,strlen(bank),1663,0,path,error)&&!strcmp(path,"Patches/User/Pads/My Tone.toml"));
    assert(!dds_bank_path(bank,strlen(bank),1663,2,path,error));
    assert(!dds_document_validate("banks.toml","format=2",8,error));
    assert(!dds_document_validate("system.toml","format=1\nstartup_patch='../bad.toml'",34,error));
    assert(!dds_path_valid("Patches/User/../bad.toml",true));
    assert(!dds_path_valid("Patches/Factory/Pads/A.toml",true));
    assert(dds_path_valid("Patches/User/音色/我的音色.toml",true));
    assert(dds_path_valid("PATCHES/USER/PADS/TONE.TOML",true));
    assert(!dds_path_valid("PATCHES/FACTORY/PADS/TONE.TOML",true));
    assert(!dds_name_valid("\xC0\xAF"));assert(!dds_name_valid(". "));
    char root[]="/tmp/dds-files.XXXXXX";assert(mkdtemp(root));
    patch_default(&a);n=dds_patch_text(&a,text,sizeof(text));
    const char *user="Patches/User/Pads/Recovery.toml";
    assert(dds_files_save(root,user,text,(size_t)n,error));
    char *read;size_t len;assert(dds_files_read(root,user,&read,&len,error));assert(dds_patch_parse(read,len,&b,NULL,error));free(read);same_fields(&a,&b);
    char final[300],pending[304],backup[304],other[300];
    snprintf(final,sizeof(final),"%s/%s",root,user);snprintf(pending,sizeof(pending),"%s.new",final);snprintf(backup,sizeof(backup),"%s.bak",final);
    // Interrupted partial write: a syntactically valid prefix must not win.
    FILE *fp=fopen(pending,"wb");assert(fp);fputs("format = 1\n",fp);assert(!fclose(fp));
    assert(dds_files_recover(root)==1 && access(pending,F_OK)!=0);
    assert(dds_files_read(root,user,&read,&len,error));free(read);
    // Completed .new after fsync; power fails before old rename.
    a.osc[0].wave=WAVE_PULSE;n=dds_patch_text(&a,text,sizeof(text));
    assert(dds_files_save(root,"Patches/User/Pads/Other.toml",text,(size_t)n,error));
    snprintf(other,sizeof(other),"%s/Patches/User/Pads/Other.toml",root);file_copy(other,pending);
    assert(dds_files_recover(root)==1);
    assert(dds_files_read(root,user,&read,&len,error));assert(dds_patch_parse(read,len,&b,NULL,error));free(read);assert(b.osc[0].wave==WAVE_PULSE);
    // Completed .new after old->.bak; final is absent.
    assert(!unlink(final));file_copy(other,pending);assert(dds_files_recover(root)==1);
    // Corrupt .new checksum after target disappears: restore the valid backup.
    assert(!unlink(final));fp=fopen(pending,"wb");assert(fp);fputs("format=1\n# end bytes=9 crc32=00000000\n",fp);assert(!fclose(fp));
    assert(dds_files_recover(root)==2 && access(final,F_OK)==0);
    assert(dds_files_read(root,user,&read,&len,error));free(read);
    assert(!dds_files_save(root,"Patches/Factory/Pads/A.toml",text,(size_t)n,error));
    assert(dds_files_list(root,0,text,sizeof(text),&warnings,error)&&strstr(text,user));
    // IDF's initial FAT generator uppercases directory names. Recovery must
    // still identify User documents; FAT itself resolves paths case-insensitively.
    char upper[300];snprintf(upper,sizeof(upper),"%s/PATCHES",root);assert(!mkdir(upper,0700));
    snprintf(upper,sizeof(upper),"%s/PATCHES/USER",root);assert(!mkdir(upper,0700));
    snprintf(upper,sizeof(upper),"%s/PATCHES/USER/PADS",root);assert(!mkdir(upper,0700));
    snprintf(upper,sizeof(upper),"%s/PATCHES/USER/PADS/TONE.TOML.NEW",root);file_copy(other,upper);
    assert(dds_files_recover(root)==1);snprintf(upper,sizeof(upper),"%s/PATCHES/USER/PADS/TONE.TOML",root);assert(!access(upper,F_OK));
    // Dot-prefixed categories and names are valid, so both recovery passes
    // must visit them just as listing and normal save/load already do.
    const char *hidden[]={"Patches/User/.Pads/Tone.toml","Patches/User/Pads/.Tone.toml"};
    for(unsigned i=0;i<2;i++) {
        assert(dds_path_valid(hidden[i],true));
        assert(dds_files_save(root,hidden[i],"format=1\n",9,error));
        snprintf(final,sizeof(final),"%s/%s",root,hidden[i]);
        snprintf(pending,sizeof(pending),"%s.new",final);assert(!rename(final,pending));
        assert(dds_files_recover(root)==1 && !access(final,F_OK) && access(pending,F_OK));
        snprintf(backup,sizeof(backup),"%s.bak",final);assert(!rename(final,backup));
        assert(dds_files_recover(root)==1 && !access(final,F_OK) && access(backup,F_OK));
        assert(dds_files_list(root,0,text,sizeof(text),&warnings,error) && strstr(text,hidden[i]));
    }
    // Recover the deepest accepted inline-table document through promotion
    // with an existing valid final (the reviewed worker-stack call chain).
    const char *nested="format=1\nfuture={v={v={v={v={v={v=0}}}}}}\n";
    const char *deep="Patches/User/Pads/Deep.toml";
    assert(dds_files_save(root,deep,nested,strlen(nested),error));
    snprintf(final,sizeof(final),"%s/%s",root,deep);snprintf(pending,sizeof(pending),"%s.new",final);
    file_copy(other,pending);assert(dds_files_recover(root)==1);
    assert(dds_files_read(root,deep,&read,&len,error));assert(dds_patch_parse(read,len,&b,NULL,error));free(read);
    puts("DDS documents: full positive LFO depth, hidden-name new/bak recovery, six-level promotion, unit/engine roundtrip, 8-bit sustain, all rates, invalid files unchanged, bank/path boundaries and interrupted-save recovery PASS");
    return 0;
}
