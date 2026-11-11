#pragma once
#include "b_protocol.h"
#include "esp_err.h"
esp_err_t b_console_start(void);
bool b_console_take(b_request_t *);
void b_console_reply(const b_reply_t *,const b_config_t *,const b_state_t *,uint32_t stream_period_ms);
void b_console_state(const b_state_t *);
void b_console_event(const b_event_t *);
void b_console_drop_counts(uint32_t *commands,uint32_t *replies,uint32_t *events);
