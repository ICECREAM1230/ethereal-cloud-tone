#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "web_control.h"
void midi_feedback_init(void);
// epoch 0 uses the currently subscribed BLE connection. A nonzero epoch is a
// subscription sync fence. True means the request was accepted or unnecessary.
bool midi_feedback_publish(const web_control_state_t *state,uint8_t except_source,uint64_t epoch);
