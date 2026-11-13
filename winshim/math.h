/* minimal math.h for the stand-alone Windows build of psp_core.dll (implemented in shim.c) */
#pragma once
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
#define isinf(x) __builtin_isinf(x)
#define signbit(x) __builtin_signbit(x)
double sqrt(double); float sqrtf(float); double fabs(double); float fabsf(float);
double floor(double); double ceil(double); double trunc(double); double nearbyint(double);
double sin(double); double cos(double); double asin(double); double pow(double, double);
double log(double); double log2(double); double log10(double); double ldexp(double, int); double frexp(double, int *);
double fmin(double, double); double fmax(double, double);
#ifndef SHIM_IMPL                                /* simple ones become single instructions (needs -msse4.1 -fno-math-errno) */
#define fabs(x) __builtin_fabs(x)
#define fabsf(x) __builtin_fabsf(x)
#define sqrt(x) __builtin_sqrt(x)
#define sqrtf(x) __builtin_sqrtf(x)
#define floor(x) __builtin_floor(x)
#define ceil(x) __builtin_ceil(x)
#define trunc(x) __builtin_trunc(x)
#endif
