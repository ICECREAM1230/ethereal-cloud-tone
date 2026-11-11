#include "ks_timbre_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef KS_TIMBRE_TOML_PROBE
extern unsigned ks_toml_probe_calls;
#endif
static void document_name_case(const char *base,const char *name_line,const char *expected)
{
    const char *name=strstr(base,"name = "),*after=strchr(name,'\n')+1;
    char document[4097];size_t prefix=(size_t)(name-base);
    assert(snprintf(document,sizeof document,"%.*s%s%s",(int)prefix,base,name_line,after)>0);
    ks_timbre_setting_t parsed;assert(ks_timbre_document_parse(document,strlen(document),&parsed));
    assert(!strcmp(parsed.name,expected));
}
static void document_depth_case(const char *prefix,unsigned depth,bool table)
{
    char document[512];size_t at=(size_t)snprintf(document,sizeof document,"%s",prefix);
    for(unsigned i=0;i<depth;i++)if(table){memcpy(document+at,"{a=",3);at+=3;}else document[at++]='[';
    document[at++]='0';for(unsigned i=0;i<depth;i++)document[at++]=table?'}':']';document[at++]='\n';
#ifdef KS_TIMBRE_TOML_PROBE
    ks_toml_probe_calls=0;
#endif
    assert(!ks_timbre_document_parse(document,at,&(ks_timbre_setting_t){0}));
#ifdef KS_TIMBRE_TOML_PROBE
    // Unknown x fails the product schema even within the syntax budget.
    // Excess depth must fail before the vendor can recurse at all.
    assert(ks_toml_probe_calls==(depth<=4?1u:0u));
#endif
}
static void toml_quotes_and_depth(const char *base)
{
    document_name_case(base,"name = 'Harp \"[]{}#'\n","Harp \"[]{}#");
    document_name_case(base,"name = \"Harp \\\"[]{}#'\"\n","Harp \"[]{}#'");
    document_name_case(base,"name = '''Harp \"[]{}#'''\n","Harp \"[]{}#");
    document_name_case(base,"name = \"\"\"Harp '[]{}#\"\"\"\n","Harp '[]{}#");
    document_name_case(base,"name = \"\"\"Harp\\\n   []{}#\"\"\"\n","Harp[]{}#");
    document_name_case(base,"name = \"\"\"Harp\"\"\"\"\n","Harp\"");
    document_name_case(base,"name = '''Harp'''''\n","Harp''");
    char commented[4097];assert(snprintf(commented,sizeof commented,"# ' \\\" [[[[[[ {{{{{ # ignored\n%s",base)>0);
    assert(ks_timbre_document_parse(commented,strlen(commented),&(ks_timbre_setting_t){0}));
    const char *prefixes[]={"name = '\"'\nx = ","name = \"'\"\nx = ",
        "name = '''\"'''\nx = ","name = \"\"\"'\"\"\"\nx = ",
        "name = 'a#[]{}\\\"'\nx = ","name = \"a\\\"b\"\nx = ",
        "# ' \" [ {\nname = 'x'\nx = ","name = \"\"\"a\\\n  \\\"b\"\"\"\nx = "};
    for(unsigned i=0;i<sizeof prefixes/sizeof *prefixes;i++)for(unsigned table=0;table<2;table++) {
        document_depth_case(prefixes[i],4,table!=0);
        document_depth_case(prefixes[i],5,table!=0);
        document_depth_case(prefixes[i],30,table!=0);
    }
}
int main(void)
{
    ks_timbre_setting_t a={.decay_ms=3000,.name="古筝 \"清音\""},b={0};
    assert(ks_timbre_factory(2,&a.preset));char text[4097];
    assert(ks_timbre_document_emit(&a,text,sizeof text));
    assert(ks_timbre_document_parse(text,strlen(text),&b));
    assert(!memcmp(&a,&b,sizeof a));
    const char *valid="{\"token\":1,\"sequence\":2,\"context\":3,\"instrument_id\":2}";
    ks_timbre_request_t q;assert(ks_timbre_request_parse(valid,strlen(valid),KST_APPLY,&q));
    assert(q.event.sequence==2&&q.instrument_id==2);
    const char *bad[]={"{}","{\"token\":1,\"sequence\":2,\"context\":3,\"instrument_id\":2,\"instrument_id\":1}","{\"token\":1,\"sequence\":2,\"context\":3,\"instrument_id\":2}x","{\"token\":1,\"sequence\":2,\"context\":3,\"instrument_id\":3}"};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++)assert(!ks_timbre_request_parse(bad[i],strlen(bad[i]),KST_APPLY,&q));
    const char *save="{\"token\":1,\"sequence\":2,\"context\":3,\"slot\":7,\"name\":\"\\u53e4\\u7b5d\"}";
    assert(ks_timbre_request_parse(save,strlen(save),KST_SAVE,&q));assert(!strcmp(q.setting.name,"古筝"));
    assert(!ks_timbre_name_valid("\xc0\x80"));assert(!ks_timbre_name_valid("\xed\xa0\x80"));
    assert(!ks_timbre_document_parse("format=2",8,&b));
    toml_quotes_and_depth(text);
    puts("KS timbre actual JSON/TOML roundtrip, strict schema and UTF-8 PASS");
}
