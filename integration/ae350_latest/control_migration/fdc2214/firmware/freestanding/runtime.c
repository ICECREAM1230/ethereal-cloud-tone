/* Local freestanding support. Volatile prevents invented libc calls. */
#include <stddef.h>
void *memset(void *target, int value, size_t size) {
    volatile unsigned char *d = (volatile unsigned char *)target;
    while (size-- != 0u) *d++ = (unsigned char)value;
    return target;
}
void *memcpy(void *target, const void *source, size_t size) {
    volatile unsigned char *d = (volatile unsigned char *)target;
    const volatile unsigned char *s = (const volatile unsigned char *)source;
    while (size-- != 0u) *d++ = *s++;
    return target;
}
