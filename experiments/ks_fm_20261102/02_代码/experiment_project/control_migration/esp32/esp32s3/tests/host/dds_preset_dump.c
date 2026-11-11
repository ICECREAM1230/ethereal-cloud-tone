// Copyright (c) 2026 ICECREAM1230
// Validate actual preset documents and export the production compiler's image.
// License: MIT
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dds_document.h"
#include "dds_patch_units.h"
#include "midi_templates.h"

int main(int argc, char **argv)
{
    assert(argc == 4);
    char text[DDS_TEXT_MAX+1], error[DDS_ERROR_MAX], saved[DDS_TEXT_MAX+1];
    FILE *f = fopen(argv[1], "rb"); assert(f);
    size_t len = fread(text, 1, sizeof(text), f); assert(!ferror(f)); fclose(f);
    assert(len && len <= DDS_TEXT_MAX);
    patch_t p, q; unsigned warnings = 0;
    if (!dds_patch_parse(text, len, &p, &warnings, error)) {
        fprintf(stderr, "%s: %s\n", argv[1], error); return 1;
    }
    assert(warnings == 0);
    int n = dds_patch_text(&p, saved, sizeof(saved)); assert(n > 0);
    assert(dds_patch_parse(saved, (size_t)n, &q, &warnings, error) && !warnings);
    size_t count; const patch_field_t *fields = patch_fields(&count);
    unsigned checked = 0;
    for (size_t k=0; k<count; k++) for (int i=0; i<fields[k].count; i++) {
        assert(patch_field_canonical(fields+k, i, &p, fields[k].get(&p,i)) == fields[k].get(&q,i));
        checked++;
    }
    midi_template_image_t image; assert(midi_templates_compile(&p, 1200, &image));
    f = fopen(argv[2], "wb"); assert(f);
    assert(fwrite(saved, 1, (size_t)n, f) == (size_t)n); assert(!fclose(f));
    f = fopen(argv[3], "w"); assert(f);
    unsigned active = p.voice_struct == VOICE_2_PLAIN ? 2 : 8;
    fprintf(f, "%u %u %d %u %u\n", active, image.key_track, (int16_t)p.filter.cutoff, p.volume, p.bend_range);
    for (unsigned e=0; e<2; e++) fprintf(f, "%u %u\n", patch_adsr_rate1(&p.env[e]), patch_adsr_rate2(&p.env[e]));
    for (unsigned l=0; l<2; l++) fprintf(f, "%u %u %d %u\n", p.lfo[l].shape, p.lfo[l].rate, p.lfo[l].depth, p.lfo[l].dest);
    for (unsigned u=0; u<8; u++) for (unsigned w=0; w<8; w++) fprintf(f, "%u%c", image.elements[u][w], w==7?'\n':' ');
    for (unsigned v=0; v<128; v++) fprintf(f, "%u %u\n", image.amp_depth[v], image.mod_depth[v]);
    assert(!fclose(f));
    printf("PASS preset bytes=%zu fields=%u warnings=0 active_elements=%u roundtrip=exact template=production\n", len, checked, active);
    return 0;
}
