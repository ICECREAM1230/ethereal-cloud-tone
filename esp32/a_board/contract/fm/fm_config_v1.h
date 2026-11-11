/* SPDX-License-Identifier: MIT */
#ifndef FM_CONFIG_V1_H
#define FM_CONFIG_V1_H
#include <stdint.h>
#define FMC_MAGIC UINT32_C(0x464d4331)
#define FMC_BASE UINT16_C(0xb410)
#define FMC_COMMIT UINT16_C(0xb41f)
/* Read-only FMC window; RVR1 retains its original 0x0720..0x0727 ABI. */
#define FMC_STATUS_SPI UINT16_C(0x07c0)
#define FMC_STATUS_APB UINT16_C(0x00d0)
#define FMC_WORDS 13u
#define FMC_STATUS_WORDS 5u
/* Paired AE350/ESP32 cold-boot patch: catalog1 preset3 electric_piano_tine.
 * Runtime selections still use the existing FMC next-NoteOn transaction. */
#define FMC_DEFAULT_PRESET_ID UINT32_C(3)
enum { FMC_OK=0, FMC_BAD_VALUE=4, FMC_UNSUPPORTED=5 };
/* words: magic, nonzero sequence, backend(1=JT51), algorithm/feedback/keys,
 * then two little-endian words per logical M1/C1/M2/C2, then CRC32 of0..11.
 * Last operator word uses only16 bits. COMMIT repeats the sequence.
 * Status: capability, accepted, completed, result, applied image CRC.
 * A completed OK installs a next-NoteOn template, not a live-voice rewrite. */
static inline uint32_t fmc_crc(const uint32_t *words,unsigned count) {
    uint32_t crc=UINT32_MAX;
    for(unsigned i=0;i<count;i++)for(unsigned byte=0;byte<4;byte++) {
        crc^=(words[i]>>(8u*byte))&255u;
        for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^(UINT32_C(0xedb88320)&(0u-(crc&1u)));
    }
    return ~crc;
}
#endif
