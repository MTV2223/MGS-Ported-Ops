/*
 * psp_gpu.c - fast drawing for the PSP graphics engine (GE), for the MGS Portable Ops PC port.
 *
 * psp_ge.py reads the game's display lists; for every "draw" command it hands the work to ge_prim() here,
 * which fetches the vertices, transforms them and fills the pixels (textures, colours, blending, depth).
 * Same results as the Python drawing code in psp_ge.py, pixel for pixel, just much faster.
 * Built together with psp_core.c (see build_core.bat).
 */
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#define GEXPORT __declspec(dllexport)
#else
#define GEXPORT __attribute__((visibility("default")))
#endif

typedef struct {
    uint32_t reg[256];
    double world[12], view[12], proj[16], bones[96];
    uint32_t vaddr, iaddr;
    uint32_t clut_len, through_dummy;
    uint8_t clut[2048];
    uint8_t *ram, *vram, *scr;
    uint64_t pixels;
    uint32_t err_addr, pad;
    uint8_t *zram;          /* depth buffer: kept apart from VRAM, like PC emulators do (see psp_ge.py) */
} GEState;

static inline uint8_t *gloc(GEState *g, uint32_t a, uint32_t n) {
    a &= 0x3FFFFFFF;
    if (a >= 0x08000000u && a + n <= 0x0A000000u) return g->ram + (a - 0x08000000u);
    if (a >= 0x04000000u && a + n <= 0x04200000u) return g->vram + (a - 0x04000000u);
    if (a >= 0x00010000u && a + n <= 0x00014000u) return g->scr + (a - 0x00010000u);
    return 0;
}
static inline double gf32(uint32_t d24) { uint32_t b = (d24 << 8); float f; memcpy(&f, &b, 4); return (double)f; }
static inline uint32_t rd32le(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint16_t rd16le(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

typedef struct { int r, g, b, a; } Col;
typedef struct { double x, y, z, w; double u, v; int has_uv; Col c; int ok; double fog; int src; int gh; } Vert;   /* gh: not ok, but only behind the camera (kept for the 60 fps in-between pictures) */   /* fog: 1 = none, 0 = all fog colour; src: which vertex of the draw (60 fps mode) */

static inline Col unpack_color(uint32_t v, int fmt) {
    Col c;
    if (fmt == 3) { c.r = v & 255; c.g = (v >> 8) & 255; c.b = (v >> 16) & 255; c.a = (v >> 24) & 255; return c; }
    if (fmt == 0) { int r = v & 31, g = (v >> 5) & 63, b = (v >> 11) & 31; c.r = (r << 3) | (r >> 2); c.g = (g << 2) | (g >> 4); c.b = (b << 3) | (b >> 2); c.a = 255; return c; }
    if (fmt == 1) { int r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31; c.r = (r << 3) | (r >> 2); c.g = (g << 3) | (g >> 2); c.b = (b << 3) | (b >> 2); c.a = (v & 0x8000) ? 255 : 0; return c; }
    c.r = (v & 15) * 17; c.g = ((v >> 4) & 15) * 17; c.b = ((v >> 8) & 15) * 17; c.a = ((v >> 12) & 15) * 17; return c;
}
static inline uint32_t pack_color(int r, int g, int b, int a, int fmt) {
    if (fmt == 3) return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
    if (fmt == 0) return (r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11);
    if (fmt == 1) return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | (a >= 128 ? 0x8000 : 0);
    return (r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8) | ((a >> 4) << 12);
}
static inline int clamp255d(double x) { return x < 0 ? 0 : x > 255 ? 255 : (int)x; }

/* ------------------------------------------------------------------ vertex layout */
typedef struct { int tc, col, nrm, pos, wf, idx, wc, through, woff, toff, coff, noff, poff, size; } Layout;
static const int ELEM[4] = {0, 1, 2, 4};
static inline int al(int o, int a) { return (o + a - 1) / a * a; }
static void layout(uint32_t vt, Layout *L) {
    L->tc = vt & 3; L->col = (vt >> 2) & 7; L->nrm = (vt >> 5) & 3; L->pos = (vt >> 7) & 3; L->wf = (vt >> 9) & 3;
    L->idx = (vt >> 11) & 3; L->wc = ((vt >> 14) & 7) + 1; L->through = (vt >> 23) & 1;
    int off = 0, mx = 1, s;
    L->woff = L->toff = L->coff = L->noff = L->poff = -1;
    if (L->wf) { s = ELEM[L->wf]; off = al(off, s); L->woff = off; off += s * L->wc; if (s > mx) mx = s; }
    if (L->tc) { s = ELEM[L->tc]; off = al(off, s); L->toff = off; off += s * 2; if (s > mx) mx = s; }
    if (L->col) { s = L->col == 7 ? 4 : 2; off = al(off, s); L->coff = off; off += s; if (s > mx) mx = s; }
    if (L->nrm) { s = ELEM[L->nrm]; off = al(off, s); L->noff = off; off += s * 3; if (s > mx) mx = s; }
    if (L->pos) { s = ELEM[L->pos]; off = al(off, s); L->poff = off; off += s * 3; if (s > mx) mx = s; }
    L->size = al(off, mx);
}

static void mul43(const double *m, double x, double y, double z, double *o) {
    o[0] = x * m[0] + y * m[3] + z * m[6] + m[9];
    o[1] = x * m[1] + y * m[4] + z * m[7] + m[10];
    o[2] = x * m[2] + y * m[5] + z * m[8] + m[11];
}

static void project_raw(GEState *g, double x, double y, double z, Vert *v);
/* ------------------------------------------------------------------ hardware lighting (per vertex, like the GE)
 * colour = emissive + material ambient x ambient light + for each light: (its ambient x material ambient
 * + its diffuse x material diffuse x N.L + its specular x material specular x (N.H)^power) x attenuation x spot.
 * Material colours come from the vertex colour where the game says so (material update flags), else registers. */
typedef struct { double r, g, b, a; } Cf;
static inline Cf cf_rgb(uint32_t v, double a) { Cf c = {(v & 255) / 255.0, ((v >> 8) & 255) / 255.0, ((v >> 16) & 255) / 255.0, a}; return c; }
static inline double vlen(const double *v) { return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
static double gpow(double v, double e) { if (!(v > 0)) return e == 0 ? 1.0 : 0.0; return pow(v, e); }
static void light_core(GEState *g, const double *n, double x, double y, double z, int has, Vert *v);
static int g_no_light;
static void light_vertex(GEState *g, const Layout *L, const uint8_t *p, double x, double y, double z, Vert *v) {
    /* normal: decoded, skinned like the position, into world space */
    double n[3] = {0, 0, 1};
    if (L->nrm) {
        const uint8_t *q = p + L->noff;
        if (L->nrm == 3) { float f[3]; memcpy(f, q, 12); n[0] = f[0]; n[1] = f[1]; n[2] = f[2]; }
        else if (L->nrm == 2) { n[0] = (int16_t)rd16le(q) / 32768.0; n[1] = (int16_t)rd16le(q + 2) / 32768.0; n[2] = (int16_t)rd16le(q + 4) / 32768.0; }
        else { n[0] = (int8_t)q[0] / 128.0; n[1] = (int8_t)q[1] / 128.0; n[2] = (int8_t)q[2] / 128.0; }
        if (L->wf) {
            double a[3] = {0, 0, 0}; const uint8_t *wq = p + L->woff;
            for (int i = 0; i < L->wc; i++) {
                double wgt;
                if (L->wf == 3) { float f; memcpy(&f, wq + 4 * i, 4); wgt = f; }
                else if (L->wf == 2) wgt = rd16le(wq + 2 * i) / 32768.0;
                else wgt = wq[i] / 128.0;
                if (wgt == 0.0) continue;
                const double *m = g->bones + i * 12;
                a[0] += (n[0] * m[0] + n[1] * m[3] + n[2] * m[6]) * wgt; a[1] += (n[0] * m[1] + n[1] * m[4] + n[2] * m[7]) * wgt;
                a[2] += (n[0] * m[2] + n[1] * m[5] + n[2] * m[8]) * wgt;
            }
            n[0] = a[0]; n[1] = a[1]; n[2] = a[2];
        }
    }
    light_core(g, n, x, y, z, L->col != 0, v);
}
/* What stays the same for every point of one draw - the lights, the material, the direction of view - is worked out
 * once per draw (it used to be for every point: a third of the time of drawing a frame full of soldiers). The sums
 * per point are the same ones, in the same order: the colours come out the same to the last bit. */
typedef struct { int on, type, comp, spot_on; double lp[3], Ld[3], at[3], sd[3], sl, cut, sexp; Cf la, ld, ls; } LightK;
static struct { uint32_t gen; const GEState *g; int ok; Cf mac, mdc, msc, amb, em; double spe, vd[3]; LightK l[4]; } g_ltk;
static uint32_t g_lk_gen = 1;
static void light_consts(GEState *g) {
    const uint32_t *r = g->reg;
    g_ltk.mac = cf_rgb(r[0x55], (r[0x58] & 255) / 255.0); g_ltk.mdc = cf_rgb(r[0x56], 1.0); g_ltk.msc = cf_rgb(r[0x57], 1.0);
    g_ltk.amb = cf_rgb(r[0x5C], (r[0x5D] & 255) / 255.0); g_ltk.em = cf_rgb(r[0x54], 0.0);
    g_ltk.spe = gf32(r[0x5B]);
    double *vd = g_ltk.vd; vd[0] = g->view[2]; vd[1] = g->view[5]; vd[2] = g->view[8]; double vl = vlen(vd);
    if (vl > 0) { vd[0] /= vl; vd[1] /= vl; vd[2] /= vl; } else { vd[0] = vd[1] = 0; vd[2] = 1; }
    for (int i = 0; i < 4; i++) {
        LightK *k = &g_ltk.l[i]; k->on = (int)(r[0x18 + i] & 1); if (!k->on) continue;
        k->type = (r[0x5F + i] >> 8) & 3; k->comp = r[0x5F + i] & 3;
        k->lp[0] = gf32(r[0x63 + 3 * i]); k->lp[1] = gf32(r[0x64 + 3 * i]); k->lp[2] = gf32(r[0x65 + 3 * i]);
        if (k->type == 0) { double l = vlen(k->lp); k->Ld[0] = l > 0 ? k->lp[0] / l : 0; k->Ld[1] = l > 0 ? k->lp[1] / l : 0; k->Ld[2] = l > 0 ? k->lp[2] / l : 0; }
        else {
            k->at[0] = gf32(r[0x7B + 3 * i]); k->at[1] = gf32(r[0x7C + 3 * i]); k->at[2] = gf32(r[0x7D + 3 * i]);
            if (k->type >= 2) { k->sd[0] = gf32(r[0x6F + 3 * i]); k->sd[1] = gf32(r[0x70 + 3 * i]); k->sd[2] = gf32(r[0x71 + 3 * i]); k->sl = vlen(k->sd);
                k->cut = gf32(r[0x8B + i]); k->sexp = gf32(r[0x87 + i]); }
        }
        k->la = cf_rgb(r[0x8F + 3 * i], 0); k->ld = cf_rgb(r[0x90 + 3 * i], 0); k->ls = cf_rgb(r[0x91 + 3 * i], 0);
    }
    g_ltk.gen = g_lk_gen; g_ltk.g = g;
}
static void light_core(GEState *g, const double *n, double x, double y, double z, int has, Vert *v) {
    const uint32_t *r = g->reg;
    if (g_ltk.gen != g_lk_gen || g_ltk.g != g) light_consts(g);
    const double *W = g->world;
    double wn[3] = {n[0] * W[0] + n[1] * W[3] + n[2] * W[6], n[0] * W[1] + n[1] * W[4] + n[2] * W[7], n[0] * W[2] + n[1] * W[5] + n[2] * W[8]};
    double nl = vlen(wn); if (nl > 0) { wn[0] /= nl; wn[1] /= nl; wn[2] /= nl; }
    double wp[3]; mul43(W, x, y, z, wp);
    int mu = has ? (int)(r[0x53] & 7) : 0;
    Cf vc = {v->c.r / 255.0, v->c.g / 255.0, v->c.b / 255.0, v->c.a / 255.0};
    Cf mac = (mu & 1) ? vc : g_ltk.mac;
    Cf mdc = (mu & 2) ? vc : g_ltk.mdc;
    Cf msc = (mu & 4) ? vc : g_ltk.msc;
    Cf amb = g_ltk.amb, em = g_ltk.em;
    double o[4] = {em.r + mac.r * amb.r, em.g + mac.g * amb.g, em.b + mac.b * amb.b, mac.a * amb.a};
    double spe = g_ltk.spe; const double *vd = g_ltk.vd;
    for (int i = 0; i < 4; i++) {
        const LightK *lk = &g_ltk.l[i];
        if (!lk->on) continue;
        int type = lk->type, comp = lk->comp;
        double Lv[3], att = 1.0, spot = 1.0;
        if (type == 0) { Lv[0] = lk->Ld[0]; Lv[1] = lk->Ld[1]; Lv[2] = lk->Ld[2]; }
        else {
            Lv[0] = lk->lp[0] - wp[0]; Lv[1] = lk->lp[1] - wp[1]; Lv[2] = lk->lp[2] - wp[2];
            double d = vlen(Lv);
            if (d > 0) { Lv[0] /= d; Lv[1] /= d; Lv[2] /= d; } else { Lv[0] = Lv[1] = 0; Lv[2] = 1; }
            double den = lk->at[0] + lk->at[1] * d + lk->at[2] * d * d;
            att = den > 0 ? 1.0 / den : 0.0; if (!(att > 0)) att = 0; else if (att > 1) att = 1;
            if (type >= 2) {
                const double *sd = lk->sd; double sl = lk->sl;
                double raw = sl > 0 ? (sd[0] * Lv[0] + sd[1] * Lv[1] + sd[2] * Lv[2]) / sl : 0;
                spot = raw >= lk->cut ? gpow(raw, lk->sexp) : 0.0;
            }
        }
        double k = att * spot;
        Cf la = lk->la, ld = lk->ld, ls = lk->ls;
        o[0] += la.r * mac.r * k; o[1] += la.g * mac.g * k; o[2] += la.b * mac.b * k;
        double nd = wn[0] * Lv[0] + wn[1] * Lv[1] + wn[2] * Lv[2], df = nd;
        if (comp == 2) df = gpow(nd, spe);
        if (df > 0) { o[0] += ld.r * mdc.r * df * k; o[1] += ld.g * mdc.g * df * k; o[2] += ld.b * mdc.b * df * k; }
        if (comp == 1 && nd >= 0) {
            double h[3] = {Lv[0] + vd[0], Lv[1] + vd[1], Lv[2] + vd[2]}, hl = vlen(h);
            if (hl > 0) { h[0] /= hl; h[1] /= hl; h[2] /= hl; } else { h[0] = h[1] = 0; h[2] = 1; }
            double sf = gpow(wn[0] * h[0] + wn[1] * h[1] + wn[2] * h[2], spe) * k;
            if (sf > 0) { o[0] += ls.r * msc.r * sf; o[1] += ls.g * msc.g * sf; o[2] += ls.b * msc.b * sf; }
        }
    }
    v->c.r = clamp255d(o[0] * 255.0 + 0.5); v->c.g = clamp255d(o[1] * 255.0 + 0.5); v->c.b = clamp255d(o[2] * 255.0 + 0.5); v->c.a = clamp255d(o[3] * 255.0 + 0.5);
}
/* decode one vertex: position (skinned, still in model space), uv, colour. Returns through-mode flag in v->w==1 */
static void decode_raw(GEState *g, const Layout *L, const uint8_t *p, Vert *v, Col ambient, double *px, double *py, double *pz) {
    double x = 0, y = 0, z = 0; int th = L->through; const uint8_t *q;
    q = p + L->poff;
    if (L->pos == 3) { float f[3]; memcpy(f, q, 12); x = f[0]; y = f[1]; z = f[2]; }
    else if (L->pos == 2) {
        if (th) { x = rd16le(q); y = rd16le(q + 2); z = rd16le(q + 4); }
        else { x = (int16_t)rd16le(q) / 32768.0; y = (int16_t)rd16le(q + 2) / 32768.0; z = (int16_t)rd16le(q + 4) / 32768.0; }
    } else {
        if (th) { x = q[0]; y = q[1]; z = q[2]; }
        else { x = (int8_t)q[0] / 128.0; y = (int8_t)q[1] / 128.0; z = (int8_t)q[2] / 128.0; }
    }
    v->has_uv = 0;
    if (L->tc) {
        q = p + L->toff; v->has_uv = 1;
        if (L->tc == 3) { float f[2]; memcpy(f, q, 8); v->u = f[0]; v->v = f[1]; }
        else if (L->tc == 2) { v->u = rd16le(q); v->v = rd16le(q + 2); if (!th) { v->u /= 32768.0; v->v /= 32768.0; } }
        else { v->u = q[0]; v->v = q[1]; if (!th) { v->u /= 128.0; v->v /= 128.0; } }
    }
    if (L->col) {
        q = p + L->coff;
        if (L->col == 7) v->c = unpack_color(rd32le(q), 3);
        else v->c = unpack_color(rd16le(q), L->col == 4 ? 0 : L->col == 5 ? 1 : 2);
    } else v->c = ambient;
    if (!th && L->wf) {
        double ax = 0, ay = 0, az = 0, o[3];
        q = p + L->woff;
        for (int i = 0; i < L->wc; i++) {
            double wgt;
            if (L->wf == 3) { float f; memcpy(&f, q + 4 * i, 4); wgt = f; }
            else if (L->wf == 2) wgt = rd16le(q + 2 * i) / 32768.0;
            else wgt = q[i] / 128.0;
            if (wgt == 0.0) continue;
            mul43(g->bones + i * 12, x, y, z, o); ax += o[0] * wgt; ay += o[1] * wgt; az += o[2] * wgt;
        }
        x = ax; y = ay; z = az;
    }
    if (!th && (g->reg[0x17] & 1) && !g_no_light) light_vertex(g, L, p, x, y, z, v);
    *px = x; *py = y; *pz = z;
}
static void project_raw(GEState *g, double x, double y, double z, Vert *v) {
    double a[3], b[3]; mul43(g->world, x, y, z, a); mul43(g->view, a[0], a[1], a[2], b);
    const double *P = g->proj; x = b[0]; y = b[1]; z = b[2];
    double cx = x * P[0] + y * P[4] + z * P[8] + P[12], cy = x * P[1] + y * P[5] + z * P[9] + P[13];
    double cz = x * P[2] + y * P[6] + z * P[10] + P[14], cw = x * P[3] + y * P[7] + z * P[11] + P[15];
    v->gh = 0;
    if (cw <= 1e-6) { v->ok = 0; if (!(cw < -1e-3)) return; v->gh = 1; }   /* (behind the camera: the triangle is left out, but its
                                                                       *  place is kept - an in-between picture may see it) */
    double ix = cx / cw, iy = cy / cw, iz = cz / cw; uint32_t *r = g->reg;
    v->x = ix * gf32(r[0x42]) + gf32(r[0x45]) - (r[0x4C] & 0xFFFF) / 16.0;
    v->y = iy * gf32(r[0x43]) + gf32(r[0x46]) - (r[0x4D] & 0xFFFF) / 16.0;
    v->z = iz * gf32(r[0x44]) + gf32(r[0x47]);
    v->w = cw; if (!v->gh) v->ok = 1;
    double fg = (z + gf32(r[0xCD])) * gf32(r[0xCE]);                /* fog by distance (view space z) */
    v->fog = fg != fg ? 1.0 : fg < 0 ? 0.0 : fg > 1 ? 1.0 : fg;
}
static int decode_project(GEState *g, const Layout *L, const uint8_t *p, Vert *v, Col ambient) {
    double x, y, z; decode_raw(g, L, p, v, ambient, &x, &y, &z);
    if (L->through) { v->x = x; v->y = y; v->z = z; v->w = 1.0; v->ok = 1; v->fog = 1.0; return 1; }
    project_raw(g, x, y, z, v); return 1;
}

/* ------------------------------------------------------------------ texture fetch (one texel at a time) */
typedef struct {
    int w, h, fmt, sw, bpp_bits, rowbytes, bw, rib; uint32_t addr; const uint8_t *base; uint32_t avail;
    Col pal[512]; int ncl; int cshift, cmask, cstart;
} Tex;

static int tex_setup(GEState *g, Tex *t) {
    uint32_t *r = g->reg;
    t->w = 1 << (r[0xB8] & 15); t->h = 1 << ((r[0xB8] >> 8) & 15); t->fmt = r[0xC3] & 15;
    t->addr = (r[0xA0] & 0xFFFFFF) | ((r[0xA8] & 0x0F0000) << 8);
    int stride = r[0xA8] & 0xFFFF; t->sw = r[0xC2] & 1;
    static const int BB[16] = {16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8, 32, 32, 32, 32, 32};
    t->bpp_bits = BB[t->fmt];
    if (!stride) stride = t->w;
    t->rowbytes = stride * t->bpp_bits / 8;
    uint32_t need = (uint32_t)t->rowbytes * t->h;
    uint32_t extra = (t->w > stride ? (uint32_t)(t->w - stride) * t->bpp_bits / 8 : 0) + 16;
    t->base = gloc(g, t->addr, need + extra); t->avail = need + extra;
    if (!t->base) { t->base = gloc(g, t->addr, need); t->avail = need; }
    if (!t->base) return 0;
    t->bw = t->rowbytes / 16; t->rib = t->h < 8 ? t->h : 8;
    t->ncl = 0;
    if (t->fmt >= 4 && t->fmt <= 7) {
        int cf = r[0xC5] & 3, csz = cf == 3 ? 4 : 2; t->ncl = (int)g->clut_len / csz; if (t->ncl > 512) t->ncl = 512;
        for (int i = 0; i < t->ncl; i++) t->pal[i] = unpack_color(csz == 4 ? rd32le(g->clut + 4 * i) : rd16le(g->clut + 2 * i), cf);
        t->cshift = (r[0xC5] >> 2) & 31; t->cmask = (r[0xC5] >> 8) & 255; t->cstart = ((r[0xC5] >> 16) & 31) * 16;
    }
    return 1;
}
/* byte at linear (unswizzled) offset */
static inline int tbyte(const Tex *t, uint32_t off) {
    if (t->sw && t->fmt < 8) {
        uint32_t y = off / t->rowbytes, xb = off % t->rowbytes;
        if (y >= (uint32_t)t->h) { /* beyond the swizzled area: python's out buffer holds zeros there */ return 0; }
        uint32_t bx = xb / 16;
        if (bx >= (uint32_t)t->bw) return 0;
        uint32_t src = (y / 8) * t->bw * t->rib * 16 + bx * t->rib * 16 + (y & 7) * 16 + (xb & 15);
        return src < t->avail ? t->base[src] : 0;
    }
    return off < t->avail ? t->base[off] : 0;
}
static Col dxt_texel(const Tex *t, int x, int y) {
    int bsz = t->fmt == 8 ? 8 : 16; Col z = {0, 0, 0, 0};
    uint32_t p = (uint32_t)((y / 4) * ((t->w + 3) / 4) + (x / 4)) * bsz;
    if (p + bsz > t->avail) return z;
    const uint8_t *blk = t->base + p, *cd = blk + bsz - 8;
    uint16_t c0 = rd16le(cd), c1 = rd16le(cd + 2); uint32_t idx = rd32le(cd + 4);
    Col a = unpack_color(c0, 0), b = unpack_color(c1, 0), pal[4];
    pal[0] = a; pal[1] = b;
    if (c0 > c1 || t->fmt != 8) {
        pal[2].r = (2 * a.r + b.r) / 3; pal[2].g = (2 * a.g + b.g) / 3; pal[2].b = (2 * a.b + b.b) / 3; pal[2].a = 255;
        pal[3].r = (a.r + 2 * b.r) / 3; pal[3].g = (a.g + 2 * b.g) / 3; pal[3].b = (a.b + 2 * b.b) / 3; pal[3].a = 255;
    } else {
        pal[2].r = (a.r + b.r) / 2; pal[2].g = (a.g + b.g) / 2; pal[2].b = (a.b + b.b) / 2; pal[2].a = 255; pal[3] = z;
    }
    int k = (y & 3) * 4 + (x & 3); Col c = pal[(idx >> (2 * k)) & 3];
    if (t->fmt == 9) { uint64_t q; memcpy(&q, blk, 8); c.a = (int)((q >> (4 * k)) & 15) * 17; }
    else if (t->fmt == 10) {
        int a0 = blk[0], a1 = blk[1]; uint64_t bits = 0; for (int i = 0; i < 6; i++) bits |= (uint64_t)blk[2 + i] << (8 * i);
        int ai = (int)((bits >> (3 * k)) & 7), al_;
        if (a0 > a1) al_ = ai == 0 ? a0 : ai == 1 ? a1 : ((8 - ai) * a0 + (ai - 1) * a1) / 7;
        else al_ = ai == 0 ? a0 : ai == 1 ? a1 : ai == 6 ? 0 : ai == 7 ? 255 : ((6 - ai) * a0 + (ai - 1) * a1) / 5;
        c.a = al_;
    }
    return c;
}
/* fetch the bytes of one texel straight from memory (swizzled or not); same results as tbyte() byte by byte */
static inline __attribute__((always_inline)) uint32_t tex_bytes(const Tex *t, int x, int y, int nbytes) {
    uint32_t xb = t->bpp_bits == 4 ? (uint32_t)x >> 1 : (uint32_t)x * (t->bpp_bits >> 3), src;
    if (t->sw) {
        if ((uint32_t)y >= (uint32_t)t->h) return 0;
        uint32_t bx = xb >> 4; if (bx >= (uint32_t)t->bw) return 0;
        src = ((uint32_t)y >> 3) * t->bw * t->rib * 16 + bx * t->rib * 16 + ((uint32_t)y & 7) * 16 + (xb & 15);
    } else src = (uint32_t)y * t->rowbytes + xb;
    if (src + nbytes > t->avail) {                      /* edge of memory: byte by byte like before */
        uint32_t v = 0; for (int i = 0; i < nbytes; i++) v |= (uint32_t)(src + i < t->avail ? t->base[src + i] : 0) << (8 * i);
        return v;
    }
    const uint8_t *q = t->base + src;
    return nbytes == 1 ? q[0] : nbytes == 2 ? (uint32_t)rd16le(q) : rd32le(q);
}
static inline __attribute__((always_inline)) Col texel(const Tex *t, int x, int y) {
    if (t->fmt >= 8) return dxt_texel(t, x, y);
    uint32_t v;
    switch (t->fmt) {
    case 0: case 1: case 2: return unpack_color(tex_bytes(t, x, y, 2), t->fmt);
    case 3: { uint32_t w = tex_bytes(t, x, y, 4); Col c = {(int)(w & 255), (int)((w >> 8) & 255), (int)((w >> 16) & 255), (int)(w >> 24)}; return c; }
    case 4: { uint32_t b = tex_bytes(t, x, y, 1); v = (x & 1) ? (b >> 4) : (b & 15); break; }
    case 5: v = tex_bytes(t, x, y, 1); break;
    case 6: v = tex_bytes(t, x, y, 2); break;
    default: v = tex_bytes(t, x, y, 4); break;
    }
    int i = (int)(((v >> t->cshift) & t->cmask) + t->cstart);
    if (i < t->ncl) return t->pal[i];
    Col z = {0, 0, 0, 0}; return z;
}
static inline int ifloor(double d) { int i = (int)d; return i - (d < i); }

/* ------------------------------------------------------------------ pixel pipeline */
typedef struct {
    uint32_t fb_addr, fb_stride, z_addr, z_stride; int fb_fmt, bpp; int sc[4];
    int texon, blend_on, atest_on, ztest_on, zwrite;
    uint8_t *fb; uint8_t *zb;
} Pipe;
static void setup(GEState *g, Pipe *p) {
    uint32_t *r = g->reg;
    p->fb_addr = 0x04000000u | (r[0x9C] & 0x1FFFFF); p->fb_stride = r[0x9D] & 0xFFFF; if (!p->fb_stride) p->fb_stride = 512;
    p->fb_fmt = r[0xD2] & 3; p->bpp = p->fb_fmt == 3 ? 4 : 2;
    p->z_addr = 0x04000000u | (r[0x9E] & 0x1FFFFF); p->z_stride = r[0x9F] & 0xFFFF; if (!p->z_stride) p->z_stride = 512;
    p->sc[0] = r[0xD4] & 0x3FF; p->sc[1] = (r[0xD4] >> 10) & 0x3FF; p->sc[2] = r[0xD5] & 0x3FF; p->sc[3] = (r[0xD5] >> 10) & 0x3FF;
    p->texon = r[0x1E] & 1; p->blend_on = r[0x21] & 1; p->atest_on = r[0x22] & 1; p->ztest_on = r[0x23] & 1; p->zwrite = !(r[0xE7] & 1);
    p->fb = g->vram + (p->fb_addr - 0x04000000u); p->zb = g->zram + (p->z_addr - 0x04000000u);
}
static Col shade0(GEState *g, Col c, const Col *tx);
static Col shade(GEState *g, Col c, const Col *tx) {
    Col o = shade0(g, c, tx);
    if (tx && (g->reg[0xC9] & 0x10000)) { o.r = o.r * 2 > 255 ? 255 : o.r * 2; o.g = o.g * 2 > 255 ? 255 : o.g * 2; o.b = o.b * 2 > 255 ? 255 : o.b * 2; }
    return o;
}
static Col shade0(GEState *g, Col c, const Col *tx) {
    if (!tx) return c;
    uint32_t r = g->reg[0xC9]; int func = r & 7, ta = (r & 0x100) != 0; Col o; Col t = *tx;
    if (func == 0) { o.r = t.r * c.r / 255; o.g = t.g * c.g / 255; o.b = t.b * c.b / 255; o.a = ta ? t.a * c.a / 255 : c.a; return o; }
    if (func == 1) {
        if (ta) { o.r = (t.r * t.a + c.r * (255 - t.a)) / 255; o.g = (t.g * t.a + c.g * (255 - t.a)) / 255; o.b = (t.b * t.a + c.b * (255 - t.a)) / 255; }
        else { o.r = t.r; o.g = t.g; o.b = t.b; }
        o.a = c.a; return o;
    }
    if (func == 2) {
        uint32_t e = g->reg[0xCA]; int er = e & 255, eg = (e >> 8) & 255, eb = (e >> 16) & 255;
        o.r = (c.r * (255 - t.r) + er * t.r) / 255; o.g = (c.g * (255 - t.g) + eg * t.g) / 255; o.b = (c.b * (255 - t.b) + eb * t.b) / 255;
        o.a = ta ? t.a * c.a / 255 : c.a; return o;
    }
    if (func == 3) { o.r = t.r; o.g = t.g; o.b = t.b; o.a = ta ? t.a : c.a; return o; }
    o.r = c.r + t.r > 255 ? 255 : c.r + t.r; o.g = c.g + t.g > 255 ? 255 : c.g + t.g; o.b = c.b + t.b > 255 ? 255 : c.b + t.b;
    o.a = ta ? t.a * c.a / 255 : c.a; return o;
}
static void blend_factor(GEState *g, int f, Col src, Col dst, int is_src, double *out) {
    double sa = src.a / 255.0, da = dst.a / 255.0, a;
    if (f == 0 || f == 1) {
        Col c = is_src ? dst : src;
        double cc[3] = {c.r / 255.0, c.g / 255.0, c.b / 255.0};
        for (int i = 0; i < 3; i++) out[i] = f == 0 ? cc[i] : 1 - cc[i];
        return;
    }
    switch (f) {
    case 2: a = sa; break; case 3: a = 1 - sa; break; case 4: a = da; break; case 5: a = 1 - da; break;
    case 6: a = 2 * sa < 1 ? 2 * sa : 1; break; case 7: a = 1 - 2 * sa > 0 ? 1 - 2 * sa : 0; break;
    case 8: a = 2 * da < 1 ? 2 * da : 1; break; case 9: a = 1 - 2 * da > 0 ? 1 - 2 * da : 0; break;
    default: {
        uint32_t fx = is_src ? g->reg[0xE0] : g->reg[0xE1];
        out[0] = (fx & 255) / 255.0; out[1] = ((fx >> 8) & 255) / 255.0; out[2] = ((fx >> 16) & 255) / 255.0; return; }
    }
    out[0] = out[1] = out[2] = a;
}
static void put(GEState *g, Pipe *p, int x, int y, double z, Col col) {
    uint32_t *r = g->reg;
    if (x < p->sc[0] || x > p->sc[2] || y < p->sc[1] || y > p->sc[3]) return;
    uint32_t off = (p->fb_addr - 0x04000000u) + ((uint32_t)y * p->fb_stride + (uint32_t)x) * p->bpp;
    if (off + p->bpp > 0x200000u) return;
    uint8_t *px = g->vram + off; uint8_t *zp = 0; int zv = 0;
    if (p->ztest_on) {
        uint32_t zo = (p->z_addr - 0x04000000u) + ((uint32_t)y * p->z_stride + (uint32_t)x) * 2;
        if (zo + 2 > 0x200000u) return;
        zp = g->zram + zo; zv = (int)((int64_t)z & 0xFFFF); int old = zp[0] | (zp[1] << 8); int f = r[0xDE] & 7;
        if (!(f == 1 || (f == 2 && zv == old) || (f == 3 && zv != old) || (f == 4 && zv < old) || (f == 5 && zv <= old) || (f == 6 && zv > old) || (f == 7 && zv >= old))) return;
    }
    if (p->atest_on) {
        int f = r[0xDB] & 7, ref = (r[0xDB] >> 8) & 255, msk = (r[0xDB] >> 16) & 255, a = col.a & msk, rf = ref & msk;
        if (!(f == 1 || (f == 2 && a == rf) || (f == 3 && a != rf) || (f == 4 && a < rf) || (f == 5 && a <= rf) || (f == 6 && a > rf) || (f == 7 && a >= rf))) return;
    }
    if (p->blend_on) {
        uint32_t ov = 0; for (int i = 0; i < p->bpp; i++) ov |= (uint32_t)px[i] << (8 * i);
        Col old = unpack_color(ov, p->fb_fmt);
        uint32_t bm = r[0xDF]; int sf = bm & 15, df = (bm >> 4) & 15, eq = (bm >> 8) & 7; double S[3], D[3];
        blend_factor(g, sf, col, old, 1, S); blend_factor(g, df, col, old, 0, D);
        int cv[3] = {col.r, col.g, col.b}, ovv[3] = {old.r, old.g, old.b}, outc[3];
        for (int i = 0; i < 3; i++) {
            double s_ = cv[i] * S[i], d_ = ovv[i] * D[i], v;
            switch (eq) {
            case 0: v = s_ + d_; break; case 1: v = s_ - d_; break; case 2: v = d_ - s_; break;
            case 3: v = cv[i] < ovv[i] ? cv[i] : ovv[i]; break; case 4: v = cv[i] > ovv[i] ? cv[i] : ovv[i]; break;
            default: v = fabs(s_ - d_);
            }
            outc[i] = clamp255d(v);
        }
        col.r = outc[0]; col.g = outc[1]; col.b = outc[2];
    }
    uint32_t v = pack_color(col.r < 0 ? 0 : col.r > 255 ? 255 : col.r, col.g < 0 ? 0 : col.g > 255 ? 255 : col.g,
                            col.b < 0 ? 0 : col.b > 255 ? 255 : col.b, col.a < 0 ? 0 : col.a > 255 ? 255 : col.a, p->fb_fmt);
    for (int i = 0; i < p->bpp; i++) px[i] = (uint8_t)(v >> (8 * i));
    g->pixels++;
    if (p->ztest_on && p->zwrite) { zp[0] = zv & 255; zp[1] = (zv >> 8) & 255; }
}
static int invisible(GEState *g, Pipe *p, const Col *cols, int n) {
    uint32_t *r = g->reg;
    if (!p->atest_on || (r[0xDB] & 7) != 6 || ((r[0xDB] >> 16) & 255) != 255 || ((r[0xDB] >> 8) & 255) != 0) return 0;
    if (p->texon && !(((r[0xC9] & 7) == 0 || (r[0xC9] & 7) == 2 || (r[0xC9] & 7) == 4) && (r[0xC9] & 0x100))) return 0;
    for (int i = 0; i < n; i++) if (cols[i].a != 0) return 0;
    return 1;
}
static void tex_params(GEState *g, double *su, double *sv, double *ou, double *ov) {
    *su = gf32(g->reg[0x48]); *sv = gf32(g->reg[0x49]); *ou = gf32(g->reg[0x4A]); *ov = gf32(g->reg[0x4B]);
}

static void sprite_draw(GEState *g, const Layout *L, int has_uv, int ix0, int ix1, int iy0, int iy1, double x0, double x1,
                        double y0, double y1, double u0, double u1, double v0, double v1, double z1, Col col);
static int g_gl_on, g_sw_on;                                          /* graphics-card drawing / software drawing (psp_gl.inc) */
static int gl_sw_wanted(GEState *g);                                 /* is the software picture needed too? */
static void gl_sprite(GEState *g, const Vert *a, const Vert *b, const Layout *L, int clear);
static void gl_tri(GEState *g, const Vert *a, const Vert *b, const Vert *c, const Layout *L);
static void gl_ghost(GEState *g, const Vert *a, const Vert *b, const Vert *c, const Layout *L);
static void sprite(GEState *g, Vert a, Vert b, const Layout *L, int clear) {
    if (g_gl_on) { gl_sprite(g, &a, &b, L, clear); if (!gl_sw_wanted(g)) return; }
    Pipe P; setup(g, &P);
    double x0 = a.x, y0 = a.y, x1 = b.x, y1 = b.y, z1 = b.z;
    Col col = b.c;
    if (!clear && invisible(g, &P, &col, 1)) return;
    static Tex T; int tex = 0;
    int has_uv = a.has_uv && b.has_uv;
    (void)T;                                                     /* (the texture is set up by sprite_draw) */
    double u0 = a.u, v0 = a.v, u1 = b.u, v1 = b.v, su, sv, ou, ov; tex_params(g, &su, &sv, &ou, &ov);
    if (!L->through && has_uv) { u0 = u0 * su + ou; v0 = v0 * sv + ov; u1 = u1 * su + ou; v1 = v1 * sv + ov; }
    if (x1 < x0) { double t = x0; x0 = x1; x1 = t; double nu0 = u1, nu1 = u0; u0 = nu0; u1 = nu1; }   /* uv0=(uv1.u, uv0.v), uv1=(uv0.u, uv1.v) */
    if (y1 < y0) { double t = y0; y0 = y1; y1 = t; double nv0 = v1, nv1 = v0; v0 = nv0; v1 = nv1; }
    int ix0 = (int)floor(x0 + 0.5), ix1 = (int)floor(x1 - 0.5 + 1e-6), iy0 = (int)floor(y0 + 0.5), iy1 = (int)floor(y1 - 0.5 + 1e-6);
    if (x1 - x0 <= 0 || y1 - y0 <= 0) return;
    if (ix0 < P.sc[0]) ix0 = P.sc[0]; if (ix1 > P.sc[2]) ix1 = P.sc[2]; if (iy0 < P.sc[1]) iy0 = P.sc[1]; if (iy1 > P.sc[3]) iy1 = P.sc[3];
    if (clear) {
        uint32_t cm = g->reg[0xD3], cv = pack_color(col.r, col.g, col.b, col.a, P.fb_fmt); int zv = (int)((int64_t)z1 & 0xFFFF);
        for (int y = iy0; y <= iy1; y++) for (int x = ix0; x <= ix1; x++) {
            uint32_t off = (P.fb_addr - 0x04000000u) + ((uint32_t)y * P.fb_stride + x) * P.bpp;
            if (cm & 0x100) {
                if (off + P.bpp <= 0x200000u) { for (int i = 0; i < P.bpp; i++) g->vram[off + i] = (uint8_t)(cv >> (8 * i)); }
            }
            if (cm & 0x400) {
                uint32_t zo = (P.z_addr - 0x04000000u) + ((uint32_t)y * P.z_stride + x) * 2;
                if (zo + 2 <= 0x200000u) { g->zram[zo] = zv & 255; g->zram[zo + 1] = (zv >> 8) & 255; }
            }
        }
        return;
    }
    sprite_draw(g, L, has_uv, ix0, ix1, iy0, iy1, x0, x1, y0, y1, u0, u1, v0, v1, z1, col);
    (void)tex;
}

/* everything a triangle needs that stays the same for a whole draw command */
typedef struct {
    Pipe P; Tex T; int tex, through, flat, uvok; double su, sv, ou, ov; uint32_t gen;
    /* pixel state, read once per draw instead of once per pixel */
    int zf, zwrite, af, aref, amask, blend, sf, df, eq, tfunc, ta, er, eg, eb, clampu, clampv;
    int dbl, fogon, fr, fgc, fb_;                     /* colour doubling, fog on, fog colour */
    int fixa[3], fixb[3];
} DrawCtx;
static uint32_t g_drawgen;                                /* bumps for every draw command */
static void ctx_setup(GEState *g, DrawCtx *d, const Layout *L, int has_uv) {
    uint32_t *r = g->reg;
    setup(g, &d->P); d->through = L->through; d->flat = (r[0x50] & 1) == 0; d->uvok = has_uv;
    d->tex = (d->P.texon && has_uv) ? tex_setup(g, &d->T) : 0;
    tex_params(g, &d->su, &d->sv, &d->ou, &d->ov); d->gen = g_drawgen;
    d->zf = d->P.ztest_on ? (int)(r[0xDE] & 7) : -1; d->zwrite = d->P.ztest_on && d->P.zwrite;
    d->af = d->P.atest_on ? (int)(r[0xDB] & 7) : -1; d->amask = (r[0xDB] >> 16) & 255; d->aref = ((r[0xDB] >> 8) & 255) & d->amask;
    d->blend = d->P.blend_on; d->sf = r[0xDF] & 15; d->df = (r[0xDF] >> 4) & 15; d->eq = (r[0xDF] >> 8) & 7;
    for (int i = 0; i < 3; i++) { d->fixa[i] = (r[0xE0] >> (8 * i)) & 255; d->fixb[i] = (r[0xE1] >> (8 * i)) & 255; }
    d->tfunc = r[0xC9] & 7; d->ta = (r[0xC9] & 0x100) != 0;
    d->er = r[0xCA] & 255; d->eg = (r[0xCA] >> 8) & 255; d->eb = (r[0xCA] >> 16) & 255;
    d->clampu = r[0xC7] & 1; d->clampv = (r[0xC7] & 0x100) != 0;
    d->dbl = (r[0xC9] & 0x10000) != 0; d->fogon = (r[0x1F] & 1) && !L->through;
    d->fr = r[0xCF] & 255; d->fgc = (r[0xCF] >> 8) & 255; d->fb_ = (r[0xCF] >> 16) & 255;
}
static inline int cmp_pass(int f, int v, int ref) {
    switch (f) { case 0: return 0; case 1: return 1; case 2: return v == ref; case 3: return v != ref; case 4: return v < ref;
    case 5: return v <= ref; case 6: return v > ref; default: return v >= ref; }
}
/* blend factor (0..255 scale) for the 3 colour channels; `other` is the colour that factors 0/1 read */
static inline void bfac(int f, Col other, int sa, int da, const int *fix, int *o) {
    int a;
    switch (f) {
    case 0: o[0] = other.r; o[1] = other.g; o[2] = other.b; return;
    case 1: o[0] = 255 - other.r; o[1] = 255 - other.g; o[2] = 255 - other.b; return;
    case 2: a = sa; break; case 3: a = 255 - sa; break; case 4: a = da; break; case 5: a = 255 - da; break;
    case 6: a = 2 * sa < 255 ? 2 * sa : 255; break; case 7: a = 255 - 2 * sa > 0 ? 255 - 2 * sa : 0; break;
    case 8: a = 2 * da < 255 ? 2 * da : 255; break; case 9: a = 255 - 2 * da > 0 ? 255 - 2 * da : 0; break;
    default: o[0] = fix[0]; o[1] = fix[1]; o[2] = fix[2]; return;
    }
    o[0] = o[1] = o[2] = a;
}
static inline Col shade_ctx0(const DrawCtx *d, Col c, Col t);
static inline Col shade_ctx(const DrawCtx *d, Col c, Col t) {          /* texture function, then colour doubling */
    Col o = shade_ctx0(d, c, t);
    if (d->dbl) { o.r = o.r * 2 > 255 ? 255 : o.r * 2; o.g = o.g * 2 > 255 ? 255 : o.g * 2; o.b = o.b * 2 > 255 ? 255 : o.b * 2; }
    return o;
}
static inline Col shade_ctx0(const DrawCtx *d, Col c, Col t) {
    Col o; int ta = d->ta;
    switch (d->tfunc) {
    case 0: o.r = t.r * c.r / 255; o.g = t.g * c.g / 255; o.b = t.b * c.b / 255; o.a = ta ? t.a * c.a / 255 : c.a; return o;
    case 1:
        if (ta) { o.r = (t.r * t.a + c.r * (255 - t.a)) / 255; o.g = (t.g * t.a + c.g * (255 - t.a)) / 255; o.b = (t.b * t.a + c.b * (255 - t.a)) / 255; }
        else { o.r = t.r; o.g = t.g; o.b = t.b; }
        o.a = c.a; return o;
    case 2:
        o.r = (c.r * (255 - t.r) + d->er * t.r) / 255; o.g = (c.g * (255 - t.g) + d->eg * t.g) / 255; o.b = (c.b * (255 - t.b) + d->eb * t.b) / 255;
        o.a = ta ? t.a * c.a / 255 : c.a; return o;
    case 3: o.r = t.r; o.g = t.g; o.b = t.b; o.a = ta ? t.a : c.a; return o;
    default:
        o.r = c.r + t.r > 255 ? 255 : c.r + t.r; o.g = c.g + t.g > 255 ? 255 : c.g + t.g; o.b = c.b + t.b > 255 ? 255 : c.b + t.b;
        o.a = ta ? t.a * c.a / 255 : c.a; return o;
    }
}
static inline int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
/* one pixel, already known to be inside the scissor box; fo/zo are byte offsets into VRAM / depth memory */
static inline void pix(GEState *g, const DrawCtx *d, uint32_t fo, uint32_t zo, int zv, Col col) {
    int bpp = d->P.bpp, fmt = d->P.fb_fmt;
    if (fo + bpp > 0x200000u) return;
    uint8_t *px = g->vram + fo, *zp = 0;
    if (d->zf >= 0) {
        if (zo + 2 > 0x200000u) return;
        zp = g->zram + zo;
        if (!cmp_pass(d->zf, zv, zp[0] | (zp[1] << 8))) return;
    }
    if (d->af >= 0 && !cmp_pass(d->af, col.a & d->amask, d->aref)) return;
    if (d->blend) {
        Col old = unpack_color(bpp == 4 ? rd32le(px) : rd16le(px), fmt);
        int S[3], D[3], cv[3] = {col.r, col.g, col.b}, ov[3] = {old.r, old.g, old.b}, o[3];
        bfac(d->sf, old, col.a, old.a, d->fixa, S); bfac(d->df, col, col.a, old.a, d->fixb, D);
        for (int i = 0; i < 3; i++) {
            int v, s_ = cv[i] * S[i], d_ = ov[i] * D[i];
            switch (d->eq) {
            case 0: v = (s_ + d_) / 255; break; case 1: v = (s_ - d_) / 255; break; case 2: v = (d_ - s_) / 255; break;
            case 3: v = cv[i] < ov[i] ? cv[i] : ov[i]; break; case 4: v = cv[i] > ov[i] ? cv[i] : ov[i]; break;
            default: v = (s_ > d_ ? s_ - d_ : d_ - s_) / 255;
            }
            o[i] = clamp8(v);
        }
        col.r = o[0]; col.g = o[1]; col.b = o[2];
    }
    uint32_t v = pack_color(clamp8(col.r), clamp8(col.g), clamp8(col.b), clamp8(col.a), fmt);
    if (bpp == 4) memcpy(px, &v, 4); else { uint16_t h = (uint16_t)v; memcpy(px, &h, 2); }
    g->pixels++;
    if (d->zwrite) { zp[0] = zv & 255; zp[1] = (zv >> 8) & 255; }
}
static inline Col sample_ctx(const DrawCtx *d, double u, double v) {
    const Tex *t = &d->T;
    int x = d->through ? ifloor(u) : ifloor(u * t->w), y = d->through ? ifloor(v) : ifloor(v * t->h);
    if (d->clampu) x = x < 0 ? 0 : x >= t->w ? t->w - 1 : x; else x &= t->w - 1;
    if (d->clampv) y = y < 0 ? 0 : y >= t->h ? t->h - 1 : y; else y &= t->h - 1;
    return texel(t, x, y);
}
/* a sprite (2D rectangle) with the same per-pixel steps as triangles */
static void sprite_draw(GEState *g, const Layout *L, int has_uv, int ix0, int ix1, int iy0, int iy1, double x0, double x1,
                        double y0, double y1, double u0, double u1, double v0, double v1, double z1, Col col) {
    static DrawCtx d; static uint32_t have = 0xFFFFFFFF; static const Layout *lastL; static int lastuv = -1;
    if (have != g_drawgen || lastL != L || lastuv != has_uv) { ctx_setup(g, &d, L, has_uv); have = g_drawgen; lastL = L; lastuv = has_uv; }
    Pipe *P = &d.P; int tex = d.tex, bpp = P->bpp, zv = (int)((int64_t)z1 & 0xFFFF);
    for (int y = iy0; y <= iy1; y++) {
        double tv = tex ? v0 + (v1 - v0) * ((y + 0.5 - y0) / (y1 - y0)) : 0;
        uint32_t fo = (P->fb_addr - 0x04000000u) + ((uint32_t)y * P->fb_stride + (uint32_t)ix0) * bpp;
        uint32_t zo = (P->z_addr - 0x04000000u) + ((uint32_t)y * P->z_stride + (uint32_t)ix0) * 2;
        for (int x = ix0; x <= ix1; x++, fo += bpp, zo += 2) {
            Col c = col;
            if (tex) c = shade_ctx(&d, col, sample_ctx(&d, u0 + (u1 - u0) * ((x + 0.5 - x0) / (x1 - x0)), tv));
            pix(g, &d, fo, zo, zv, c);
        }
    }
}
GEXPORT void fb_to_rgb(const uint8_t *vram, uint32_t addr, int stride, int fmt, uint8_t *out);
#include "psp_gl.inc"
/* slow draw commands (test aid for the stutter log): when, how long, which command and vertex count, framebuffer, texture
 * (address | size codes), software too | through, software pixels, ticks in textures / copying back / sending */
static uint64_t g_sp[64][10], g_sp_thr; static uint32_t g_sp_n, g_sp_took;
GEXPORT void gl_slowprim_threshold(uint64_t t) { g_sp_thr = t; }
GEXPORT int gl_slowprims(uint64_t *out, int max) { int n = 0; while (g_sp_took < g_sp_n && n < max) { memcpy(out + n * 10, g_sp[g_sp_took & 63], 80); g_sp_took++; n++; } if (g_sp_n - g_sp_took > 64) g_sp_took = g_sp_n - 64; return n; }
#include "psp_vkgl.inc"                                             /* (the Vulkan renderer: OpenGL calls on Vulkan) */
static void triangle_ctx(GEState *g, DrawCtx *d, const Vert *A, const Vert *B, const Vert *C);
static void triangle(GEState *g, const Vert *pa, const Vert *pb, const Vert *pc, const Layout *L, int clear) {
    if (clear) {
        double x0 = pa->x, y0 = pa->y, x1 = pb->x, y1 = pb->y, x2 = pc->x, y2 = pc->y;
        if ((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0) == 0) return;
        sprite(g, *pa, fabs(x2 - x0) > 0 ? *pc : *pb, L, clear); return;
    }
    if (g->reg[0x1D] & 1) {                                        /* back faces left out (cull mode: which winding is the front) */
        double ar = (pb->x - pa->x) * (pc->y - pa->y) - (pc->x - pa->x) * (pb->y - pa->y);
        if ((g->reg[0x9B] & 1) ? !(ar > 0) : !(ar < 0)) return;
    }
    if (g_gl_on) { gl_tri(g, pa, pb, pc, L); if (!gl_sw_wanted(g)) return; }
    static DrawCtx d; static uint32_t have = 0xFFFFFFFF; static const Layout *lastL; static int lastuv = -1;
    int uv = pa->has_uv && pb->has_uv && pc->has_uv;
    if (have != g_drawgen || lastL != L || lastuv != uv) { ctx_setup(g, &d, L, uv); have = g_drawgen; lastL = L; lastuv = uv; }
    triangle_ctx(g, &d, pa, pb, pc);
}
static void triangle_ctx(GEState *g, DrawCtx *d, const Vert *A, const Vert *B, const Vert *C) {
    Pipe *P = &d->P;
    double x0 = A->x, y0 = A->y, x1 = B->x, y1 = B->y, x2 = C->x, y2 = C->y;
    double area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area == 0) return;
    Col cs[3] = {A->c, B->c, C->c};
    if (invisible(g, P, cs, 3)) return;
    int tex = d->tex;
    double u0 = A->u, v0 = A->v, u1 = B->u, v1 = B->v, u2 = C->u, v2 = C->v;
    if (tex && !d->through) { u0 = u0 * d->su + d->ou; v0 = v0 * d->sv + d->ov; u1 = u1 * d->su + d->ou; v1 = v1 * d->sv + d->ov; u2 = u2 * d->su + d->ou; v2 = v2 * d->sv + d->ov; }
    double mnx = fmin(x0, fmin(x1, x2)), mxx = fmax(x0, fmax(x1, x2)), mny = fmin(y0, fmin(y1, y2)), mxy = fmax(y0, fmax(y1, y2));
    if (mxx < P->sc[0] - 1 || mnx > P->sc[2] + 1 || mxy < P->sc[1] - 1 || mny > P->sc[3] + 1) return;   /* off screen */
    int minx = ifloor(mnx), maxx = (int)ceil(mxx), miny = ifloor(mny), maxy = (int)ceil(mxy);
    if (minx < P->sc[0]) minx = P->sc[0]; if (maxx > P->sc[2]) maxx = P->sc[2]; if (miny < P->sc[1]) miny = P->sc[1]; if (maxy > P->sc[3]) maxy = P->sc[3];
    if (maxx < minx || maxy < miny) return;
    double iw0 = 1.0 / A->w, iw1 = 1.0 / B->w, iw2 = 1.0 / C->w, inv = 1.0 / area;
    int flat = d->flat || (A->c.r == B->c.r && A->c.r == C->c.r && A->c.g == B->c.g && A->c.g == C->c.g &&
                           A->c.b == B->c.b && A->c.b == C->c.b && A->c.a == B->c.a && A->c.a == C->c.a);
    /* l0 = C0 + px*D0, l1 = C1 + px*D1 along a row (px = x + 0.5): find where all three are >= 0, then step along */
    double D0 = (y1 - y2) * inv, D1 = (y2 - y0) * inv, D2 = -(D0 + D1);
    double ar = A->c.r, ag = A->c.g, ab = A->c.b, aa = A->c.a, br = B->c.r, bg = B->c.g, bb = B->c.b, ba = B->c.a;
    double cr = C->c.r, cg = C->c.g, cb = C->c.b, ca = C->c.a, z0 = A->z, z1 = B->z, z2 = C->z;
    int bpp = P->bpp;
    int fastpath = tex && d->tfunc == 0 && d->ta && !d->blend && d->T.fmt < 8 && d->zf >= 0;
    for (int y = miny; y <= maxy; y++) {
        double py = y + 0.5;
        double C0 = (x1 * (y2 - py) - x2 * (y1 - py)) * inv, C1 = (x2 * (y0 - py) - x0 * (y2 - py)) * inv, C2 = 1.0 - C0 - C1;
        double lo = minx, hi = maxx + 1;
        const double Cs[3] = {C0, C1, C2}, Ds[3] = {D0, D1, D2};
        int empty = 0;
        for (int e = 0; e < 3; e++) {
            if (Ds[e] > 0) { double t = (-1e-9 - Cs[e]) / Ds[e]; if (t > lo) lo = t; }
            else if (Ds[e] < 0) { double t = (-1e-9 - Cs[e]) / Ds[e]; if (t < hi) hi = t; }
            else if (Cs[e] < -1e-9) { empty = 1; break; }
        }
        if (empty) continue;
        int xs = (int)ceil(lo - 0.5 - 1e-6), xe = ifloor(hi - 0.5 + 1e-6);   /* a hair of slack: the exact test below decides */
        if (xs < minx) xs = minx; if (xe > maxx) xe = maxx;
        if (xs > xe) continue;
        uint32_t fo = (P->fb_addr - 0x04000000u) + ((uint32_t)y * P->fb_stride + (uint32_t)xs) * bpp;
        uint32_t zo = (P->z_addr - 0x04000000u) + ((uint32_t)y * P->z_stride + (uint32_t)xs) * 2;
        /* everything below is a straight line along the row: start values at xs, then add a step per pixel */
        double px0 = xs + 0.5;
        double l0 = C0 + px0 * D0, l1 = C1 + px0 * D1, l2 = C2 + px0 * D2;
        double zz = z0 * l0 + z1 * l1 + z2 * l2, dz = z0 * D0 + z1 * D1 + z2 * D2;
        double P0 = l0 * iw0, P1 = l1 * iw1, P2 = l2 * iw2, dP0 = D0 * iw0, dP1 = D1 * iw1, dP2 = D2 * iw2;
        double PS = P0 + P1 + P2, dPS = dP0 + dP1 + dP2;
        double U = u0 * P0 + u1 * P1 + u2 * P2, dU = u0 * dP0 + u1 * dP1 + u2 * dP2;
        double V = v0 * P0 + v1 * P1 + v2 * P2, dV = v0 * dP0 + v1 * dP1 + v2 * dP2;
        double CR = ar * P0 + br * P1 + cr * P2, dCR = ar * dP0 + br * dP1 + cr * dP2;
        double CG = ag * P0 + bg * P1 + cg * P2, dCG = ag * dP0 + bg * dP1 + cg * dP2;
        double CB = ab * P0 + bb * P1 + cb * P2, dCB = ab * dP0 + bb * dP1 + cb * dP2;
        double CA = aa * P0 + ba * P1 + ca * P2, dCA = aa * dP0 + ba * dP1 + ca * dP2;
        double FG = A->fog * P0 + B->fog * P1 + C->fog * P2, dFG = A->fog * dP0 + B->fog * dP1 + C->fog * dP2;
        int zf = d->zf; const uint8_t *zram = g->zram; Col fc = C->c;
        if (fastpath) {
            /* the common case, written out: texture x vertex colour (with alpha), depth test, alpha test,
             * no blending. Same steps and results as the general loop below. */
            const Tex *T = &d->T; int tw = T->w, th = T->h, through = d->through, cu = d->clampu, cv = d->clampv;
            int af = d->af, amask = d->amask, aref = d->aref, ffmt = P->fb_fmt, zw = d->zwrite, dbl = d->dbl, fogon = d->fogon; uint8_t *vram = g->vram, *zr = g->zram;
            /* most pixels here fail the depth test (walls hidden behind walls), so only the edge and depth values are
             * stepped along; the rest is worked out (start + k * step) for the pixels that get drawn */
            for (int x = xs, k = 0; x <= xe; x++, k++, fo += bpp, zo += 2, l0 += D0, l1 += D1, l2 += D2, zz += dz) {
                if (l0 < -1e-9 || l1 < -1e-9 || l2 < -1e-9) continue;
                if (fo + bpp > 0x200000u || zo + 2 > 0x200000u) continue;
                int zv = (int)((int64_t)zz & 0xFFFF), zold = zr[zo] | (zr[zo + 1] << 8);
                if (zf == 7 ? zv < zold : !cmp_pass(zf, zv, zold)) continue;
                double kk = (double)k, ps = PS + kk * dPS;
                if (ps == 0) continue;
                double is = 1.0 / ps, u = (U + kk * dU) * is, v = (V + kk * dV) * is;
                Col c;
                if (flat) c = fc;
                else { c.r = (int)((CR + kk * dCR) * is); c.g = (int)((CG + kk * dCG) * is); c.b = (int)((CB + kk * dCB) * is); c.a = (int)((CA + kk * dCA) * is); }
                int tx = through ? ifloor(u) : ifloor(u * tw), ty = through ? ifloor(v) : ifloor(v * th);
                if (cu) tx = tx < 0 ? 0 : tx >= tw ? tw - 1 : tx; else tx &= tw - 1;
                if (cv) ty = ty < 0 ? 0 : ty >= th ? th - 1 : ty; else ty &= th - 1;
                Col t = texel(T, tx, ty), o;
                o.r = t.r * c.r / 255; o.g = t.g * c.g / 255; o.b = t.b * c.b / 255; o.a = t.a * c.a / 255;
                if (dbl) { o.r = o.r * 2 > 255 ? 255 : o.r * 2; o.g = o.g * 2 > 255 ? 255 : o.g * 2; o.b = o.b * 2 > 255 ? 255 : o.b * 2; }
                if (af >= 0 && (af == 6 ? !((o.a & amask) > aref) : !cmp_pass(af, o.a & amask, aref))) continue;
                if (fogon) {                                   /* fog: blend towards the fog colour with distance */
                    double f = (FG + kk * dFG) * is, h = 1.0 - f;
                    o.r = (int)(o.r * f + d->fr * h); o.g = (int)(o.g * f + d->fgc * h); o.b = (int)(o.b * f + d->fb_ * h);
                }
                uint32_t pv = pack_color(clamp8(o.r), clamp8(o.g), clamp8(o.b), clamp8(o.a), ffmt);
                if (bpp == 4) memcpy(vram + fo, &pv, 4); else { uint16_t h = (uint16_t)pv; memcpy(vram + fo, &h, 2); }
                g->pixels++;
                if (zw) { zr[zo] = zv & 255; zr[zo + 1] = (zv >> 8) & 255; }
            }
            continue;
        }
        for (int x = xs; x <= xe; x++, fo += bpp, zo += 2, l0 += D0, l1 += D1, l2 += D2, zz += dz, PS += dPS,
                                     U += dU, V += dV, CR += dCR, CG += dCG, CB += dCB, CA += dCA, FG += dFG) {
            if (l0 < -1e-9 || l1 < -1e-9 || l2 < -1e-9) continue;
            int zv = (int)((int64_t)zz & 0xFFFF);
            if (zf >= 2 && zo + 2 <= 0x200000u && !cmp_pass(zf, zv, zram[zo] | (zram[zo + 1] << 8))) continue;   /* hidden: skip the work */
            Col col;
            if (flat && !tex && !d->fogon) col = fc;
            else {
                if (PS == 0) continue;
                double is = 1.0 / PS;
                if (flat) col = fc;
                else { col.r = (int)(CR * is); col.g = (int)(CG * is); col.b = (int)(CB * is); col.a = (int)(CA * is); }
                if (tex) col = shade_ctx(d, col, sample_ctx(d, U * is, V * is));
                if (d->fogon) {
                    double f = FG * is, h = 1.0 - f;
                    col.r = (int)(col.r * f + d->fr * h); col.g = (int)(col.g * f + d->fgc * h); col.b = (int)(col.b * f + d->fb_ * h);
                }
            }
            pix(g, d, fo, zo, zv, col);
        }
    }
}

static void gl_line(GEState *g, const Vert *a, const Vert *b, const Layout *L);
static void line(GEState *g, Vert a, Vert b, const Layout *L) {
    if (g_gl_on) { gl_line(g, &a, &b, L); if (!gl_sw_wanted(g)) return; }
    Pipe P; setup(g, &P);
    int x0 = (int)floor(a.x), y0 = (int)floor(a.y), x1 = (int)floor(b.x), y1 = (int)floor(b.y);
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1, n = dx > dy ? dx : dy;
    if (n > 4096) return;
    int flat = (g->reg[0x50] & 1) == 0;
    for (int i = 0; i <= n; i++) {
        double t = n ? (double)i / n : 0.0;
        int x = (int)floor(a.x + (b.x - a.x) * t), y = (int)floor(a.y + (b.y - a.y) * t);
        Col c = b.c;
        if (!flat) { c.r = (int)(a.c.r + (b.c.r - a.c.r) * t); c.g = (int)(a.c.g + (b.c.g - a.c.g) * t); c.b = (int)(a.c.b + (b.c.b - a.c.b) * t); c.a = (int)(a.c.a + (b.c.a - a.c.a) * t); }
        put(g, &P, x, y, a.z + (b.z - a.z) * t, shade(g, c, 0));
    }
}

/* draw one PRIM command. returns 0 ok, 1 = bad memory (python stops the display list), 2 = points/lines (ignored) */
#define PC_BEHIND 1500.0   /* how far behind the camera counts as out of view (a character is about that tall) */
GEXPORT int ge_prim(GEState *g, uint32_t d) {
    uint32_t *r = g->reg; g_drawgen++; g_lk_gen++;
    int count = d & 0xFFFF, ptype = (d >> 16) & 7;
    Layout L; layout(r[0x12], &L);
    if (count == 0 || L.size == 0 || L.poff < 0) return 0;
    static uint32_t idxs[65536]; static Vert verts[65536]; static Vert cache[65536]; static uint8_t have[65536];
    int mx;
    if (L.idx) {
        int isz = ELEM[L.idx]; const uint8_t *ib = gloc(g, g->iaddr, (uint32_t)count * isz);
        if (!ib) { g->err_addr = g->iaddr; return 1; }
        mx = 0;
        for (int i = 0; i < count; i++) {
            uint32_t v = isz == 1 ? ib[i] : isz == 2 ? rd16le(ib + 2 * i) : rd32le(ib + 4 * i);
            if (v > 65535) { g->err_addr = g->iaddr; return 1; }
            idxs[i] = v; if ((int)v + 1 > mx) mx = v + 1;
        }
    } else { for (int i = 0; i < count; i++) idxs[i] = i; mx = count; }
    const uint8_t *vb = gloc(g, g->vaddr, (uint32_t)mx * L.size);
    if (!vb) { g->err_addr = g->vaddr; return 1; }
    /* Wholly off the screen? The game hands over whole rooms; most of it is out of view. Only the places of the points
     * are worked out first (no colours, lights, textures): when all lie past the same edge of the picture - by a
     * margin, as the in-between pictures look a little to the side and the shading looks past the edge - the draw
     * is left out. Not with a point behind the camera. Characters (bones; in an alert most of them are elsewhere
     * on the map): the box around their points, moved by each bone - every point lies between those - and a wider
     * margin, as they move between two pictures and their shadow reaches further than they do. */
    if (ptype >= 3 && ptype <= 5 && count >= 3 && !L.through && !(r[0xD3] & 1) && gl_prim_cull_ok()) {
        static float pw[65536]; static double pxw[65536], pyw[65536];
        double wv[12], m[16]; const double *W = g->world, *V = g->view, *P = g->proj;
        for (int c = 0; c < 4; c++) {                             /* world, then view (4 x 3 each) */
            double x = W[c * 3], y = W[c * 3 + 1], z = W[c * 3 + 2];
            for (int k = 0; k < 3; k++) wv[c * 3 + k] = x * V[k] + y * V[3 + k] + z * V[6 + k] + (c == 3 ? V[9 + k] : 0.0);
        }
        for (int c = 0; c < 4; c++) for (int k = 0; k < 4; k++)   /* then the projection: m[c * 4 + k] = part of clip k from input c */
            m[c * 4 + k] = wv[c * 3] * P[k] + wv[c * 3 + 1] * P[4 + k] + wv[c * 3 + 2] * P[8 + k] + (c == 3 ? P[12 + k] : 0.0);
        double sxs = gf32(r[0x42]), sys = gf32(r[0x43]), ox = gf32(r[0x45]) - (r[0x4C] & 0xFFFF) / 16.0, oy = gf32(r[0x46]) - (r[0x4D] & 0xFFFF) / 16.0;
        double X[4], Y[4], Wc[4];                                 /* screen x * w, screen y * w, w */
        for (int c = 0; c < 4; c++) { Wc[c] = m[c * 4 + 3]; X[c] = m[c * 4] * sxs + Wc[c] * ox; Y[c] = m[c * 4 + 1] * sys + Wc[c] * oy; }
        const double mg = L.wf ? 160.0 : 100.0;
        double xlo = (double)(r[0xD4] & 0x3FF) - mg, ylo = (double)((r[0xD4] >> 10) & 0x3FF) - mg;
        double xhi = (double)(r[0xD5] & 0x3FF) + 1.0 + mg + 44.0, yhi = (double)((r[0xD5] >> 10) & 0x3FF) + 1.0 + mg + 76.0;
        int all = 31, pos = L.pos; const uint8_t *pb = vb + L.poff; size_t st = (size_t)L.size;
        if (L.wf) {
            double lo[8][3], hi[8][3]; int used = 0, wc = L.wc > 8 ? 8 : L.wc; const uint8_t *wb = vb + L.woff;
            for (int i = 0; i < count && all; i++) {
                const uint8_t *q = pb + (size_t)idxs[i] * st, *wq = wb + (size_t)idxs[i] * st; double c[3], sum = 0;
                if (pos == 3) { float f[3]; memcpy(f, q, 12); c[0] = f[0]; c[1] = f[1]; c[2] = f[2]; }
                else if (pos == 2) { c[0] = (int16_t)rd16le(q) / 32768.0; c[1] = (int16_t)rd16le(q + 2) / 32768.0; c[2] = (int16_t)rd16le(q + 4) / 32768.0; }
                else { c[0] = (int8_t)q[0] / 128.0; c[1] = (int8_t)q[1] / 128.0; c[2] = (int8_t)q[2] / 128.0; }
                if (c[0] != c[0] || c[1] != c[1] || c[2] != c[2]) all = 0;
                for (int j = 0; j < wc; j++) {
                    double wg; if (L.wf == 3) { float f; memcpy(&f, wq + 4 * j, 4); wg = f; } else if (L.wf == 2) wg = rd16le(wq + 2 * j) / 32768.0; else wg = wq[j] / 128.0;
                    if (wg != 0.0) { sum += wg; if (!(wg > 0.0)) all = 0;       /* (a box for each bone: of the points it moves) */
                        if (!(used >> j & 1)) { used |= 1 << j; for (int k = 0; k < 3; k++) lo[j][k] = hi[j][k] = c[k]; }
                        else for (int k = 0; k < 3; k++) { if (c[k] < lo[j][k]) lo[j][k] = c[k]; if (c[k] > hi[j][k]) hi[j][k] = c[k]; } }
                }
                if (!(sum > 0.97 && sum < 1.03)) all = 0;             /* (the box only holds for weights that add up to one) */
            }
            double wsum = 0; int wn = 0;
            for (int j = 0; j < wc && all; j++) if (used >> j & 1) for (int cn = 0; cn < 8; cn++) {
                double o[3]; mul43(g->bones + j * 12, (cn & 1) ? hi[j][0] : lo[j][0], (cn & 2) ? hi[j][1] : lo[j][1], (cn & 4) ? hi[j][2] : lo[j][2], o);
                double w = o[0] * Wc[0] + o[1] * Wc[1] + o[2] * Wc[2] + Wc[3];
                if (!(w > 1e-2)) { all &= w < -PC_BEHIND ? 16 : 0; wn++; continue; }
                double sx = o[0] * X[0] + o[1] * X[1] + o[2] * X[2] + X[3], sy = o[0] * Y[0] + o[1] * Y[1] + o[2] * Y[2] + Y[3];
                all &= (sx < xlo * w ? 1 : 0) | (sx > xhi * w ? 2 : 0) | (sy < ylo * w ? 4 : 0) | (sy > yhi * w ? 8 : 0);
                wsum += w; wn++;
            }
            if (all && wn) {                                      /* counted like the draws that are shown (see below) */
                if (all & 16) { gl_prim_culled(g, &L, 0, pw, 0); return 0; }
                int nw = 0; float wa = (float)(wsum / wn); size_t pl = pos == 3 ? 12 : pos == 2 ? 6 : 3;
#define PC_SAME(A, B) (idxs[A] == idxs[B] || !memcmp(pb + (size_t)idxs[A] * st, pb + (size_t)idxs[B] * st, pl))
                if (ptype == 3) { for (int k = 0; k + 2 < count; k += 3) if (!PC_SAME(k, k + 1) && !PC_SAME(k + 1, k + 2) && !PC_SAME(k, k + 2)) pw[nw++] = wa; }
                else if (ptype == 4) { for (int k = 0; k + 2 < count; k++) if (!PC_SAME(k, k + 1) && !PC_SAME(k + 1, k + 2) && !PC_SAME(k, k + 2)) pw[nw++] = wa; }
                else { for (int k = 1; k + 1 < count; k++) if (!PC_SAME(0, k) && !PC_SAME(k, k + 1) && !PC_SAME(0, k + 1)) pw[nw++] = wa; }
#undef PC_SAME
                gl_prim_culled(g, &L, nw * 3, pw, nw);
                return 0;
            }
            all = 0;
        }
        for (int i = 0; i < count && all; i++) {
            const uint8_t *q = pb + (size_t)idxs[i] * st; double x, y, z;
            if (pos == 3) { float f[3]; memcpy(f, q, 12); x = f[0]; y = f[1]; z = f[2]; }
            else if (pos == 2) { x = (int16_t)rd16le(q) / 32768.0; y = (int16_t)rd16le(q + 2) / 32768.0; z = (int16_t)rd16le(q + 4) / 32768.0; }
            else { x = (int8_t)q[0] / 128.0; y = (int8_t)q[1] / 128.0; z = (int8_t)q[2] / 128.0; }
            double w = x * Wc[0] + y * Wc[1] + z * Wc[2] + Wc[3];
            pw[i] = (float)w; pxw[i] = pyw[i] = 0;
            if (!(w > 1e-2)) { all &= w < -PC_BEHIND ? 16 : 0; continue; }   /* (behind the camera - well behind: left out as well - or not a number) */
            double sx = x * X[0] + y * X[1] + z * X[2] + X[3], sy = x * Y[0] + y * Y[1] + z * Y[2] + Y[3];
            all &= (sx < xlo * w ? 1 : 0) | (sx > xhi * w ? 2 : 0) | (sy < ylo * w ? 4 : 0) | (sy > yhi * w ? 8 : 0);
            pw[i] = (float)w; pxw[i] = sx; pyw[i] = sy;
        }
        if (all) {
            /* (counted like the draws that are shown: triangles with no area - the joints of a strip - are not; the
             *  first corner of each tells how far away it is) */
            int nw = 0;
            if (all & 16) { gl_prim_culled(g, &L, 0, pw, 0); return 0; }   /* (all behind the camera: such triangles never counted) */
            for (int i = 0; i < count; i++) { double iw = 1.0 / (double)pw[i]; pxw[i] *= iw; pyw[i] *= iw; }
#define PC_TRI(A, B, C) { if ((pxw[B] - pxw[A]) * (pyw[C] - pyw[A]) - (pxw[C] - pxw[A]) * (pyw[B] - pyw[A]) != 0) pw[nw++] = pw[A]; }
            if (ptype == 3) { for (int k = 0; k + 2 < count; k += 3) PC_TRI(k, k + 1, k + 2) }
            else if (ptype == 4) { for (int k = 0; k + 2 < count; k++) { if (k & 1) PC_TRI(k + 1, k, k + 2) else PC_TRI(k, k + 1, k + 2) } }
            else { float w0 = pw[0]; for (int k = 1; k + 1 < count; k++) if ((pxw[k] - pxw[0]) * (pyw[k + 1] - pyw[0]) - (pxw[k + 1] - pxw[0]) * (pyw[k] - pyw[0]) != 0) pw[nw++] = w0; }
#undef PC_TRI
            gl_prim_culled(g, &L, nw * 3, pw, nw);
            return 0;
        }
    }
    Col amb = {(int)(r[0x55] & 255), (int)((r[0x55] >> 8) & 255), (int)((r[0x55] >> 16) & 255), (int)(r[0x58] & 255)};   /* no colour in the vertices: the material colour */
    memset(have, 0, (size_t)mx);
    for (int i = 0; i < count; i++) {
        uint32_t k = idxs[i];
        if (!have[k]) { decode_project(g, &L, vb + (size_t)k * L.size, &cache[k], amb); cache[k].src = (int)k; have[k] = 1; }
        verts[i] = cache[k];
    }
    int clear = r[0xD3] & 1;
    if (ptype == 6) {
        for (int k = 0; k + 1 < count; k += 2) if (verts[k].ok && verts[k + 1].ok) sprite(g, verts[k], verts[k + 1], &L, clear);
    } else if (ptype == 3) {
        for (int k = 0; k + 2 < count; k += 3) { if (verts[k].ok && verts[k + 1].ok && verts[k + 2].ok) triangle(g, &verts[k], &verts[k + 1], &verts[k + 2], &L, clear);
            else if (g_gl_on && !clear && ((&verts[k])->ok || (&verts[k])->gh) && ((&verts[k + 1])->ok || (&verts[k + 1])->gh) && ((&verts[k + 2])->ok || (&verts[k + 2])->gh)) gl_ghost(g, (&verts[k]), (&verts[k + 1]), (&verts[k + 2]), &L); }
    } else if (ptype == 4) {
        for (int k = 0; k + 2 < count; k++) {
            Vert *A = (k % 2 == 0) ? &verts[k] : &verts[k + 1], *B = (k % 2 == 0) ? &verts[k + 1] : &verts[k];
            if (A->ok && B->ok && verts[k + 2].ok) triangle(g, A, B, &verts[k + 2], &L, clear);
            else if (g_gl_on && !clear && (A->ok || A->gh) && (B->ok || B->gh) && ((&verts[k + 2])->ok || (&verts[k + 2])->gh)) gl_ghost(g, A, B, (&verts[k + 2]), &L);
        }
    } else if (ptype == 5) {
        for (int k = 1; k + 1 < count; k++) { if (verts[0].ok && verts[k].ok && verts[k + 1].ok) triangle(g, &verts[0], &verts[k], &verts[k + 1], &L, clear);
            else if (g_gl_on && !clear && ((&verts[0])->ok || (&verts[0])->gh) && ((&verts[k])->ok || (&verts[k])->gh) && ((&verts[k + 1])->ok || (&verts[k + 1])->gh)) gl_ghost(g, (&verts[0]), (&verts[k]), (&verts[k + 1]), &L); }
    } else if (ptype == 0) {
        Pipe P; setup(g, &P);
        for (int k = 0; k < count; k++) if (verts[k].ok) put(g, &P, (int)floor(verts[k].x), (int)floor(verts[k].y), verts[k].z, shade(g, verts[k].c, 0));
    } else if (ptype == 1 || ptype == 2) {
        int step = ptype == 1 ? 2 : 1;
        for (int k = 0; k + 1 < count; k += step) if (verts[k].ok && verts[k + 1].ok) line(g, verts[k], verts[k + 1], &L);
    }
    return 0;
}

/* ------------------------------------------------------------------ curved surfaces (BEZIER 0x05 / SPLINE 0x06)
 * The control points are blended with the curve weights, then the surface is cut into small triangles. */
static void bspline_knots(int n, int type, double *k) {          /* n = number of control points - 1, k has n+5 slots */
    for (int i = 0; i < n + 5; i++) k[i] = 0;
    for (int i = 0; i < n - 1; i++) k[i + 3] = i;
    if ((type & 1) == 0) { k[0] = -3; k[1] = -2; k[2] = -1; }
    if ((type & 2) == 0) { k[n + 2] = n - 1; k[n + 3] = n; k[n + 4] = n + 1; }
    else { k[n + 2] = n - 2; k[n + 3] = n - 2; k[n + 4] = n - 2; }
}
/* weights of control points i..i+3 at t inside knot span [k[i+3], k[i+4]] (Cox-de Boor) */
static void bspline_w(const double *k, int i, double t, double *w) {
    double N[7] = {0, 0, 0, 0, 0, 0, 0}; N[3] = 1;               /* degree 0: only span i+3 is on */
    for (int p = 1; p <= 3; p++)
        for (int j = 0; j + p < 7; j++) {
            int a = i + j; double v = 0;
            double d1 = k[a + p] - k[a], d2 = k[a + p + 1] - k[a + 1];
            if (d1 != 0) v += (t - k[a]) / d1 * N[j];
            if (d2 != 0) v += (k[a + p + 1] - t) / d2 * N[j + 1];
            N[j] = v;
        }
    for (int j = 0; j < 4; j++) w[j] = N[j];
}
static void bez_w(double t, double *w) { double s = 1 - t; w[0] = s * s * s; w[1] = 3 * t * s * s; w[2] = 3 * t * t * s; w[3] = t * t * t; }

typedef struct { double x, y, z, u, v; Col c; int has_uv; } CP;
static int g_patch_n;
GEXPORT int ge_patch_count(void) { return g_patch_n; }   /* (test aid) curved surfaces drawn so far */
static int ge_patch(GEState *g, uint32_t cmd, uint32_t d) {
    g_patch_n++;
    uint32_t *r = g->reg; g_drawgen++; g_lk_gen++;
    int nu = d & 0xFF, nv = (d >> 8) & 0xFF, tu = (d >> 16) & 3, tv = (d >> 18) & 3;
    int divu = r[0x36] & 0x7F, divv = (r[0x36] >> 8) & 0x7F, prim = r[0x37] & 3;
    if (divu < 1) divu = 1; if (divv < 1) divv = 1; if (divu > 64) divu = 64; if (divv > 64) divv = 64;
    Layout L; layout(r[0x12], &L);
    if (nu < 4 || nv < 4 || L.size == 0 || L.poff < 0 || nu * nv > 4096) return 0;
    if (cmd == 0x05 && ((nu - 1) % 3 || (nv - 1) % 3)) return 0;
    int n = nu * nv; static CP cp[4096]; static uint32_t ix[4096]; int mx = 0;
    if (L.idx) {
        int isz = ELEM[L.idx]; const uint8_t *ib = gloc(g, g->iaddr, (uint32_t)n * isz);
        if (!ib) { g->err_addr = g->iaddr; return 1; }
        for (int i = 0; i < n; i++) { uint32_t v = isz == 1 ? ib[i] : isz == 2 ? rd16le(ib + 2 * i) : rd32le(ib + 4 * i); if (v > 65535) return 1; ix[i] = v; if ((int)v + 1 > mx) mx = v + 1; }
    } else { for (int i = 0; i < n; i++) ix[i] = i; mx = n; }
    const uint8_t *vb = gloc(g, g->vaddr, (uint32_t)mx * L.size);
    if (!vb) { g->err_addr = g->vaddr; return 1; }
    Col amb = {(int)(r[0x55] & 255), (int)((r[0x55] >> 8) & 255), (int)((r[0x55] >> 16) & 255), (int)(r[0x58] & 255)};   /* no colour in the vertices: the material colour */
    g_no_light = 1;
    for (int i = 0; i < n; i++) {
        Vert t; decode_raw(g, &L, vb + (size_t)ix[i] * L.size, &t, amb, &cp[i].x, &cp[i].y, &cp[i].z);
        cp[i].u = t.u; cp[i].v = t.v; cp[i].c = t.c; cp[i].has_uv = t.has_uv;
    }
    g_no_light = 0;
    int lit = !L.through && (r[0x17] & 1);
    int pu, pv; double ku[260], kv[260];
    if (cmd == 0x05) { pu = (nu - 1) / 3; pv = (nv - 1) / 3; }
    else { pu = nu - 3; pv = nv - 3; bspline_knots(nu - 1, tu, ku); bspline_knots(nv - 1, tv, kv); }
    int gu = pu * divu + 1, gv = pv * divv + 1;                     /* grid of points over the whole surface */
    if ((size_t)gu * gv > 65536) return 0;
    /* Wholly off the screen? (as in ge_prim. The world of this game is made of such surfaces, a whole room at a time.)
     * A curved surface lies between its control points: when those are all past the same edge of the picture, so is
     * every point of it - found before the surface is cut up and lit. */
    if (prim == 0 && !L.through && gl_prim_cull_ok()) {
        static float pw[65536];
        double wv[12], m[16]; const double *W = g->world, *V = g->view, *P = g->proj;
        for (int c = 0; c < 4; c++) {
            double x = W[c * 3], y = W[c * 3 + 1], z = W[c * 3 + 2];
            for (int k = 0; k < 3; k++) wv[c * 3 + k] = x * V[k] + y * V[3 + k] + z * V[6 + k] + (c == 3 ? V[9 + k] : 0.0);
        }
        for (int c = 0; c < 4; c++) for (int k = 0; k < 4; k++)
            m[c * 4 + k] = wv[c * 3] * P[k] + wv[c * 3 + 1] * P[4 + k] + wv[c * 3 + 2] * P[8 + k] + (c == 3 ? P[12 + k] : 0.0);
        double sxs = gf32(r[0x42]), sys = gf32(r[0x43]), ox = gf32(r[0x45]) - (r[0x4C] & 0xFFFF) / 16.0, oy = gf32(r[0x46]) - (r[0x4D] & 0xFFFF) / 16.0;
        double X[4], Y[4], Wc[4];
        for (int c = 0; c < 4; c++) { Wc[c] = m[c * 4 + 3]; X[c] = m[c * 4] * sxs + Wc[c] * ox; Y[c] = m[c * 4 + 1] * sys + Wc[c] * oy; }
        const double mg = L.wf ? 160.0 : 100.0;
        double xlo = (double)(r[0xD4] & 0x3FF) - mg, ylo = (double)((r[0xD4] >> 10) & 0x3FF) - mg;
        double xhi = (double)(r[0xD5] & 0x3FF) + 1.0 + mg + 44.0, yhi = (double)((r[0xD5] >> 10) & 0x3FF) + 1.0 + mg + 76.0;
        int all = 31;
        for (int i = 0; i < n && all; i++) {
            double x = cp[i].x, y = cp[i].y, z = cp[i].z, w = x * Wc[0] + y * Wc[1] + z * Wc[2] + Wc[3];
            pw[i] = (float)w;
            if (!(w > 1e-2)) { all &= w < -PC_BEHIND ? 16 : 0; continue; }
            double sx = x * X[0] + y * X[1] + z * X[2] + X[3], sy = x * Y[0] + y * Y[1] + z * Y[2] + Y[3];
            all &= (sx < xlo * w ? 1 : 0) | (sx > xhi * w ? 2 : 0) | (sy < ylo * w ? 4 : 0) | (sy > yhi * w ? 8 : 0);
        }
        if (all) {
            int nw = (all & 16) ? 0 : 2 * (gu - 1) * (gv - 1);       /* (its triangles, counted as if drawn; how far: as its control points) */
            if (nw > 65536) nw = 65536;
            for (int k = n; k < nw; k++) pw[k] = pw[k % n];
            { Layout LT = L; LT.tc = LT.tc ? LT.tc : 3; gl_prim_culled(g, &LT, nw * 3, pw, nw); }   /* (the grid always carries uv) */
            return 0;
        }
    }
    static Vert grid[65536]; static double MP[65536][3];
    /* the curve weights depend only on the column (u) or only on the row (v): work them out once */
    static double WU[65537][4], WV[65537][4]; static int BU[65537], BV[65537];
    for (int pass = 0; pass < 2; pass++) {
        int gn = pass ? gv : gu, dv = pass ? divv : divu, pn = pass ? pv : pu; double *kk = pass ? kv : ku;
        for (int j = 0; j < gn; j++) {
            int q = j / dv; if (q >= pn) q = pn - 1;
            double sfrac = (double)(j - q * dv) / dv, *w = pass ? WV[j] : WU[j];
            if (cmd == 0x05) { bez_w(sfrac, w); (pass ? BV : BU)[j] = q * 3; }
            else { bspline_w(kk, q, kk[q + 3] + sfrac * (kk[q + 4] - kk[q + 3]), w); (pass ? BV : BU)[j] = q; }
        }
    }
    static double RW[4096][9];                                       /* one grid row: the 4 control rows blended together */

    for (int jv = 0; jv < gv; jv++) {
        const double *wv = WV[jv]; int bv = BV[jv];
        for (int col = 0; col < nu; col++) {
            double *o = RW[col]; for (int k = 0; k < 9; k++) o[k] = 0;
            for (int a = 0; a < 4; a++) {
                const CP *c = &cp[(bv + a) * nu + col]; double w = wv[a];
                o[0] += c->x * w; o[1] += c->y * w; o[2] += c->z * w; o[3] += c->u * w; o[4] += c->v * w;
                o[5] += c->c.r * w; o[6] += c->c.g * w; o[7] += c->c.b * w; o[8] += c->c.a * w;
            }
        }
        for (int ju = 0; ju < gu; ju++) {
            const double *wu = WU[ju]; int bu = BU[ju];
            double x = 0, y = 0, z = 0, u = 0, v = 0, cr = 0, cg = 0, cb = 0, ca = 0;
            for (int b = 0; b < 4; b++) {
                const double *c = RW[bu + b]; double w = wu[b];
                x += c[0] * w; y += c[1] * w; z += c[2] * w; u += c[3] * w; v += c[4] * w;
                cr += c[5] * w; cg += c[6] * w; cb += c[7] * w; ca += c[8] * w;
            }
            Vert *o = &grid[jv * gu + ju]; o->src = jv * gu + ju;
            if (!cp[0].has_uv) { u = (double)ju / divu; v = (double)jv / divv; }    /* generated: patch number + position in it */
            o->u = u; o->v = v; o->has_uv = 1;
            o->c.r = clamp255d(cr); o->c.g = clamp255d(cg); o->c.b = clamp255d(cb); o->c.a = clamp255d(ca);
            MP[jv * gu + ju][0] = x; MP[jv * gu + ju][1] = y; MP[jv * gu + ju][2] = z;
            if (L.through) { o->x = x; o->y = y; o->z = z; o->w = 1; o->ok = 1; o->fog = 1.0; }
            else project_raw(g, x, y, z, o);
        }
    }
    if (lit) {                                                       /* lighting: normals from the surface's slope */
        for (int jv = 0; jv < gv; jv++) for (int ju = 0; ju < gu; ju++) {
            int u0 = ju > 0 ? ju - 1 : ju, u1 = ju + 1 < gu ? ju + 1 : ju, v0 = jv > 0 ? jv - 1 : jv, v1 = jv + 1 < gv ? jv + 1 : jv;
            const double *a = MP[jv * gu + u0], *b = MP[jv * gu + u1], *c = MP[v0 * gu + ju], *e = MP[v1 * gu + ju];
            double tu[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, tv[3] = {e[0] - c[0], e[1] - c[1], e[2] - c[2]};
            double nn[3] = {tu[1] * tv[2] - tu[2] * tv[1], tu[2] * tv[0] - tu[0] * tv[2], tu[0] * tv[1] - tu[1] * tv[0]};
            if (r[0x38] & 1) { nn[0] = -nn[0]; nn[1] = -nn[1]; nn[2] = -nn[2]; }
            Vert *o = &grid[jv * gu + ju]; const double *m = MP[jv * gu + ju];
            light_core(g, nn, m[0], m[1], m[2], L.col != 0, o);
        }
    }
    if (prim != 0) return 0;                                         /* lines / points of patches: not drawn */
    Layout LT = L; LT.tc = LT.tc ? LT.tc : 3;                         /* the grid always carries uv */
    for (int jv = 0; jv + 1 < gv; jv++) for (int ju = 0; ju + 1 < gu; ju++) {
        Vert *a = &grid[jv * gu + ju], *b = &grid[jv * gu + ju + 1], *c = &grid[(jv + 1) * gu + ju], *e = &grid[(jv + 1) * gu + ju + 1];
        if (a->ok && b->ok && c->ok) triangle(g, a, c, b, &LT, 0);        /* (the GE's order: matters for culling) */
        else if (g_gl_on && (a->ok || a->gh) && (c->ok || c->gh) && (b->ok || b->gh)) gl_ghost(g, a, c, b, &LT);
        if (b->ok && e->ok && c->ok) triangle(g, b, c, e, &LT, 0);
        else if (g_gl_on && (b->ok || b->gh) && (c->ok || c->gh) && (e->ok || e->gh)) gl_ghost(g, b, c, e, &LT);
    }
    return 0;
}

GEXPORT int ge_state_size(void) { return (int)sizeof(GEState); }

/* picture on screen -> 480x272 RGB bytes (for the game window) */
GEXPORT void fb_to_rgb(const uint8_t *vram, uint32_t addr, int stride, int fmt, uint8_t *out) {
    uint32_t off = (addr & 0x3FFFFFFF) - 0x04000000u; int bpp = fmt == 3 ? 4 : 2;
    if ((addr & 0x3FFFFFFF) < 0x04000000u || off >= 0x200000u) { memset(out, 0, 480 * 272 * 3); return; }
    if (stride <= 0) stride = 512;
    for (int y = 0; y < 272; y++) {
        for (int x = 0; x < 480; x++) {
            uint32_t p = off + ((uint32_t)y * stride + x) * bpp, v = 0;
            if (p + bpp <= 0x200000u) for (int i = 0; i < bpp; i++) v |= (uint32_t)vram[p + i] << (8 * i);
            Col c = unpack_color(v, fmt);
            uint8_t *o = out + (y * 480 + x) * 3; o[0] = (uint8_t)c.r; o[1] = (uint8_t)c.g; o[2] = (uint8_t)c.b;
        }
    }
}

/* ------------------------------------------------------------------ display-list interpreter
 * Runs the game's GE command list in C (psp_ge.py's GE._run did this in Python and that was the slowest part
 * of the whole port). Anything rare is handed back to Python: the return value says why. */
typedef struct {
    uint32_t pc, stall, base, offset_addr;
    uint32_t stack[32]; int32_t sp;
    int32_t mi[4];                       /* matrix load positions: world 0x3A, view 0x3C, proj 0x3E, bones 0x2A */
    int32_t prev, has_sig; uint32_t sig; int32_t fin;
    int32_t render, clut_changed;
    uint64_t commands, prims;
    uint32_t stop_cmd, stop_data;
} GEList;

static inline uint32_t gaddr(GEList *L, uint32_t d) { return (L->offset_addr + (((L->base & 0x000F0000u) << 8) | d)) & 0x0FFFFFFFu; }

/* 0 = reached the stall address, 1 = END (fin says whether FINISH came first), 2 = SIGNAL for python (stop_data),
 * 3 = command for python (stop_cmd/stop_data), 4 = bad memory (err_addr), 5 = too many steps, 6 = draw error */
/* another thread wants the drawing thread for a moment (a picture due at the next screen refresh): ge_run stops after
 * the current draw command with 7, Python draws the picture and carries on with the list */
static volatile int g_ge_yield;
GEXPORT void ge_yield(int on) { g_ge_yield = on; }
static int ge_run__b(GEState *g, GEList *L, int max_steps);
GEXPORT int ge_run(GEState *g, GEList *L, int max_steps) { uint64_t _t = pf_top_begin(); int _r = ge_run__b(g, L, max_steps); pf_top_end(3, _t); return _r; }
static int ge_run__b(GEState *g, GEList *L, int max_steps) {
    uint32_t *reg = g->reg;
    for (int steps = 0; steps < max_steps; steps++) {
        uint32_t pc = L->pc;
        if (L->stall && pc == L->stall) return 0;
        const uint8_t *p = gloc(g, pc, 4);
        if (!p) { g->err_addr = pc; return 4; }
        uint32_t w = rd32le(p), c = w >> 24, d = w & 0xFFFFFFu;
        L->pc = pc + 4; L->commands++;
        int prev = L->prev; L->prev = (int)c;
        switch (c) {
        case 0x0F: L->fin = 1; continue;
        case 0x0E: L->sig = d; L->has_sig = 1; continue;
        case 0x0C:
            if (prev == 0x0E) {
                uint32_t sd = L->has_sig ? L->sig : 0, beh = (sd >> 16) & 0xFF; L->has_sig = 0; L->sig = 0;
                if (beh == 0x10 || beh == 0x11 || beh == 0x13 || beh == 0x14) {
                    uint32_t tgt = ((sd & 0xFFFF) << 16) | (d & 0xFFFF);
                    if (beh == 0x13 || beh == 0x14) tgt += pc - 4;
                    if ((beh == 0x11 || beh == 0x14) && L->sp < 32) L->stack[L->sp++] = L->pc;
                    L->pc = tgt & 0x0FFFFFFCu;
                } else if (beh == 0x12) { if (L->sp > 0) L->pc = L->stack[--L->sp]; }
                else { L->stop_data = sd; return 2; }
                continue;
            }
            return 1;
        case 0x08: L->pc = gaddr(L, d) & ~3u; continue;
        case 0x0A: if (L->sp < 32) L->stack[L->sp++] = L->pc; L->pc = gaddr(L, d) & ~3u; continue;
        case 0x0B: if (L->sp > 0) L->pc = L->stack[--L->sp]; continue;
        }
        reg[c] = d;
        switch (c) {
        case 0x10: L->base = d; break;
        case 0x13: L->offset_addr = (d << 8) & 0x0FFFFFFFu; break;
        case 0x14: L->offset_addr = pc & 0x0FFFFFFFu; break;
        case 0x01: g->vaddr = gaddr(L, d); break;
        case 0x02: g->iaddr = gaddr(L, d); break;
        case 0x04: case 0x05: case 0x06: {
            L->prims++;
            if (L->render) {
                uint64_t _p0 = PF_TSC(), _t6 = g_pf[6], _t7 = g_pf[7], _t10 = g_pf[10]; uint64_t _px = g->pixels;
                int res = c == 0x04 ? ge_prim(g, d) : ge_patch(g, c, d); pf_add(15, _p0);
                uint64_t _dt = PF_TSC() - _p0;
                if (g_sp_thr && _dt > g_sp_thr) {                        /* (a slow draw command: kept for the stutter log) */
                    uint64_t *o = g_sp[g_sp_n++ & 63]; uint32_t *r = g->reg;
                    o[0] = _p0; o[1] = _dt; o[2] = (uint64_t)c | ((uint64_t)(d & 0xFFFF) << 8); o[3] = r[0x9C] & 0x1FFFFF;
                    o[4] = (r[0x1E] & 1) ? (((r[0xA0] & 0xFFFFFF) | ((r[0xA8] & 0xFF0000) << 8)) | ((uint64_t)(r[0xB8] & 15) << 32) | ((uint64_t)((r[0xB8] >> 8) & 15) << 40)) : 0;
                    o[5] = (uint64_t)gl_sw_wanted(g) | ((uint64_t)((r[0x12] >> 23) & 1) << 1); o[6] = g->pixels - _px;
                    o[7] = g_pf[6] - _t6; o[8] = g_pf[7] - _t7; o[9] = g_pf[10] - _t10;
                }
                if (res == 1) return 6;
            }
            Layout LY; layout(reg[0x12], &LY);                         /* the GE moves on past the vertices it used */
            uint32_t cnt = c == 0x04 ? (d & 0xFFFF) : (d & 0xFF) * ((d >> 8) & 0xFF);
            if (LY.idx) g->iaddr += cnt * ELEM[LY.idx]; else g->vaddr += cnt * LY.size;
            if (g_ge_yield) return 7;
            break; }
        case 0x3A: L->mi[0] = d & 0x7F; break;
        case 0x3C: L->mi[1] = d & 0x7F; break;
        case 0x3E: L->mi[2] = d & 0x7F; break;
        case 0x2A: L->mi[3] = d & 0x7F; break;
        case 0x3B: g->world[L->mi[0] % 12] = gf32(d); L->mi[0]++; break;
        case 0x3D: g->view[L->mi[1] % 12] = gf32(d); L->mi[1]++; break;
        case 0x3F: g->proj[L->mi[2] % 16] = gf32(d); L->mi[2]++; break;
        case 0x2B: g->bones[L->mi[3] % 96] = gf32(d); L->mi[3]++; break;
        case 0xC4: {                                                     /* load colour table */
            uint32_t a = (reg[0xB0] & 0xFFFFFFu) | ((reg[0xB1] & 0x0F0000u) << 8), n = (d & 0x3F) * 32;
            const uint8_t *src = n ? gloc(g, a, n) : 0;
            if (n > 2048) n = 2048;
            if (src) { memcpy(g->clut, src, n); g->clut_len = n; } else g->clut_len = 0;
            if (n > g_clut_max) g_clut_max = n;
            L->clut_changed = 1; break; }
        case 0xEA: L->stop_cmd = c; L->stop_data = d; return 3;        /* block copy: python does it */
        }
    }
    return 5;
}
GEXPORT int ge_list_size(void) { return (int)sizeof(GEList); }

/* ------------------------------------------------------------------ the PSP sound chip's mixing (sceSasCore), native.
 * The same steps as psp_sound.py's Sas.mix did in Python - per voice, per sample - which with many sounds at once (an
 * alert: music, gunfire, a dive) took enough of the game thread to slow the game down. psp_sound.py keeps the voices
 * and copies the playing ones in and out of SasV around each call. */
typedef struct {
    int64_t addr, size, loop, pitch, lv, rv, env, rel_step;
    int64_t ar, dr, sr, rr, sl;
    int32_t at, dt, stp, rt, st;
    int32_t pcm, playing, adsr, release, loopflag, done, endnext;
    int64_t blk, loop_blk;            /* loop_blk < 0: none */
    int32_t s1, s2, bi, nbuf, cur, prev;
    double frac;
    int16_t buf[28];
} SasV;
enum { SAS_ATTACK, SAS_DECAY, SAS_SUSTAIN, SAS_RELEASE, SAS_OFF };
#define SAS_ENV_MAX 0x40000000LL
static const int sas_vagf[16][2] = { {0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60} };
static uint8_t *sas_ram, *sas_vram, *sas_scr;
static const uint8_t *sas_loc(uint64_t a, uint32_t n) {
    a &= 0x3FFFFFFF;
    if (a >= 0x08000000u && a + n <= 0x0A000000u) return sas_ram + (a - 0x08000000u);
    if (a >= 0x04000000u && a + n <= 0x04200000u) return sas_vram + (a - 0x04000000u);
    if (a >= 0x00010000u && a + n <= 0x00014000u) return sas_scr + (a - 0x00010000u);
    return 0;
}
static int64_t sas_fdiv(int64_t a, int64_t b) { int64_t q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }   /* Python // */
static void sas_walk(SasV *v, int typ, int64_t rate) {
    int64_t h = v->env, d;
    if (typ == 0) h += rate;
    else if (typ == 1) h -= rate;
    else if (typ == 2) h += h <= SAS_ENV_MAX * 3 / 4 ? rate : sas_fdiv(rate, 4);
    else if (typ == 3) { d = h - SAS_ENV_MAX; d += (int64_t)(((__int128)(-d) * rate) >> 32); h = d + SAS_ENV_MAX - sas_fdiv(rate + 3, 4); }
    else if (typ == 4) { d = h - SAS_ENV_MAX; d += (int64_t)(((__int128)(-d) * rate) >> 32); h = d + 0x4000 + SAS_ENV_MAX; }
    else h = rate;
    v->env = h;
}
static int64_t sas_clampenv(int64_t e) { return e < 0 ? 0 : e > SAS_ENV_MAX ? SAS_ENV_MAX : e; }
static void sas_env_step(SasV *v) {
    switch (v->st) {
    case SAS_ATTACK: sas_walk(v, v->at, v->ar); if (v->env >= SAS_ENV_MAX || v->env < 0) { v->env = sas_clampenv(v->env); v->st = SAS_DECAY; } break;
    case SAS_DECAY: sas_walk(v, v->dt, v->dr); if (v->env < v->sl) { v->env = sas_clampenv(v->env); v->st = SAS_SUSTAIN; } break;
    case SAS_SUSTAIN: sas_walk(v, v->stp, v->sr); if (v->env <= 0) { v->env = 0; v->st = SAS_RELEASE; } break;
    case SAS_RELEASE: sas_walk(v, v->rt, v->rr); if (v->env <= 0) { v->env = 0; v->st = SAS_OFF; } break;
    }
}
static void sas_decode(SasV *v) {
    int64_t a = v->addr + v->blk * 16;
    if (v->size && v->blk * 16 >= v->size) { v->nbuf = 0; v->done = 1; return; }
    const uint8_t *b = sas_loc((uint64_t)a, 16);
    if (!b) { v->nbuf = 0; v->done = 1; return; }
    int pred = b[0] >> 4, shift = b[0] & 15, flags = b[1];
    if (flags & 4) v->loop_blk = v->blk;
    if (flags == 7) { v->nbuf = 0; v->done = 1; return; }
    int f0 = sas_vagf[pred][0], f1 = sas_vagf[pred][1], s1 = v->s1, s2 = v->s2, sh = shift <= 12 ? 12 - shift : 0;
    for (int i = 0; i < 28; i++) {
        int n = (b[2 + (i >> 1)] >> ((i & 1) ? 4 : 0)) & 15;
        n = (n & 8 ? n - 16 : n) * (1 << sh);
        int64_t p = (int64_t)s1 * f0 + (int64_t)s2 * f1, s = n + (p >> 6);
        s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
        v->buf[i] = (int16_t)s; s2 = s1; s1 = (int)s;
    }
    v->s1 = s1; v->s2 = s2; v->nbuf = 28; v->bi = 0;
    if (flags & 1) {
        if ((flags & 2) && v->loop_blk >= 0 && v->loop) v->blk = v->loop_blk;
        else { v->blk += 1; v->endnext = 1; v->size = v->blk * 16; }
    } else v->blk += 1;
}
static int sas_next(SasV *v) {
    if (v->pcm) {
        int64_t i = v->blk, half = sas_fdiv(v->size, 2);
        if (i >= half) {
            if (v->loop >= 0 && v->loop < half && v->loopflag) i = v->blk = v->loop;
            else { v->done = 1; return 0; }
        }
        v->blk += 1;
        const uint8_t *p = sas_loc((uint64_t)(v->addr + 2 * i), 2);
        if (!p) return 0;
        return (int16_t)(p[0] | p[1] << 8);
    }
    if (v->bi >= v->nbuf) {
        if (v->done) return 0;
        sas_decode(v);
        if (!v->nbuf) { v->done = 1; return 0; }
    }
    return v->buf[v->bi++];
}
/* mixes the voices into L / R (grain samples each, added to) */
GEXPORT void sas_mix(uint8_t *ram, uint8_t *vram, uint8_t *scr, SasV *vs, int nv, int grain, int64_t *L, int64_t *R) {
    sas_ram = ram; sas_vram = vram; sas_scr = scr;
    for (int k = 0; k < nv; k++) {
        SasV *v = &vs[k]; double step = (double)v->pitch / 4096.0;
        for (int i = 0; i < grain; i++) {
            v->frac += step;
            while (v->frac >= 1.0) { v->frac -= 1.0; v->prev = v->cur; v->cur = sas_next(v); }
            if (v->adsr) {
                sas_env_step(v);
                if (v->st == SAS_OFF) { v->env = 0; v->playing = 0; break; }
            } else if (v->release) {
                v->env -= v->rel_step;
                if (v->env <= 0) { v->env = 0; v->playing = 0; break; }
            }
            if (v->done && v->bi >= v->nbuf) { v->playing = 0; v->env = 0; break; }
            int64_t e = v->env < SAS_ENV_MAX ? v->env : SAS_ENV_MAX, s = ((int64_t)v->cur * (e >> 15)) >> 15;
            L[i] += (s * v->lv) >> 12; R[i] += (s * v->rv) >> 12;
        }
    }
}
/* the finished buffer: (with the old contents mixed in, CoreWithMix) clamped and written as 16-bit stereo */
GEXPORT void sas_out(int16_t *out, const int64_t *L, const int64_t *R, int grain, int withmix, int64_t lv, int64_t rv) {
    for (int i = 0; i < grain; i++) {
        int64_t l = L[i], r = R[i];
        if (withmix) { l += ((int64_t)out[2 * i] * lv) >> 12; r += ((int64_t)out[2 * i + 1] * rv) >> 12; }
        out[2 * i] = (int16_t)(l < -32768 ? -32768 : l > 32767 ? 32767 : l);
        out[2 * i + 1] = (int16_t)(r < -32768 ? -32768 : r > 32767 ? 32767 : r);
    }
}
