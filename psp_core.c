/*
 * psp_core.c - fast PSP CPU (Allegrex) interpreter for the MGS Portable Ops PC port.
 *
 * Runs the game's ordinary instructions (integer, branches, loads/stores, floating point) natively,
 * 100x+ faster than the Python version. Anything it does not handle (vector unit / VFPU, system calls)
 * is handed back to Python, which does it and continues. Same behaviour as psp_cpu.py, instruction for instruction.
 *
 * Build (Windows, once):   pip install ziglang
 *                          python -m ziglang cc -O2 -shared -o psp_core.dll psp_core.c
 * Build (Linux/macOS):     cc -O2 -shared -fPIC -o psp_core.so psp_core.c -lm
 * (build_core.bat does the Windows build for you.)
 */
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#define RAM_LO  0x08000000u
#define RAM_HI  0x0A000000u
#define VRAM_LO 0x04000000u
#define VRAM_HI 0x04200000u
#define SCR_LO  0x00010000u
#define SCR_HI  0x00014000u

typedef struct {
    uint32_t r[32];
    uint32_t hi, lo, pc;
    uint32_t f[32];
    uint32_t fcr31, cc;
    uint32_t v[128], vc[16];          /* vector unit registers and control registers */
    uint32_t halt;
    uint32_t fault_addr;
    uint64_t icount;
    uint8_t *ram, *vram, *scr;
    uint8_t *stubmap;          /* one byte per RAM word: 1 = system-call stub lives here */
} State;

enum { R_BUDGET = 0, R_STUB = 1, R_HALT = 2, R_PYTHON = 3, R_BADMEM = 4, R_SYSCALL = 5, R_BREAK = 6 };

#define BAD ((uint8_t *)0)

static inline uint8_t *loc(State *s, uint32_t a, uint32_t n) {
    a &= 0x3FFFFFFF;
    if (a >= RAM_LO && a + n <= RAM_HI) return s->ram + (a - RAM_LO);
    if (a >= VRAM_LO && a + n <= VRAM_HI) return s->vram + (a - VRAM_LO);
    if (a >= SCR_LO && a + n <= SCR_HI) return s->scr + (a - SCR_LO);
    return BAD;
}
static inline uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }   /* little-endian hosts */
/* write watch (for tracking down memory that gets overwritten): play.py can ask to be told who writes a few bytes */
static uint8_t *g_wlo, *g_whi; static uint32_t g_curpc, g_hits, g_hitpc[16], g_hitval[16]; static uint8_t *g_hitp[16];
#ifdef PROF_OPS
static uint64_t g_ops[2048], g_vops[65536];
#endif
static inline void wcheck(uint8_t *p, int n, uint32_t v) {
    if (p < g_whi && p + n > g_wlo && g_hits < 16) { g_hitpc[g_hits] = g_curpc; g_hitval[g_hits] = v; g_hitp[g_hits] = p; g_hits++; }
}
static inline void wr32(uint8_t *p, uint32_t v) { wcheck(p, 4, v); memcpy(p, &v, 4); }
static inline uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static inline void wr16(uint8_t *p, uint16_t v) { wcheck(p, 2, v); memcpy(p, &v, 2); }
static inline float i2f(uint32_t i) { float f; memcpy(&f, &i, 4); return f; }
static inline uint32_t f2i(float f) { uint32_t i; memcpy(&i, &f, 4); return i; }

/* can this instruction be run here (as opposed to handing it to Python)? used for delay slots */
static int native_ok(uint32_t ins) {
    uint32_t op = ins >> 26, fn = ins & 63, rs = (ins >> 21) & 31, rt = (ins >> 16) & 31, sa = (ins >> 6) & 31;
    switch (op) {
    case 0:
        switch (fn) {
        case 0x00: case 0x02: case 0x03: case 0x04: case 0x06: case 0x07: case 0x0A: case 0x0B: case 0x0F:
        case 0x10: case 0x11: case 0x12: case 0x13: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B:
        case 0x1C: case 0x1D: case 0x2E: case 0x2F: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:
        case 0x26: case 0x27: case 0x2A: case 0x2B: case 0x2C: case 0x2D: return 1;
        default: return 0;                   /* jr/jalr in a delay slot, syscall, break, unknown */
        }
    case 8: case 9: case 0xA: case 0xB: case 0xC: case 0xD: case 0xE: case 0xF:
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26:
    case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2E: case 0x2F: case 0x30: case 0x38: case 0x31: case 0x39:
        return 1;
    case 0x1F:
        if (fn == 0 || fn == 4) return 1;
        if (fn == 0x20 && (sa == 0x10 || sa == 0x18 || sa == 0x02 || sa == 0x03 || sa == 0x14)) return 1;
        return 0;
    case 0x11:
        if (rs == 0 || rs == 2 || rs == 4 || rs == 6) return 1;
        if (rs == 16) return fn <= 7 || fn == 0xC || fn == 0xD || fn == 0xE || fn == 0xF || fn == 0x24 || fn >= 0x30;
        if (rs == 20) return fn == 0x20;
        return 0;
    default: return 0;
    }
    (void)rt;
}

static int vfpu_supported(uint32_t ins);
static inline int is_vfpu_op(uint32_t op) {
    return op == 0x12 || op == 0x18 || op == 0x19 || op == 0x1A || op == 0x1B || op == 0x32 || op == 0x34 || op == 0x35 ||
           op == 0x36 || op == 0x37 || op == 0x3A || op == 0x3C || op == 0x3D || op == 0x3E || op == 0x3F;
}
static inline uint32_t clz32(uint32_t v) { if (!v) return 32; uint32_t n = 0; while (!(v & 0x80000000u)) { v <<= 1; n++; } return n; }

static int32_t f_to_int(float a, int mode) {             /* mode: 0 round-even, 1 trunc, 2 ceil, 3 floor */
    if (a != a) return 0x7FFFFFFF;
    if (isinf(a)) return 0x7FFFFFFF;
    double d = (double)a, v;
    switch (mode) { case 0: v = nearbyint(d); break; case 1: v = trunc(d); break; case 2: v = ceil(d); break; default: v = floor(d); }
    if (v > 2147483647.0) v = 2147483647.0;
    if (v < -2147483648.0) v = -2147483648.0;
    return (int32_t)v;
}

