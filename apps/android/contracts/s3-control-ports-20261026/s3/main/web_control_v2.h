#pragma once
#include "web_control.h"

bool web_control_v2_parse(const char *body, web_v2_event_t *event);
bool web_control_v2_valid(const web_v2_event_t *event);
uint32_t web_control_context_epoch(const web_control_state_t *state);
