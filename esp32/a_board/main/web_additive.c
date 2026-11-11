#include "web_additive.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

typedef struct {
    const char *body;
    size_t length,position;
} json_t;

static void whitespace(json_t *json)
{
    while(json->position<json->length) {
        char c=json->body[json->position];
        if(c!=' ' && c!='\r' && c!='\n' && c!='\t')break;
        json->position++;
    }
}
static bool take(json_t *json,char expected)
{
    whitespace(json);
    if(json->position==json->length || json->body[json->position]!=expected)return false;
    json->position++;return true;
}
static int hex_digit(unsigned char c)
{
    if(c>='0' && c<='9')return c-'0';
    if(c>='a' && c<='f')return c-'a'+10;
    if(c>='A' && c<='F')return c-'A'+10;
    return -1;
}
static bool key_string(json_t *json,char key[17])
{
    if(!take(json,'"'))return false;
    unsigned used=0;
    while(json->position<json->length) {
        unsigned c=(unsigned char)json->body[json->position++];
        if(c=='"'){key[used]=0;return true;}
        if(c=='\\') {
            if(json->position==json->length)return false;
            c=(unsigned char)json->body[json->position++];
            switch(c) {
            case '"': case '\\': case '/': break;
            case 'b': c='\b';break;
            case 'f': c='\f';break;
            case 'n': c='\n';break;
            case 'r': c='\r';break;
            case 't': c='\t';break;
            case 'u':
                c=0;
                for(unsigned i=0;i<4;i++) {
                    if(json->position==json->length)return false;
                    int digit=hex_digit((unsigned char)json->body[json->position++]);
                    if(digit<0)return false;
                    c=c*16u+(unsigned)digit;
                }
                break;
            default:return false;
            }
        }
        // Every accepted key is printable ASCII, at most 16 decoded bytes.
        if(c<0x20 || c>0x7e || used==16)return false;
        key[used++]=(char)c;
    }
    return false;
}
static bool field(json_t *json,const char *const names[],unsigned count,
                  unsigned *seen,unsigned *index)
{
    char key[17];
    if(!key_string(json,key) || !take(json,':'))return false;
    for(unsigned i=0;i<count;i++)if(!strcmp(key,names[i])) {
        unsigned bit=1u<<i;
        if(*seen&bit)return false;
        *seen|=bit;*index=i;return true;
    }
    return false;
}
static bool integer(json_t *json,uint32_t maximum,uint32_t *out)
{
    whitespace(json);size_t start=json->position;uint32_t value=0;
    while(json->position<json->length) {
        unsigned char c=(unsigned char)json->body[json->position];
        if(c<'0' || c>'9')break;
        if(json->position>start && json->body[start]=='0')return false;
        unsigned digit=c-'0';
        if(value>maximum/10u || (value==maximum/10u && digit>maximum%10u))return false;
        value=value*10u+digit;json->position++;
    }
    if(json->position==start)return false;
    *out=value;return true;
}
static bool boolean(json_t *json,bool *out)
{
    whitespace(json);size_t remaining=json->length-json->position;
    if(remaining>=4 && !memcmp(json->body+json->position,"true",4)) {
        json->position+=4;*out=true;return true;
    }
    if(remaining>=5 && !memcmp(json->body+json->position,"false",5)) {
        json->position+=5;*out=false;return true;
    }
    return false;
}
static bool envelope(json_t *json,additive_envelope_t *env)
{
    static const char *const names[]={"attack_inc","decay_inc","sustain","release_inc"};
    if(!take(json,'{'))return false;
    unsigned seen=0;
    for(;;) {
        unsigned index;uint32_t value;
        if(!field(json,names,4,&seen,&index) || !integer(json,65535,&value))return false;
        switch(index) {
        case 0:env->attack_inc=(uint16_t)value;break;
        case 1:env->decay_inc=(uint16_t)value;break;
        case 2:env->sustain=(uint16_t)value;break;
        default:env->release_inc=(uint16_t)value;break;
        }
        if(take(json,'}'))return seen==15u;
        if(!take(json,','))return false;
    }
}
static bool partial(json_t *json,additive_partial_t *item)
{
    static const char *const names[]={"ratio_q16","weight","group"};
    static const uint32_t maximum[]={16777215,65535,3};
    if(!take(json,'{'))return false;
    unsigned seen=0;
    for(;;) {
        unsigned index;uint32_t value;
        if(!field(json,names,3,&seen,&index) || !integer(json,maximum[index],&value))return false;
        if(!index)item->ratio_q16=value;
        else if(index==1)item->weight=(uint16_t)value;
        else item->group=(uint8_t)value;
        if(take(json,'}'))return seen==7u;
        if(!take(json,','))return false;
    }
}
static bool envelopes(json_t *json,additive_patch_t *patch)
{
    if(!take(json,'['))return false;
    for(unsigned i=0;i<ADDITIVE_ENVELOPE_GROUPS;i++) {
        if(i && !take(json,','))return false;
        if(!envelope(json,&patch->env[i]))return false;
    }
    return take(json,']');
}
static bool partials(json_t *json,additive_patch_t *patch)
{
    if(!take(json,'['))return false;
    for(unsigned i=0;i<ADDITIVE_PARTIALS;i++) {
        if(i && !take(json,','))return false;
        if(!partial(json,&patch->partials[i]))return false;
    }
    return take(json,']');
}
static bool patch_object(json_t *json,additive_patch_t *patch)
{
    static const char *const names[]={"bypass_lowpass","centroid_q8","slope",
        "comb_period","comb_depth","master_gain","master_pan","bend_range","envelopes","partials"};
    static const uint32_t maximum[]={0,8191,65535,31,65535,65535,65535,127};
    if(!take(json,'{'))return false;
    unsigned seen=0;
    for(;;) {
        unsigned index;uint32_t value;
        if(!field(json,names,10,&seen,&index))return false;
        if(index==0) {if(!boolean(json,&patch->bypass_lowpass))return false;}
        else if(index==8) {if(!envelopes(json,patch))return false;}
        else if(index==9) {if(!partials(json,patch))return false;}
        else {
            if(!integer(json,maximum[index],&value))return false;
            switch(index) {
            case 1:patch->centroid_q8=(uint16_t)value;break;
            case 2:patch->slope=(uint16_t)value;break;
            case 3:patch->comb_period=(uint8_t)value;break;
            case 4:patch->comb_depth=(uint16_t)value;break;
            case 5:patch->master_gain=(uint16_t)value;break;
            case 6:patch->master_pan=(uint16_t)value;break;
            default:patch->bend_range=(uint8_t)value;break;
            }
        }
        if(take(json,'}'))return seen==1023u;
        if(!take(json,','))return false;
    }
}
bool web_additive_parse(const char *body,size_t length,web_additive_request_t *out)
{
    static const char *const names[]={"contract_version","token","sequence","context_epoch","patch"};
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!body || !length || length>WEB_ADDITIVE_MAX_BODY_BYTES)return false;
    web_additive_request_t request;memset(&request,0,sizeof(request));
    json_t json={.body=body,.length=length};unsigned seen=0;
    if(!take(&json,'{'))return false;
    for(;;) {
        unsigned index;uint32_t value;
        if(!field(&json,names,5,&seen,&index))return false;
        if(index==4) {if(!patch_object(&json,&request.patch))return false;}
        else {
            if(!integer(&json,UINT32_MAX,&value) || !value)return false;
            if(!index){if(value!=1)return false;}
            else if(index==1)request.token=value;
            else if(index==2)request.sequence=value;
            else request.context_epoch=value;
        }
        if(take(&json,'}'))break;
        if(!take(&json,','))return false;
    }
    whitespace(&json);
    if(seen!=31u || json.position!=json.length || !additive_patch_validate(&request.patch))return false;
    *out=request;return true;
}

