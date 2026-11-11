#include "web_control.h"
#include "ae350_control.h"
#include "web_control_v2.h"
#include "fm_contract.h"
#include <string.h>
#include <limits.h>
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static web_control_state_t s_state;
static portMUX_TYPE s_reverb_lock=portMUX_INITIALIZER_UNLOCKED;
static web_v2_event_t s_reverb_reservation;
static uint32_t s_reverb_next_id=1;

bool web_control_reverb_reserve(web_v2_event_t *event,int64_t now)
{
    if(!event || event->c || !event->token || !event->sequence || !event->context_epoch ||
       event->client>=WEB_CLIENTS || event->op || event->a>REVERB_WET_MAX || event->b>REVERB_DAMP_MAX ||
       now<0 || event->expires<=now || event->expires-now>WEB_LEASE_US)return false;
    bool reserved=false;
    portENTER_CRITICAL(&s_reverb_lock);
    if(s_reverb_reservation.c && now>=s_reverb_reservation.expires)
        memset(&s_reverb_reservation,0,sizeof(s_reverb_reservation));
    if(!s_reverb_reservation.c && s_reverb_next_id) {
        event->c=s_reverb_next_id++;
        s_reverb_reservation=*event;reserved=true;
    }
    portEXIT_CRITICAL(&s_reverb_lock);return reserved;
}
bool web_control_reverb_matches(const web_v2_event_t *event)
{
    if(!event || !event->c)return false;
    portENTER_CRITICAL(&s_reverb_lock);
    const web_v2_event_t *r=&s_reverb_reservation;
    bool matches=r->c==event->c && r->token==event->token && r->sequence==event->sequence &&
        r->context_epoch==event->context_epoch && r->client==event->client &&
        r->expires==event->expires && r->op==event->op && r->a==event->a && r->b==event->b;
    portEXIT_CRITICAL(&s_reverb_lock);return matches;
}
void web_control_reverb_release(uint32_t id)
{
    if(!id)return;
    portENTER_CRITICAL(&s_reverb_lock);
    if(s_reverb_reservation.c==id)memset(&s_reverb_reservation,0,sizeof(s_reverb_reservation));
    portEXIT_CRITICAL(&s_reverb_lock);
}
void web_control_store(const web_control_state_t *state)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state = *state;
    portEXIT_CRITICAL(&s_state_lock);
}
void web_control_read(web_control_state_t *state)
{
    portENTER_CRITICAL(&s_state_lock);
    *state = s_state;
    portEXIT_CRITICAL(&s_state_lock);
}
bool web_control_cc_allowed(unsigned cc)
{
    switch (cc) {
    case 7: case 10: case 14: case 15: case 20: case 21: case 22: case 23:
    case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31:
    case 71: case 72: case 73: case 74: case 75: case 76: case 77: case 79:
    case 85: case 86: case 87: case 102: case 103: case 104: case 105:
    case 106: case 107: case 109: case 110: case 111: case 112: case 113:
        return true;
    default: return false;
    }
}
bool web_control_parse(const char *body, web_event_t *event)
{
    uint32_t n[5];
    const char *p = body;
    for (unsigned i = 0; i < 5; i++) {
        if (*p < '0' || *p > '9') return false;
        uint32_t v = 0;
        do {
            unsigned d = (unsigned)(*p++ - '0');
            if (v > (UINT32_MAX - d) / 10) return false;
            v = v * 10 + d;
        } while (*p >= '0' && *p <= '9');
        n[i] = v;
        if (i < 4) { if (*p++ != ' ') return false; }
    }
    if (*p || !n[0] || !n[1]) return false;
    if (n[2] == WEB_CC) {
        if (!web_control_cc_allowed(n[3]) || n[4] > 127) return false;
    } else if (n[2] == WEB_KEYS_UPDATE) {
        if (n[3] > WEB_KEYS_MASK || n[4] < 1 || n[4] > 127) return false;
    } else if (n[2] == WEB_STOP) {
        if (n[3] || n[4]) return false;
    } else if(n[2]==WEB_ENGINE) {
        if(n[3]>1 || n[4])return false;
    } else if(n[2]==WEB_KS_DECAY) {
        if(n[3]<200 || n[3]>8000 || n[4])return false;
    } else return false;
    *event = (web_event_t){.token=n[0], .sequence=n[1], .op=n[2], .a=n[3], .b=n[4]};
    return true;
}

uint32_t web_control_context_epoch(const web_control_state_t *state)
{
    // The production voice owner starts at 1. Legacy host fixtures use zero.
    return state->context_epoch ? state->context_epoch : 1;
}
bool web_control_v2_valid(const web_v2_event_t *e)
{
    if (!e || !e->token || !e->sequence || !e->context_epoch) return false;
    switch (e->op) {
    case FM_HTTP_OPCODE_CC:
        return web_control_cc_allowed(e->a) && e->b <= 127 && !e->c;
    case FM_HTTP_OPCODE_KEYS_UPDATE:
        return e->a <= WEB_KEYS_MASK && e->b >= 1 && e->b <= 127 && !e->c;
    case FM_HTTP_OPCODE_STOP:
        return !e->a && !e->b && !e->c;
    case FM_HTTP_OPCODE_ENGINE_SELECT:
        return (e->a <= FM_ENGINE_FM || e->a==ENGINE_MODE_FLUTE) && !e->c &&
            (e->a == FM_ENGINE_FM ? (e->b != 0 || CONFIG_FPGA_AE350_CONTROL) : e->b == 0);
    case FM_HTTP_OPCODE_KS_DECAY:
        return e->a >= 200 && e->a <= 8000 && !e->b && !e->c;
    case FM_HTTP_OPCODE_FM_PARAMETER:
        // Backend-specific scope/range validation needs a matched runtime profile.
        return e->a != 0;
    case FM_HTTP_OPCODE_FM_PRESET:
        return !e->b && !e->c;
    default:
        return false;
    }
}
bool web_control_v2_parse(const char *body, web_v2_event_t *event)
{
    if (!body || !event) return false;
    uint32_t n[7];
    const char *p = body;
    for (unsigned i = 0; i < 7; i++) {
        if (*p < '0' || *p > '9') return false;
        uint32_t value = 0;
        do {
            unsigned digit = (unsigned)(*p++ - '0');
            if (value > (UINT32_MAX - digit) / 10) return false;
            value = value * 10 + digit;
        } while (*p >= '0' && *p <= '9');
        n[i] = value;
        if (i < 6 && *p++ != ' ') return false;
    }
    if (*p || n[3] > UINT8_MAX) return false;
    web_v2_event_t parsed = {.token=n[0], .sequence=n[1], .context_epoch=n[2],
        .op=(uint8_t)n[3], .a=n[4], .b=n[5], .c=n[6]};
    if (!web_control_v2_valid(&parsed)) return false;
    *event = parsed;
    return true;
}
