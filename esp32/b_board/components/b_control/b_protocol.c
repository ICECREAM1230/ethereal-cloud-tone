#include "b_protocol.h"
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *const routes[]={"auto","usb","ble","both"};
static const char *const fields[]={"channel","notes","gains","velocity","gate","volume",
    "transpose","fine","cutoff","resonance","polarity","debounce","enc_edges",
    "enc_reverse","route","pins"};
static const char *const verbs[]={"ping","get","status","apply","panic","save","strike","stream"};

const char *b_code_name(b_code_t code)
{
    static const char *const names[]={"ok","bad_request","invalid_config","revision_conflict",
        "busy","queue_full","io_error","storage_error","drain_timeout","unsupported"};
    return (unsigned)code<sizeof(names)/sizeof(*names)?names[code]:"bad_request";
}

const char *b_lifecycle_name(b_lifecycle_t lifecycle)
{
    static const char *const names[]={"boot","running","quiescing","saving","reconfiguring","fault"};
    return (unsigned)lifecycle<sizeof(names)/sizeof(*names)?names[lifecycle]:"fault";
}

static bool unsigned_number(const char *p,size_t size,uint32_t maximum,uint32_t *out)
{
    if(!size) return false;
    uint32_t value=0;
    for(size_t i=0;i<size;i++) {
        if(p[i]<'0' || p[i]>'9') return false;
        unsigned digit=(unsigned)(p[i]-'0');
        if(value>maximum/10u || (value==maximum/10u && digit>maximum%10u)) return false;
        value=value*10u+digit;
    }
    *out=value;
    return true;
}

static bool signed_number(const char *p,size_t size,int32_t *out)
{
    bool negative=size && *p=='-';
    if(negative) { ++p;--size; }
    uint32_t value;
    if(!unsigned_number(p,size,negative?UINT32_C(2147483648):INT32_MAX,&value)) return false;
    *out=negative?(int32_t)(-(int64_t)value):(int32_t)value;
    return true;
}

static bool array_values(const char *p,int32_t *out,unsigned count)
{
    for(unsigned i=0;i<count;i++) {
        const char *end=strchr(p,',');
        size_t length=end?(size_t)(end-p):strlen(p);
        if(!signed_number(p,length,&out[i]) || ((i+1<count)!=(end!=NULL))) return false;
        if(end) p=end+1;
    }
    return true;
}

static bool route_value(const char *p,int32_t *out)
{
    for(unsigned i=0;i<4;i++) if(strcmp(p,routes[i])==0) { *out=(int32_t)i;return true; }
    return false;
}

static bool parse_field(b_patch_t *patch,unsigned field,const char *value)
{
    b_config_t *c=&patch->values;
    int32_t *scalar=NULL;
    switch((b_field_t)field) {
    case B_FIELD_CHANNEL:scalar=&c->channel;break;
    case B_FIELD_NOTES:return array_values(value,c->notes,12);
    case B_FIELD_GAINS:return array_values(value,c->gains,12);
    case B_FIELD_VELOCITY:return array_values(value,c->velocity,4);
    case B_FIELD_GATE:scalar=&c->gate;break;
    case B_FIELD_VOLUME:scalar=&c->volume;break;
    case B_FIELD_TRANSPOSE:scalar=&c->transpose;break;
    case B_FIELD_FINE:scalar=&c->fine;break;
    case B_FIELD_CUTOFF:scalar=&c->cutoff;break;
    case B_FIELD_RESONANCE:scalar=&c->resonance;break;
    case B_FIELD_POLARITY:scalar=&c->polarity;break;
    case B_FIELD_DEBOUNCE:scalar=&c->debounce;break;
    case B_FIELD_ENC_EDGES:scalar=&c->enc_edges;break;
    case B_FIELD_ENC_REVERSE:scalar=&c->enc_reverse;break;
    case B_FIELD_ROUTE:return route_value(value,&c->route);
    case B_FIELD_PINS: {
        int32_t values[12];
        if(!array_values(value,values,12)) return false;
        for(unsigned i=0;i<4;i++) for(unsigned p=0;p<3;p++) c->pins[i][p]=values[i*3+p];
        return true;
    }
    default:return false;
    }
    return signed_number(value,strlen(value),scalar);
}

