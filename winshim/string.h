/* minimal string.h for the stand-alone Windows build of psp_core.dll (no C library needed) */
#pragma once
typedef unsigned long long size_t;
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
#ifndef SHIM_IMPL                                /* let the compiler do small copies itself (a call per 4-byte read is slow) */
#define memcpy(d, s, n) __builtin_memcpy((d), (s), (n))
#define memset(d, c, n) __builtin_memset((d), (c), (n))
#define memcmp(a, b, n) __builtin_memcmp((a), (b), (n))
#endif
