#ifndef CONTROL_EVENT_H
#define CONTROL_EVENT_H
#include <stdint.h>
#ifndef ENABLE_AE350_FDC
#define ENABLE_AE350_FDC 0
#endif
typedef struct {
 /* nbytes includes status: Note/CC/Bend=3, Program/ChannelPressure=2. */
 uint8_t source,status,data1,data2,nbytes;
 uint32_t seq;
 uint64_t start_tick,end_tick;
} control_midi_event_t;
typedef struct {
 uint8_t source,data[3],count,first,last,aborted;
 uint32_t seq;
 uint64_t start_tick,end_tick;
} control_sysex_chunk_t;
/* Engine IDs: 0=subtractive (32 voices), 1=KS (16), 2=FM (8).
 * kind: 0=ON, 1=OFF, 2=PITCH, 3=GAIN, 4=KILL,
 * 5=SUB_EXPRESSION, 6=KS_REFRESH_LOSS, 7=SUB_BEND.
 * Feature-enabled source3 ON/kind8 SENSOR: pitch[13:0] is absolute pitch14,
 * pitch[30:14] is gain Q16 (0..65536); no MIDI bend/velocity translation.
 * Other pitch is MIDI note in unsigned Q16 plus signed bend offset.
 * generation identifies one occupancy of a reusable voice slot. */
typedef struct {
 uint8_t engine,voice,kind,source,channel,note,velocity,volume,pan;
 /* W3 upper16: bit15 means ON raw bend valid; bit14 reserved, bits13:0 raw bend. */
 uint16_t aux;
 uint32_t generation,pitch;
} control_voice_command_t;
#endif