static b_code_t parse_human(int argc,const char *const argv[],b_request_t *r)
{
    if(argc<2) return B_BAD_REQUEST;
    if(argc==2) {
        if(strcmp(argv[1],"status")==0) r->verb=B_VERB_STATUS;
        else if(strcmp(argv[1],"get")==0 || strcmp(argv[1],"channel")==0) r->verb=B_VERB_GET;
        else if(strcmp(argv[1],"panic")==0) r->verb=B_VERB_PANIC;
        else if(strcmp(argv[1],"save")==0) r->verb=B_VERB_SAVE;
        else return B_UNSUPPORTED;
        return B_OK;
    }
    r->verb=B_VERB_APPLY;
    if(argc==6 && strcmp(argv[1],"velocity")==0) {
        r->patch.fields=UINT32_C(1)<<B_FIELD_VELOCITY;
        for(unsigned i=0;i<4;i++)
            if(!signed_number(argv[i+2],strlen(argv[i+2]),&r->patch.values.velocity[i])) return B_BAD_REQUEST;
        return B_OK;
    }
    if(argc==6 && strcmp(argv[1],"encpins")==0) {
        int32_t row;
        if(!signed_number(argv[2],strlen(argv[2]),&row) || row<1 || row>4) return B_BAD_REQUEST;
        r->patch.pins_row=(int8_t)(row-1);
        r->patch.fields=UINT32_C(1)<<B_FIELD_PINS;
        for(unsigned p=0;p<3;p++)
            if(!signed_number(argv[p+3],strlen(argv[p+3]),&r->patch.values.pins[row-1][p])) return B_BAD_REQUEST;
        return B_OK;
    }
    if(argc!=3) return B_BAD_REQUEST;
    if(strcmp(argv[1],"channel")==0) {
        uint32_t channel_id;
        if(!unsigned_number(argv[2],strlen(argv[2]),15,&channel_id)) return B_BAD_REQUEST;
        r->patch.fields=UINT32_C(1)<<B_FIELD_CHANNEL;
        r->patch.values.channel=(int32_t)channel_id+1;
        return B_OK;
    }
    static const b_field_t allowed[]={B_FIELD_ROUTE,B_FIELD_TRANSPOSE,B_FIELD_VOLUME,
        B_FIELD_FINE,B_FIELD_POLARITY,B_FIELD_GATE};
    for(unsigned i=0;i<sizeof(allowed)/sizeof(*allowed);i++) if(strcmp(argv[1],fields[allowed[i]])==0) {
        r->patch.fields=UINT32_C(1)<<allowed[i];
        return parse_field(&r->patch,allowed[i],argv[2])?B_OK:B_BAD_REQUEST;
    }
    return B_UNSUPPORTED;
}

static b_code_t parse_tokens(int argc,const char *const argv[],b_request_t *out)
{
    b_request_t r={0};r.patch.pins_row=-1;
    b_code_t result=B_BAD_REQUEST;
    if(argc>0 && strcmp(argv[0],"harp")==0) result=parse_human(argc,argv,&r);
    else if(argc>1 && strcmp(argv[0],"rpc")==0) {
        if(!unsigned_number(argv[1],strlen(argv[1]),UINT32_MAX,&r.id) || !r.id) goto done;
        if(argc<4) goto done;
        if(strcmp(argv[2],"-1")!=0) {
            if(!unsigned_number(argv[2],strlen(argv[2]),UINT32_MAX,&r.expected_revision)) goto done;
            r.check_revision=true;
        }
        unsigned verb;
        for(verb=0;verb<sizeof(verbs)/sizeof(*verbs);verb++) if(strcmp(argv[3],verbs[verb])==0) break;
        if(verb==sizeof(verbs)/sizeof(*verbs)) { result=B_UNSUPPORTED;goto done; }
        r.verb=(b_verb_t)verb;
        if(r.verb==B_VERB_APPLY) {
            if(argc==4) goto done;
            for(int i=4;i<argc;i++) {
                const char *equals=strchr(argv[i],'=');
                if(!equals || equals==argv[i] || !equals[1] || strchr(equals+1,'=')) goto done;
                size_t name_length=(size_t)(equals-argv[i]);
                unsigned field;
                for(field=0;field<B_FIELD_COUNT;field++)
                    if(strlen(fields[field])==name_length && memcmp(argv[i],fields[field],name_length)==0) break;
                if(field==B_FIELD_COUNT || (r.patch.fields & (UINT32_C(1)<<field)) ||
                   !parse_field(&r.patch,field,equals+1)) goto done;
                r.patch.fields|=UINT32_C(1)<<field;
            }
        } else if(r.verb==B_VERB_STRIKE) {
            if(argc!=6 || !unsigned_number(argv[4],strlen(argv[4]),12,&r.beam) || !r.beam ||
               !unsigned_number(argv[5],strlen(argv[5]),60000,&r.duration_ms) || !r.duration_ms) goto done;
        } else if(r.verb==B_VERB_STREAM) {
            if(argc!=5 || !unsigned_number(argv[4],strlen(argv[4]),2000,&r.period_ms) ||
               (r.period_ms && r.period_ms<200)) goto done;
        } else if(argc!=4) goto done;
        result=B_OK;
    }
done:
    if(result==B_OK) *out=r;
    else { memset(out,0,sizeof(*out));out->id=r.id; }
    return result;
}

