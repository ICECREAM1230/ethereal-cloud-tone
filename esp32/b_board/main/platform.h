#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "harp_core.h"

typedef int32_t b_encoder_pins_t[HARP_ENCODERS][3];
extern const int b_laser_pins[HARP_LASERS];
void b_board_defaults(b_encoder_pins_t pins);
esp_err_t b_board_validate(const b_encoder_pins_t pins);
esp_err_t b_board_apply(const b_encoder_pins_t pins);
esp_err_t b_board_init(const b_encoder_pins_t pins);
uint8_t b_board_read(uint16_t *laser_levels, uint8_t ab[4], uint8_t *pressed);
esp_err_t b_usb_init(void);
bool b_usb_ready(uint32_t *epoch);
bool b_usb_send(void *, const harp_midi_t *, uint32_t epoch);
void b_usb_drain(void);
esp_err_t b_ble_init(void);
bool b_ble_ready(uint32_t *epoch);
bool b_ble_send(void *, const harp_midi_t *, uint32_t epoch);
uint32_t b_ble_write_errors(void);