/* execute one non-branch instruction. returns 0 ok, else an R_ code */
static inline __attribute__((always_inline)) int exec_simple(State *s, uint32_t ins, uint32_t pc) {
    uint32_t *r = s->r;
    uint32_t op = ins >> 26, rs = (ins >> 21) & 31, rt = (ins >> 16) & 31, rd = (ins >> 11) & 31;
    uint32_t sa = (ins >> 6) & 31, fn = ins & 63, imm = ins & 0xFFFF;
    int32_t simm = (int16_t)imm;
    uint8_t *p; uint32_t a;
    switch (op) {
    case 0:
        switch (fn) {
        case 0x00: r[rd] = r[rt] << sa; break;
        case 0x02: r[rd] = (rs == 1 && sa) ? ((r[rt] >> sa) | (r[rt] << (32 - sa))) : (r[rt] >> sa); break;
        case 0x03: r[rd] = (uint32_t)((int32_t)r[rt] >> sa); break;
        case 0x04: r[rd] = r[rt] << (r[rs] & 31); break;
        case 0x06: { uint32_t n = r[rs] & 31; r[rd] = (((ins >> 6) & 1) && n) ? ((r[rt] >> n) | (r[rt] << (32 - n))) : (r[rt] >> n); break; }
        case 0x07: r[rd] = (uint32_t)((int32_t)r[rt] >> (r[rs] & 31)); break;
        case 0x0A: if (r[rt] == 0) r[rd] = r[rs]; break;
        case 0x0B: if (r[rt] != 0) r[rd] = r[rs]; break;
        case 0x0C: return R_SYSCALL;
        case 0x0D: return R_BREAK;
        case 0x0F: break;
        case 0x10: r[rd] = s->hi; break;
        case 0x11: s->hi = r[rs]; break;
        case 0x12: r[rd] = s->lo; break;
        case 0x13: s->lo = r[rs]; break;
        case 0x16: r[rd] = clz32(r[rs]); break;
        case 0x17: r[rd] = clz32(~r[rs]); break;
        case 0x18: { int64_t m = (int64_t)(int32_t)r[rs] * (int64_t)(int32_t)r[rt]; s->lo = (uint32_t)m; s->hi = (uint32_t)((uint64_t)m >> 32); break; }
        case 0x19: { uint64_t m = (uint64_t)r[rs] * (uint64_t)r[rt]; s->lo = (uint32_t)m; s->hi = (uint32_t)(m >> 32); break; }
        case 0x1A: {
            int32_t x = (int32_t)r[rs], y = (int32_t)r[rt];
            if (y) {
                if (x == INT32_MIN && y == -1) { s->lo = 0x80000000u; s->hi = 0; }
                else { s->lo = (uint32_t)(x / y); s->hi = (uint32_t)(x % y); }
            }
            break; }
        case 0x1B: if (r[rt]) { s->lo = r[rs] / r[rt]; s->hi = r[rs] % r[rt]; } break;
        case 0x1C: case 0x1D: case 0x2E: case 0x2F: {
            uint64_t acc = ((uint64_t)s->hi << 32) | s->lo, m;
            if (fn == 0x1C || fn == 0x2E) m = (uint64_t)((int64_t)(int32_t)r[rs] * (int64_t)(int32_t)r[rt]);
            else m = (uint64_t)r[rs] * (uint64_t)r[rt];
            acc = (fn == 0x1C || fn == 0x1D) ? acc + m : acc - m;
            s->lo = (uint32_t)acc; s->hi = (uint32_t)(acc >> 32); break; }
        case 0x20: case 0x21: r[rd] = r[rs] + r[rt]; break;
        case 0x22: case 0x23: r[rd] = r[rs] - r[rt]; break;
        case 0x24: r[rd] = r[rs] & r[rt]; break;
        case 0x25: r[rd] = r[rs] | r[rt]; break;
        case 0x26: r[rd] = r[rs] ^ r[rt]; break;
        case 0x27: r[rd] = ~(r[rs] | r[rt]); break;
        case 0x2A: r[rd] = (int32_t)r[rs] < (int32_t)r[rt]; break;
        case 0x2B: r[rd] = r[rs] < r[rt]; break;
        case 0x2C: r[rd] = ((int32_t)r[rs] > (int32_t)r[rt]) ? r[rs] : r[rt]; break;
        case 0x2D: r[rd] = ((int32_t)r[rs] < (int32_t)r[rt]) ? r[rs] : r[rt]; break;
        default: return R_PYTHON;
        }
        break;
    case 8: case 9: r[rt] = r[rs] + (uint32_t)simm; break;
    case 0xA: r[rt] = (int32_t)r[rs] < simm; break;
    case 0xB: r[rt] = r[rs] < (uint32_t)simm; break;
    case 0xC: r[rt] = r[rs] & imm; break;
    case 0xD: r[rt] = r[rs] | imm; break;
    case 0xE: r[rt] = r[rs] ^ imm; break;
    case 0xF: r[rt] = imm << 16; break;
    case 0x1F:
        if (fn == 0) { uint32_t size = rd + 1; r[rt] = size >= 32 ? (r[rs] >> sa) : ((r[rs] >> sa) & ((1u << size) - 1)); }
        else if (fn == 4) {
            uint32_t msb = rd, lsb = sa; int size = (int)msb - (int)lsb + 1;
            if (size <= 0) return R_PYTHON;
            uint32_t mask = (size >= 32 ? 0xFFFFFFFFu : ((1u << size) - 1)) << lsb;
            r[rt] = (r[rt] & ~mask) | ((r[rs] << lsb) & mask);
        } else if (fn == 0x20) {
            uint32_t v = r[rt];
            if (sa == 0x10) r[rd] = (uint32_t)(int32_t)(int8_t)(v & 0xFF);
            else if (sa == 0x18) r[rd] = (uint32_t)(int32_t)(int16_t)(v & 0xFFFF);
            else if (sa == 0x02) r[rd] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
            else if (sa == 0x03) r[rd] = (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
            else if (sa == 0x14) { uint32_t o = 0; for (int i = 0; i < 32; i++) if (v & (1u << i)) o |= 1u << (31 - i); r[rd] = o; }
            else return R_PYTHON;
        } else return R_PYTHON;
        break;
    case 0x20: a = r[rs] + simm; if (!(p = loc(s, a, 1))) goto bad; r[rt] = (uint32_t)(int32_t)(int8_t)p[0]; break;
    case 0x21: a = r[rs] + simm; if (!(p = loc(s, a, 2))) goto bad; r[rt] = (uint32_t)(int32_t)(int16_t)rd16(p); break;
    case 0x23: case 0x30: a = r[rs] + simm; if (!(p = loc(s, a, 4))) goto bad; r[rt] = rd32(p); break;
    case 0x24: a = r[rs] + simm; if (!(p = loc(s, a, 1))) goto bad; r[rt] = p[0]; break;
    case 0x25: a = r[rs] + simm; if (!(p = loc(s, a, 2))) goto bad; r[rt] = rd16(p); break;
    case 0x22: { /* lwl */
        a = r[rs] + simm; uint32_t al = a & ~3u, sh = a & 3; if (!(p = loc(s, al, 4))) { a = al; goto bad; }
        uint32_t w = rd32(p), k = 8 * (3 - sh);
        r[rt] = (w << k) | (r[rt] & (k ? ((1u << k) - 1) : 0)); break; }
    case 0x26: { /* lwr */
        a = r[rs] + simm; uint32_t al = a & ~3u, sh = a & 3; if (!(p = loc(s, al, 4))) { a = al; goto bad; }
        uint32_t w = rd32(p), keep = sh ? (0xFFFFFFFFu << (32 - 8 * sh)) : 0;
        r[rt] = (w >> (8 * sh)) | (r[rt] & keep); break; }
    case 0x28: a = r[rs] + simm; if (!(p = loc(s, a, 1))) goto bad; wcheck(p, 1, r[rt]); p[0] = (uint8_t)r[rt]; break;
    case 0x29: a = r[rs] + simm; if (!(p = loc(s, a, 2))) goto bad; wr16(p, (uint16_t)r[rt]); break;
    case 0x2B: a = r[rs] + simm; if (!(p = loc(s, a, 4))) goto bad; wr32(p, r[rt]); break;
    case 0x2A: { /* swl */
        a = r[rs] + simm; uint32_t al = a & ~3u, sh = a & 3; if (!(p = loc(s, al, 4))) { a = al; goto bad; }
        uint32_t w = rd32(p), m = sh == 3 ? 0xFFFFFFFFu : ((1u << (8 * (sh + 1))) - 1);
        wr32(p, (w & ~m) | ((r[rt] >> (8 * (3 - sh))) & m)); break; }
    case 0x2E: { /* swr */
        a = r[rs] + simm; uint32_t al = a & ~3u, sh = a & 3; if (!(p = loc(s, al, 4))) { a = al; goto bad; }
        uint32_t w = rd32(p), m = 0xFFFFFFFFu << (8 * sh);
        wr32(p, (w & ~m) | ((r[rt] << (8 * sh)) & m)); break; }
    case 0x2F: break;                                   /* cache */
    case 0x38: a = r[rs] + simm; if (!(p = loc(s, a, 4))) goto bad; wr32(p, r[rt]); r[rt] = 1; break;   /* sc */
    case 0x31: a = r[rs] + simm; if (!(p = loc(s, a, 4))) goto bad; s->f[rt] = rd32(p); break;          /* lwc1 */
    case 0x39: a = r[rs] + simm; if (!(p = loc(s, a, 4))) goto bad; wr32(p, s->f[rt]); break;           /* swc1 */
    case 0x11: {
        uint32_t *f = s->f;
        if (rs == 0) r[rt] = f[rd];
        else if (rs == 2) r[rt] = rd == 31 ? s->fcr31 : 0;
        else if (rs == 4) f[rd] = r[rt];
        else if (rs == 6) { if (rd == 31) s->fcr31 = r[rt]; }
        else if (rs == 16) {
            float x = i2f(f[rd]), y = i2f(f[rt]); uint32_t d = sa;
            switch (fn) {
            case 0: f[d] = f2i(x + y); break;
            case 1: f[d] = f2i(x - y); break;
            case 2: f[d] = f2i(x * y); break;
            case 3: f[d] = y != 0.0f ? f2i(x / y) : (x >= 0 ? 0x7F800000u : 0xFF800000u); break;
            case 4: f[d] = x >= 0 ? f2i(sqrtf(x)) : 0x7FC00000u; break;
            case 5: f[d] = f2i(fabsf(x)); break;
            case 6: f[d] = f[rd]; break;
            case 7: f[d] = f[rd] ^ 0x80000000u; break;
            case 0xC: case 0x24: f[d] = (uint32_t)f_to_int(x, 0); break;
            case 0xD: f[d] = (uint32_t)f_to_int(x, 1); break;
            case 0xE: f[d] = (uint32_t)f_to_int(x, 2); break;
            case 0xF: f[d] = (uint32_t)f_to_int(x, 3); break;
            default:
                if (fn >= 0x30) {
                    uint32_t c = fn & 15; int un = (x != x) || (y != y);
                    int res = ((c & 1) && un) || ((c & 2) && x == y) || ((c & 4) && x < y);
                    s->cc = (un && !(c & 1)) ? 0 : (res ? 1 : 0);
                } else return R_PYTHON;
            }
        } else if (rs == 20 && fn == 0x20) s->f[sa] = f2i((float)(int32_t)s->f[rd]);
        else return R_PYTHON;
        break; }
    default: return R_PYTHON;
    }
    r[0] = 0;
    return 0;
bad:
    s->fault_addr = a;
    return R_BADMEM;
}

/* ======================================================================= VFPU (vector unit)
 * Mirrors psp_vfpu.py exactly (values held as double, rounded to float32 after each operation the same way).
 * Anything not listed here returns R_PYTHON and psp_vfpu.py does it.                                          */
#define CTRL_S 0
#define CTRL_T 1
#define CTRL_D 2
#define CTRL_CC 3
static const double PFX_CONST[8] = {0.0, 1.0, 2.0, 0.5, 3.0, 1.0 / 3.0, 0.25, 1.0 / 6.0};
static double CSTV[20];
static int cst_ready = 0;
static void init_cst(void) {
    double pi = 3.14159265358979323846, e = 2.71828182845904523536;
    double v[20] = {0.0, 3.4028234663852886e+38, sqrt(2.0), sqrt(0.5), 2 / sqrt(pi), 2 / pi, 1 / pi, pi / 4, pi / 2, pi, e,
                    log2(e), log10(e), log(2.0), log(10.0), 2 * pi, pi / 6, log10(2.0), log2(10.0), sqrt(3.0) / 2};
    memcpy(CSTV, v, sizeof v); cst_ready = 1;
}
#define PI_D 3.14159265358979323846

static inline double vf(uint32_t b) { return (double)i2f(b); }
static inline double rnd(double x) { return (double)(float)x; }
static inline uint32_t fbits(double x) { return f2i((float)x); }

static void vec_idx_calc(int reg, int n, int *out) {
    int mtx = (reg >> 2) & 7, col = reg & 3, tr = (reg >> 5) & 1, row;
    if (n == 1) { tr = 0; row = (reg >> 5) & 3; }
    else if (n == 3) row = (reg >> 6) & 1;
    else row = (reg >> 5) & 2;
    for (int i = 0; i < n; i++) out[i] = tr ? mtx * 16 + ((row + i) & 3) * 4 + col : mtx * 16 + col * 4 + ((row + i) & 3);
}
static void mat_idx_calc(int reg, int n, int *out) {   /* out[j*4+i] */
    int mtx = (reg >> 2) & 7, col = reg & 3, tr = (reg >> 5) & 1, row;
    if (n == 1) { tr = 0; row = (reg >> 5) & 3; }
    else if (n == 3) row = (reg >> 6) & 1;
    else row = (reg >> 5) & 2;
    for (int k = 0; k < 16; k++) out[k] = 0;
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++)
        out[j * 4 + i] = tr ? mtx * 16 + ((row + i) & 3) * 4 + ((col + j) & 3) : mtx * 16 + ((col + j) & 3) * 4 + ((row + i) & 3);
}
/* the register numbers above never change: work them out once into tables */
static int8_t VIDX[5][128][4], MIDX[5][128][16]; static int idx_ready;
static void idx_init(void) {
    for (int n = 1; n <= 4; n++) for (int reg = 0; reg < 128; reg++) {
        int a[4] = {0, 0, 0, 0}, b[16]; vec_idx_calc(reg, n, a); mat_idx_calc(reg, n, b);
        for (int i = 0; i < 4; i++) VIDX[n][reg][i] = (int8_t)a[i];
        for (int i = 0; i < 16; i++) MIDX[n][reg][i] = (int8_t)b[i];
    }
    idx_ready = 1;
}
static inline void vec_idx(int reg, int n, int *out) {
    if (!idx_ready) idx_init();
    const int8_t *t = VIDX[n][reg & 127]; for (int i = 0; i < n; i++) out[i] = t[i];
}
static inline void mat_idx(int reg, int n, int *out) {
    if (!idx_ready) idx_init();
    const int8_t *t = MIDX[n][reg & 127]; for (int i = 0; i < 16; i++) out[i] = t[i];
}
static void rdv(State *s, int reg, int n, double *v) { int ix[4]; vec_idx(reg, n, ix); for (int i = 0; i < 4; i++) v[i] = i < n ? vf(s->v[ix[i]]) : 0.0; }
static void rdm(State *s, int reg, int n, double *m) {
    int ix[16]; mat_idx(reg, n, ix);
    for (int k = 0; k < 16; k++) m[k] = 0.0;
    for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) m[j * 4 + i] = vf(s->v[ix[j * 4 + i]]);
}
static void wr_bits(State *s, int reg, const uint32_t *b, int n) {
    int ix[4]; vec_idx(reg, n, ix); uint32_t d = s->vc[CTRL_D];
    for (int i = 0; i < n; i++) { if ((d >> (8 + i)) & 1) continue; s->v[ix[i]] = b[i]; }
}
static void wrv(State *s, int reg, const double *v, int n) { uint32_t b[4]; for (int i = 0; i < n; i++) b[i] = fbits(v[i]); wr_bits(s, reg, b, n); }
static void wrm(State *s, int reg, const double *m, int n) {
    int ix[16]; mat_idx(reg, n, ix);
    for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) s->v[ix[j * 4 + i]] = fbits(m[j * 4 + i]);
}
/* prefix: v has n valid lanes; result in out[0..3] (lanes >= n copied from v / zero, like the Python version) */
static void apply_st(uint32_t p, const double *v, int n, double invalid, double *out) {
    double orig[4];
    if (p == 0xE4) { out[0] = v[0]; out[1] = v[1]; out[2] = v[2]; out[3] = v[3]; return; }
    for (int i = 0; i < 4; i++) { orig[i] = i < n ? v[i] : invalid; out[i] = v[i]; }
    for (int i = 0; i < n; i++) {
        int sw = (p >> (2 * i)) & 3, ab = (p >> (8 + i)) & 1, cs = (p >> (12 + i)) & 1, ng = (p >> (16 + i)) & 1;
        double x = cs ? PFX_CONST[sw + (ab << 2)] : (ab ? fabs(orig[sw]) : orig[sw]);
        out[i] = ng ? -x : x;
    }
}
static inline uint32_t SWZ(int x, int y, int z, int w) { return x | y << 2 | z << 4 | w << 6; }
static inline uint32_t rewrite(uint32_t p, uint32_t remove, uint32_t add) { return (p & ~remove) | add; }
static void dsat(State *s, double *v, int n) {
    uint32_t d = s->vc[CTRL_D];
    if (!(d & 0xFF)) return;
    for (int i = 0; i < n; i++) {
        int k = (d >> (2 * i)) & 3;
        if (k == 1) v[i] = v[i] <= 0 ? 0.0 : (v[i] > 1 ? 1.0 : v[i]);
        else if (k == 3) v[i] = v[i] < -1 ? -1.0 : (v[i] > 1 ? 1.0 : v[i]);
    }
}
static inline void eat(State *s) { s->vc[CTRL_S] = 0xE4; s->vc[CTRL_T] = 0xE4; s->vc[CTRL_D] = 0; }
static inline void last_only_d(State *s, int n) { uint32_t d = s->vc[CTRL_D]; s->vc[CTRL_D] = ((d & (1u << 8)) << (n - 1)) | ((d & 3) << (2 * n - 2)); }
static double dot(const double *a, const double *b, int n) { double t = 0.0; for (int i = 0; i < n; i++) t = rnd(t + rnd(a[i] * b[i])); return t; }
static double vdiv(double a, double b) {
    if (b == 0.0) {
        if (a == 0.0 || a != a) return NAN;
        int neg = (a < 0) != (signbit(b) != 0);
        return neg ? -INFINITY : INFINITY;
    }
    return rnd(a / b);
}