b_code_t b_protocol_parse_line(const char *line,size_t length,b_request_t *out)
{
    if(!out) return B_BAD_REQUEST;
    memset(out,0,sizeof(*out));
    if(!line || !length || length>B_PROTOCOL_MAX_LINE) return B_BAD_REQUEST;
    if(line[length-1]=='\r') --length;
    char buffer[B_PROTOCOL_MAX_LINE+1];
    for(size_t i=0;i<length;i++) {
        unsigned char c=(unsigned char)line[i];
        if((c<32 || c>126) && c!='\t') return B_BAD_REQUEST;
        buffer[i]=(char)c;
    }
    buffer[length]='\0';
    const char *argv[32];int argc=0;
    char *cursor=buffer;
    while(*cursor) {
        while(*cursor==' ' || *cursor=='\t') ++cursor;
        if(!*cursor) break;
        if(argc==(int)(sizeof(argv)/sizeof(*argv))) return B_BAD_REQUEST;
        argv[argc++]=cursor;
        while(*cursor && *cursor!=' ' && *cursor!='\t') ++cursor;
        if(*cursor) *cursor++='\0';
    }
    return parse_tokens(argc,argv,out);
}

b_code_t b_protocol_parse_argv(int argc,const char *const argv[],b_request_t *out)
{
    if(!out) return B_BAD_REQUEST;
    memset(out,0,sizeof(*out));
    if(!argv || argc<1 || argc>32) return B_BAD_REQUEST;
    size_t total=(size_t)argc-1;
    for(int i=0;i<argc;i++) {
        if(!argv[i] || !argv[i][0]) return B_BAD_REQUEST;
        size_t size=0;
        while(argv[i][size]) {
            unsigned char c=(unsigned char)argv[i][size];
            if(c<33 || c>126 || ++total>B_PROTOCOL_MAX_LINE) return B_BAD_REQUEST;
            ++size;
        }
    }
    return parse_tokens(argc,argv,out);
}

typedef struct { char *out;size_t capacity,used;bool good; } writer_t;
static void append(writer_t *w,const char *format,...)
{
    if(!w->good) return;
    va_list args;va_start(args,format);
    int n=vsnprintf(w->out+w->used,w->capacity-w->used,format,args);
    va_end(args);
    if(n<0 || (size_t)n>=w->capacity-w->used) w->good=false;
    else w->used+=(size_t)n;
}

static void signed_array(writer_t *w,const int32_t *values,unsigned count)
{
    append(w,"[");
    for(unsigned i=0;i<count;i++) append(w,"%s%"PRId32,i?",":"",values[i]);
    append(w,"]");
}

static void unsigned_array(writer_t *w,const uint32_t *values,unsigned count)
{
    append(w,"[");
    for(unsigned i=0;i<count;i++) append(w,"%s%"PRIu32,i?",":"",values[i]);
    append(w,"]");
}

static void config_json(writer_t *w,const b_config_t *c)
{
    append(w,"{\"channel\":%"PRId32",\"notes\":",c->channel);signed_array(w,c->notes,12);
    append(w,",\"gains\":");signed_array(w,c->gains,12);
    append(w,",\"velocity\":");signed_array(w,c->velocity,4);
    append(w,",\"gate\":%"PRId32",\"volume\":%"PRId32",\"transpose\":%"PRId32
        ",\"fine\":%"PRId32",\"cutoff\":%"PRId32",\"resonance\":%"PRId32
        ",\"polarity\":%"PRId32",\"debounce\":%"PRId32",\"enc_edges\":%"PRId32
        ",\"enc_reverse\":%"PRId32",\"route\":\"%s\",\"pins\":[",
        c->gate,c->volume,c->transpose,c->fine,c->cutoff,c->resonance,c->polarity,
        c->debounce,c->enc_edges,c->enc_reverse,routes[c->route]);
    for(unsigned i=0;i<4;i++) { append(w,"%s",i?",":"");signed_array(w,c->pins[i],3); }
    append(w,"]}");
}

