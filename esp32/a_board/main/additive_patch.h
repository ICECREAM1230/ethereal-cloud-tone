#pragma once
/* 加法合成器保留开发代码：未完成、未接入（2026-10-24）。
 * 当前产品不编译或调用该模块；接口、契约和历史测试仅供后续继续开发，
 * 不代表已完成当前 FPGA 的握手、控制或实板验收。
 */
#include <stdbool.h>
#include <stdint.h>

#define ADDITIVE_ENVELOPE_GROUPS 4u
#define ADDITIVE_PARTIALS 32u
#define ADDITIVE_SHADOW_WORDS 160u

typedef struct {
    uint16_t attack_inc, decay_inc, sustain, release_inc;
} additive_envelope_t;
typedef struct {
    uint32_t ratio_q16;
    uint16_t weight;
    uint8_t group;
} additive_partial_t;
typedef struct {
    bool bypass_lowpass;
    uint16_t centroid_q8, slope, comb_depth, master_gain, master_pan;
    uint8_t comb_period, bend_range;
    additive_envelope_t env[ADDITIVE_ENVELOPE_GROUPS];
    additive_partial_t partials[ADDITIVE_PARTIALS];
} additive_patch_t;

void additive_patch_default(additive_patch_t *patch);
bool additive_patch_validate(const additive_patch_t *patch);
/* Complete shadow image, including zero reserved words; no COMMIT.
 * Order: A000..A01F, then A100..A17F. Validate the patch before iteration. */
bool additive_patch_word(const additive_patch_t *patch, bool selected,
                         unsigned index, uint16_t *address, uint32_t *word);