static void st_bits(uint32_t p, const uint32_t *b, int len, int n, uint32_t *out) {   /* apply_st_bits */
    uint32_t orig[4];
    for (int i = 0; i < 4; i++) { orig[i] = i < len ? b[i] : 0; out[i] = orig[i]; }
    if (p == 0xE4) return;
    for (int i = 0; i < n; i++) {
        int sw = (p >> (2 * i)) & 3, ab = (p >> (8 + i)) & 1, cs = (p >> (12 + i)) & 1, ng = (p >> (16 + i)) & 1;
        uint32_t x = cs ? fbits(PFX_CONST[sw + (ab << 2)]) : (ab ? (orig[sw] & 0x7FFFFFFFu) : orig[sw]);
        out[i] = ng ? x ^ 0x80000000u : x;
    }
}
static double h2f(uint32_t h) {
    int s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 0x3FF; double v;
    if (e == 0) v = ldexp((double)m, -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = ldexp(1024.0 + m, e - 25);
    return s ? -v : v;
}
static uint32_t f2h(double f) {
    if (f != f) return 0x7FFF;
    uint32_t sign = signbit(f) ? 0x8000 : 0; double a = fabs(f);
    if (isinf(a) || a >= 65520.0) return sign | 0x7C00;
    if (a < ldexp(1.0, -14)) {                                   /* subnormal half */
        double q = a * 16777216.0;                               /* units of 2^-24 */
        uint32_t m = (uint32_t)nearbyint(q);
        return sign | m;                                         /* m may round up to 0x400 = smallest normal: still correct bits */
    }
    int e; double fr = frexp(a, &e);                             /* a = fr * 2^e, fr in [0.5,1) */
    double mant = fr * 2048.0;                                   /* 11 bits incl. hidden */
    uint32_t mi = (uint32_t)nearbyint(mant);
    if (mi == 2048) { mi = 1024; e += 1; }
    int he = e - 1 + 15;
    if (he >= 31) return sign | 0x7C00;
    return sign | ((uint32_t)he << 10) | (mi & 0x3FF);
}
static inline int size_of(uint32_t ins) { return (int)(((ins >> 7) & 1) + ((ins >> 14) & 2) + 1); }
static inline uint32_t consts1(int lane, int c) { return ((c & 3) << (2 * lane)) | ((c & 4) << (6 + lane)) | (1u << (12 + lane)); }

/* returns 1 if this VFPU instruction is done natively (used to decide before touching anything) */
static int vfpu_supported(uint32_t ins) {
    uint32_t op = ins >> 26;
    if (op == 0x3F) return (ins >> 16) == 0xFFFF;
    if (op == 0x12) { uint32_t sub = (ins >> 21) & 31; return sub == 3 || sub == 7; }      /* bvf/bvt handled as a branch */
    if (op == 0x32 || op == 0x3A || op == 0x36 || op == 0x3E || op == 0x35 || op == 0x3D || op == 0x37) return 1;
    if (op == 0x18) { uint32_t sub = (ins >> 23) & 7; return sub == 0 || sub == 1 || sub == 7; }
    if (op == 0x19) { uint32_t sub = (ins >> 23) & 7; return sub == 0 || sub == 1 || sub == 2 || sub == 4 || sub == 5; }
    if (op == 0x1B) { uint32_t sub = (ins >> 23) & 7; return sub == 0 || sub == 2 || sub == 3 || sub == 6 || sub == 7; }
    if (op == 0x34) {
        uint32_t sub = (ins >> 21) & 31, rt = (ins >> 16) & 31;
        if (sub == 0) return rt <= 7 || (rt >= 16 && rt <= 24 && rt != 25) || rt == 26 || rt == 28;
        if (sub == 2) return rt == 2 || rt == 3 || rt == 4 || rt == 6 || rt == 7 || rt == 10 || rt == 16 || rt == 17 || (rt >= 25 && rt <= 27);
        if (sub == 1) return rt == 18 || rt == 19 || (rt >= 24 && rt <= 31);
        if (sub == 3) return 1;
        if (sub >= 16 && sub <= 21) return 1;
        return 0;
    }
    if (op == 0x3C) {
        uint32_t sub = (ins >> 21) & 31;
        if (sub < 24) return 1;
        if (sub == 29) return 1;
        if (sub == 28) { uint32_t k = (ins >> 16) & 15; return k == 0 || k == 3 || k == 6 || k == 7; }
        return 0;
    }
    return 0;
}

static uint32_t g_vskey[4096]; static uint8_t g_vsval[4096];
static uint32_t g_vdirty = 1;                /* set when native code may have changed the vector registers */
static int vfpu_exec(State *s, uint32_t ins, uint32_t pc) {
    uint32_t op = ins >> 26; uint32_t *r = s->r; g_vdirty = 1;
#ifdef PROF_OPS
    g_vops[ins >> 16]++;
#endif
    int vd = ins & 0x7F, vs = (ins >> 8) & 0x7F, vt = (ins >> 16) & 0x7F;
    double S_[4], T_[4], D_[4], tmp[4];
    { uint32_t h = (ins * 2654435761u) >> 20;                 /* "done here?" remembered per instruction word */
      if (g_vskey[h] != ins || !g_vsval[h]) { g_vskey[h] = ins; g_vsval[h] = (uint8_t)(vfpu_supported(ins) + 1); }
      if (g_vsval[h] != 2) return R_PYTHON; }
    if (!cst_ready) init_cst();
    if (op == 0x3F) return 0;
    if (op == 0x12) {
        uint32_t sub = (ins >> 21) & 31, rt = (ins >> 16) & 31, imm = ins & 0xFF; int ix[1];
        if (sub == 3) { if (rt) { if (imm < 128) { vec_idx(imm, 1, ix); r[rt] = s->v[ix[0]]; } else if (imm < 144) r[rt] = s->vc[imm - 128]; } }
        else {
            if (imm < 128) { vec_idx(imm, 1, ix); s->v[ix[0]] = r[rt]; }
            else if (imm < 144) s->vc[imm - 128] = r[rt] & (imm - 128 < 2 ? 0xFFFFFu : imm - 128 == 2 ? 0xFFFu : 0xFFFFFFFFu);
        }
        r[0] = 0; return 0;
    }
    if (op == 0x32 || op == 0x3A) {
        int vt5 = ((ins >> 16) & 31) | ((ins & 3) << 5), ix[1]; uint32_t a = r[(ins >> 21) & 31] + (uint32_t)(int32_t)(int16_t)(ins & 0xFFFC);
        uint8_t *p = loc(s, a, 4); if (!p) { s->fault_addr = a; return R_BADMEM; }
        vec_idx(vt5, 1, ix);
        if (op == 0x32) s->v[ix[0]] = rd32(p); else wr32(p, s->v[ix[0]]);
        return 0;
    }
    if (op == 0x36 || op == 0x3E) {
        int vt6 = ((ins >> 16) & 31) | ((ins & 1) << 5), ix[4]; uint32_t a = r[(ins >> 21) & 31] + (uint32_t)(int32_t)(int16_t)(ins & 0xFFFC);
        uint8_t *p = loc(s, a, 16); if (!p) { s->fault_addr = a; return R_BADMEM; }
        vec_idx(vt6, 4, ix);
        for (int i = 0; i < 4; i++) { if (op == 0x36) s->v[ix[i]] = rd32(p + 4 * i); else wr32(p + 4 * i, s->v[ix[i]]); }
        return 0;
    }
    if (op == 0x35 || op == 0x3D) {
        int vt6 = ((ins >> 16) & 31) | ((ins & 1) << 5), ix[4]; uint32_t a = r[(ins >> 21) & 31] + (uint32_t)(int32_t)(int16_t)(ins & 0xFFFC);
        int off = (a >> 2) & 3, right = (ins & 2) != 0; vec_idx(vt6, 4, ix);
        int cnt = right ? 4 - off : off + 1;
        for (int i = 0; i < cnt; i++) {                     /* check every address first: no half-done instruction */
            uint32_t aa = right ? a + 4 * i : a - 4 * i; if (!loc(s, aa, 4)) { s->fault_addr = aa; return R_BADMEM; }
        }
        for (int i = 0; i < cnt; i++) {
            uint32_t aa = right ? a + 4 * i : a - 4 * i; uint8_t *p = loc(s, aa, 4); int k = right ? ix[i] : ix[3 - i];
            if (op == 0x35) s->v[k] = rd32(p); else wr32(p, s->v[k]);
        }
        return 0;
    }
    if (op == 0x37) {
        int k = (ins >> 23) & 7;
        if (k < 6) { int reg = k >> 1; uint32_t data = ins & 0xFFFFF; if (reg == 2) data &= 0xFFF; s->vc[reg] = data; return 0; }
        double val;
        if (k == 6) val = (double)(int16_t)(ins & 0xFFFF);
        else {
            uint32_t h = ins & 0xFFFF, e = (h >> 10) & 31, m = h & 0x3FF; double sg = (h & 0x8000) ? -1.0 : 1.0;
            val = sg * (e == 0 ? ldexp((double)m, -24) : (e == 31 && m == 0 ? INFINITY : e == 31 ? NAN : ldexp(1024.0 + m, (int)e - 25)));
        }
        D_[0] = val; dsat(s, D_, 1); wrv(s, vt, D_, 1); eat(s); return 0;
    }
    int n = size_of(ins);
    uint32_t PS = s->vc[CTRL_S], PT = s->vc[CTRL_T];
    /* fast paths for the most common operations when no prefix is pending (same arithmetic as below, without
     * the general prefix machinery) */
#ifndef NO_VFAST
    if (PS == 0xE4 && PT == 0xE4 && s->vc[CTRL_D] == 0) {
        if (!idx_ready) idx_init();
        const int8_t *is_ = VIDX[n][vs], *it_ = VIDX[n][vt], *id_ = VIDX[n][vd];
        uint32_t out[4]; double a, b;
        if (op == 0x34 && ((ins >> 16) & 0x1F) == 0 && ((ins >> 21) & 31) == 0) {         /* vmov */
            for (int i = 0; i < n; i++) out[i] = s->v[is_[i]];           /* a plain move: the bits as they are */
            for (int i = 0; i < n; i++) s->v[id_[i]] = out[i];
            return 0;
        }
        if (op == 0x34 && ((ins >> 21) & 31) == 0 && (((ins >> 16) & 0x1F) == 6 || ((ins >> 16) & 0x1F) == 7)) {   /* vzero / vone */
            uint32_t c = ((ins >> 16) & 0x1F) == 7 ? 0x3F800000u : 0;
            for (int i = 0; i < n; i++) s->v[id_[i]] = c;
            return 0;
        }
        uint32_t sub = (ins >> 23) & 7;
        if ((op == 0x18 && (sub == 0 || sub == 1)) || (op == 0x19 && sub == 0)) {         /* vadd / vsub / vmul */
            for (int i = 0; i < n; i++) {
                a = vf(s->v[is_[i]]); b = vf(s->v[it_[i]]);
                out[i] = fbits(op == 0x19 ? rnd(a * b) : sub == 0 ? rnd(a + b) : rnd(a - b));
            }
            for (int i = 0; i < n; i++) s->v[id_[i]] = out[i];
            return 0;
        }
        if (op == 0x19 && sub == 1) {                                                       /* vdot */
            double sv[4], tv[4];
            for (int i = 0; i < n; i++) { sv[i] = vf(s->v[is_[i]]); tv[i] = vf(s->v[it_[i]]); }
            s->v[VIDX[1][vd][0]] = fbits(dot(sv, tv, n));
            return 0;
        }
        if (op == 0x1B && sub == 0) {                                                       /* vcmp */
            int cond = ins & 15; uint32_t cc = 0, any = 0, all = 1, aff = 0x30;
            for (int i = 0; i < n; i++) {
                double x = vf(s->v[is_[i]]), y = vf(s->v[it_[i]]); int c = 0;
                int isn = x != x, isi = isinf(x) != 0;
                switch (cond) {
                case 0: c = 0; break; case 1: c = x == y; break; case 2: c = x < y; break; case 3: c = x <= y; break;
                case 4: c = 1; break; case 5: c = x != y; break; case 6: c = x >= y; break; case 7: c = x > y; break;
                case 8: c = x == 0; break; case 9: c = isn; break; case 10: c = isi; break; case 11: c = isn || isi; break;
                case 12: c = x != 0; break; case 13: c = !isn; break; case 14: c = !isi; break; default: c = !(isn || isi);
                }
                c = c ? 1 : 0; cc |= (uint32_t)c << i; any |= c; all &= c; aff |= 1u << i;
            }
            s->vc[CTRL_CC] = (s->vc[CTRL_CC] & ~aff) | ((cc | (any << 4) | (all << 5)) & aff);
            return 0;
        }
    }
#endif
    if (op == 0x18 || op == 0x19 || op == 0x1B) {
        uint32_t sub = (ins >> 23) & 7;
        double sv[4], tv[4];
        if ((op == 0x18 && (sub == 0 || sub == 1 || sub == 7)) || (op == 0x19 && sub == 0)) {
            rdv(s, vs, n, sv); rdv(s, vt, n, tv);
            if (op == 0x18 && sub == 7) {
                double one[4] = {sv[n - 1], 0, 0, 0}, o2[4];
                apply_st(PS, one, 1, -INFINITY, o2); sv[n - 1] = o2[0];
                one[0] = tv[n - 1]; apply_st(PT, one, 1, -INFINITY, o2); tv[n - 1] = o2[0];
                for (int i = 0; i < 4; i++) { S_[i] = sv[i]; T_[i] = tv[i]; }
            } else { apply_st(PS, sv, n, 0.0, S_); apply_st(PT, tv, n, 0.0, T_); }
            for (int i = 0; i < n; i++) {
                if (op == 0x18 && sub == 0) D_[i] = rnd(S_[i] + T_[i]);
                else if (op == 0x18 && sub == 1) D_[i] = rnd(S_[i] - T_[i]);
                else if (op == 0x18) D_[i] = vdiv(S_[i], T_[i]);
                else D_[i] = rnd(S_[i] * T_[i]);
            }
            if (op == 0x18 && sub == 7) last_only_d(s, n);
            dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
        }
        if (op == 0x19 && sub == 1) {
            rdv(s, vs, n, sv); rdv(s, vt, n, tv); apply_st(PS, sv, n, 0.0, S_); apply_st(PT, tv, n, 0.0, T_);
            D_[0] = dot(S_, T_, n); dsat(s, D_, 1); wrv(s, vd, D_, 1); eat(s); return 0;
        }
        if (op == 0x19 && sub == 4) {                     /* vhdp */
            rdv(s, vs, n, sv); rdv(s, vt, n, tv); apply_st(PT, tv, n, 0.0, T_);
            uint32_t sws = 0, cns = 0;
            for (int i = 0; i < 4; i++) { if (i == n - 1) { sws |= 3u << (2 * i); cns |= consts1(i, 1); } }
            apply_st(rewrite(PS, sws, cns), sv, 4, 0.0, S_);
            D_[0] = dot(S_, T_, n); dsat(s, D_, 1); wrv(s, vd, D_, 1); eat(s); return 0;
        }
        if (op == 0x19 && sub == 2) {                     /* vscl */
            rdv(s, vs, n, sv); apply_st(PS, sv, n, 0.0, S_);
            int tl = (vt >> 5) & 3, ix[1]; double tv4[4] = {0, 0, 0, 0}; vec_idx(vt, 1, ix); tv4[tl] = vf(s->v[ix[0]]);
            apply_st(rewrite(PT, 0xFF, SWZ(tl, tl, tl, tl)), tv4, 4, 0.0, T_);
            for (int i = 0; i < n; i++) D_[i] = rnd(S_[i] * T_[i]);
            dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
        }
        if (op == 0x19 && sub == 5) {                     /* vcrs */
            rdv(s, vs, n, sv); rdv(s, vt, n, tv);
            apply_st(rewrite(PS, SWZ(3, 3, 3, 0), SWZ(1, 2, 0, 0)), sv, n, 0.0, S_);
            apply_st(rewrite(PT, SWZ(3, 3, 3, 0), SWZ(2, 0, 1, 0)), tv, n, 0.0, T_);
            for (int i = 0; i < n; i++) D_[i] = rnd(S_[i] * T_[i]);
            dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
        }
        rdv(s, vs, n, sv); rdv(s, vt, n, tv); apply_st(PS, sv, n, 0.0, S_); apply_st(PT, tv, n, 0.0, T_);
        if (op == 0x1B && (sub == 2 || sub == 3)) {
            for (int i = 0; i < n; i++) D_[i] = sub == 2 ? (T_[i] < S_[i] ? T_[i] : S_[i]) : (T_[i] > S_[i] ? T_[i] : S_[i]);
            dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
        }
        if (op == 0x1B && sub == 0) {                     /* vcmp */
            int cond = ins & 15; uint32_t cc = 0, any = 0, all = 1, aff = 0x30;
            for (int i = 0; i < n; i++) {
                double a = S_[i], b = T_[i]; int c = 0;
                int isn = a != a, isi = isinf(a) != 0;
                switch (cond) {
                case 0: c = 0; break; case 1: c = a == b; break; case 2: c = a < b; break; case 3: c = a <= b; break;
                case 4: c = 1; break; case 5: c = a != b; break; case 6: c = a >= b; break; case 7: c = a > b; break;
                case 8: c = a == 0; break; case 9: c = isn; break; case 10: c = isi; break; case 11: c = isn || isi; break;
                case 12: c = a != 0; break; case 13: c = !isn; break; case 14: c = !isi; break; default: c = !(isn || isi);
                }
                c = c ? 1 : 0; cc |= (uint32_t)c << i; any |= c; all &= c; aff |= 1u << i;
            }
            s->vc[CTRL_CC] = (s->vc[CTRL_CC] & ~aff) | ((cc | (any << 4) | (all << 5)) & aff);
            eat(s); return 0;
        }
        if (op == 0x1B && (sub == 6 || sub == 7)) {
            for (int i = 0; i < n; i++) D_[i] = ((sub == 6) ? (S_[i] >= T_[i]) : (S_[i] < T_[i])) ? 1.0 : 0.0;
            dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
        }
        return R_PYTHON;
    }
    if (op == 0x34) {
        uint32_t sub = (ins >> 21) & 31, rt = (ins >> 16) & 31; double sv[4];
        if (sub == 0) {
            if (rt == 3 || rt == 6 || rt == 7) {
                if (rt == 3) { int offmask = n >= 3 ? 3 : 1, off = vd & offmask; for (int i = 0; i < n; i++) D_[i] = off == (i & offmask) ? 1.0 : 0.0; }
                else for (int i = 0; i < n; i++) D_[i] = rt == 7 ? 1.0 : 0.0;
                dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
            }
            if (rt == 0 && PS == 0xE4 && (s->vc[CTRL_D] & 0xFF) == 0) {   /* vmov without source prefix: copy the bits as
                                                                    they are (a float round trip would change NaN patterns,
                                                                    and games keep packed colours in these registers) */
                int ix[4]; uint32_t b[4]; vec_idx(vs, n, ix); for (int i = 0; i < n; i++) b[i] = s->v[ix[i]];
                wr_bits(s, vd, b, n); eat(s); return 0;
            }
            rdv(s, vs, n, sv);
            if (rt == 1 || rt == 2) apply_st(rewrite(PS, 0, rt == 1 ? (0xFu << 8) : (0xFu << 16)), sv, n, 0.0, S_);
            else if (rt >= 16) {
                double one[4] = {sv[n - 1], 0, 0, 0}, o2[4]; apply_st(PS, one, 1, INFINITY, o2);
                for (int i = 0; i < 4; i++) S_[i] = sv[i];
                S_[n - 1] = o2[0];
            } else apply_st(PS, sv, n, 0.0, S_);
            for (int i = 0; i < n; i++) {
                double x = S_[i], y;
                switch (rt) {
                case 0: case 1: case 2: y = x; break;
                case 4: y = x <= 0 ? 0.0 : (x > 1 ? 1.0 : x); break;
                case 5: y = x < -1 ? -1.0 : (x > 1 ? 1.0 : x); break;
                case 16: y = vdiv(1.0, x); break;
                case 17: y = x > 0 ? vdiv(1.0, sqrt(x)) : (x == 0 ? INFINITY : NAN); break;
                case 18: y = sin(x * PI_D / 2); break;
                case 19: y = cos(x * PI_D / 2); break;
                case 20: y = pow(2.0, x); if (isinf(y) && !isinf(x)) y = NAN; break;
                case 21: y = x > 0 ? log2(x) : (x == 0 ? -INFINITY : NAN); break;
                case 22: y = x >= 0 ? sqrt(x) : NAN; break;
                case 23: y = (x >= -1 && x <= 1) ? asin(x) * 2 / PI_D : NAN; break;
                case 24: y = -vdiv(1.0, x); break;
                case 26: y = -sin(x * PI_D / 2); break;
                case 28: y = pow(2.0, -x); if (isinf(y) && !isinf(x)) y = NAN; break;
                default: return R_PYTHON;
                }
                if (isinf(x) && (rt == 18 || rt == 19 || rt == 26)) y = NAN;      /* python math.sin(inf) raises -> nan */
                D_[i] = rnd(y);
            }
            if (rt >= 16) last_only_d(s, n);
            dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
        }
        if (sub == 1) {
            int ix[4]; uint32_t b[4] = {0, 0, 0, 0}, sb[4], d[4] = {0, 0, 0, 0}; int on = n;
            if (rt == 19 || (rt >= 24 && rt <= 31)) { vec_idx(vs, n, ix); for (int i = 0; i < n; i++) b[i] = s->v[ix[i]]; }
            if (rt == 19) {                                  /* vh2f */
                st_bits(PS, b, n, n, sb);
                if (n == 1) { D_[0] = h2f(sb[0] & 0xFFFF); D_[1] = h2f(sb[0] >> 16); on = 2; }
                else { D_[0] = h2f(sb[0] & 0xFFFF); D_[1] = h2f(sb[0] >> 16); D_[2] = h2f(sb[1] & 0xFFFF); D_[3] = h2f(sb[1] >> 16); on = 4; }
                dsat(s, D_, on); wrv(s, vd, D_, on); eat(s); return 0;
            }
            if (rt == 18) {                                  /* vf2h */
                rdv(s, vs, n, sv); apply_st(PS, sv, 4, 0.0, S_);
                if (n <= 2) { d[0] = f2h(S_[0]) | f2h(S_[1]) << 16; on = 1; }
                else { d[0] = f2h(S_[0]) | f2h(S_[1]) << 16; d[1] = f2h(S_[2]) | f2h(S_[3]) << 16; on = 2; }
                wr_bits(s, vd, d, on); eat(s); return 0;
            }
            int sel = rt & 3;
            if (rt <= 27) {                                  /* vuc2i / vc2i / vus2i / vs2i */
                st_bits(PS, b, n, n, sb);
                if (sel == 0) { uint32_t v = sb[0]; for (int k = 0; k < 4; k++) { d[k] = ((v & 0xFF) * 0x01010101u) >> 1; v >>= 8; } on = 4; }
                else if (sel == 1) { uint32_t v = sb[0]; d[0] = (v & 0xFF) << 24; d[1] = (v & 0xFF00) << 16; d[2] = (v & 0xFF0000) << 8; d[3] = v & 0xFF000000u; on = 4; }
                else {
                    int m_ = n; on = 2; if (n == 3 || n == 4) m_ = 2; if (m_ == 2) on = 4;
                    for (int k = 0; k < m_; k++) {
                        uint32_t v = sb[k];
                        if (sel == 2) { d[2 * k] = (v & 0xFFFF) << 15; d[2 * k + 1] = (v & 0xFFFF0000u) >> 1; }
                        else { d[2 * k] = (v & 0xFFFF) << 16; d[2 * k + 1] = v & 0xFFFF0000u; }
                    }
                }
                wr_bits(s, vd, d, on); eat(s); return 0;
            }
            st_bits(PS, b, n, 4, sb);                        /* vi2uc / vi2c / vi2us / vi2s */
            if (sel == 0) { for (int k = 0; k < 4; k++) { int32_t v = (int32_t)sb[k]; if (v < 0) v = 0; d[0] |= (((uint32_t)v >> 23) & 0xFF) << (8 * k); } on = 1; }
            else if (sel == 1) { for (int k = 0; k < 4; k++) d[0] |= (sb[k] >> 24) << (8 * k); on = 1; }
            else {
                for (int k = 0; k < (n + 1) / 2; k++) {
                    uint32_t lo = sb[2 * k], hi = sb[2 * k + 1];
                    if (sel == 2) {
                        int32_t l = (int32_t)lo, h = (int32_t)hi; if (l < 0) l = 0; if (h < 0) h = 0;
                        uint32_t L2 = (uint32_t)l >> 15, H2 = (uint32_t)h >> 15; d[k] = (L2 & 0xFFFF) | ((H2 & 0xFFFF) << 16);
                    } else d[k] = (lo >> 16) | ((hi >> 16) << 16);
                }
                on = n >= 3 ? 2 : 1;
            }
            wr_bits(s, vd, d, on); eat(s); return 0;
        }
        if (sub == 2) {
            if (rt == 6 || rt == 7) {
                rdv(s, vs, n, sv); apply_st(PS, sv, n, 0.0, S_); double tot = 0.0;
                for (int i = 0; i < n; i++) tot = rnd(tot + S_[i]);
                if (rt == 7) tot = rnd(tot / n);
                D_[0] = tot; dsat(s, D_, 1); wrv(s, vd, D_, 1); eat(s); return 0;
            }
            if (rt == 4) {
                rdv(s, vs, n, sv); apply_st(rewrite(PS, 0, 0xFu << 16), sv, n, 0.0, S_);
                for (int i = 0; i < n; i++) D_[i] = rnd(1.0 + S_[i]);
                dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
            }
            if (rt == 10) {
                rdv(s, vs, n, sv); apply_st(PS, sv, n, 0.0, S_);
                for (int i = 0; i < n; i++) { double x = S_[i]; D_[i] = (x != x || fabs(x) < 1.1754943508222875e-38) ? 0.0 : (x > 0 ? 1.0 : -1.0); }
                dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
            }
            if (rt == 2 || rt == 3) {
                rdv(s, vs, n, sv);
                if (rt == 2) { D_[0] = sv[0] + sv[1]; D_[1] = sv[0] - sv[1]; D_[2] = sv[2] + sv[3]; D_[3] = sv[2] - sv[3]; }
                else { D_[0] = sv[0] + sv[2]; D_[1] = sv[1] + sv[3]; D_[2] = sv[0] - sv[2]; D_[3] = sv[1] - sv[3]; }
                for (int i = 0; i < 4; i++) D_[i] = rnd(D_[i]);
                dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
            }
            if (rt >= 25 && rt <= 27) {
                int ix[4]; uint32_t b[4], o[4], cols[4]; vec_idx(vs, 4, ix);
                for (int i = 0; i < 4; i++) b[i] = s->v[ix[i]];
                /* integer prefix (apply_st_bits) */
                if (PS == 0xE4) for (int i = 0; i < 4; i++) o[i] = b[i];
                else {
                    for (int i = 0; i < 4; i++) o[i] = b[i];
                    for (int i = 0; i < 4; i++) {
                        int sw = (PS >> (2 * i)) & 3, ab = (PS >> (8 + i)) & 1, cs = (PS >> (12 + i)) & 1, ng = (PS >> (16 + i)) & 1;
                        uint32_t x = cs ? fbits(PFX_CONST[sw + (ab << 2)]) : (ab ? (b[sw] & 0x7FFFFFFFu) : b[sw]);
                        o[i] = ng ? x ^ 0x80000000u : x;
                    }
                }
                for (int i = 0; i < 4; i++) {
                    uint32_t v = o[i], A = (v >> 24) & 0xFF, B = (v >> 16) & 0xFF, G = (v >> 8) & 0xFF, R = v & 0xFF;
                    cols[i] = rt == 25 ? ((A >> 4) << 12 | (B >> 4) << 8 | (G >> 4) << 4 | (R >> 4))
                            : rt == 26 ? ((A >> 7) << 15 | (B >> 3) << 10 | (G >> 3) << 5 | (R >> 3))
                                       : ((B >> 3) << 11 | (G >> 2) << 5 | (R >> 3));
                }
                uint32_t out[2] = {cols[0] | cols[1] << 16, cols[2] | cols[3] << 16};
                wr_bits(s, vd, out, n == 1 ? 1 : 2); eat(s); return 0;
            }
            if (rt == 16) { int imm = (ins >> 8) & 0x7F, ix[1]; vec_idx(vd, 1, ix); s->v[ix[0]] = imm < 16 ? s->vc[imm] : 0; return 0; }
            if (rt == 17) { int imm = ins & 0x7F, ix[1]; if (imm < 16) { vec_idx(vs, 1, ix); s->vc[imm] = s->v[ix[0]]; } return 0; }
            return R_PYTHON;
        }
        if (sub == 3) {
            double c = rt < 20 ? CSTV[rt] : 0.0;
            for (int i = 0; i < n; i++) D_[i] = rnd(c);
            dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
        }
        if (sub >= 16 && sub <= 20) {
            int imm = (ins >> 16) & 31, ix[4]; vec_idx(vs, n, ix);
            if (sub == 20) {
                for (int i = 0; i < n; i++) sv[i] = (double)(int32_t)s->v[ix[i]];
                for (int i = n; i < 4; i++) sv[i] = 0.0;
                apply_st(PS, sv, n, 0.0, S_);
                for (int i = 0; i < n; i++) D_[i] = rnd(S_[i] / (double)(1u << imm));
                dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
            }
            rdv(s, vs, n, sv); apply_st(PS, sv, n, 0.0, S_); uint32_t out[4];
            for (int i = 0; i < n; i++) {
                double x = S_[i];
                if (x != x) { out[i] = 0x7FFFFFFF; continue; }
                double v = x * (double)(1u << imm);
                if (v >= 2147483647.0) out[i] = 0x7FFFFFFF;
                else if (v <= -2147483648.0) out[i] = 0x80000000u;
                else {
                    double iv;
                    if (sub == 16) iv = floor(v + 0.5);
                    else if (sub == 17) iv = trunc(v);
                    else if (sub == 18) iv = ceil(v);
                    else iv = floor(v);
                    out[i] = (uint32_t)(int64_t)iv;
                }
            }
            wr_bits(s, vd, out, n); eat(s); return 0;
        }
        if (sub == 21) {                                  /* vcmov */
            int tf = (ins >> 19) & 1, imm3 = (ins >> 16) & 7; double dv[4];
            rdv(s, vs, n, sv); apply_st(PS, sv, n, 0.0, S_); rdv(s, vd, n, dv); apply_st(PT, dv, n, 0.0, D_);
            uint32_t cc = s->vc[CTRL_CC];
            if (imm3 == 6) { for (int i = 0; i < n; i++) if ((int)((cc >> i) & 1) == !tf) D_[i] = S_[i]; }
            else if ((int)((cc >> imm3) & 1) == !tf) for (int i = 0; i < n; i++) D_[i] = S_[i];
            dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
        }
        return R_PYTHON;
    }
    if (op == 0x3C) {
        uint32_t sub = (ins >> 21) & 31; double sm[16], tm[16], dm[16];
        if (sub < 4) {                                    /* vmmul */
            rdm(s, vs, n, sm); rdm(s, vt, n, tm);
            for (int k = 0; k < 16; k++) dm[k] = 0.0;
            for (int a = 0; a < n; a++) for (int b = 0; b < n; b++) {
                double sa[4], ta[4];
                if (a == n - 1 && b == n - 1) { apply_st(PS, sm + b * 4, 4, 0.0, sa); apply_st(PT, tm + a * 4, 4, 0.0, ta); }
                else { memcpy(sa, sm + b * 4, sizeof sa); memcpy(ta, tm + a * 4, sizeof ta); }
                dm[a * 4 + b] = dot(sa, ta, n);
            }
            last_only_d(s, n);
            dsat(s, dm + 4 * (n - 1), n);
            wrm(s, vd, dm, n); eat(s); return 0;
        }
        if (sub < 16) {                                   /* vtfm / vhtfm */
            int ins2 = (ins >> 23) & 3, msz = ins2 + 1; double tv[4], t2[4], sl[4], tp[4];
            rdm(s, vs, msz, sm); rdv(s, vt, msz, tv);
            int tn = n < ins2 + 1 ? n : ins2 + 1;
            for (int i = 0; i < 4; i++) t2[i] = i < tn ? tv[i] : (i == ins2 ? 1.0 : 0.0);
            for (int i = 0; i < 4; i++) D_[i] = 0.0;
            for (int i = 0; i < ins2; i++) D_[i] = dot(sm + i * 4, t2, msz);
            apply_st(PS, sm + ins2 * 4, 4, 0.0, sl);
            uint32_t remove = SWZ(0, n < 2 ? 3 : 0, n < 3 ? 3 : 0, n < 4 ? 3 : 0), add = 0;
            for (int lane = 1; lane < 4; lane++) {
                int c = -1;
                if (n < lane + 1) c = 0;
                if (ins2 >= n && ins2 == lane) c = 1;
                if (c >= 0) add |= consts1(lane, c);
            }
            apply_st(rewrite(PT, remove, add), tv, 4, 0.0, tp);
            D_[ins2] = dot(sl, tp, msz);
            last_only_d(s, msz);
            dsat(s, D_, msz); wrv(s, vd, D_, msz); eat(s); return 0;
        }
        if (sub < 20) {                                   /* vmscl */
            int ix[1]; rdm(s, vs, n, sm); vec_idx(vt, 1, ix); double sc = vf(s->v[ix[0]]);
            for (int k = 0; k < 16; k++) dm[k] = 0.0;
            for (int a = 0; a < n; a++) for (int b = 0; b < n; b++) dm[a * 4 + b] = rnd(sm[a * 4 + b] * sc);
            wrm(s, vd, dm, n); eat(s); return 0;
        }
        if (sub < 24) {                                   /* vcrsp.t / vqmul.q */
            double sv[4], tv[4], q[4], tp[4], ss[4];
            rdv(s, vs, n, sv); rdv(s, vt, n, tv);
            for (int i = 0; i < 4; i++) D_[i] = 0.0;
            if (n == 3) {
                q[0] = 0.0; q[1] = tv[2]; q[2] = -tv[1]; q[3] = 0.0; D_[0] = dot(sv, q, 4);
                q[0] = -tv[2]; q[1] = 0.0; q[2] = tv[0]; q[3] = 0.0; D_[1] = dot(sv, q, 4);
                apply_st(rewrite(PT, 0xFF | (0xFu << 16), SWZ(1, 0, 3, 2) | (2u << 16)), tv, 4, 0.0, tp);
                apply_st(PS, sv, 4, 0.0, ss); D_[2] = dot(ss, tp, 4);
            } else if (n == 4) {
                q[0] = tv[3]; q[1] = tv[2]; q[2] = -tv[1]; q[3] = tv[0]; D_[0] = dot(sv, q, 4);
                q[0] = -tv[2]; q[1] = tv[3]; q[2] = tv[0]; q[3] = tv[1]; D_[1] = dot(sv, q, 4);
                q[0] = tv[1]; q[1] = -tv[0]; q[2] = tv[3]; q[3] = tv[2]; D_[2] = dot(sv, q, 4);
                apply_st(rewrite(PT, 0xFF | (0xFu << 16), SWZ(0, 1, 2, 3) | (7u << 16)), tv, 4, 0.0, tp);
                apply_st(PS, sv, 4, 0.0, ss); D_[3] = dot(ss, tp, 4);
            }
            if (n > 1) last_only_d(s, n);
            dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
        }
        if (sub == 29) {                                  /* vrot */
            int imm = (ins >> 16) & 31, neg = (imm & 16) != 0, sl = (imm >> 2) & 3, cl = imm & 3, ix[1];
            vec_idx(vs, 1, ix); double x = vf(s->v[ix[0]]);
            double sine = sin(x * PI_D / 2), cosine = cos(x * PI_D / 2);
            if (isinf(x)) { sine = NAN; cosine = NAN; }
            if (neg) sine = -sine;
            for (int i = 0; i < 4; i++) D_[i] = 0.0;
            if (sl == cl) for (int i = 0; i < 4; i++) D_[i] = sine; else D_[sl] = sine;
            D_[cl] = cosine;
            for (int i = 0; i < 4; i++) D_[i] = rnd(D_[i]);
            dsat(s, D_, n); wrv(s, vd, D_, n); eat(s); return 0;
        }
        if (sub == 28) {
            int k = (ins >> 16) & 15;
            if (k == 0) {                                 /* vmmov: copy the bits as they are */
                int ixs[16], ixd[16]; uint32_t b[16]; mat_idx(vs, n, ixs); mat_idx(vd, n, ixd);
                for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) b[j * 4 + i] = s->v[ixs[j * 4 + i]];
                for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) s->v[ixd[j * 4 + i]] = b[j * 4 + i];
                eat(s); return 0;
            }
            for (int i = 0; i < 16; i++) dm[i] = 0.0;
            for (int a = 0; a < n; a++) for (int b = 0; b < n; b++) dm[a * 4 + b] = k == 3 ? (a == b ? 1.0 : 0.0) : (k == 7 ? 1.0 : 0.0);
            wrm(s, vd, dm, n); eat(s); return 0;
        }
        return R_PYTHON;
    }
    (void)pc; (void)tmp;
    return R_PYTHON;
}

