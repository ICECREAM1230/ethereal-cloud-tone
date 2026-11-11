#include "ks_timbre_control.h"
#include "vendor/tomlc17/tomlc17.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
/* Compact bounded token tree. It accepts only the grammar needed by this API;
 * duplicate keys, trailing bytes, unknown fields and nonfinite values fail. */
typedef struct {unsigned start,end,next,count;char type;} jt_t;
typedef struct {const char *s;size_t n,p;unsigned used; jt_t t[128];} jp_t;
static void ws(jp_t *p){while(p->p<p->n && strchr(" \t\r\n",p->s[p->p]))p->p++;}
static int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static bool jstring(jp_t *p)
{
    if(p->s[p->p++]!='"')return false;
    while(p->p<p->n) {
        unsigned char c=p->s[p->p++];
        if(c=='"')return true;
        if(c<32)return false;
        if(c=='\\') {
            if(p->p>=p->n)return false;
            char e=p->s[p->p++];
            if(e=='u') {for(unsigned j=0;j<4;j++)if(p->p>=p->n || hex(p->s[p->p++])<0)return false;}
            else if(!strchr("\"\\/bfnrt",e))return false;
        }
    }
    return false;
}
static bool value(jp_t *p,unsigned depth)
{
    ws(p);if(depth>5 || p->used>=128 || p->p>=p->n)return false;
    unsigned at=p->used++;jt_t *t=&p->t[at];t->start=(unsigned)p->p;char c=p->s[p->p];t->type=c;
    if(c=='{' || c=='[') {
        p->p++;ws(p);char close=c=='{'?'}':']';
        if(p->p<p->n && p->s[p->p]==close)p->p++;
        else while(1) {
            if(c=='{') {
                ws(p);if(p->p>=p->n || p->s[p->p]!='"' || !value(p,depth+1))return false;
                ws(p);if(p->p>=p->n || p->s[p->p++]!=':')return false;
            }
            if(!value(p,depth+1))return false;
            t->count++;ws(p);if(p->p>=p->n)return false;
            char d=p->s[p->p++];if(d==close)break;if(d!=',')return false;
        }
    } else if(c=='"') {if(!jstring(p))return false;}
    else if(c=='t' || c=='f') {
        const char *b=c=='t'?"true":"false";size_t n=strlen(b);
        if(p->n-p->p<n || memcmp(p->s+p->p,b,n))return false;
        p->p+=n;
    } else {
        t->type='n';if(c=='-')p->p++;
        if(p->p>=p->n)return false;
        if(p->s[p->p]=='0')p->p++;
        else {if(p->s[p->p]<'1'||p->s[p->p]>'9')return false;while(p->p<p->n && isdigit((unsigned char)p->s[p->p]))p->p++;}
        if(p->p<p->n && p->s[p->p]=='.') {p->p++;size_t before=p->p;while(p->p<p->n && isdigit((unsigned char)p->s[p->p]))p->p++;if(p->p==before)return false;}
        if(p->p<p->n && (p->s[p->p]=='e'||p->s[p->p]=='E')) {p->p++;if(p->p<p->n && strchr("+-",p->s[p->p]))p->p++;size_t before=p->p;while(p->p<p->n && isdigit((unsigned char)p->s[p->p]))p->p++;if(p->p==before)return false;}
    }
    t->end=(unsigned)p->p;t->next=p->used;return true;
}
static bool strget(const jp_t *p,unsigned i,char *out,size_t size)
{
    const jt_t *t=&p->t[i];if(t->type!='"')return false;size_t n=0;
    for(unsigned k=t->start+1;k+1<t->end;k++) {
        unsigned char c=p->s[k];uint32_t cp=c;
        if(c=='\\') {
            c=p->s[++k];switch(c) {
            case 'b':cp=8;break;case 'f':cp=12;break;case 'n':cp=10;break;case 'r':cp=13;break;case 't':cp=9;break;
            case 'u':
                cp=0;for(unsigned j=0;j<4;j++)cp=(cp<<4)|(unsigned)hex(p->s[++k]);
                if(cp>=0xd800 && cp<=0xdbff) {
                    if(k+6>=t->end || p->s[k+1]!='\\'||p->s[k+2]!='u')return false;
                    k+=2;uint32_t lo=0;for(unsigned j=0;j<4;j++)lo=(lo<<4)|(unsigned)hex(p->s[++k]);
                    if(lo<0xdc00||lo>0xdfff)return false;
                    cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);
                } else if(cp>=0xdc00 && cp<=0xdfff)return false;
                break;
            default:cp=c;break;
            }
            if(!cp)return false;
            unsigned bytes=cp<128?1:cp<2048?2:cp<65536?3:4;
            if(n+bytes>=size)return false;
            if(bytes==1)out[n++]=(char)cp;
            else {for(unsigned j=bytes-1;j;j--)out[n+bytes-1-j]=(char)((j==bytes-1?(0xffu<<(8-bytes)):0x80u)|(cp>>(6*j)&(j==bytes-1?(0x7fu>>bytes):63)));out[n+bytes-1]=(char)(0x80|(cp&63));n+=bytes;}
        } else {if(n+1>=size)return false;out[n++]=(char)c;}
    }
    out[n]=0;return true;
}
static bool utf8(const char *s,bool name)
{
    size_t n=strlen(s);if(name && (!n||n>=KS_TIMBRE_NAME))return false;
    for(size_t i=0;i<n;i++) {
        unsigned char c=s[i];if(name && (c<32||c==127))return false;
        if(c<128)continue;
        unsigned more;uint32_t cp;
        if(c>=0xc2&&c<=0xdf){more=1;cp=c&31;}else if(c>=0xe0&&c<=0xef){more=2;cp=c&15;}else if(c>=0xf0&&c<=0xf4){more=3;cp=c&7;}else return false;
        if(i+more>=n)return false;
        for(unsigned j=0;j<more;j++){c=s[++i];if((c&0xc0)!=0x80)return false;cp=(cp<<6)|(c&63);}
        if((more==2&&cp<2048)||(more==3&&cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
    }
    return true;
}
bool ks_timbre_name_valid(const char *s)
{
    if(!s || !utf8(s,true))return false;
    for(const unsigned char *p=(const unsigned char *)s;*p;p++)if(*p>32)return true;
    return false;
}
static bool number(const jp_t *p,unsigned i,double *out)
{
    const jt_t *t=&p->t[i];if(t->type!='n'||t->end-t->start>63)return false;
    char b[64],*end;memcpy(b,p->s+t->start,t->end-t->start);b[t->end-t->start]=0;errno=0;
    double d=strtod(b,&end);if(*end||errno||!isfinite(d))return false;*out=d;return true;
}
static bool uintget(const jp_t *p,unsigned i,uint32_t *out)
{double n;if(!number(p,i,&n)||n<0||n>UINT32_MAX||trunc(n)!=n)return false;*out=(uint32_t)n;return true;}
static int member(const jp_t *p,unsigned obj,const char *name)
{
    if(p->t[obj].type!='{')return -1;
    int found=-1;
    for(unsigned k=obj+1;k<p->t[obj].next;) {
        char key[64];if(!strget(p,k,key,sizeof key))return -2;unsigned v=k+1;
        if(!strcmp(key,name)){if(found>=0)return -2;found=(int)v;}k=p->t[v].next;
    }
    return found;
}
#define PHYS_FIELDS(X) X(pluck_position) X(noise_mix) X(noise_lowpass) X(velocity_brightness) X(loss_weight) X(dispersion) X(release_ms) X(dry)
#define ID_FIELDS(X) X(instrument_id) X(preset_schema) X(model_version) X(factory_revision)
static bool preset_json(const jp_t *p,unsigned obj,ks_timbre_preset_t *out)
{
    if(p->t[obj].type!='{'||p->t[obj].count!=13)return false;
    ks_timbre_preset_t q={0};int k;
#define GETID(f) k=member(p,obj,#f);if(k<0||!uintget(p,(unsigned)k,&q.f))return false;
    ID_FIELDS(GETID)
#undef GETID
#define GETPHYS(f) k=member(p,obj,#f);if(k<0||!number(p,(unsigned)k,&q.f))return false;
    PHYS_FIELDS(GETPHYS)
#undef GETPHYS
    k=member(p,obj,"modes");if(k<0||p->t[k].type!='['||p->t[k].count!=4)return false;
    unsigned at=(unsigned)k+1;
    for(unsigned j=0;j<4;j++) {
        if(p->t[at].type!='{'||p->t[at].count!=3)return false;
        int f=member(p,at,"frequency_hz"),t=member(p,at,"t60_seconds"),w=member(p,at,"weight");
        if(f<0||t<0||w<0||!number(p,(unsigned)f,&q.modes[j].frequency_hz)||!number(p,(unsigned)t,&q.modes[j].t60_seconds)||!number(p,(unsigned)w,&q.modes[j].weight))return false;
        at=p->t[at].next;
    }
    if(!ks_timbre_validate(&q))return false;
    *out=q;return true;
}
bool ks_timbre_request_parse(const char *s,size_t n,ks_timbre_op_t op,ks_timbre_request_t *out)
{
    if(!s||!out||!n||n>KS_TIMBRE_HTTP_MAX||memchr(s,0,n))return false;
    jp_t *p=calloc(1,sizeof *p);if(!p)return false;p->s=s;p->n=n;
    bool ok=value(p,0);ws(p);if(!ok||p->p!=n||p->t[0].type!='{'){free(p);return false;}
    ks_timbre_request_t r={.op=op};unsigned expected=3;int a=member(p,0,"token"),b=member(p,0,"sequence"),c=member(p,0,"context");
    ok=a>=0&&b>=0&&c>=0&&uintget(p,(unsigned)a,&r.event.token)&&uintget(p,(unsigned)b,&r.event.sequence)&&uintget(p,(unsigned)c,&r.event.context_epoch)&&r.event.token&&r.event.sequence&&r.event.context_epoch;
    int i=member(p,0,"instrument_id"),q=member(p,0,"preset"),slot=member(p,0,"slot"),name=member(p,0,"name"),doc=member(p,0,"document"),enabled=member(p,0,"enabled");
    if(i==-2||q==-2||slot==-2||name==-2||doc==-2||enabled==-2)ok=false;
    if(i>=0){expected++;r.has_instrument=true;ok=ok&&uintget(p,(unsigned)i,&r.instrument_id)&&r.instrument_id<=2;}
    if(slot>=0){expected++;r.has_slot=true;ok=ok&&uintget(p,(unsigned)slot,&r.slot)&&r.slot<8;}
    if(q>=0){expected++;r.has_preset=true;ok=ok&&preset_json(p,(unsigned)q,&r.setting.preset);}
    if(name>=0){expected++;ok=ok&&strget(p,(unsigned)name,r.setting.name,sizeof r.setting.name)&&ks_timbre_name_valid(r.setting.name);}
    if(enabled>=0){expected++;ok=ok&&(p->t[enabled].type=='t'||p->t[enabled].type=='f');r.enabled=p->t[enabled].type=='t';}
    if(doc>=0) {
        expected++;char *text=malloc(KS_TIMBRE_DOCUMENT_MAX+1);
        ok=ok&&text&&strget(p,(unsigned)doc,text,KS_TIMBRE_DOCUMENT_MAX+1)&&ks_timbre_document_parse(text,strlen(text),&r.setting);free(text);
        /* Request's explicit name takes precedence over document metadata. */
        if(name>=0)ok=ok&&strget(p,(unsigned)name,r.setting.name,sizeof r.setting.name);
    }
    if(op==KST_APPLY)ok=ok&&(r.has_instrument!=r.has_preset)&&!r.has_slot&&name<0&&doc<0&&enabled<0;
    else if(op==KST_SAVE)ok=ok&&r.has_slot&&name>=0&&!r.has_instrument&&!r.has_preset&&doc<0&&enabled<0;
    else if(op==KST_LOAD)ok=ok&&(r.has_slot!=r.has_instrument)&&!r.has_preset&&name<0&&doc<0&&enabled<0;
    else if(op==KST_IMPORT)ok=ok&&r.has_slot&&name>=0&&doc>=0&&!r.has_instrument&&!r.has_preset&&enabled<0;
    else if(op==KST_STARTUP)ok=ok&&enabled>=0&&(!r.enabled||(r.has_slot!=r.has_instrument))&&!r.has_preset&&name<0&&doc<0;
    else ok=false;
    ok=ok&&p->t[0].count==expected;free(p);if(ok)*out=r;return ok;
}
static bool tn(toml_datum_t table,const char *key,double *v)
{toml_datum_t d=toml_get(table,key);if(d.type!=TOML_INT64&&d.type!=TOML_FP64)return false;*v=d.type==TOML_INT64?(double)d.u.int64:d.u.fp64;return isfinite(*v);}
static bool tu(toml_datum_t table,const char *key,uint32_t *v)
{double d;if(!tn(table,key,&d)||d<0||d>UINT32_MAX||trunc(d)!=d)return false;*v=(uint32_t)d;return true;}
bool ks_timbre_document_parse(const char *s,size_t n,ks_timbre_setting_t *out)
{
    if(!s||!out||!n||n>4096||memchr(s,0,n))return false;
    char *utf=malloc(n+1);if(!utf)return false;memcpy(utf,s,n);utf[n]=0;bool valid_utf=utf8(utf,false);free(utf);if(!valid_utf)return false;
    /* Reject excessive nesting before invoking the shared parser. */
    unsigned depth=0;bool quote=false,escape=false,comment=false;
    for(size_t j=0;j<n;j++){char c=s[j];if(comment){if(c=='\n')comment=false;continue;}if(quote){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quote=false;continue;}if(c=='#'){comment=true;continue;}if(c=='"'){quote=true;continue;}if(c=='['||c=='{'){if(++depth>4)return false;}else if((c==']'||c=='}')&&depth)depth--;}
    toml_result_t t=toml_parse(s,(int)n);if(!t.ok){toml_free(t);return false;}
    ks_timbre_setting_t q={0};toml_datum_t root=t.toptab;bool ok=root.u.tab.size==16;uint32_t format=0;
    ok=ok&&tu(root,"format",&format)&&format==1&&tu(root,"decay_ms",&q.decay_ms)&&q.decay_ms>=200&&q.decay_ms<=8000;
#define TI(f) ok=ok&&tu(root,#f,&q.preset.f);
    ID_FIELDS(TI)
#undef TI
#define TP(f) ok=ok&&tn(root,#f,&q.preset.f);
    PHYS_FIELDS(TP)
#undef TP
    toml_datum_t name=toml_get(root,"name"),modes=toml_get(root,"modes");
    ok=ok&&name.type==TOML_STRING&&name.u.str.len>0&&name.u.str.len<65&&strlen(name.u.s)==(size_t)name.u.str.len&&ks_timbre_name_valid(name.u.s)&&modes.type==TOML_ARRAY&&modes.u.arr.size==4;
    if(ok)memcpy(q.name,name.u.s,(size_t)name.u.str.len+1);
    for(unsigned j=0;ok&&j<4;j++){toml_datum_t m=modes.u.arr.elem[j];ok=m.type==TOML_TABLE&&m.u.tab.size==3&&tn(m,"frequency_hz",&q.preset.modes[j].frequency_hz)&&tn(m,"t60_seconds",&q.preset.modes[j].t60_seconds)&&tn(m,"weight",&q.preset.modes[j].weight);}
    ok=ok&&ks_timbre_validate(&q.preset);toml_free(t);if(ok)*out=q;return ok;
}
static bool add(char *out,size_t size,size_t *at,const char *fmt,...)
{va_list ap;va_start(ap,fmt);int n=vsnprintf(out+*at,size-*at,fmt,ap);va_end(ap);if(n<0||(size_t)n>=size-*at)return false;*at+=(size_t)n;return true;}
bool ks_timbre_document_emit(const ks_timbre_setting_t *q,char *out,size_t size)
{
    if(!q||!out||!size||!ks_timbre_validate(&q->preset)||!ks_timbre_name_valid(q->name)||q->decay_ms<200||q->decay_ms>8000)return false;
    size_t at=0;bool ok=add(out,size,&at,"format = 1\nname = \"");
    for(const char *p=q->name;ok&&*p;p++)ok=add(out,size,&at,(*p=='"'||*p=='\\')?"\\%c":"%c",*p);
    ok=ok&&add(out,size,&at,"\"\ndecay_ms = %u\n",(unsigned)q->decay_ms);
#define EI(f) ok=ok&&add(out,size,&at,#f " = %u\n",(unsigned)q->preset.f);
    ID_FIELDS(EI)
#undef EI
#define EP(f) ok=ok&&add(out,size,&at,#f " = %.17g\n",q->preset.f);
    PHYS_FIELDS(EP)
#undef EP
    for(unsigned j=0;ok&&j<4;j++)ok=add(out,size,&at,"\n[[modes]]\nfrequency_hz = %.17g\nt60_seconds = %.17g\nweight = %.17g\n",q->preset.modes[j].frequency_hz,q->preset.modes[j].t60_seconds,q->preset.modes[j].weight);
    return ok&&at<=4096;
}
