/* Only byte-copy primitives required by this freestanding component. */
#ifndef FDC_FREESTANDING_STRING_H
#define FDC_FREESTANDING_STRING_H
#include <stddef.h>
void *memset(void *target, int value, size_t size);
void *memcpy(void *target, const void *source, size_t size);
#endif