#ifdef PROF_OPS
EXPORT void core_ops(uint64_t *o) { memcpy(o, g_ops, sizeof g_ops); }
EXPORT void core_vops(uint64_t *o) { memcpy(o, g_vops, sizeof g_vops); }
#endif
EXPORT uint32_t core_vdirty(void) { uint32_t d = g_vdirty; g_vdirty = 0; return d; }   /* and clears it */
EXPORT int core_version(void) { return 4; }
EXPORT int core_state_size(void) { return (int)sizeof(State); }

/* "can the delay-slot instruction run here" and "is this VFPU instruction done here", remembered per instruction word */
static uint32_t g_okkey[4096]; static uint8_t g_okval[4096];
static inline int ds_ok(uint32_t ds) {
    uint32_t h = (ds * 2654435761u) >> 20;
    if (g_okkey[h] == ds && g_okval[h]) return g_okval[h] - 1;
    int v = native_ok(ds) || (is_vfpu_op(ds >> 26) && vfpu_supported(ds));
    g_okkey[h] = ds; g_okval[h] = (uint8_t)(v + 1); return v;
}
static uint8_t g_isv[64];                                   /* 1 = VFPU opcode */
static int g_isv_ready;

/* ----- decoded-instruction cache: the most common plain instructions are taken apart once and then run from a
 * single switch. Each entry remembers the instruction word it was made from, so code that is loaded or changed
 * later is simply decoded again. Everything else goes the general way (exec_simple / vfpu_exec / branches). */
