/* Narrow freestanding compatibility for theremin_control.c only. */
#ifndef FDC_FREESTANDING_MATH_H
#define FDC_FREESTANDING_MATH_H
#define isfinite(value) __builtin_isfinite(value)
static inline float fabsf(float value) { return __builtin_fabsf(value); }
#endif
