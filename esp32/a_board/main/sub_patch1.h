#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SUB_PATCH1_CAP_REG UINT16_C(0x071e)
#define SUB_PATCH1_MAGIC UINT32_C(0x53554231)
#define SUB_PATCH1_VERSION UINT32_C(0x00010000)
#define SUB_PATCH1_FIELD_MASK UINT32_C(0x00000fff)
// These controls need the complete native/CSP extension, not merely a legacy
// DDS template receipt. Other oscillator/pan/amp-velocity controls still work.
static inline bool sub_patch1_required_cc(unsigned cc)
{
    switch(cc) {
    case 7:case 71:case 72:case 73:case 74:case 75:case 76:case 77:case 79:
    case 87:case 102:case 103:case 104:case 105:case 106:case 107:
    case 109:case 110:case 111:case 112:case 113:return true;
    default:return false;
    }
}
