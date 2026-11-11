// Copyright (c) 2026 ICECREAM1230
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
#include "dds_document.h"
#include "dds_patch_units.h"
#include "vendor/tomlc17/tomlc17.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool fail(char *error, const char *reason)
{ snprintf(error, DDS_ERROR_MAX, "%s", reason); return false; }

// Bounded text/nesting keeps untrusted imports within the small worker stack.
// Follow the pinned TOML scanner: multiline endings contain 3..5 quotes.
// Strings/comments do not affect nesting; every container must be balanced.
static bool text_bounded(const char *s, size_t n)
{
    if (!n || n > DDS_TEXT_MAX || memchr(s, 0, n)) return false;
    char closes[6],quote=0;
    unsigned depth=0;bool multiline=false;
    for(size_t i=0;i<n;) {
        char c=s[i];
        if(quote) {
            if(quote=='"' && c=='\\') {
                if(i+1==n)return false;
                i+=2;continue;
            }
            if(c==quote) {
                if(!multiline){quote=0;i++;continue;}
                size_t run=1;while(run<n-i && s[i+run]==quote)run++;
                if(run>=3) {
                    if(run>5)return false;
                    quote=0;
                }
                i+=run;continue;
            }
            if(!multiline && (c=='\n' || c=='\r'))return false;
            i++;continue;
        }
        if(c=='#'){while(i<n && s[i]!='\n')i++;continue;}
        if(c=='"' || c=='\'') {
            quote=c;multiline=n-i>=3 && s[i+1]==c && s[i+2]==c;
            i+=multiline?3u:1u;continue;
        }
        if(c=='[' || c=='{') {
            if(depth==sizeof closes)return false;
            closes[depth++]=c=='['?']':'}';
        } else if(c==']' || c=='}') {
            if(!depth || closes[--depth]!=c)return false;
        }
        i++;
    }
    return !quote && !depth;
}
static bool document(const char *s,size_t n,toml_result_t *r,char *error)
{
    if(!text_bounded(s,n))return fail(error,"document length/nesting invalid");
    // Parsing runs only in the single file worker (or before tasks at boot).
#ifndef ESP_PLATFORM
    toml_option_t option=toml_default_option();option.check_utf8=true;toml_set_option(option);
#endif
    /* Firmware sets the shared parser option once before starting any task.
     * KS imports can parse concurrently with this file worker. */
    *r=toml_parse(s,(int)n);
    if(!r->ok) {snprintf(error,DDS_ERROR_MAX,"TOML: %.140s",r->errmsg);toml_free(*r);return false;}
    toml_datum_t v=toml_get(r->toptab,"format");
    if(v.type!=TOML_INT64 || v.u.int64!=DDS_FORMAT) {toml_free(*r);return fail(error,"unsupported or missing format (expected 1)");}
    return true;
}
bool dds_name_valid(const char *s)
{
    size_t n=strlen(s);
    if(!n || n>64 || *s==' ' || s[n-1]==' ' || s[n-1]=='.')return false;
    for(size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)s[i];
        if(c<32 || c==127 || strchr("\\/:*?\"<>|",c))return false;
        if(c<128)continue;
        unsigned more;uint32_t cp;
        if(c>=0xC2 && c<=0xDF){more=1;cp=c&31;}
        else if(c>=0xE0 && c<=0xEF){more=2;cp=c&15;}
        else if(c>=0xF0 && c<=0xF4){more=3;cp=c&7;}
        else return false;
        if(i+more>=n)return false;
        for(unsigned j=0;j<more;j++){c=(unsigned char)s[++i];if((c&0xC0)!=0x80)return false;cp=(cp<<6)|(c&63);}
        if((more==1 && cp<128)||(more==2 && cp<2048)||(more==3 && cp<65536)||cp>0x10FFFF||(cp>=0xD800 && cp<=0xDFFF))return false;
    }
    return true;
}
bool dds_path_valid(const char *path,bool user_only)
{
    if(strlen(path)>=DDS_PATH_MAX)return false;
    const char *s;
    if(!strncasecmp(path,"Patches/User/",13))s=path+13;
    else if(!user_only && !strncasecmp(path,"Patches/Factory/",16))s=path+16;
    else return false;
    const char *slash=strchr(s,'/');
    if(!slash || strchr(slash+1,'/'))return false;
    size_t cat=(size_t)(slash-s),n=strlen(slash+1);
    if(cat>64 || n<6 || n>69 || strcasecmp(slash+1+n-5,".toml"))return false;
    char category[65],name[65];
    memcpy(category,s,cat);category[cat]=0;
    memcpy(name,slash+1,n-5);name[n-5]=0;
    return dds_name_valid(category) && dds_name_valid(name);
}
bool dds_patch_parse(const char *text,size_t len,patch_t *patch,unsigned *warnings,char *error)
{
    toml_result_t r;
    if(!document(text,len,&r,error))return false;
    patch_t candidate;patch_default(&candidate);
    candidate.lfo[0].dest=2;
    unsigned warn=0;size_t count;const patch_field_t *fields=patch_fields(&count);
    bool ok=true;
    for(size_t k=0;ok && k<count;k++) {
        const patch_field_t *f=fields+k;
        toml_datum_t t=toml_get(r.toptab,f->table);
        if(t.type==TOML_UNKNOWN)continue;
        if(t.type!=(f->count==1?TOML_TABLE:TOML_ARRAY)) {ok=fail(error,"incorrect patch table/array type");break;}
        for(int i=0;ok && i<f->count;i++) {
            toml_datum_t table=t;
            if(f->count>1){if(i>=t.u.arr.size)continue;table=t.u.arr.elem[i];if(table.type!=TOML_TABLE){ok=fail(error,"array entry is not a table");break;}}
            toml_datum_t value=toml_get(table,f->key);
            if(value.type==TOML_UNKNOWN)continue;
            if(f->kind==PF_NAME) {
                if(value.type!=TOML_STRING || (size_t)value.u.str.len!=strlen(value.u.s)){ok=fail(error,"name must be a string without NUL");break;}
                if(!strcmp(f->table,"lfo") && !strcmp(f->key,"destination") && i==0 && strcmp(value.u.s,"pitch"))warn++;
                if(!patch_field_set_name(f,i,&candidate,value.u.s))warn++;
            } else {
                if(value.type!=TOML_INT64 && value.type!=TOML_FP64){ok=fail(error,"patch parameter must be numeric");break;}
                double n=value.type==TOML_INT64?(double)value.u.int64:value.u.fp64;
                bool allow_inf=(!strcmp(f->key,"resonance") && n>0) || (!strcmp(f->table,"part") && !strcmp(f->key,"volume") && n<0);
                if(isnan(n) || (isinf(n) && !allow_inf) || (f->integer && trunc(n)!=n)){ok=fail(error,"invalid numeric value");break;}
                // The local producer API sends LFO DEPTH as uint16_t. Signed
                // negative sends are not implemented here; don't pretend an
                // imported negative depth is rendered correctly.
                if(!strcmp(f->table,"lfo") && !strcmp(f->key,"depth") && n<0){ok=fail(error,"negative LFO depth is unsupported locally");break;}
                patch_field_set_number(f,i,&candidate,n);
            }
        }
    }
    if(ok){*patch=candidate;if(warnings)*warnings=warn;}
    toml_free(r);return ok;
}
static bool append(char *text,size_t size,size_t *used,const char *fmt,...)
{
    if(text && *used>=size)return false;
    va_list args;va_start(args,fmt);int n=vsnprintf(text?text+*used:NULL,text?size-*used:0,fmt,args);va_end(args);
    if(n<0 || (text && (size_t)n>=size-*used))return false;
    *used+=(size_t)n;return true;
}
int dds_patch_text(const patch_t *patch,char *text,size_t size)
{
    size_t n,used=0;const patch_field_t *fields=patch_fields(&n);
    if(!append(text,size,&used,"format = 1\n"))return -1;
    for(size_t k=0;k<n;) {
        size_t end=k;while(end<n && !strcmp(fields[end].table,fields[k].table))end++;
        for(int i=0;i<fields[k].count;i++) {
            if(!append(text,size,&used,fields[k].count==1?"\n[%s]\n":"\n[[%s]]\n",fields[k].table))return -1;
            for(size_t j=k;j<end;j++) {
                char value[64];int len=patch_field_format(fields+j,i,patch,value,sizeof(value));
                if(len<0 || len>=(int)sizeof(value) || !append(text,size,&used,"%s = %s\n",fields[j].key,value))return -1;
            }
        }
        k=end;
    }
    return (int)used;
}
static bool string_path(toml_datum_t value,char *path,char *error)
{
    if(value.type!=TOML_STRING || value.u.str.len<=0 || value.u.str.len>=DDS_PATH_MAX ||
       (size_t)value.u.str.len!=strlen(value.u.s) || !dds_path_valid(value.u.s,false))return fail(error,"invalid preset path");
    strcpy(path,value.u.s);return true;
}
bool dds_system_path(const char *text,size_t len,char *path,char *error)
{
    toml_result_t r;if(!document(text,len,&r,error))return false;
    toml_datum_t v=toml_get(r.toptab,"startup_patch");
    bool ok;
    if(v.type==TOML_UNKNOWN || (v.type==TOML_STRING && v.u.str.len==0)) {path[0]=0;ok=true;}
    else ok=string_path(v,path,error);
    toml_free(r);return ok;
}
static bool bank_table(toml_datum_t banks,char *error)
{
    if(banks.type==TOML_UNKNOWN)return true;
    if(banks.type!=TOML_TABLE)return fail(error,"bank must be a table");
    for(int i=0;i<banks.u.tab.size;i++) {
        const char *key=banks.u.tab.key[i];char *end;unsigned long number=strtoul(key,&end,10);
        for(const char *c=key;*c;c++)if(*c<'0' || *c>'9')return fail(error,"invalid bank number");
        if(!*key || *end || number>16383 || (key[0]=='0' && key[1]))return fail(error,"invalid bank number");
        toml_datum_t b=banks.u.tab.value[i];
        if(b.type!=TOML_TABLE)return fail(error,"bank entry must be a table");
        toml_datum_t name=toml_get(b,"name"),programs=toml_get(b,"programs");
        if(name.type!=TOML_UNKNOWN && (name.type!=TOML_STRING || name.u.str.len>64))return fail(error,"invalid bank name");
        if(programs.type!=TOML_ARRAY || programs.u.arr.size>128)return fail(error,"bank programs must be an array of at most 128 paths");
        for(int j=0;j<programs.u.arr.size;j++){char path[DDS_PATH_MAX];if(!string_path(programs.u.arr.elem[j],path,error))return false;}
    }
    return true;
}
bool dds_bank_path(const char *text,size_t len,unsigned bank,unsigned program,char *path,char *error)
{
    if(bank>16383 || program>127)return fail(error,"bank/program out of range");
    toml_result_t r;if(!document(text,len,&r,error))return false;
    toml_datum_t banks=toml_get(r.toptab,"bank");bool ok=bank_table(banks,error);
    if(ok) {
        char key[8];snprintf(key,sizeof(key),"%u",bank);
        toml_datum_t b=toml_get(banks,key),p=toml_get(b,"programs");
        if(p.type!=TOML_ARRAY || (int)program>=p.u.arr.size)ok=fail(error,"bank/program is not mapped");
        else ok=string_path(p.u.arr.elem[program],path,error);
    }
    toml_free(r);return ok;
}
bool dds_document_validate(const char *path,const char *text,size_t len,char *error)
{
    if(!strcasecmp(path,"system.toml")){char startup[DDS_PATH_MAX];return dds_system_path(text,len,startup,error);}
    if(!strcasecmp(path,"banks.toml")) {
        toml_result_t r;if(!document(text,len,&r,error))return false;
        bool ok=bank_table(toml_get(r.toptab,"bank"),error);toml_free(r);return ok;
    }
    if(!dds_path_valid(path,false))return fail(error,"invalid document path");
    patch_t p;return dds_patch_parse(text,len,&p,NULL,error);
}
