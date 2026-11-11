#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ble_control_codec.h"

static size_t frame(uint8_t *p,uint32_t id,unsigned offset,unsigned total,
                    unsigned flags,const void *body,size_t n)
{
    const uint8_t header[]={0x53,1,1,(uint8_t)flags,
        (uint8_t)(id>>24),(uint8_t)(id>>16),(uint8_t)(id>>8),(uint8_t)id,
        (uint8_t)(offset>>8),(uint8_t)offset,(uint8_t)(total>>8),(uint8_t)total};
    memcpy(p,header,sizeof(header));memcpy(p+sizeof(header),body,n);
    return sizeof(header)+n;
}
static ble_control_decode_result_t message(ble_control_decoder_t *d,const void *text,
                                            size_t n,size_t limit,uint32_t id)
{
    uint8_t p[514];ble_control_decode_result_t result=BLE_CONTROL_MORE;
    for(size_t at=0;at<n;){
        size_t count=n-at;if(count>limit-12)count=limit-12;
        size_t length=frame(p,id,(unsigned)at,(unsigned)n,
            (at==0?1u:0u)|(at+count==n?2u:0u),(const char*)text+at,count);
        result=ble_control_decoder_feed(d,p,length,limit);
        if(result==BLE_CONTROL_INVALID)return result;
        if(at+count<n)assert(result==BLE_CONTROL_MORE);
        at+=count;
    }
    return result;
}
static void valid_requests(void)
{
    ble_control_decoder_t d;ble_control_decoder_init(&d);
    const char *json="{\"path\":\"/api/v2/capabilities\",\"body\":null}";
    assert(message(&d,json,strlen(json),20,1)==BLE_CONTROL_COMPLETE);
    assert(!strcmp(d.path,"/api/v2/capabilities")&&d.body_null&&d.body_length==0);
    assert(ble_control_decoder_next(&d));
    json=" { \"body\" : \"\", \"path\" : \"/api/v2/session\" }\r\n";
    assert(message(&d,json,strlen(json),185-3,2)==BLE_CONTROL_COMPLETE);
    assert(!d.body_null&&d.body_length==0);
    assert(ble_control_decoder_next(&d));
    json="{\"path\":\"/api/v2/command\",\"body\":\"\\u4e2d\\ud83d\\ude00\\n\\t\\b\\f\\r\\\"\\\\\\/\"}";
    assert(message(&d,json,strlen(json),13,3)==BLE_CONTROL_COMPLETE);
    assert(d.body_length==15&&!memcmp(d.body,"中😀\n\t\b\f\r\"\\/",15));
}
static void invalid_json(void)
{
    const char *cases[]={
        "{}","[]","null","{\"path\":\"/api/state\"}",
        "{\"path\":\"/api/state\",\"body\":1}",
        "{\"path\":null,\"body\":null}",
        "{\"path\":\"/api/state\",\"path\":\"/api/state\",\"body\":null}",
        "{\"path\":\"/api/state\",\"body\":null,\"x\":null}",
        "{\"path\":\"/api/state\",\"body\":null,}",
        "{\"path\":\"/api/state\",\"body\":null}x",
        "{\"path\":\"/api/state\",\"body\":nu ll}",
        "{\"path\":\"https://example/api/state\",\"body\":null}",
        "{\"path\":\"/api/../state\",\"body\":null}",
        "{\"path\":\"/api//state\",\"body\":null}",
        "{\"path\":\"/api/state#x\",\"body\":null}",
        "{\"path\":\"/api/\\u0000state\",\"body\":null}",
        "{\"path\":\"/api/中\",\"body\":null}",
        "{\"path\":\"/api/state\",\"body\":\"\\x20\"}",
        "{\"path\":\"/api/state\",\"body\":\"\\u12xz\"}",
        "{\"path\":\"/api/state\",\"body\":\"\\ud800\"}",
        "{\"path\":\"/api/state\",\"body\":\"\\ud800a\"}",
        "{\"path\":\"/api/state\",\"body\":\"\\ud800\\u0020\"}",
        "{\"path\":\"/api/state\",\"body\":\"\\udc00\"}",
        "{\"path\":\"/api/state\",\"body\":\"\n\"}",
        "{\"path\":\"/api/state\",\"body\":\"\xc0\x80\"}",
        "{\"path\":\"/api/state\",\"body\":\"\xed\xa0\x80\"}",
        "{\"path\":\"/api/state\",\"body\":\"\xf4\x90\x80\x80\"}",
        "{\"path\":\"/api/state\",\"body\":\"\xe2\x80\"}",
        "\xef\xbb\xbf{\"path\":\"/api/state\",\"body\":null}",
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(*cases);i++){
        ble_control_decoder_t d;ble_control_decoder_init(&d);
        assert(message(&d,cases[i],strlen(cases[i]),13,1)==BLE_CONTROL_INVALID);
        assert(d.failed&&!d.complete&&!ble_control_decoder_next(&d));
    }
}
static void boundaries(void)
{
    ble_control_decoder_t d;char *json=malloc(65536);assert(json);
    const char *prefix="{\"path\":\"/api/command\",\"body\":\"";
    size_t at=strlen(prefix);memcpy(json,prefix,at);
    memset(json+at,'a',4097);memcpy(json+at+4097,"\"}",2);
    ble_control_decoder_init(&d);
    assert(message(&d,json,at+4097+2,514,1)==BLE_CONTROL_INVALID);
    memcpy(json+at+4096,"\"}",2);ble_control_decoder_init(&d);
    assert(message(&d,json,at+4096+2,20,1)==BLE_CONTROL_COMPLETE&&d.body_length==4096);
    memcpy(json,prefix,at);
    for(unsigned i=0;i<4096;i++)memcpy(json+at+i*6,"\\u0000",6);
    memcpy(json+at+4096*6,"\"}",2);ble_control_decoder_init(&d);
    assert(message(&d,json,at+4096*6+2,182,1)==BLE_CONTROL_COMPLETE&&d.body_length==4096);
    for(unsigned i=0;i<4096;i++)assert(d.body[i]==0);
    const char *small="{\"path\":\"/api/state\",\"body\":null}";
    memcpy(json,small,strlen(small));memset(json+strlen(small),' ',65535-strlen(small));
    ble_control_decoder_init(&d);
    assert(message(&d,json,65535,514,UINT32_MAX)==BLE_CONTROL_COMPLETE&&d.received==65535);
    assert(ble_control_decoder_next(&d));
    assert(message(&d,small,strlen(small),182,1)==BLE_CONTROL_INVALID);
    size_t n=(size_t)sprintf(json,"{\"path\":\"/api/");
    memset(json+n,'x',251);memcpy(json+n+251,"\",\"body\":null}",14);
    ble_control_decoder_init(&d);
    assert(message(&d,json,n+251+14,182,1)==BLE_CONTROL_COMPLETE&&d.path_length==256);
    memset(json+n,'x',252);memcpy(json+n+252,"\",\"body\":null}",14);
    ble_control_decoder_init(&d);
    assert(message(&d,json,n+252+14,182,1)==BLE_CONTROL_INVALID);
    free(json);
}
static void invalid_frames(void)
{
    const char *json="{\"path\":\"/api/state\",\"body\":null}";
    size_t jn=strlen(json);uint8_t p[514],q[514];
    size_t n=frame(p,1,0,(unsigned)jn,3,json,jn);
    const unsigned positions[]={0,1,2,3,7,8,9,10,11};
    for(size_t i=0;i<sizeof(positions)/sizeof(*positions);i++){
        ble_control_decoder_t d;ble_control_decoder_init(&d);memcpy(q,p,n);
        q[positions[i]]^=0x40;
        // A different nonzero request_id is legal; zero is not.
        if(positions[i]==7)q[7]=0;
        assert(ble_control_decoder_feed(&d,q,n,514)==BLE_CONTROL_INVALID);
    }
    for(size_t length=0;length<=12;length++){
        ble_control_decoder_t d;ble_control_decoder_init(&d);
        assert(ble_control_decoder_feed(&d,p,length,514)==BLE_CONTROL_INVALID);
    }
    ble_control_decoder_t d;ble_control_decoder_init(&d);
    assert(ble_control_decoder_feed(&d,p,n,20)==BLE_CONTROL_INVALID);
    for(unsigned mode=0;mode<7;mode++){
        ble_control_decoder_init(&d);
        size_t first=frame(p,1,0,(unsigned)jn,1,json,8);
        assert(ble_control_decoder_feed(&d,p,first,20)==BLE_CONTROL_MORE);
        size_t second=frame(q,1,8,(unsigned)jn,0,json+8,8);
        if(mode==0)q[9]=0; // replay / overlap
        if(mode==1)q[9]=9; // gap
        if(mode==2)q[7]=2; // interleaved request
        if(mode==3)q[11]++; // changed total
        if(mode==4)q[3]=1; // repeated FIRST
        if(mode==5)q[3]=2; // premature LAST
        if(mode==6)q[3]=4; // unknown flag
        assert(ble_control_decoder_feed(&d,q,second,20)==BLE_CONTROL_INVALID);
    }
    ble_control_decoder_init(&d);
    assert(message(&d,json,jn,182,5)==BLE_CONTROL_COMPLETE);
    assert(ble_control_decoder_next(&d));
    assert(message(&d,json,jn,182,5)==BLE_CONTROL_INVALID);
    // The published big-endian frame fixture, with an invalid RPC body {},
    // must reach JSON rejection, without changing the decoded frame identity.
    static const uint8_t fixture[]={0x53,1,1,3,1,2,3,4,0,0,0,2,0x7b,0x7d};
    ble_control_decoder_init(&d);
    assert(ble_control_decoder_feed(&d,fixture,sizeof(fixture),20)==BLE_CONTROL_INVALID);
    assert(d.request_id==0x01020304u&&d.total==2);
}
typedef struct {char bytes[65536];size_t used,packets,limit;uint32_t id;bool fail;} capture_t;
static bool capture(void *context,const uint8_t *p,size_t n)
{
    capture_t *c=context;
    assert(n>12&&n<=c->limit&&p[0]==0x53&&p[1]==1&&p[2]==2);
    uint32_t id=((uint32_t)p[4]<<24)|((uint32_t)p[5]<<16)|((uint32_t)p[6]<<8)|p[7];
    unsigned at=((unsigned)p[8]<<8)|p[9],total=((unsigned)p[10]<<8)|p[11];
    assert(id==c->id&&at==c->used&&(p[3]&~3u)==0);
    assert(((p[3]&1)!=0)==(at==0)&&((p[3]&2)!=0)==(at+n-12==total));
    if(c->fail)return false;
    memcpy(c->bytes+c->used,p+12,n-12);c->used+=n-12;c->packets++;
    c->bytes[c->used]=0;return true;
}
static void response_encoding(void)
{
    const char *body="{\"accepted\":1,\"stage\":\"received\",\"fpga\":\"unverified\"}";
    const char *expected="{\"code\":202,\"body\":\"{\\\"accepted\\\":1,\\\"stage\\\":\\\"received\\\",\\\"fpga\\\":\\\"unverified\\\"}\"}";
    ble_control_encoder_t count,send;
    assert(ble_control_encoder_init(&count,202,1,20,0,NULL,NULL));
    assert(ble_control_encoder_body(&count,body,strlen(body))&&ble_control_encoder_finish(&count));
    assert(count.length==strlen(expected));
    for(size_t limit=13;limit<=514;limit+=167){
        capture_t c={.limit=limit,.id=0x01020304};
        assert(ble_control_encoder_init(&send,202,c.id,limit,(uint16_t)count.length,capture,&c));
        for(size_t i=0;i<strlen(body);i++)assert(ble_control_encoder_body(&send,body+i,1));
        assert(ble_control_encoder_finish(&send)&&c.used==count.length&&!strcmp(c.bytes,expected));
    }
    const char unicode[]="中😀\n\0\"\\";
    assert(ble_control_encoder_init(&count,200,1,20,0,NULL,NULL));
    for(size_t i=0;i<sizeof(unicode)-1;i++)assert(ble_control_encoder_body(&count,unicode+i,1));
    assert(ble_control_encoder_finish(&count));
    capture_t c={.limit=20,.id=1};
    assert(ble_control_encoder_init(&send,200,1,20,(uint16_t)count.length,capture,&c));
    assert(ble_control_encoder_body(&send,unicode,sizeof(unicode)-1)&&ble_control_encoder_finish(&send));
    assert(!strcmp(c.bytes,"{\"code\":200,\"body\":\"中😀\\u000a\\u0000\\\"\\\\\"}"));
    char *maximum=malloc(65514);assert(maximum);memset(maximum,'x',65514);
    assert(ble_control_encoder_init(&count,200,1,514,0,NULL,NULL));
    assert(ble_control_encoder_body(&count,maximum,65513)&&ble_control_encoder_finish(&count));
    assert(count.length==65535);
    c=(capture_t){.limit=514,.id=1};
    assert(ble_control_encoder_init(&send,200,1,514,65535,capture,&c));
    assert(ble_control_encoder_body(&send,maximum,65513)&&ble_control_encoder_finish(&send)&&c.used==65535);
    assert(ble_control_encoder_init(&count,200,1,514,0,NULL,NULL));
    assert(ble_control_encoder_body(&count,maximum,65514)&&!ble_control_encoder_finish(&count));
    free(maximum);
    assert(ble_control_encoder_init(&count,200,1,20,0,NULL,NULL));
    assert(!ble_control_encoder_body(&count,"\xed\xa0\x80",3));
    assert(ble_control_encoder_init(&count,200,1,20,0,NULL,NULL));
    assert(ble_control_encoder_body(&count,"\xe2\x80",2)&&!ble_control_encoder_finish(&count));
    c=(capture_t){.limit=20,.id=1,.fail=true};
    assert(!ble_control_encoder_init(&send,200,1,20,22,capture,&c));
    assert(!ble_control_encoder_init(&count,99,1,20,0,NULL,NULL));
    assert(!ble_control_encoder_init(&count,200,0,20,0,NULL,NULL));
}
static void advertising_and_fences(void)
{
    uint8_t a[31],s[31];size_t an=0,sn=0;
    assert(ble_control_advertising(a,&an,s,&sn)&&an<=31&&sn<=31);
    assert(an==21&&a[0]==2&&a[1]==1&&a[2]==6&&a[3]==17&&a[4]==6);
    assert(!memcmp(a+5,ble_control_service_uuid_le,16));
    assert(s[0]==17&&s[1]==6&&!memcmp(s+2,ble_control_midi_uuid_le,16));
    assert(memcmp(ble_control_midi_uuid_le,ble_control_service_uuid_le,16));
    assert(s[19]==8&&sn==20+strlen(BLE_CONTROL_SHORT_NAME));
    assert(!memcmp(s+20,BLE_CONTROL_SHORT_NAME,strlen(BLE_CONTROL_SHORT_NAME)));
    assert(ble_control_packet_limit(0)==20&&ble_control_packet_limit(23)==20);
    assert(ble_control_packet_limit(185)==182&&ble_control_packet_limit(517)==514);
    assert(ble_control_packet_limit(65535)==514);
    assert(ble_control_link_matches(4,7,4,7,true,true));
    assert(!ble_control_link_matches(3,7,4,7,true,true));
    assert(!ble_control_link_matches(4,6,4,7,true,true));
    assert(!ble_control_link_matches(4,7,4,7,false,true));
    assert(!ble_control_link_matches(4,7,4,7,true,false));
    assert(!ble_control_link_matches(0,7,0,7,true,true));
    assert(sizeof(ble_control_decoder_t)<4608&&sizeof(ble_control_encoder_t)<640);
}
int main(int argc,char **argv)
{
    valid_requests();
    invalid_json();boundaries();invalid_frames();response_encoding();advertising_and_fences();
    printf("ble_control_codec: strict frames/UTF8/JSON/bounds/response/AD/fences passed; decoder=%zu encoder=%zu\n",
        sizeof(ble_control_decoder_t),sizeof(ble_control_encoder_t));
    for(int i=1;i<argc;i++){
        FILE *input=fopen(argv[i],"rb");assert(input);
        ble_control_encoder_t counter;char bytes[37];size_t n;
        assert(ble_control_encoder_init(&counter,200,1,20,0,NULL,NULL));
        while((n=fread(bytes,1,sizeof(bytes),input)))assert(ble_control_encoder_body(&counter,bytes,n));
        assert(!ferror(input)&&!fclose(input)&&ble_control_encoder_finish(&counter));
        printf("%s\t%u\n",argv[i],counter.length);
    }
    return 0;
}
