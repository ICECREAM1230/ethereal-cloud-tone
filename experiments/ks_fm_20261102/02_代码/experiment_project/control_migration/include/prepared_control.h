/* SPDX-License-Identifier: MIT */
#ifndef PREPARED_CONTROL_H
#define PREPARED_CONTROL_H
#include <stdint.h>
#define PC_LINK_ID UINT32_C(0x43544c32)
#define PC_LINK_ABI UINT32_C(0x00020000)
#define PC_SPI_ABI UINT32_C(2)
enum { PC_RELEASE=1,PC_KILL=4,PC_BEGIN=0x10,PC_DATA=0x11,
       PC_PARAMS=0x12,PC_COMMIT=0x13,PC_GLOBAL=0x15 };
enum { PC_SUB_NEW=0,PC_SUB_HOT=1,PC_SUB_SCALARS=2 };
enum { PC_FM_NEW=0,PC_FM_HOT=1,PC_FM_RESET=0,PC_FM_GLOBAL_WRITE=1,PC_FM_FINISH=2 };
enum { PC_ACCEPTED=0,PC_STALE=1,PC_INVALID=2 };
enum { PC_TEMPLATE_META=0x90,PC_TEMPLATE_DATA=0x94,PC_TEMPLATE_POP=0x98,
       PC_TEMPLATE_STATUS=0x9c,PC_TEMPLATE_ACK_BASE=0xa0 };
enum { PC_ACK_SUB=0,PC_ACK_KS=1,PC_ACK_KST=2,PC_ACK_KST_COMPLETED=3,
       PC_ACK_KST_RESULT=4,PC_ACK_KST_CRC=5,PC_ACK_FM_SENT=6,PC_ACK_RESERVED=7 };
/* Mode RPC v1 uses the existing template FIFO and reserved status word.
 * Receipt confirms CPU ownership/control routing, not a serialized DAC sample. */
#define PC_MODE_REQUEST UINT16_C(0xb401)
#define PC_MODE_STATUS_REG UINT16_C(0x071d)
#define PC_MODE_VERSION UINT32_C(0x10000000)
/* Status v2 advertises MIDI channel routing; B401 requests remain v1. */
#define PC_MODE_CHANNEL_STATUS_VERSION UINT32_C(0x20000000)
#define PC_MODE_SEQUENCE_MASK UINT32_C(0x000fffff)
#define PC_MODE_CAP UINT32_C(0x80)
#define PC_MODE_FAULT UINT32_C(0x40)
#define PC_MODE_READY UINT32_C(0x20)
#define PC_MODE_BUSY UINT32_C(0x10)
/* SUB patch v1 reuses 15 formerly reserved words in the fixed 64-word image. */
#define PC_SUB_PATCH1_MAGIC UINT32_C(0x53554231)
#define PC_SUB_PATCH1_VERSION UINT32_C(0x00010000)
#define PC_SUB_PATCH1_MASK UINT32_C(0x00000fff)
#define PC_SUB_PATCH1_BEGIN_WORD UINT32_C(0x00010fff)
#define PC_SUB_PATCH1_CAP_APB UINT32_C(0x00c0)
#define PC_SUB_PATCH1_CAP_SPI UINT16_C(0x071e)
enum { PC_SUB_GLOBAL_BEGIN=1,PC_SUB_GLOBAL_DATA=2,PC_SUB_GLOBAL_COMMIT=3 };
/* word[0]=flags/op/voice/engine, word[1..3]=native payload,
 * word[4]=voice generation. Physical sequence is separate APB COMMIT data.
 * Full scalar/context layouts and exact rounding: docs/control-prepared-v2.md. */
typedef struct { uint32_t word[5]; } prepared_packet_t;
#endif