enum { H_GEN = 0, H_NOP, H_ADDIU, H_ADDU, H_SUBU, H_AND, H_OR, H_XOR, H_NOR, H_SLT, H_SLTU, H_ANDI, H_ORI, H_XORI,
       H_LUI, H_SLTI, H_SLTIU, H_SLL, H_SRL, H_SRA, H_SLLV, H_SRLV, H_SRAV, H_MOVZ, H_MOVN, H_MFHI, H_MFLO,
       H_LW, H_SW, H_LBU, H_LB, H_LHU, H_LH, H_SB, H_SH, H_LWC1, H_SWC1, H_MFC1, H_MTC1, H_SEB, H_SEH,
       H_EXT, H_INS, H_MAX, H_MIN, H_MULT, H_MULTU };
typedef struct { uint32_t pc, ins; uint8_t h, rs, rt, rd; uint8_t sa, pad[3]; uint32_t imm; } DI;
#define DC_BITS 16
static DI g_dc[1 << DC_BITS];
static void dc_decode(DI *e, uint32_t pc, uint32_t ins) {
    uint32_t op = ins >> 26, rs = (ins >> 21) & 31, rt = (ins >> 16) & 31, rd = (ins >> 11) & 31, sa = (ins >> 6) & 31, fn = ins & 63;
    uint32_t simm = (uint32_t)(int32_t)(int16_t)(ins & 0xFFFF), zimm = ins & 0xFFFF; int h = H_GEN; uint32_t imm = simm;
    switch (op) {
    case 0:
        switch (fn) {
        case 0x00: h = (rd == 0) ? H_NOP : H_SLL; break;
        case 0x02: if (rs == 0) h = H_SRL; break;                 /* rotr stays general */
        case 0x03: h = H_SRA; break;
        case 0x04: h = H_SLLV; break;
        case 0x06: if (sa == 0) h = H_SRLV; break;
        case 0x07: h = H_SRAV; break;
        case 0x0A: h = H_MOVZ; break;
        case 0x0B: h = H_MOVN; break;
        case 0x10: h = H_MFHI; break;
        case 0x12: h = H_MFLO; break;
        case 0x18: h = H_MULT; break;
        case 0x19: h = H_MULTU; break;
        case 0x20: case 0x21: h = H_ADDU; break;
        case 0x22: case 0x23: h = H_SUBU; break;
        case 0x24: h = H_AND; break;
        case 0x25: h = H_OR; break;
        case 0x26: h = H_XOR; break;
        case 0x27: h = H_NOR; break;
        case 0x2A: h = H_SLT; break;
        case 0x2B: h = H_SLTU; break;
        case 0x2C: h = H_MAX; break;
        case 0x2D: h = H_MIN; break;
        }
        break;
    case 8: case 9: h = H_ADDIU; break;
    case 0xA: h = H_SLTI; break;
    case 0xB: h = H_SLTIU; break;
    case 0xC: h = H_ANDI; imm = zimm; break;
    case 0xD: h = H_ORI; imm = zimm; break;
    case 0xE: h = H_XORI; imm = zimm; break;
    case 0xF: h = H_LUI; imm = zimm << 16; break;
    case 0x20: h = H_LB; break;
    case 0x21: h = H_LH; break;
    case 0x23: h = H_LW; break;
    case 0x24: h = H_LBU; break;
    case 0x25: h = H_LHU; break;
    case 0x28: h = H_SB; break;
    case 0x29: h = H_SH; break;
    case 0x2B: h = H_SW; break;
    case 0x31: h = H_LWC1; break;
    case 0x39: h = H_SWC1; break;
    case 0x11: if (rs == 0) h = H_MFC1; else if (rs == 4) h = H_MTC1; break;
    case 0x1F:
        if (fn == 0) h = H_EXT;
        else if (fn == 4 && rd >= sa) h = H_INS;
        else if (fn == 0x20 && sa == 0x10) h = H_SEB;
        else if (fn == 0x20 && sa == 0x18) h = H_SEH;
        break;
    }
    e->pc = pc; e->ins = ins; e->h = (uint8_t)h; e->rs = (uint8_t)rs; e->rt = (uint8_t)rt; e->rd = (uint8_t)rd; e->sa = (uint8_t)sa; e->imm = imm;
}
/* data address -> host pointer; RAM first (almost always) */
static inline uint8_t *dloc(State *s, uint32_t a, uint32_t n) {
    uint32_t off = (a & 0x3FFFFFFF) - RAM_LO;
    if (off <= RAM_HI - RAM_LO - n) return s->ram + off;
    return loc(s, a, n);
}

