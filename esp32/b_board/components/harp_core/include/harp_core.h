#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HARP_LASERS 12
#define HARP_ENCODERS 4

typedef struct { uint8_t length, data[3]; } harp_midi_t;
typedef struct {
    uint8_t channel, first_note, velocity_min, velocity_max, encoder_edges, reverse_mask;
    bool laser_active_high;
    uint16_t laser_debounce_ms, button_debounce_ms;
    uint16_t duration_min_ms, duration_max_ms, gate_ms;
} harp_config_t;
typedef struct { bool candidate, stable, armed; uint32_t changed; } harp_filter_t;
typedef struct {
    harp_config_t config;
    harp_filter_t laser[HARP_LASERS], button[HARP_ENCODERS];
    bool sampled;
    uint8_t encoder_seen, encoder_ab[HARP_ENCODERS];
    int8_t encoder_accum[HARP_ENCODERS];
    uint16_t active_beams;
    uint16_t playing_beams;
    uint32_t shade_started[HARP_LASERS], last_duration_ms[HARP_LASERS];
    uint32_t note_deadline[HARP_LASERS], note_serial[HARP_LASERS];
    uint8_t velocity[HARP_LASERS];
    uint8_t note_map[HARP_LASERS];
    uint16_t velocity_gain[HARP_LASERS];
    uint32_t hit_count[HARP_LASERS];
    bool last_synthetic[HARP_LASERS];
    int transpose, fine_cents;
    uint8_t volume, cutoff, resonance;
    uint32_t panic_serial, trigger_serial, illegal_edges;
} harp_model_t;
typedef struct {
    bool notes[128];
    uint8_t channel, volume, cutoff, resonance;
    uint8_t velocity[128];
    uint32_t note_serial[128];
    uint16_t bend;
    uint32_t panic_serial;
} harp_target_t;
typedef struct {
    bool online, initialized, pending;
    uint32_t epoch, panic_seen, sent, retries;
    uint8_t phase;
    bool notes[128];
    uint32_t note_serial[128], pending_serial;
    int parameter[4];
    harp_midi_t message;
    uint8_t effect, index;
    int value;
} harp_sink_t;
typedef bool (*harp_send_fn)(void *, const harp_midi_t *, uint32_t epoch);
typedef enum { HARP_ROUTE_AUTO, HARP_ROUTE_USB, HARP_ROUTE_BLE, HARP_ROUTE_BOTH } harp_route_t;

bool harp_config_valid(const harp_config_t *);
bool harp_init(harp_model_t *, const harp_config_t *);
void harp_reset_inputs(harp_model_t *);
void harp_sample(harp_model_t *, uint16_t levels, const uint8_t ab[4], uint8_t pressed,
                 uint8_t enabled_encoders, uint32_t now_ms);
void harp_turn(harp_model_t *, unsigned encoder, int steps);
void harp_panic(harp_model_t *);
bool harp_set_mapping(harp_model_t *, const uint8_t notes[HARP_LASERS],
                      const uint16_t gains[HARP_LASERS]);
int harp_effective_note(const harp_model_t *, unsigned beam);
bool harp_debug_strike(harp_model_t *, unsigned beam, uint32_t duration_ms, uint32_t now_ms);
void harp_target(const harp_model_t *, harp_target_t *);
bool harp_usb_encode(const harp_midi_t *, uint8_t packet[4]);
size_t harp_ble_encode(const harp_midi_t *, uint32_t now_ms, uint8_t packet[5]);
unsigned harp_route_mask(harp_route_t, bool usb_ready, bool ble_ready);
void harp_sink_init(harp_sink_t *);
unsigned harp_sink_pump(harp_sink_t *, const harp_target_t *, bool enabled, bool ready,
                        uint32_t epoch, unsigned budget, harp_send_fn, void *);
bool harp_sink_quiet(const harp_sink_t *);
