#include "ble_control_codec.h"
#include <string.h>

const uint8_t ble_control_service_uuid_le[16] = {
    0xd0,0xd2,0xb7,0xb2,0x52,0x8b,0xc6,0x9a,0x0c,0x4f,0xf9,0x24,0xc0,0xa3,0xa0,0x7d
};
const uint8_t ble_control_midi_uuid_le[16] = {
    0x00,0xc7,0xc4,0x4e,0xe3,0x6c,0x51,0xa7,0x33,0x4b,0xe8,0xed,0x5a,0x0e,0xb8,0x03
};
bool ble_control_advertising(uint8_t a[31],size_t *an,uint8_t s[31],size_t *sn)
{
    const char name[]=BLE_CONTROL_SHORT_NAME;
    _Static_assert(18+2+sizeof(name)-1<=31,"BLE scan response exceeds 31 bytes");
    if(!a||!an||!s||!sn)return false;
    a[0]=2;a[1]=1;a[2]=6;
    a[3]=17;a[4]=6;memcpy(a+5,ble_control_service_uuid_le,16);*an=21;
    s[0]=17;s[1]=6;memcpy(s+2,ble_control_midi_uuid_le,16);
    s[18]=(uint8_t)sizeof(name);s[19]=8;
    memcpy(s+20,name,sizeof(name)-1);*sn=20+sizeof(name)-1;
    return true;
}
size_t ble_control_packet_limit(uint16_t mtu)
{
    if(mtu<23)mtu=23;
    size_t n=(size_t)mtu-3;
    return n>BLE_CONTROL_PACKET_MAX?BLE_CONTROL_PACKET_MAX:n;
}
bool ble_control_link_matches(uint64_t e,uint16_t c,uint64_t ce,uint16_t cc,bool l,bool s)
{return e!=0&&l&&s&&e==ce&&c==cc;}

