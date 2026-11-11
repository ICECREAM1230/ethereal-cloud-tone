// Private App control framing. This codec has no RTOS, BLE, heap or SPI dependency.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLE_CONTROL_HEADER_SIZE 12u
#define BLE_CONTROL_PACKET_MAX 514u
#define BLE_CONTROL_MESSAGE_MAX 65535u
#define BLE_CONTROL_BODY_MAX 4096u
#define BLE_CONTROL_PATH_MAX 256u
#define BLE_CONTROL_SHORT_NAME "FPGA_BT"

extern const uint8_t ble_control_service_uuid_le[16];
extern const uint8_t ble_control_midi_uuid_le[16];
// Two independent legacy AD records, each <=31 bytes. UUID lists are marked
// incomplete because the other service is carried in the other AD record.
bool ble_control_advertising(uint8_t advertising[31], size_t *advertising_length,
                             uint8_t scan_response[31], size_t *scan_length);
size_t ble_control_packet_limit(uint16_t mtu);
bool ble_control_link_matches(uint64_t epoch, uint16_t connection,
                              uint64_t current_epoch, uint16_t current_connection,
                              bool live, bool subscribed);

typedef struct {
    uint32_t value, minimum;
    uint8_t remaining;
} ble_control_utf8_t;

typedef enum {
    BLE_CONTROL_INVALID = -1,
    BLE_CONTROL_MORE = 0,
    BLE_CONTROL_COMPLETE = 1,
} ble_control_decode_result_t;

typedef struct {
    char path[BLE_CONTROL_PATH_MAX + 1];
    char body[BLE_CONTROL_BODY_MAX + 1];
    char key[5];
    size_t path_length, body_length;
    uint32_t request_id, last_request_id, unicode_value;
    uint16_t total, received, high_surrogate;
    ble_control_utf8_t utf8;
    uint8_t state, target, field, seen, key_length, escape, digits, null_at;
    bool active, complete, failed, body_null;
} ble_control_decoder_t;

void ble_control_decoder_init(ble_control_decoder_t *decoder);
// Preserves the monotonically increasing request-id watermark.
bool ble_control_decoder_next(ble_control_decoder_t *decoder);
ble_control_decode_result_t ble_control_decoder_feed(
    ble_control_decoder_t *decoder, const uint8_t *packet, size_t length,
    size_t packet_limit);

typedef bool (*ble_control_packet_fn)(void *context, const uint8_t *packet,
                                     size_t length);
typedef struct {
    uint8_t packet[BLE_CONTROL_PACKET_MAX];
    size_t packet_limit, used;
    uint32_t request_id, length, offset;
    uint16_t total;
    ble_control_utf8_t utf8;
    ble_control_packet_fn send;
    void *context;
    bool failed, finished;
} ble_control_encoder_t;

// total=0 selects a counting pass. A nonzero total is the measured complete
// JSON envelope size; send receives framed packets, never an unframed body.
bool ble_control_encoder_init(ble_control_encoder_t *encoder, unsigned code,
                              uint32_t request_id, size_t packet_limit,
                              uint16_t total, ble_control_packet_fn send,
                              void *context);
bool ble_control_encoder_body(ble_control_encoder_t *encoder, const char *bytes,
                              size_t length);
bool ble_control_encoder_finish(ble_control_encoder_t *encoder);
