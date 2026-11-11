#include "ks_timbre_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
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
    puts("KS timbre actual JSON/TOML roundtrip, strict schema and UTF-8 PASS");
}