static bool utf8_byte(ble_control_utf8_t *u,uint8_t c)
{
    if(u->remaining){
        if((c&0xc0)!=0x80)return false;
        u->value=(u->value<<6)|(c&0x3f);
        if(--u->remaining==0 && (u->value<u->minimum||u->value>0x10ffff||
            (u->value>=0xd800&&u->value<=0xdfff)))return false;
        return true;
    }
    if(c<0x80)return true;
    if(c>=0xc2&&c<=0xdf){u->value=c&0x1f;u->minimum=0x80;u->remaining=1;return true;}
    if(c>=0xe0&&c<=0xef){u->value=c&0x0f;u->minimum=0x800;u->remaining=2;return true;}
    if(c>=0xf0&&c<=0xf4){u->value=c&7;u->minimum=0x10000;u->remaining=3;return true;}
    return false;
}
enum {JSON_START,JSON_KEY,JSON_COLON,JSON_VALUE,JSON_STRING,JSON_NULL,JSON_AFTER,JSON_END};
enum {TARGET_KEY=1,TARGET_PATH=2,TARGET_BODY=3};
static bool whitespace(uint8_t c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
static bool append_decoded(ble_control_decoder_t *d,uint8_t c)
{
    if(d->target==TARGET_KEY){
        if(d->key_length>=sizeof(d->key)-1)return false;
        d->key[d->key_length++]=(char)c;d->key[d->key_length]=0;
    }else if(d->target==TARGET_PATH){
        if(d->path_length>=BLE_CONTROL_PATH_MAX)return false;
        d->path[d->path_length++]=(char)c;d->path[d->path_length]=0;
    }else{
        if(d->body_length>=BLE_CONTROL_BODY_MAX)return false;
        d->body[d->body_length++]=(char)c;d->body[d->body_length]=0;
    }
    return true;
}
static bool append_codepoint(ble_control_decoder_t *d,uint32_t v)
{
    if(v<0x80)return append_decoded(d,(uint8_t)v);
    if(v<0x800)return append_decoded(d,(uint8_t)(0xc0|(v>>6)))&&
        append_decoded(d,(uint8_t)(0x80|(v&0x3f)));
    if(v<0x10000)return append_decoded(d,(uint8_t)(0xe0|(v>>12)))&&
        append_decoded(d,(uint8_t)(0x80|((v>>6)&0x3f)))&&
        append_decoded(d,(uint8_t)(0x80|(v&0x3f)));
    return append_decoded(d,(uint8_t)(0xf0|(v>>18)))&&
        append_decoded(d,(uint8_t)(0x80|((v>>12)&0x3f)))&&
        append_decoded(d,(uint8_t)(0x80|((v>>6)&0x3f)))&&
        append_decoded(d,(uint8_t)(0x80|(v&0x3f)));
}
static bool valid_path(const char *p,size_t n)
{
    if(n<5||memcmp(p,"/api/",5)||strstr(p,"..")||strstr(p,"//"))return false;
    for(size_t i=0;i<n;i++){
        uint8_t c=(uint8_t)p[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
            c=='_'||c=='.'||c=='/'||c=='?'||c=='='||c=='&'||c=='%'||c=='+'||c=='-'))return false;
    }
    return true;
}
static bool close_string(ble_control_decoder_t *d)
{
    if(d->target==TARGET_KEY){
        unsigned bit;
        if(d->key_length==4&&!memcmp(d->key,"path",4)){d->field=TARGET_PATH;bit=1;}
        else if(d->key_length==4&&!memcmp(d->key,"body",4)){d->field=TARGET_BODY;bit=2;}
        else return false;
        if(d->seen&bit)return false;
        d->seen|=bit;d->state=JSON_COLON;
    }else{
        if(d->target==TARGET_PATH&&!valid_path(d->path,d->path_length))return false;
        d->state=JSON_AFTER;
    }
    return true;
}
static int hex_digit(uint8_t c)
{
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
static bool string_byte(ble_control_decoder_t *d,uint8_t c)
{
    if(d->utf8.remaining)return utf8_byte(&d->utf8,c)&&append_decoded(d,c);
    if(d->escape==1){
        d->escape=0;
        switch(c){
        case '"':case '\\':case '/':return append_decoded(d,c);
        case 'b':return append_decoded(d,'\b');case 'f':return append_decoded(d,'\f');
        case 'n':return append_decoded(d,'\n');case 'r':return append_decoded(d,'\r');
        case 't':return append_decoded(d,'\t');
        case 'u':d->escape=2;d->unicode_value=0;d->digits=0;return true;
        default:return false;
        }
    }
    if(d->escape==3){if(c!='\\')return false;d->escape=4;return true;}
    if(d->escape==4){
        if(c!='u')return false;
        d->escape=5;d->unicode_value=0;d->digits=0;return true;
    }
    if(d->escape==2||d->escape==5){
        int n=hex_digit(c);if(n<0)return false;
        d->unicode_value=(d->unicode_value<<4)|(unsigned)n;
        if(++d->digits<4)return true;
        uint32_t v=d->unicode_value;
        if(d->escape==5){
            if(v<0xdc00||v>0xdfff)return false;
            v=0x10000+(((uint32_t)d->high_surrogate-0xd800)<<10)+(v-0xdc00);
            d->high_surrogate=0;
        }else if(v>=0xd800&&v<=0xdbff){d->high_surrogate=(uint16_t)v;d->escape=3;return true;}
        else if(v>=0xdc00&&v<=0xdfff)return false;
        d->escape=0;return append_codepoint(d,v);
    }
    if(c=='"')return close_string(d);
    if(c=='\\'){d->escape=1;return true;}
    if(c<0x20)return false;
    return utf8_byte(&d->utf8,c)&&append_decoded(d,c);
}
static bool json_byte(ble_control_decoder_t *d,uint8_t c)
{
    if(d->state==JSON_STRING)return string_byte(d,c);
    if(d->state==JSON_NULL){
        static const char literal[]="null";
        if(c!=(uint8_t)literal[d->null_at])return false;
        if(++d->null_at==4){d->body_null=true;d->state=JSON_AFTER;}
        return true;
    }
    if(whitespace(c))return true;
    switch(d->state){
    case JSON_START:if(c!='{')return false;d->state=JSON_KEY;return true;
    case JSON_KEY:
        if(c!='"')return false;
        d->target=TARGET_KEY;d->key_length=0;d->key[0]=0;d->state=JSON_STRING;return true;
    case JSON_COLON:if(c!=':')return false;d->state=JSON_VALUE;return true;
    case JSON_VALUE:
        if(d->field==TARGET_BODY&&c=='n'){d->null_at=1;d->state=JSON_NULL;return true;}
        if(c!='"')return false;
        d->target=d->field;d->state=JSON_STRING;return true;
    case JSON_AFTER:
        if(c==','&&d->seen!=3){d->state=JSON_KEY;return true;}
        if(c=='}'&&d->seen==3){d->state=JSON_END;return true;}
        return false;
    default:return false;
    }
}
void ble_control_decoder_init(ble_control_decoder_t *d){memset(d,0,sizeof(*d));}
bool ble_control_decoder_next(ble_control_decoder_t *d)
{
    if(!d||!d->complete||d->failed)return false;
    uint32_t last=d->last_request_id;ble_control_decoder_init(d);d->last_request_id=last;
    return true;
}
static ble_control_decode_result_t invalid(ble_control_decoder_t *d)
{if(d)d->failed=true;return BLE_CONTROL_INVALID;}
ble_control_decode_result_t ble_control_decoder_feed(ble_control_decoder_t *d,
    const uint8_t *p,size_t n,size_t limit)
{
    if(!d||!p||d->failed||d->complete||limit<13||limit>514||n<=12||n>limit||
        p[0]!=0x53||p[1]!=1||p[2]!=1||(p[3]&~3u))return invalid(d);
    uint32_t id=((uint32_t)p[4]<<24)|((uint32_t)p[5]<<16)|((uint32_t)p[6]<<8)|p[7];
    unsigned at=((unsigned)p[8]<<8)|p[9],total=((unsigned)p[10]<<8)|p[11];
    size_t payload=n-12;
    if(!id||!total||payload>total||at>total-payload||at!=d->received||
        ((p[3]&1)!=0)!=(!d->active)||((p[3]&2)!=0)!=(at+payload==total))return invalid(d);
    if(!d->active){
        if(id<=d->last_request_id||at!=0)return invalid(d);
        d->active=true;d->request_id=id;d->last_request_id=id;d->total=(uint16_t)total;
    }else if(id!=d->request_id||total!=d->total)return invalid(d);
    for(size_t i=12;i<n;i++)if(!json_byte(d,p[i]))return invalid(d);
    d->received=(uint16_t)(at+payload);
    if(p[3]&2){
        if(d->state!=JSON_END||d->utf8.remaining||d->escape)return invalid(d);
        d->complete=true;return BLE_CONTROL_COMPLETE;
    }
    return BLE_CONTROL_MORE;
}

static bool flush_packet(ble_control_encoder_t *e)
{
    if(!e->used)return true;
    uint8_t *p=e->packet;
    p[0]=0x53;p[1]=1;p[2]=2;
    p[3]=(uint8_t)((e->offset==0?1:0)|(e->offset+e->used==e->total?2:0));
    p[4]=(uint8_t)(e->request_id>>24);p[5]=(uint8_t)(e->request_id>>16);
    p[6]=(uint8_t)(e->request_id>>8);p[7]=(uint8_t)e->request_id;
    p[8]=(uint8_t)(e->offset>>8);p[9]=(uint8_t)e->offset;
    p[10]=(uint8_t)(e->total>>8);p[11]=(uint8_t)e->total;
    if(!e->send(e->context,p,12+e->used)){e->failed=true;return false;}
    e->offset+=(uint32_t)e->used;e->used=0;return true;
}
static bool encoded_byte(ble_control_encoder_t *e,uint8_t c)
{
    if(e->failed||e->finished||e->length>=BLE_CONTROL_MESSAGE_MAX||
        (e->total&&e->length>=e->total)){e->failed=true;return false;}
    e->length++;
    if(!e->total)return true;
    e->packet[12+e->used++]=c;
    return e->used<e->packet_limit-12||flush_packet(e);
}
static bool encoded_text(ble_control_encoder_t *e,const char *p,size_t n)
{for(size_t i=0;i<n;i++)if(!encoded_byte(e,(uint8_t)p[i]))return false;return true;}
bool ble_control_encoder_init(ble_control_encoder_t *e,unsigned code,uint32_t id,
    size_t limit,uint16_t total,ble_control_packet_fn send,void *context)
{
    if(!e)return false;
    memset(e,0,sizeof(*e));
    if(code<100||code>599||!id||limit<13||limit>514||(total&&!send)){e->failed=true;return false;}
    e->packet_limit=limit;e->total=total;e->request_id=id;e->send=send;e->context=context;
    char prefix[]="{\"code\":000,\"body\":\"";
    prefix[8]=(char)('0'+code/100);prefix[9]=(char)('0'+code/10%10);prefix[10]=(char)('0'+code%10);
    return encoded_text(e,prefix,sizeof(prefix)-1);
}
bool ble_control_encoder_body(ble_control_encoder_t *e,const char *bytes,size_t n)
{
    if(!e||e->failed||e->finished||(!bytes&&n))return false;
    static const char hex[]="0123456789abcdef";
    for(size_t i=0;i<n;i++){
        uint8_t c=(uint8_t)bytes[i];
        if(!utf8_byte(&e->utf8,c)){e->failed=true;return false;}
        if(c=='"'||c=='\\'){
            if(!encoded_byte(e,'\\')||!encoded_byte(e,c))return false;
        }else if(c<0x20){
            char escaped[]={'\\','u','0','0',hex[c>>4],hex[c&15]};
            if(!encoded_text(e,escaped,sizeof(escaped)))return false;
        }else if(!encoded_byte(e,c))return false;
    }
    return true;
}
bool ble_control_encoder_finish(ble_control_encoder_t *e)
{
    if(!e||e->failed||e->finished)return false;
    if(e->utf8.remaining){e->failed=true;return false;}
    if(!encoded_text(e,"\"}",2))return false;
    if(e->total&&(e->length!=e->total||!flush_packet(e))){e->failed=true;return false;}
    e->finished=true;return true;
}
