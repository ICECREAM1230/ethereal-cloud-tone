#include "midi_feedback_transport.h"

#include <string.h>

static bool same_request(const midi_feedback_transport_t *transport,
                         uint64_t epoch, uint16_t conn,
                         const uint8_t *packet, size_t length,
                         const midi_feedback_owner_guard_t *owner)
{
    return transport->epoch == epoch && transport->conn == conn &&
        transport->length == length &&
        memcmp(transport->packet, packet, length) == 0 &&
        midi_feedback_transport_owner_matches(transport, owner);
}

midi_feedback_transport_action_t midi_feedback_transport_begin(
    midi_feedback_transport_t *transport, uint64_t epoch, uint16_t conn,
    const uint8_t *packet, size_t length,
    const midi_feedback_owner_guard_t *owner, uint32_t *token)
{
    if (!epoch || !packet || !length ||
        length > sizeof transport->packet || !owner)
        return MIDI_FEEDBACK_TRANSPORT_FAILED;

    bool same = transport->phase != MIDI_FEEDBACK_TRANSPORT_EMPTY &&
        same_request(transport, epoch, conn, packet, length, owner);
    if (transport->phase == MIDI_FEEDBACK_TRANSPORT_COMPLETE && same) {
        midi_feedback_transport_action_t action = transport->result == 0 ?
            MIDI_FEEDBACK_TRANSPORT_SENT : MIDI_FEEDBACK_TRANSPORT_FAILED;
        transport->phase = MIDI_FEEDBACK_TRANSPORT_EMPTY;
        return action;
    }
    if (transport->phase == MIDI_FEEDBACK_TRANSPORT_COMPLETE)
        transport->phase = MIDI_FEEDBACK_TRANSPORT_EMPTY;
    if (transport->phase != MIDI_FEEDBACK_TRANSPORT_EMPTY) {
        if (same) {
            *token = transport->token;
            return MIDI_FEEDBACK_TRANSPORT_WAIT;
        }
        return MIDI_FEEDBACK_TRANSPORT_BUSY;
    }

    if (++transport->token == 0)
        ++transport->token;
    transport->epoch = epoch;
    transport->conn = conn;
    transport->length = length;
    transport->owner = *owner;
    memcpy(transport->packet, packet, length);
    transport->result = 0;
    transport->phase = MIDI_FEEDBACK_TRANSPORT_QUEUED;
    *token = transport->token;
    return MIDI_FEEDBACK_TRANSPORT_ENQUEUE;
}

bool midi_feedback_transport_mark_running(midi_feedback_transport_t *transport,
                                          uint32_t token)
{
    if (transport->phase != MIDI_FEEDBACK_TRANSPORT_QUEUED ||
        transport->token != token)
        return false;
    transport->phase = MIDI_FEEDBACK_TRANSPORT_RUNNING;
    return true;
}

bool midi_feedback_transport_complete(midi_feedback_transport_t *transport,
                                      uint32_t token, int result)
{
    if ((transport->phase != MIDI_FEEDBACK_TRANSPORT_QUEUED &&
         transport->phase != MIDI_FEEDBACK_TRANSPORT_RUNNING) ||
        transport->token != token)
        return false;
    transport->result = result;
    transport->phase = MIDI_FEEDBACK_TRANSPORT_COMPLETE;
    return true;
}

bool midi_feedback_transport_owner_matches(
    const midi_feedback_transport_t *transport,
    const midi_feedback_owner_guard_t *owner)
{
    return memcmp(&transport->owner, owner, sizeof(*owner)) == 0;
}