static portMUX_TYPE s_job_lock=portMUX_INITIALIZER_UNLOCKED;
static web_additive_job_t s_job;
static bool s_taken;
static uint32_t s_next_id=1;

bool web_additive_queue_push(const web_additive_request_t *request,uint8_t client,
                             int64_t expires,uint32_t *id)
{
    if(!id)return false;
    *id=0;
    if(!request || !request->token || !request->sequence || !request->context_epoch ||
       client>=4 || expires<=0 || !additive_patch_validate(&request->patch))return false;
    portENTER_CRITICAL(&s_job_lock);
    if(!s_job.id && s_next_id) {
        s_job.request=*request;s_job.client=client;s_job.expires=expires;
        s_job.id=s_next_id++;s_taken=false;*id=s_job.id;
    }
    portEXIT_CRITICAL(&s_job_lock);
    // Never recycle IDs during this boot, including cancel-all and exhaustion.
    return *id!=0;
}
bool web_additive_queue_take(uint32_t id,web_additive_job_t *job)
{
    if(!job)return false;
    memset(job,0,sizeof(*job));if(!id)return false;
    bool found=false;
    portENTER_CRITICAL(&s_job_lock);
    if(s_job.id==id && !s_taken){*job=s_job;s_taken=true;found=true;}
    portEXIT_CRITICAL(&s_job_lock);return found;
}
bool web_additive_queue_cancel(uint32_t id)
{
    if(!id)return false;
    bool found=false;
    portENTER_CRITICAL(&s_job_lock);
    if(s_job.id==id){memset(&s_job,0,sizeof(s_job));s_taken=false;found=true;}
    portEXIT_CRITICAL(&s_job_lock);return found;
}
void web_additive_queue_cancel_all(void)
{
    portENTER_CRITICAL(&s_job_lock);
    memset(&s_job,0,sizeof(s_job));s_taken=false;
    portEXIT_CRITICAL(&s_job_lock);
}