static void state_json(writer_t *w,const b_state_t *s)
{
    append(w,"{\"v\":1,\"type\":\"state\",\"uptime_ms\":%"PRIu32",\"revision\":%"PRIu32
        ",\"lifecycle\":\"%s\",\"raw\":%u,\"shade\":%u,\"playing\":%u,\"duration\":",
        s->uptime_ms,s->revision,b_lifecycle_name(s->lifecycle),(unsigned)s->raw,(unsigned)s->shade,(unsigned)s->playing);
    unsigned_array(w,s->duration,12);append(w,",\"velocity\":");unsigned_array(w,s->velocity,12);
    append(w,",\"notes\":");signed_array(w,s->notes,12);append(w,",\"hits\":");unsigned_array(w,s->hits,12);
    append(w,",\"controls\":{\"transpose\":%"PRId32",\"volume\":%"PRId32",\"fine\":%"PRId32
        ",\"cutoff\":%"PRId32",\"resonance\":%"PRId32"},\"links\":{\"usb\":%s,\"ble\":%s,\"selected\":%"PRIu32
        ",\"usb_sent\":%"PRIu32",\"ble_sent\":%"PRIu32",\"usb_retries\":%"PRIu32
        ",\"ble_retries\":%"PRIu32",\"ble_errors\":%"PRIu32"},\"timing\":{\"last_gap_us\":%"PRIu32
        ",\"max_gap_us\":%"PRIu32",\"max_work_us\":%"PRIu32",\"missed_periods\":%"PRIu32
        "},\"drops\":{\"commands\":%"PRIu32",\"replies\":%"PRIu32",\"events\":%"PRIu32
        "},\"illegal_edges\":%"PRIu32",\"encoder_enabled\":%"PRIu32",\"ab\":[%"PRIu32",%"PRIu32",%"PRIu32",%"PRIu32
        "],\"pressed\":%"PRIu32",\"heap_free\":%"PRIu32",\"events\":[",
        s->controls.transpose,s->controls.volume,s->controls.fine,s->controls.cutoff,s->controls.resonance,
        s->links.usb?"true":"false",s->links.ble?"true":"false",s->links.selected,s->links.usb_sent,
        s->links.ble_sent,s->links.usb_retries,s->links.ble_retries,s->links.ble_errors,
        s->timing.last_gap_us,s->timing.max_gap_us,s->timing.max_work_us,s->timing.missed_periods,
        s->drops.commands,s->drops.replies,s->drops.events,s->illegal_edges,s->encoder_enabled,
        s->ab[0],s->ab[1],s->ab[2],s->ab[3],s->pressed,s->heap_free);
    for(size_t i=0;i<s->event_count;i++) {
        const b_event_t *e=&s->events[i];
        append(w,"%s[%"PRIu32",%"PRIu32",%"PRIu32",%"PRIu32",%"PRIu32",%"PRId32",%s,%"PRIu32"]",
            i?",":"",e->seq,e->time_ms,e->channel,e->duration_ms,e->velocity,e->note,
            e->synthetic?"true":"false",e->revision);
    }
    append(w,"]}");
}

static bool valid_state(const b_state_t *s)
{
    return s && s->event_count<=B_PROTOCOL_MAX_EVENTS && (unsigned)s->lifecycle<=B_LIFECYCLE_FAULT;
}

size_t b_protocol_format_reply(char *out,size_t capacity,const b_reply_t *r,
                               const b_config_t *c,const b_state_t *s)
{
    if(!out || !capacity) return 0;
    out[0]='\0';
    if(!r || !r->ready || (unsigned)r->code>B_UNSUPPORTED ||
       (r->include_config && !b_config_valid(c)) || (r->include_state && !valid_state(s))) return 0;
    writer_t w={out,capacity,0,true};
    append(&w,"@B {\"v\":1,\"type\":\"reply\",\"id\":%"PRIu32",\"ok\":%s,\"code\":\"%s\",\"revision\":%"PRIu32,
        r->id,r->code==B_OK?"true":"false",b_code_name(r->code),r->revision);
    if(r->include_config) {
        append(&w,",\"midi_channel_id\":%"PRId32",\"config\":",c->channel-1);config_json(&w,c);
    }
    if(r->include_state) { append(&w,",\"state\":");state_json(&w,s); }
    append(&w,"}\n");
    if(!w.good) { out[0]='\0';return 0; }
    return w.used;
}

size_t b_protocol_format_state(char *out,size_t capacity,const b_state_t *s)
{
    if(!out || !capacity) return 0;
    out[0]='\0';
    if(!valid_state(s)) return 0;
    writer_t w={out,capacity,0,true};
    append(&w,"@B ");state_json(&w,s);append(&w,"\n");
    if(!w.good) { out[0]='\0';return 0; }
    return w.used;
}