/* run up to n instructions. returns an R_ code; s->pc is where Python should continue. */
EXPORT int core_run(State *s, int64_t n) {
    uint64_t ic = s->icount, end = ic + (uint64_t)n;
    uint32_t *r = s->r, pc = s->pc;
    uint8_t *ram = s->ram; const uint8_t *stubmap = s->stubmap; const uint32_t halt = s->halt;
    int code = R_BUDGET;
    if (!g_isv_ready) { for (int i = 0; i < 64; i++) g_isv[i] = (uint8_t)is_vfpu_op((uint32_t)i); g_isv_ready = 1; }
#define LEAVE(c) do { code = (c); goto out; } while (0)
    while (ic < end) {
        g_curpc = pc;
        if (pc == halt) LEAVE(R_HALT);
        uint32_t off = (pc & 0x3FFFFFFF) - RAM_LO, ins;
        if (off < RAM_HI - RAM_LO - 3) {                    /* the usual case: code in RAM */
            if (stubmap[off >> 2]) LEAVE(R_STUB);
            ins = rd32(ram + off);
        } else {
            uint8_t *p = loc(s, pc, 4);
            if (!p) { s->fault_addr = pc; LEAVE(R_BADMEM); }
            ins = rd32(p);
        }
#ifdef PROF_OPS
        g_ops[ins >> 21]++;
#endif
        DI *de = &g_dc[(pc >> 2) & ((1 << DC_BITS) - 1)];
        if (de->pc != pc || de->ins != ins) dc_decode(de, pc, ins);
        if (de->h != H_GEN) {
            uint32_t *R = r, a; uint8_t *p;
            switch (de->h) {
            case H_NOP: break;
            case H_ADDIU: R[de->rt] = R[de->rs] + de->imm; break;
            case H_ADDU: R[de->rd] = R[de->rs] + R[de->rt]; break;
            case H_SUBU: R[de->rd] = R[de->rs] - R[de->rt]; break;
            case H_AND: R[de->rd] = R[de->rs] & R[de->rt]; break;
            case H_OR: R[de->rd] = R[de->rs] | R[de->rt]; break;
            case H_XOR: R[de->rd] = R[de->rs] ^ R[de->rt]; break;
            case H_NOR: R[de->rd] = ~(R[de->rs] | R[de->rt]); break;
            case H_SLT: R[de->rd] = (int32_t)R[de->rs] < (int32_t)R[de->rt]; break;
            case H_SLTU: R[de->rd] = R[de->rs] < R[de->rt]; break;
            case H_ANDI: R[de->rt] = R[de->rs] & de->imm; break;
            case H_ORI: R[de->rt] = R[de->rs] | de->imm; break;
            case H_XORI: R[de->rt] = R[de->rs] ^ de->imm; break;
            case H_LUI: R[de->rt] = de->imm; break;
            case H_SLTI: R[de->rt] = (int32_t)R[de->rs] < (int32_t)de->imm; break;
            case H_SLTIU: R[de->rt] = R[de->rs] < de->imm; break;
            case H_SLL: R[de->rd] = R[de->rt] << de->sa; break;
            case H_SRL: R[de->rd] = R[de->rt] >> de->sa; break;
            case H_SRA: R[de->rd] = (uint32_t)((int32_t)R[de->rt] >> de->sa); break;
            case H_SLLV: R[de->rd] = R[de->rt] << (R[de->rs] & 31); break;
            case H_SRLV: R[de->rd] = R[de->rt] >> (R[de->rs] & 31); break;
            case H_SRAV: R[de->rd] = (uint32_t)((int32_t)R[de->rt] >> (R[de->rs] & 31)); break;
            case H_MOVZ: if (R[de->rt] == 0) R[de->rd] = R[de->rs]; break;
            case H_MOVN: if (R[de->rt] != 0) R[de->rd] = R[de->rs]; break;
            case H_MFHI: R[de->rd] = s->hi; break;
            case H_MFLO: R[de->rd] = s->lo; break;
            case H_MAX: R[de->rd] = ((int32_t)R[de->rs] > (int32_t)R[de->rt]) ? R[de->rs] : R[de->rt]; break;
            case H_MIN: R[de->rd] = ((int32_t)R[de->rs] < (int32_t)R[de->rt]) ? R[de->rs] : R[de->rt]; break;
            case H_MULT: { int64_t m = (int64_t)(int32_t)R[de->rs] * (int64_t)(int32_t)R[de->rt]; s->lo = (uint32_t)m; s->hi = (uint32_t)((uint64_t)m >> 32); break; }
            case H_MULTU: { uint64_t m = (uint64_t)R[de->rs] * (uint64_t)R[de->rt]; s->lo = (uint32_t)m; s->hi = (uint32_t)(m >> 32); break; }
            case H_SEB: R[de->rd] = (uint32_t)(int32_t)(int8_t)(R[de->rt] & 0xFF); break;
            case H_SEH: R[de->rd] = (uint32_t)(int32_t)(int16_t)(R[de->rt] & 0xFFFF); break;
            case H_EXT: { uint32_t size = de->rd + 1u; R[de->rt] = size >= 32 ? (R[de->rs] >> de->sa) : ((R[de->rs] >> de->sa) & ((1u << size) - 1)); break; }
            case H_INS: { uint32_t lsb = de->sa, size = de->rd - lsb + 1u;
                          uint32_t mask = (size >= 32 ? 0xFFFFFFFFu : ((1u << size) - 1)) << lsb;
                          R[de->rt] = (R[de->rt] & ~mask) | ((R[de->rs] << lsb) & mask); break; }
            case H_MFC1: R[de->rt] = s->f[de->rd]; break;
            case H_MTC1: s->f[de->rd] = R[de->rt]; break;
#define DADDR(n) a = R[de->rs] + de->imm; if (!(p = dloc(s, a, n))) { s->fault_addr = a; LEAVE(R_BADMEM); }
            case H_LW: DADDR(4) R[de->rt] = rd32(p); break;
            case H_SW: DADDR(4) wr32(p, R[de->rt]); break;
            case H_LBU: DADDR(1) R[de->rt] = p[0]; break;
            case H_LB: DADDR(1) R[de->rt] = (uint32_t)(int32_t)(int8_t)p[0]; break;
            case H_LHU: DADDR(2) R[de->rt] = rd16(p); break;
            case H_LH: DADDR(2) R[de->rt] = (uint32_t)(int32_t)(int16_t)rd16(p); break;
            case H_SB: DADDR(1) wcheck(p, 1, R[de->rt]); p[0] = (uint8_t)R[de->rt]; break;
            case H_SH: DADDR(2) wr16(p, (uint16_t)R[de->rt]); break;
            case H_LWC1: DADDR(4) s->f[de->rt] = rd32(p); break;
            case H_SWC1: DADDR(4) wr32(p, s->f[de->rt]); break;
#undef DADDR
            }
            R[0] = 0; ic++; pc += 4; continue;
        }
        uint32_t op = ins >> 26;
        if (op >= 8 && op != 0x11 && op != 0x12 && !(op >= 0x14 && op <= 0x17)) {   /* not a branch: run it */
            int e = g_isv[op] ? vfpu_exec(s, ins, pc) : exec_simple(s, ins, pc);
            if (e) LEAVE(e);
            ic++; pc += 4; continue;
        }
        uint32_t rs = (ins >> 21) & 31, rt = (ins >> 16) & 31, rd = (ins >> 11) & 31, fn = ins & 63;
        int32_t simm = (int16_t)(ins & 0xFFFF);
        int is_branch = 0, taken = 0, likely = 0, link = 0; uint32_t target = 0, linkreg = 31;
        switch (op) {
        case 0:
            if (fn == 0x08) { is_branch = 1; taken = 1; target = r[rs]; }
            else if (fn == 0x09) { is_branch = 1; taken = 1; target = r[rs]; link = 1; linkreg = rd; }
            break;
        case 1:
            if (rt == 0 || rt == 1 || rt == 2 || rt == 3 || rt == 0x10 || rt == 0x11) {
                int32_t v = (int32_t)r[rs]; is_branch = 1; target = pc + 4 + ((uint32_t)simm << 2);
                taken = (rt & 1) ? (v >= 0) : (v < 0); likely = (rt == 2 || rt == 3); link = (rt >= 0x10);
            } else LEAVE(R_PYTHON);
            break;
        case 2: case 3:
            is_branch = 1; taken = 1; target = ((pc + 4) & 0xF0000000u) | ((ins & 0x3FFFFFF) << 2); link = (op == 3); break;
        case 4: case 5: case 6: case 7: case 0x14: case 0x15: case 0x16: case 0x17: {
            int32_t a = (int32_t)r[rs], b = (int32_t)r[rt]; uint32_t k = op & 7;
            is_branch = 1; target = pc + 4 + ((uint32_t)simm << 2); likely = op >= 0x14;
            taken = k == 4 ? a == b : k == 5 ? a != b : k == 6 ? a <= 0 : a > 0; break; }
        case 0x12:
            if (rs == 8) {
                uint32_t t = (ins >> 16) & 3, ccbit = (s->vc[CTRL_CC] >> ((ins >> 18) & 7)) & 1;
                is_branch = 1; likely = (t & 2) != 0; target = pc + 4 + ((uint32_t)simm << 2);
                taken = (t == 1 || t == 3) ? ccbit == 1 : ccbit == 0;
            }
            break;
        case 0x11:
            if (rs == 8) {
                uint32_t t = rt & 1; is_branch = 1; likely = (rt & 2) != 0; target = pc + 4 + ((uint32_t)simm << 2);
                taken = (s->cc != 0) == (t == 1);
            }
            break;
        }
        if (is_branch) {
            uint32_t ds = 0;
            int need_ds = !(likely && !taken);
            if (need_ds) {
                uint32_t doff = ((pc + 4) & 0x3FFFFFFF) - RAM_LO;
                if (doff < RAM_HI - RAM_LO - 3) ds = rd32(ram + doff);
                else { uint8_t *q = loc(s, pc + 4, 4); if (!q) LEAVE(R_PYTHON); ds = rd32(q); }
                if (!ds_ok(ds)) LEAVE(R_PYTHON);             /* Python does this branch */
            }
            ic++;
            if (link) r[linkreg] = pc + 8;
            r[0] = 0;
            if (!need_ds) { pc += 8; continue; }
            ic++;
            int e = g_isv[ds >> 26] ? vfpu_exec(s, ds, pc + 4) : exec_simple(s, ds, pc + 4);
            if (e) { ic -= 2; LEAVE(e); }                   /* should not happen (checked above) */
            pc = taken ? target : pc + 8;
            continue;
        }
        if (op == 0x11 && rs == 8) LEAVE(R_PYTHON);
        int e = g_isv[op] ? vfpu_exec(s, ins, pc) : exec_simple(s, ins, pc);
        if (e) LEAVE(e);
        ic++; pc += 4;
    }
out:
#undef LEAVE
    s->pc = pc; s->icount = ic;
    return code;
}

/* watch RAM [lo, hi) for writes by game code; core_hits() hands back (pc, value, address) of up to 16 writes */
EXPORT void core_watch(State *s, uint32_t lo, uint32_t hi) {
    lo &= 0x3FFFFFFF; hi &= 0x3FFFFFFF;
    if (lo >= RAM_LO && hi <= RAM_HI && lo < hi) { g_wlo = s->ram + (lo - RAM_LO); g_whi = s->ram + (hi - RAM_LO); } else g_wlo = g_whi = 0;
    g_hits = 0;
}
EXPORT uint32_t core_hits(State *s, uint32_t *pcs, uint32_t *vals, uint32_t *addrs) {
    uint32_t n = g_hits;
    for (uint32_t i = 0; i < n; i++) { pcs[i] = g_hitpc[i]; vals[i] = g_hitval[i]; addrs[i] = (uint32_t)(g_hitp[i] - s->ram) + RAM_LO; }
    g_hits = 0; return n;
}
