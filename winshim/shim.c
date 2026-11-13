/* tiny C-library stand-in so psp_core.dll needs nothing else on Windows (x86-64) */
#define SHIM_IMPL
#include <stdint.h>
#include "string.h"
#include "math.h"
int _fltused = 0;

void *memcpy(void *d, const void *s, size_t n) { void *r = d; __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory"); return r; }
void *memset(void *d, int c, size_t n) { void *r = d; __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory"); return r; }
int memcmp(const void *a, const void *b, size_t n) { const unsigned char *p = a, *q = b; for (size_t i = 0; i < n; i++) if (p[i] != q[i]) return p[i] < q[i] ? -1 : 1; return 0; }

static inline uint64_t bits(double x) { uint64_t u; __builtin_memcpy(&u, &x, 8); return u; }
static inline double dbl(uint64_t u) { double x; __builtin_memcpy(&x, &u, 8); return x; }

double sqrt(double x) { double r; __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x)); return r; }
float sqrtf(float x) { float r; __asm__("sqrtss %1, %0" : "=x"(r) : "x"(x)); return r; }
double fabs(double x) { return dbl(bits(x) & 0x7FFFFFFFFFFFFFFFull); }
float fabsf(float x) { uint32_t u; __builtin_memcpy(&u, &x, 4); u &= 0x7FFFFFFFu; __builtin_memcpy(&x, &u, 4); return x; }
static const double TWO52 = 4503599627370496.0;
double trunc(double x) {
    if (!(fabs(x) < TWO52)) return x;                           /* big, inf or nan */
    double t = (double)(int64_t)x;
    return dbl((bits(t) & 0x7FFFFFFFFFFFFFFFull) | (bits(x) & 0x8000000000000000ull));
}
double floor(double x) { double t = trunc(x); return t > x ? t - 1.0 : t; }
double ceil(double x) { double t = trunc(x); return t < x ? t + 1.0 : t; }
double nearbyint(double x) {
    if (!(fabs(x) < TWO52)) return x;
    volatile double a = fabs(x) + TWO52; double r = a - TWO52;
    return dbl(bits(r) | (bits(x) & 0x8000000000000000ull));
}
double fmin(double a, double b) { if (a != a) return b; if (b != b) return a; return a < b ? a : b; }
double fmax(double a, double b) { if (a != a) return b; if (b != b) return a; return a > b ? a : b; }
double ldexp(double x, int e) {
    if (x == 0 || x != x || isinf(x)) return x;
    while (e > 1023) { x *= dbl(0x7FE0000000000000ull); e -= 1023; }
    while (e < -1022) { x *= dbl(0x0010000000000000ull); e += 1022; }
    return x * dbl((uint64_t)(e + 1023) << 52);
}
double frexp(double x, int *e) {
    *e = 0;
    if (x == 0 || x != x || isinf(x)) return x;
    int adj = 0; uint64_t u = bits(x);
    if (((u >> 52) & 0x7FF) == 0) { x *= dbl(0x4350000000000000ull); u = bits(x); adj = -54; }   /* subnormal: * 2^54 */
    *e = (int)((u >> 52) & 0x7FF) - 1022 + adj;
    return dbl((u & 0x800FFFFFFFFFFFFFull) | 0x3FE0000000000000ull);
}
/* x87 instructions do the transcendental maths */
double sin(double x) { if (isinf(x) || x != x) return NAN; double r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
double cos(double x) { if (isinf(x) || x != x) return NAN; double r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
double log2(double x) {
    if (x != x || x < 0) return NAN; if (x == 0) return -INFINITY; if (isinf(x)) return x;
    double r; __asm__("fyl2x" : "=t"(r) : "0"(x), "u"(1.0) : "st(1)"); return r;
}
double log(double x) { return log2(x) * 0.69314718055994530942; }
double log10(double x) { return log2(x) * 0.30102999566398119521; }
static double atan2_(double y, double x) { double r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
double asin(double x) { if (x != x || x > 1 || x < -1) return NAN; return atan2_(x, sqrt((1.0 - x) * (1.0 + x))); }
static double exp2_(double x) {
    if (x != x) return x;
    if (x > 1100) return INFINITY;
    if (x < -1100) return 0.0;
    double n = nearbyint(x), f = x - n, r;
    __asm__("f2xm1" : "=t"(r) : "0"(f));
    return ldexp(r + 1.0, (int)n);
}
double pow(double b, double e) {
    if (b == 2.0) return exp2_(e);
    if (e == 0) return 1.0;
    if (b == 0) return e > 0 ? 0.0 : INFINITY;
    if (b < 0) return NAN;
    return exp2_(e * log2(b));
}
