/*
 * psp_gl.inc - drawing on the graphics card (OpenGL 3.3) for the MGS Portable Ops PC port.
 * Included by psp_gpu.c. The same draw commands the software renderer handles are turned into OpenGL draws into
 * one off-screen picture (framebuffer object) per PSP framebuffer address, at SCALE times the PSP resolution.
 * The software renderer keeps running too (unless switched off), so PSP video memory stays exactly as before:
 * effects that read it back, and the game's own reads, behave as they always did.
 *
 * Everything OpenGL is reached through function pointers that Python hands over (gl_setup), so the DLL still
 * needs no import libraries: on Windows they come from opengl32.dll / wglGetProcAddress, on Linux from EGL.
 */

/* ------------------------------------------------------------------ the OpenGL calls we use */
typedef unsigned int GLenum, GLuint, GLbitfield; typedef int GLint, GLsizei; typedef float GLfloat; typedef double GLdouble;
typedef unsigned char GLboolean; typedef char GLchar; typedef intptr_t GLsizeiptr, GLintptr;
static void snprintf_(char *d, int n, const char *fmt, ...) {          /* just enough: '%s' with one string */
    __builtin_va_list ap; __builtin_va_start(ap, fmt); const char *arg = 0; int k = 0;
    for (const char *f = fmt; *f && k < n - 1; f++) {
        if (f[0] == '%' && f[1] == 's') { if (!arg) arg = __builtin_va_arg(ap, const char *); for (const char *a = arg; *a && k < n - 1; a++) d[k++] = *a; f++; }
        else d[k++] = *f;
    }
    d[k] = 0; __builtin_va_end(ap);
}
#define GLFN(ret, name, args) static ret (*p_##name) args
GLFN(void, glGenTextures, (GLsizei, GLuint *)); GLFN(void, glDeleteTextures, (GLsizei, const GLuint *));
GLFN(void, glBindTexture, (GLenum, GLuint)); GLFN(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *));
GLFN(void, glTexParameteri, (GLenum, GLenum, GLint)); GLFN(void, glActiveTexture, (GLenum));
GLFN(void, glGenFramebuffers, (GLsizei, GLuint *)); GLFN(void, glBindFramebuffer, (GLenum, GLuint));
GLFN(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)); GLFN(GLenum, glCheckFramebufferStatus, (GLenum));
GLFN(void, glGenRenderbuffers, (GLsizei, GLuint *)); GLFN(void, glBindRenderbuffer, (GLenum, GLuint));
GLFN(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)); GLFN(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint));
GLFN(void, glViewport, (GLint, GLint, GLsizei, GLsizei)); GLFN(void, glScissor, (GLint, GLint, GLsizei, GLsizei));
GLFN(void, glEnable, (GLenum)); GLFN(void, glDisable, (GLenum)); GLFN(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat));
GLFN(void, glClearDepth, (GLdouble)); GLFN(void, glClear, (GLbitfield)); GLFN(void, glDepthFunc, (GLenum)); GLFN(void, glDepthMask, (GLboolean));
GLFN(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)); GLFN(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum));
GLFN(void, glBlendEquation, (GLenum)); GLFN(void, glBlendColor, (GLfloat, GLfloat, GLfloat, GLfloat));
GLFN(GLuint, glCreateShader, (GLenum)); GLFN(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *));
GLFN(void, glCompileShader, (GLuint)); GLFN(void, glGetShaderiv, (GLuint, GLenum, GLint *)); GLFN(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *));
GLFN(GLuint, glCreateProgram, (void)); GLFN(void, glAttachShader, (GLuint, GLuint)); GLFN(void, glLinkProgram, (GLuint));
GLFN(void, glGetProgramiv, (GLuint, GLenum, GLint *)); GLFN(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *));
GLFN(void, glUseProgram, (GLuint)); GLFN(GLint, glGetUniformLocation, (GLuint, const GLchar *));
GLFN(void, glUniform1i, (GLint, GLint)); GLFN(void, glUniform2f, (GLint, GLfloat, GLfloat)); GLFN(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat));
GLFN(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *));
GLFN(void, glGenVertexArrays, (GLsizei, GLuint *)); GLFN(void, glBindVertexArray, (GLuint));
GLFN(void, glGenBuffers, (GLsizei, GLuint *)); GLFN(void, glBindBuffer, (GLenum, GLuint));
GLFN(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum));
GLFN(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)); GLFN(void, glEnableVertexAttribArray, (GLuint));
GLFN(void, glDrawArrays, (GLenum, GLint, GLsizei)); GLFN(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *));
GLFN(void, glPixelStorei, (GLenum, GLint)); GLFN(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum));
GLFN(GLenum, glGetError, (void)); GLFN(void, glFinish, (void)); GLFN(void, glFlush, (void));
GLFN(void, glRenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei));   /* (optional ones) */
GLFN(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)); GLFN(void, glUniform1f, (GLint, GLfloat));
GLFN(void, glGenerateMipmap, (GLenum)); GLFN(void, glTexParameterf, (GLenum, GLenum, GLfloat));
GLFN(void, glStencilFunc, (GLenum, GLint, GLuint)); GLFN(void, glStencilOp, (GLenum, GLenum, GLenum)); GLFN(void, glStencilMask, (GLuint)); GLFN(void, glClearStencil, (GLint));
#define GL_STENCIL_TEST 0x0B90
#define GL_STENCIL_BUFFER_BIT 0x0400
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A

#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_SCISSOR_TEST 0x0C11
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_DEPTH_CLAMP 0x864F
#define GL_CULL_FACE 0x0B44
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_NEVER 0x0200
#define GL_ALWAYS 0x0207
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_CONSTANT_COLOR 0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#define GL_FUNC_ADD 0x8006
#define GL_MIN 0x8007
#define GL_MAX 0x8008
#define GL_FUNC_SUBTRACT 0x800A
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_TRIANGLES 0x0004
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_UNPACK_ALIGNMENT 0x0CF5


/* ---- (stutter log) where the drawing thread's time went in a slow job. Each top-level job (a picture, a new game
 * frame, a piece of the game's drawing, an HD texture) adds up the time of its parts; a job longer than the threshold
 * is kept with its parts in a small ring that play.py reads (gl_prof_take) for stutter_logs. Times in TSC ticks. */
#define PF_N 16
#define PF_TSC() __builtin_ia32_rdtsc()
typedef struct { uint64_t kind, when, total, part[PF_N], cnt[PF_N]; } PfRec;
static uint64_t g_pf[PF_N], g_pfc[PF_N], g_pf_thr; static int g_pf_depth; static PfRec g_pfr[64]; static volatile uint32_t g_pfr_n;
static uint64_t g_pfsum[5][2 + 2 * PF_N];
static inline void pf_add(int i, uint64_t t0) { g_pf[i] += PF_TSC() - t0; g_pfc[i]++; }
static inline uint64_t pf_top_begin(void) { if (!g_pf_depth++) { memset(g_pf, 0, sizeof g_pf); memset(g_pfc, 0, sizeof g_pfc); } return PF_TSC(); }
static inline void pf_top_end(int kind, uint64_t t0) {
    uint64_t t1 = PF_TSC(), d = t1 - t0;
    if (--g_pf_depth > 0) return;
    g_pf_depth = 0;
    if (kind >= 0 && kind < 5) {                                       /* running totals of every job, by kind */
        uint64_t *S = g_pfsum[kind]; S[0]++; S[1] += d;
        for (int i = 0; i < PF_N; i++) { S[2 + i] += g_pf[i]; S[2 + PF_N + i] += g_pfc[i]; }
    }
    if (!g_pf_thr || d < g_pf_thr) return;
    PfRec *r = &g_pfr[g_pfr_n & 63]; r->kind = (uint64_t)kind; r->when = t0; r->total = d;
    memcpy(r->part, g_pf, sizeof g_pf); memcpy(r->cnt, g_pfc, sizeof g_pfc); g_pfr_n++;
}
GEXPORT void gl_prof_threshold(uint64_t ticks) { g_pf_thr = ticks; }   /* 0 = off */
/* running totals since start, per job kind 0-4: jobs, ticks, ticks per part [PF_N], calls per part [PF_N] */
GEXPORT void gl_prof_sums(uint64_t *out) { memcpy(out, g_pfsum, sizeof g_pfsum); }
/* the slow jobs since *from (a running count): each 3 + 2 * PF_N numbers; returns how many were copied */
GEXPORT int gl_prof_take(uint32_t *from, uint64_t *out, int max) {
    uint32_t n = g_pfr_n, i = *from; int m = 0;
    if (n - i > 64) i = n - 64;
    for (; i != n && m < max; i++, m++) { const PfRec *r = &g_pfr[i & 63]; memcpy(out + (size_t)m * (3 + 2 * PF_N), r, sizeof *r); }
    *from = i; return m;
}
/* ------------------------------------------------------------------ state */
static uint32_t g_skip_tex = 0xFFFFFFFF;
static int g_gl_on = 0, g_sw_on = 1, g_scale = 1, g_filter = 0, g_msaa = 1, g_mips = 0, g_stencil = 0; static float g_aniso = 0;
static char g_gl_err[512];
static GLuint g_prog, g_vao, g_vbo;
static GLint u_shadow, u_lmode, u_lk, u_lcam, u_ao, u_aox, u_aoy, u_aos;
static GLint u_fbsize, u_tex, u_texon, u_tfunc, u_ta, u_atest, u_aref, u_amask, u_env, u_dbl, u_fogon, u_fogcol;

/* fbo/color: the picture as a texture. With anti-aliasing (MSAA) drawing goes into msfbo (several samples per pixel)
 * and is "resolved" into color when the picture is needed (dirty = drawn since). s*: the 60 fps shadow copy. */
typedef struct { uint32_t addr; int fmt, w, h; GLuint fbo, color, depth; uint32_t last_use, drawn; int need_sw; uint32_t dl_gen, ndraw, dl_frame, dl_req, rd_frame, cgen; int dl_cnt, gpu_skip; GLuint cfbo, ccolor;
                 GLuint sfbo, scolor, sdepth; GLuint msfbo, mscolor, msdepth, smsfbo, smscolor, smsdepth; int dirty; } GLFb;
#define MAX_FB 16
static GLFb g_fbs[MAX_FB]; static int g_nfb; static uint32_t g_frame_no;

typedef struct { uint64_t key, hash; GLuint id; int w, h; uint32_t last_use; int rep, raw; uint32_t ka, kb, kc; } GLTex;   /* k*: its HD pack name (0 = none) */   /* raw: the PSP picture not decoded yet (the HD one is used) */   /* rep: HD replacement (index) or -1 */
#define MAX_TEX 1024
static GLTex g_tex[MAX_TEX]; static int g_ntex;

typedef struct { float x, y, z, w, u, v; uint8_t r, g, b, a; float fog; float lx, ly, lz; } GLVtx;   /* l: place in the shadow picture (lz < 0: none) */
#define MAX_VTX 196608
static GLVtx g_vtx[MAX_VTX]; static int g_nvtx;
/* where put_vtx writes: g_vtx (streaming), or the current frame recording (60 fps mode) from g_vbase on */
static GLVtx *g_vbuf = g_vtx; static int g_vcap = MAX_VTX, g_vbase = 0;

/* ---- 60 fps (in-between pictures). The game runs at its own 20 fps; every draw of a frame is recorded (its
 * OpenGL state and its vertices). Between two real frames the last frame is drawn again with every vertex moved part
 * of the way from where the same draw was in the frame before. Drawn into shadow copies of the framebuffers, so
 * the real ones stay exactly as the game left them. */
typedef struct {
    int fbi; uint32_t fbaddr; int sc[4];                      /* scissor rectangle in PSP pixels, x1/y1 exclusive */
    uint8_t zon, zf, zmask, bon, through; GLenum bs, bd, beq; int bfix; float bc[4];
    uint8_t son, sf, sref, smask, sop[3], swmask;             /* the PSP's stencil: on, test, value, masks, fail / depth fail / pass */
    GLuint tex; uint8_t tfilt, clu, clv; int texon, tfunc, ta, dbl, atest, amask, aref, fogon; float env[3], fogcol[3];
    uint32_t va;                                              /* where the draw's vertices are in PSP memory */
    uint8_t skin;                                             /* drawn with bones (a character): moves by itself */
    int8_t pj; uint8_t lit; float ldir[3];                    /* lighting: camera set (-1 none), lit by a sun-like light + its direction */
} GLSt;
typedef struct { GLSt st; int kind, v0, n, match, ssw, ex, g0, gn, gok; uint64_t key, key2; GLbitfield bits; float cc[4]; double cd; float cx, cy; } GLCmd;   /* kind 0 draw, 1 clear; key2 = key + vertex address; cx,cy = middle on screen */
#define FR_VTX (1 << 19)                                      /* vertices per recorded frame (the game uses ~70-140 thousand) */
#define FR_CMD 16384
typedef struct { float X, Y, Z, W, fog, u, v; } GLPrev;   /* u, v: its texture place then (scrolling dust, water) */      /* where a vertex was one frame before, in clip space */
typedef struct { GLVtx *v; GLCmd *c; int *src; GLPrev *pv; uint8_t *pf; GLVtx *gv; uint8_t *gc; int ngv, gbase; int nv, nc, over, ok; uint32_t fbmask; int lerped, rejected, unpaired, guessed, cut, rescued; } GLRec;
static GLVtx g_rv[4][FR_VTX]; static GLCmd g_rc[4][FR_CMD]; static GLVtx g_lv[FR_VTX];
/* triangles the game drew but the PSP left out because a corner was behind the camera: not in the real picture, but an
 * in-between picture's camera may see all of them (a wall right beside the camera as it swings: the pale blocks) */
#define FR_GV (1 << 16)
static GLVtx g_rgv[4][FR_GV]; static uint8_t g_rgc[4][FR_GV / 3];
static int g_rs[4][FR_VTX];        /* per recorded vertex: which vertex of its draw it came from */
static GLPrev g_rp[4][FR_VTX];     /* per recorded vertex: where it was in the frame before */
/* screen edges: what the game stops drawing as it leaves the screen is missing from the new frame, so in-between
 * pictures had nothing there and showed what lies behind it (far scenery, sky) for a picture or two when the camera
 * turned. Those draws of the frame before are drawn too, moved on by the camera: g_rfw = where each point of a frame
 * is in the frame after it (if it stood still), g_rlost = the draws of the frame before a frame that left the screen */
static GLPrev g_rfw[4][FR_VTX]; static int g_rlost[4][FR_CMD], g_rnlost[4], g_lost_on = 1;
GEXPORT void gl_lost(int on) { g_lost_on = on; }   /* (test aid) */
static uint8_t g_rf[4][FR_VTX];    /* ... 0 = known, 1 = wasn't there (its triangle is left out), 3 = not known yet */
static int *g_sbuf;                /* = g_rs[current] while recording */
static GLRec g_rec[4]; static int g_rcur = 0, g_rlast = 1, g_rprev = 2, g_rprev2 = 3, g_rec_on = 0;   /* (prev2: so in-between pictures can still use the pair before, when a frame comes early) */
/* ---- remaster lighting: what each recorded frame needs to put real shadows into its pictures. The vertices only
 * keep their place on the screen and their distance (w); with the frame's camera (projection + view matrix) that
 * gives back their place in the world, where a shadow picture from the sun's direction can be made. */
typedef struct { double sx, sy, ox, oy, sz, oz; double P[16], V[12]; double Mi[9]; int ok; int nv; } GLPj;   /* one camera set (sz, oz: depth scale and offset) */
#define MAX_PJ 16
typedef struct { GLPj pj[MAX_PJ]; int npj, main; double lsum[3]; double lw; double B[9], bmin[3], bsize, zsize; int box; double dbg[8]; } GLLit;
static GLLit g_lit[4];
/* (speed) the screen-place error of each recorded point (see scr_resid), worked out once per recorded frame, not again
 * for each of its in-between pictures: per frame slot, per draw a mark of the recording it was worked out for */
typedef struct { float x, y, z; } GLRes;
static GLRes g_rres[4][FR_VTX]; static uint32_t g_rres_mark[4][FR_CMD], g_rgen[4] = {1, 1, 1, 1};
static int g_light_on;                                       /* 0 off, 1 remaster */
static GLSt g_st;                                             /* state of the batch being drawn */
static void ap_forget(void);
static uint32_t g_batch_gen = 0xFFFFFFFF; static const Layout *g_batch_L; static int g_batch_uv = -1, g_batch_ok;
static int g_bt_fbt; static float g_bt_fu, g_bt_fv;   /* (a framebuffer's picture as the texture, straight from the graphics card: its coordinates) */
static int g_bt_tex, g_bt_through, g_bt_flat, g_bt_tw, g_bt_th, g_bt_clu, g_bt_clv; static double g_bt_su, g_bt_sv, g_bt_ou, g_bt_ov;
static GLFb *g_cur_fb;
/* Software drawing is only kept for PSP framebuffers whose pictures the game itself reads back (used as a texture
 * for an effect, or copied with a block transfer): only those need correct PSP video memory. Everything else is
 * drawn by the graphics card alone. (gl_setup(..., software_too=1) keeps software drawing for everything.) */
static uint32_t g_swq_gen = 0xFFFFFFFF; static int g_swq;
static int gl_sw_wanted(GEState *g) {
    if (g_sw_on || !g_gl_on) return 1;
    if (g_swq_gen == g_drawgen) return g_swq;
    uint32_t a = g->reg[0x9C] & 0x1FFFFF; int w = 1;
    for (int i = 0; i < g_nfb; i++) if (g_fbs[i].addr == a) { w = g_fbs[i].need_sw; break; }
    g_swq = w; g_swq_gen = g_drawgen; return w;
}
/* the game reads n bytes of video memory at addr: framebuffers there need their software picture from now on */
GEXPORT int gl_note_read(uint32_t addr, uint32_t n) {
    addr &= 0x3FFFFFFF; int marked = 0;
    if (addr < 0x04000000u || addr >= 0x04200000u) return 0;
    uint32_t lo = addr - 0x04000000u, hi = lo + (n ? n : 1);
    for (int i = 0; i < g_nfb; i++) {
        GLFb *f = &g_fbs[i]; uint32_t bpp = f->fmt == 3 ? 4 : 2, flo = f->addr, fhi = f->addr + (uint32_t)f->w * 272 * bpp;
        if (lo < fhi && hi > flo) { f->rd_frame = g_frame_no; if (!f->need_sw) { f->need_sw = 1; marked++; g_swq_gen = 0xFFFFFFFF; } }
    }
    return marked;
}
GEXPORT int gl_sw_count(void) { int n = 0; for (int i = 0; i < g_nfb; i++) n += g_fbs[i].need_sw; return n; }
/* (test aid) each framebuffer: address, format, width, software too, card skipped, last drawn, last read, draws */
GEXPORT int gl_fb_info(uint32_t *o, int max) { int n = 0; for (int i = 0; i < g_nfb && n < max; i++, n++) { GLFb *f = &g_fbs[i]; o[n * 8] = f->addr; o[n * 8 + 1] = (uint32_t)f->fmt; o[n * 8 + 2] = (uint32_t)f->w; o[n * 8 + 3] = (uint32_t)f->need_sw; o[n * 8 + 4] = (uint32_t)f->gpu_skip; o[n * 8 + 5] = g_frame_no - f->drawn; o[n * 8 + 6] = g_frame_no - f->rd_frame; o[n * 8 + 7] = f->ndraw; } return n; }
static int g_dbg_on, g_dbg_n; static uint32_t g_dbg[2048][12], g_dbg2[2048][4];
GEXPORT void gl_debug2(uint32_t *out, int n) { memcpy(out, g_dbg2, sizeof(uint32_t) * 4 * (size_t)n); }
GEXPORT int gl_debug(int on, uint32_t *out) { int n = g_dbg_n; if (out) memcpy(out, g_dbg, sizeof(uint32_t) * 12 * (size_t)n); g_dbg_on = on; g_dbg_n = 0; return n; }

static void *(*g_getproc)(const char *);
static int load_gl(void) {
#define L_(name) if (!(*(void **)&p_##name = g_getproc(#name))) { snprintf_(g_gl_err, sizeof g_gl_err, "missing %s", #name); return 0; }
    L_(glGenTextures) L_(glDeleteTextures) L_(glBindTexture) L_(glTexImage2D) L_(glTexParameteri) L_(glActiveTexture)
    L_(glGenFramebuffers) L_(glBindFramebuffer) L_(glFramebufferTexture2D) L_(glCheckFramebufferStatus)
    L_(glGenRenderbuffers) L_(glBindRenderbuffer) L_(glRenderbufferStorage) L_(glFramebufferRenderbuffer)
    L_(glViewport) L_(glScissor) L_(glEnable) L_(glDisable) L_(glClearColor) L_(glClearDepth) L_(glClear) L_(glDepthFunc)
    L_(glDepthMask) L_(glColorMask) L_(glBlendFuncSeparate) L_(glBlendEquation) L_(glBlendColor)
    L_(glCreateShader) L_(glShaderSource) L_(glCompileShader) L_(glGetShaderiv) L_(glGetShaderInfoLog)
    L_(glCreateProgram) L_(glAttachShader) L_(glLinkProgram) L_(glGetProgramiv) L_(glGetProgramInfoLog) L_(glUseProgram)
    L_(glGetUniformLocation) L_(glUniform1i) L_(glUniform2f) L_(glUniform3f) L_(glBindAttribLocation)
    L_(glGenVertexArrays) L_(glBindVertexArray) L_(glGenBuffers) L_(glBindBuffer) L_(glBufferData)
    L_(glVertexAttribPointer) L_(glEnableVertexAttribArray) L_(glDrawArrays) L_(glReadPixels) L_(glPixelStorei)
    L_(glBlitFramebuffer) L_(glGetError) L_(glFinish) L_(glFlush)
#undef L_
    *(void **)&p_glUniform4f = g_getproc("glUniform4f"); *(void **)&p_glUniform1f = g_getproc("glUniform1f");
    /* (the PSP's stencil - optional: without it, drawings cut out with it show uncut) */
    *(void **)&p_glStencilFunc = g_getproc("glStencilFunc"); *(void **)&p_glStencilOp = g_getproc("glStencilOp");
    *(void **)&p_glStencilMask = g_getproc("glStencilMask"); *(void **)&p_glClearStencil = g_getproc("glClearStencil");
    g_stencil = p_glStencilFunc && p_glStencilOp && p_glStencilMask && p_glClearStencil;
    return 1;
}

/* remaster lighting, shared by the OpenGL and Vulkan shaders. v_l = place in the shadow picture (x, y: 0..1 across it,
 * z: 0..1 from the sun). lk = shadow darkness, extra light where the sun shines, 1/size, one texel in z units;
 * lcam = the camera in the same space (z scaled to x/y units), and that z scale. The surface's facing comes from how
 * v_l changes across the pixel (flat for each triangle, like a cleaned-up PSP look). */
#define LIGHT_FN \
    "vec4 sh_pack(float d) { d = clamp(d, 0.0, 0.99999); vec3 e = fract(d * vec3(1.0, 255.0, 65025.0)); e -= e.yzz * vec3(1.0 / 255.0, 1.0 / 255.0, 0.0); return vec4(e, 1.0); }\n" \
    "float sh_pcf(vec2 uv, float z, float sz, float reach) {\n" \
    "  vec2 p = uv * sz - 1.5; vec2 i0 = floor(p); vec2 fr = p - i0; float s = 0.0;\n" \
    "  for (int j = 0; j < 4; j++) { float wy = j == 0 ? 1.0 - fr.y : (j == 3 ? fr.y : 1.0);\n" \
    "    for (int i = 0; i < 4; i++) { float wx = i == 0 ? 1.0 - fr.x : (i == 3 ? fr.x : 1.0);\n" \
    "      vec3 e = texture(u_shadow, (i0 + vec2(i, j) + 0.5) / sz).rgb;\n" \
    "      float d = dot(e, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0)); s += wx * wy * (z <= d || z - d > reach ? 1.0 : 0.0); } }\n" \
    "  return s / 9.0;\n" \
    "}\n" \
    "float light_factor(vec3 l, int mode, vec4 lk, vec4 lc) {\n" \
    "  vec3 m = vec3(l.xy, l.z * lc.w); vec3 n = cross(dFdx(m), dFdy(m)); float nn = dot(n, n), ndl = 1.0;\n" \
    "  if (nn > 1e-24) { n *= inversesqrt(nn); if (dot(n, lc.xyz - m) < 0.0) n = -n; ndl = -n.z; }\n" \
    "  float vis = 1.0; vec2 e2 = min(l.xy, 1.0 - l.xy); float edge = min(e2.x, e2.y);\n" \
    "  if (edge > 0.0 && l.z < 1.0 && l.z > 0.0) {\n" \
    "    float tn = sqrt(max(1.0 - ndl * ndl, 0.0)) / max(ndl, 0.2);\n" \
    "    float bias = mode == 3 ? max(lk.w * 6.0, lk.z) : lk.w * (1.5 + 2.0 * tn);\n" \
    "    float reach = lk.z * 12.0;\n" /* (a character's shadow reaches about two of its heights along the sun - not through a floor onto the storey below: a guard on the roof) */ \
    "    float pv = sh_pcf(l.xy, l.z - bias, 2048.0, reach);\n" \
    "    if (mode == 3) pv = 0.5 * (pv + sh_pcf(l.xy, l.z - bias, 819.2, reach));\n" \
    "    vis = mix(1.0, pv, smoothstep(0.0, 0.05, edge));\n" \
    "  }\n" \
    "  if (mode == 1) { float key = (ndl > 0.0 ? vis : 0.0) * clamp(ndl * 1.25, 0.0, 1.0); return mix(1.0 - lk.x, 1.0 + lk.y, key); }\n" \
    "  return mix(1.0 - lk.x * 0.8, 1.0, vis);\n" \
    "}\n" \
    "float ao_d(vec2 uv) { return dot(texture(u_ao, uv).rgb, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0)); }\n" \
    "float ao_factor(vec2 fc, float px, float py, float k) {\n" \
    "  vec2 uv = fc * vec2(px, py); float d0 = ao_d(uv); if (d0 > 0.995 || k <= 0.0) return 1.0;\n" \
    "  vec2 t = 2.0 * vec2(px, py);\n" \
    "  float l = ao_d(uv - vec2(t.x, 0.0)), r = ao_d(uv + vec2(t.x, 0.0)), b = ao_d(uv - vec2(0.0, t.y)), u = ao_d(uv + vec2(0.0, t.y));\n" \
    "  vec2 g = vec2(abs(r - d0) < abs(d0 - l) ? r - d0 : d0 - l, abs(u - d0) < abs(d0 - b) ? u - d0 : d0 - b) / t;\n" \
    "  vec2 rad = vec2(k / d0, k / d0 * 1.765); rad = min(rad, vec2(0.08, 0.14));\n" \
    "  float a = 0.0, occ = 0.0;\n" \
    "  for (int i = 0; i < 16; i++) {\n" \
    "    float an = a + float(i) * 2.3999632, rr = sqrt((float(i) + 0.5) / 16.0);\n" \
    "    vec2 o = vec2(cos(an), sin(an)) * rad * rr;\n" \
    "    float d = ao_d(clamp(uv + o, 0.001, 0.999)), de = d0 + dot(g, o);\n" \
    "    float x = (de - d) * 0.95 / k;\n" \
    "    occ += smoothstep(0.1, 0.35, x) * (1.0 - smoothstep(1.2, 2.5, x));\n" \
    "  }\n" \
    "  return 1.0 - 0.9 * occ / 16.0;\n" \
    "}\n"
static const char *VS =
    "#version 330 core\n"
    "in vec4 a_pos; in vec2 a_uv; in vec4 a_col; in float a_fog; in vec3 a_l;\n"
    "uniform vec2 u_fbsize; uniform int u_lmode; uniform vec4 u_lk;\n"
    "centroid out vec2 v_uv; centroid out vec4 v_col; out float v_fog; out vec3 v_l;\n"
    "void main() {\n"
    "  v_l = a_l;\n"
    "  if (u_lmode == 2) { v_uv = a_uv; v_col = a_col; v_fog = a_fog; gl_Position = vec4(a_l.xy * 2.0 - 1.0, a_l.z * 2.0 - 1.0, 1.0); return; }\n"
    "  if (u_lmode == 4) v_l = vec3(a_pos.w * u_lk.z, 0.0, 0.0);\n"
    "  float w = a_pos.w;\n"
    "  vec3 n = vec3(a_pos.x / u_fbsize.x * 2.0 - 1.0, 1.0 - a_pos.y / u_fbsize.y * 2.0, a_pos.z / 65535.0 * 2.0 - 1.0);\n"
    "  gl_Position = vec4(n * w, w);\n"
    "  v_uv = a_uv; v_col = a_col; v_fog = a_fog;\n"
    "}\n";
static const char *FS =
    "#version 330 core\n"
    "centroid in vec2 v_uv; centroid in vec4 v_col; in float v_fog; in vec3 v_l; out vec4 o;\n"
    "uniform sampler2D u_tex; uniform int u_texon, u_tfunc, u_ta, u_atest, u_aref, u_amask, u_dbl, u_fogon; uniform vec3 u_env, u_fogcol;\n"
    "uniform sampler2D u_shadow, u_ao; uniform int u_lmode; uniform vec4 u_lk, u_lcam; uniform float u_aox, u_aoy, u_aos;\n"
    LIGHT_FN
    "void main() {\n"
    /* depth with the PSP's precision (whole steps of its 16-bit depth): pieces of the world that overlap a little at
     * their edges (floor tiles, wall parts) lie at almost the same depth, and the card's much finer depth picked
     * a different one in front from one moment to the next as the camera moved a hair - lines flickering along the
     * tile edges. At the PSP's precision they come out equal and the later one is in front, as on the PSP */
    "  gl_FragDepth = u_lmode == 2 ? gl_FragCoord.z : floor(gl_FragCoord.z * 65535.0 + 0.5) / 65535.0;\n"
    "  vec4 c = v_col;\n"
    "  if (u_texon != 0) {\n"
    "    vec4 t = texture(u_tex, v_uv); vec4 r;\n"
    "    if (u_tfunc == 0) { r.rgb = t.rgb * c.rgb; r.a = u_ta != 0 ? t.a * c.a : c.a; }\n"
    "    else if (u_tfunc == 1) { r.rgb = u_ta != 0 ? mix(c.rgb, t.rgb, t.a) : t.rgb; r.a = c.a; }\n"
    "    else if (u_tfunc == 2) { r.rgb = mix(c.rgb, u_env, t.rgb); r.a = u_ta != 0 ? t.a * c.a : c.a; }\n"
    "    else if (u_tfunc == 3) { r.rgb = t.rgb; r.a = u_ta != 0 ? t.a : c.a; }\n"
    "    else { r.rgb = min(c.rgb + t.rgb, vec3(1.0)); r.a = u_ta != 0 ? t.a * c.a : c.a; }\n"
    "    if (u_dbl != 0) r.rgb = min(r.rgb * 2.0, vec3(1.0));\n"
    "    c = r;\n"
    "  }\n"
    "  if (u_atest >= 0) {\n"
    "    int a = int(c.a * 255.0 + 0.5) & u_amask; int f = u_atest; bool p;\n"
    "    if (f == 0) p = false; else if (f == 1) p = true; else if (f == 2) p = a == u_aref; else if (f == 3) p = a != u_aref;\n"
    "    else if (f == 4) p = a < u_aref; else if (f == 5) p = a <= u_aref; else if (f == 6) p = a > u_aref; else p = a >= u_aref;\n"
    "    if (!p) discard;\n"
    "  }\n"
    "  if (u_lmode == 2) { o = sh_pack(v_l.z); return; }\n"
    "  if (u_lmode == 4) { o = sh_pack(v_l.x); return; }\n"
    "  if (u_lmode == 1 || u_lmode == 3) c.rgb = min(c.rgb * light_factor(v_l, u_lmode, u_lk, u_lcam) * ao_factor(gl_FragCoord.xy, u_aox, u_aoy, u_lmode == 1 ? u_aos : u_aos * 0.5), vec3(1.0));\n"
    "  if (u_fogon != 0) c.rgb = mix(u_fogcol, c.rgb, clamp(v_fog, 0.0, 1.0));\n"
    "  o = c;\n"
    "}\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = p_glCreateShader(type); p_glShaderSource(s, 1, &src, 0); p_glCompileShader(s);
    GLint ok = 0; p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { p_glGetShaderInfoLog(s, sizeof g_gl_err, 0, g_gl_err); return 0; }
    return s;
}

/* Python calls this on the drawing thread with an OpenGL 3.3 context current. 0 = ok, else see gl_error() */
GEXPORT int gl_setup(void *(*getproc)(const char *), int scale, int software_too) {
    g_getproc = getproc; g_gl_err[0] = 0;
    if (!load_gl()) return 1;
    GLuint vs = compile(GL_VERTEX_SHADER, VS), fs = compile(GL_FRAGMENT_SHADER, FS);
    if (!vs || !fs) return 2;
    g_prog = p_glCreateProgram(); p_glAttachShader(g_prog, vs); p_glAttachShader(g_prog, fs);
    p_glBindAttribLocation(g_prog, 0, "a_pos"); p_glBindAttribLocation(g_prog, 1, "a_uv"); p_glBindAttribLocation(g_prog, 2, "a_col"); p_glBindAttribLocation(g_prog, 3, "a_fog"); p_glBindAttribLocation(g_prog, 4, "a_l");
    p_glLinkProgram(g_prog); GLint ok = 0; p_glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) { p_glGetProgramInfoLog(g_prog, sizeof g_gl_err, 0, g_gl_err); return 3; }
#define U_(v) v = p_glGetUniformLocation(g_prog, #v)
    U_(u_fbsize); U_(u_tex); U_(u_texon); U_(u_tfunc); U_(u_ta); U_(u_atest); U_(u_aref); U_(u_amask); U_(u_env); U_(u_dbl); U_(u_fogon); U_(u_fogcol); U_(u_shadow); U_(u_lmode); U_(u_lk); U_(u_lcam); U_(u_ao); U_(u_aox); U_(u_aoy); U_(u_aos);
#undef U_
    p_glGenVertexArrays(1, &g_vao); p_glBindVertexArray(g_vao);
    p_glGenBuffers(1, &g_vbo); p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    p_glVertexAttribPointer(0, 4, GL_FLOAT, 0, sizeof(GLVtx), (void *)0);
    p_glVertexAttribPointer(1, 2, GL_FLOAT, 0, sizeof(GLVtx), (void *)16);
    p_glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, 1, sizeof(GLVtx), (void *)24);
    p_glVertexAttribPointer(3, 1, GL_FLOAT, 0, sizeof(GLVtx), (void *)28);
    p_glVertexAttribPointer(4, 3, GL_FLOAT, 0, sizeof(GLVtx), (void *)32);
    p_glEnableVertexAttribArray(0); p_glEnableVertexAttribArray(1); p_glEnableVertexAttribArray(2); p_glEnableVertexAttribArray(3); p_glEnableVertexAttribArray(4);
    p_glUseProgram(g_prog); p_glUniform1i(u_tex, 0); p_glUniform1i(u_shadow, 1); p_glUniform1i(u_ao, 2); p_glUniform1i(u_lmode, 0);
    p_glDisable(GL_CULL_FACE); p_glEnable(GL_DEPTH_CLAMP);
    p_glPixelStorei(GL_PACK_ALIGNMENT, 1); p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    g_scale = scale < 1 ? 1 : scale > 8 ? 8 : scale; g_sw_on = software_too ? 1 : 0; ap_forget();
    g_nfb = 0; g_ntex = 0; g_nvtx = 0; g_batch_gen = 0xFFFFFFFF; g_cur_fb = 0;
    g_gl_on = 1;
    return p_glGetError() ? 4 : 0;
}
GEXPORT const char *gl_error(void) { return g_gl_err; }
GEXPORT void gl_options(int filter) { g_filter = filter; }
static float g_lk_shadow = 0.35f, g_lk_sun = 0.15f; static int g_ao_on = 1;
GEXPORT void gl_ao(int on) { g_ao_on = on; }
/* remaster lighting: mode 0 = off, 1 = on (characters cast shadows), 2 = on, everything casts shadows; shadow = how dark
 * shadows are (0..1), sun = extra light where the sun shines (0..1), angle = the sun's angle from overhead (degrees) */
static double g_sun_angle = 25.0;                            /* degrees from straight overhead */
static int g_cast_all;                                       /* 0: only characters cast shadows, 1: everything */
GEXPORT void gl_lighting(int mode, float shadow, float sun, float angle) {
    g_cast_all = mode == 2; g_light_on = mode && p_glUniform4f ? 1 : 0;
    if (angle >= 0.f && angle <= 80.f) g_sun_angle = angle;
    if (shadow >= 0.f && shadow <= 1.f) g_lk_shadow = shadow; if (sun >= 0.f && sun <= 1.f) g_lk_sun = sun;
}
GEXPORT void gl_dbg_skip(uint32_t tex) { g_skip_tex = tex; }   /* (test aid) leave out draws using this picture */
/* picture quality, before anything is drawn: msaa = samples per pixel (1 = off), mips = smaller texture copies +
 * anisotropic filtering for textures seen at an angle (used with texture filtering). Returns what could be had. */
GEXPORT int gl_quality(int msaa, int mips) {
    *(void **)&p_glRenderbufferStorageMultisample = g_getproc("glRenderbufferStorageMultisample");
    *(void **)&p_glGenerateMipmap = g_getproc("glGenerateMipmap"); *(void **)&p_glTexParameterf = g_getproc("glTexParameterf");
    g_msaa = msaa > 1 && p_glRenderbufferStorageMultisample ? (msaa > 8 ? 8 : msaa) : 1;
    if (g_msaa > 1) {                                             /* what the card allows */
        GLint mx = 0; void (*gi)(GLenum, GLint *) = (void (*)(GLenum, GLint *))g_getproc("glGetIntegerv");
        if (gi) { gi(0x8D57 /* MAX_SAMPLES */, &mx); if (mx > 0 && g_msaa > mx) g_msaa = mx; }
    }
    g_mips = mips && p_glGenerateMipmap ? 1 : 0; g_aniso = 0;
    if (g_mips && p_glTexParameterf) {
        GLfloat mx = 0; void (*gf)(GLenum, GLfloat *) = (void (*)(GLenum, GLfloat *))g_getproc("glGetFloatv");
        while (p_glGetError()) {}
        if (gf) { gf(0x84FF /* MAX_TEXTURE_MAX_ANISOTROPY */, &mx); if (p_glGetError() == 0 && mx > 1) g_aniso = mx > 16 ? 16 : mx; }
    }
    return g_msaa | (g_mips << 8) | ((int)g_aniso << 16);
}
GEXPORT void gl_off(void) { g_gl_on = 0; g_sw_on = 1; }

/* ------------------------------------------------------------------ framebuffers */
/* a picture to draw into: colour texture (+ depth), or with MSAA a multisample colour+depth pair plus the texture.
 * Objects already made (a framebuffer slot used again for another address) are kept and only given the new size:
 * making new ones each time and never deleting the old ones (the multisample ones are big: ~200 MB at resolution 5)
 * used up graphics memory, and with Vulkan its table of framebuffers (256) - after that, drawing went to the window */
static void make_target__b(int w, int h, GLuint *fbo, GLuint *color, GLuint *depth, GLuint *msfbo, GLuint *mscolor, GLuint *msdepth);
static void make_target(int w, int h, GLuint *fbo, GLuint *color, GLuint *depth, GLuint *msfbo, GLuint *mscolor, GLuint *msdepth) { uint64_t _t = PF_TSC(); make_target__b(w, h, fbo, color, depth, msfbo, mscolor, msdepth); pf_add(4, _t); }
static void make_target__b(int w, int h, GLuint *fbo, GLuint *color, GLuint *depth, GLuint *msfbo, GLuint *mscolor, GLuint *msdepth) {
    if (!*color) p_glGenTextures(1, color);
    p_glBindTexture(GL_TEXTURE_2D, *color);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (!*fbo) p_glGenFramebuffers(1, fbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *color, 0);
    if (g_msaa > 1) {
        p_glClearColor(0, 0, 0, 0); p_glDisable(GL_SCISSOR_TEST); p_glColorMask(1, 1, 1, 1); p_glClear(GL_COLOR_BUFFER_BIT);   /* (the plain texture too) */
        if (!*mscolor) p_glGenRenderbuffers(1, mscolor);
        p_glBindRenderbuffer(GL_RENDERBUFFER, *mscolor);
        p_glRenderbufferStorageMultisample(GL_RENDERBUFFER, g_msaa, GL_RGBA8, w, h);
        if (!*msdepth) p_glGenRenderbuffers(1, msdepth);
        p_glBindRenderbuffer(GL_RENDERBUFFER, *msdepth);
        p_glRenderbufferStorageMultisample(GL_RENDERBUFFER, g_msaa, g_stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24, w, h);
        if (!*msfbo) p_glGenFramebuffers(1, msfbo);
        p_glBindFramebuffer(GL_FRAMEBUFFER, *msfbo);
        p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, *mscolor);
        p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, g_stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *msdepth);
    } else {
        if (!*depth) p_glGenRenderbuffers(1, depth);
        p_glBindRenderbuffer(GL_RENDERBUFFER, *depth);
        p_glRenderbufferStorage(GL_RENDERBUFFER, g_stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24, w, h);
        p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, g_stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *depth);
    }
    if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) snprintf_(g_gl_err, sizeof g_gl_err, "framebuffer incomplete");
    p_glClearColor(0, 0, 0, 0); p_glClearDepth(0); p_glDisable(GL_SCISSOR_TEST); p_glColorMask(1, 1, 1, 1); p_glDepthMask(1);
    if (g_stencil) { p_glStencilMask(0xFF); p_glClearStencil(0); }
    p_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | (g_stencil ? GL_STENCIL_BUFFER_BIT : 0));
    g_cur_fb = 0; ap_forget();
}
/* MSAA: turn the multisample picture into the plain texture (only if drawn since the last time) */
static void resolve__b(GLuint msfbo, GLuint fbo, int w, int h);
static void resolve(GLuint msfbo, GLuint fbo, int w, int h) { uint64_t _t = PF_TSC(); resolve__b(msfbo, fbo, w, h); pf_add(5, _t); }
static void resolve__b(GLuint msfbo, GLuint fbo, int w, int h) {
    if (!msfbo) return;
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, msfbo); p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    p_glDisable(GL_SCISSOR_TEST);
    p_glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0); g_cur_fb = 0; ap_forget();
}
static void fb_resolve(GLFb *f) { if (f->msfbo && f->dirty) { resolve(f->msfbo, f->fbo, f->w * g_scale, f->h * g_scale); f->dirty = 0; } }
static GLFb *fb_for(uint32_t addr, int fmt, int stride) {
    addr &= 0x1FFFFF;
    for (int i = 0; i < g_nfb; i++) if (g_fbs[i].addr == addr) { g_fbs[i].last_use = g_frame_no; return &g_fbs[i]; }
    GLFb *f, keep; memset(&keep, 0, sizeof keep);
    if (g_nfb < MAX_FB) f = &g_fbs[g_nfb++];
    else {                                                          /* reuse the one unused the longest - and its objects */
        f = &g_fbs[0]; for (int i = 1; i < g_nfb; i++) if (g_fbs[i].last_use < f->last_use) f = &g_fbs[i];
        keep = *f;
    }
    memset(f, 0, sizeof *f);
    f->fbo = keep.fbo; f->color = keep.color; f->depth = keep.depth; f->msfbo = keep.msfbo; f->mscolor = keep.mscolor; f->msdepth = keep.msdepth;
    f->cfbo = keep.cfbo; f->ccolor = keep.ccolor; if (f->ccolor) { p_glBindTexture(GL_TEXTURE_2D, f->ccolor); p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (stride > 0 && stride <= 1024 ? stride : 512) * g_scale, 512 * g_scale, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0); }
    f->sfbo = keep.sfbo; f->scolor = keep.scolor; f->sdepth = keep.sdepth; f->smsfbo = keep.smsfbo; f->smscolor = keep.smscolor; f->smsdepth = keep.smsdepth;
    f->addr = addr; f->fmt = fmt; f->w = stride > 0 && stride <= 1024 ? stride : 512; f->h = 512; f->last_use = g_frame_no;
    make_target(f->w * g_scale, f->h * g_scale, &f->fbo, &f->color, &f->depth, &f->msfbo, &f->mscolor, &f->msdepth);
    if (f->sfbo) make_target(f->w * g_scale, f->h * g_scale, &f->sfbo, &f->scolor, &f->sdepth, &f->smsfbo, &f->smscolor, &f->smsdepth);
    return f;
}

/* ------------------------------------------------------------------ textures (decoded to RGBA on the CPU, cached) */
static uint64_t hash_bytes(const uint8_t *p, uint32_t n) {
    uint64_t h = 1469598103934665603ull; uint32_t i = 0;
    for (; i + 8 <= n; i += 8) { uint64_t v; memcpy(&v, p + i, 8); h = (h ^ v) * 1099511628211ull; h ^= h >> 29; }
    for (; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
/* big textures in video memory (a framebuffer used as a texture: 280 KB) were hashed again for every draw that
 * used them - an explosion's blur effect draws ~450 strips from one: 100 ms of hashing a frame. Their hash is kept
 * while nothing can have changed video memory: same frame, no copy / download / movie picture since
 * (g_vram_gen), no software drawing since (g->pixels). */
static uint32_t g_vram_gen = 1;
GEXPORT void gl_vram_touch(void) { g_vram_gen++; }
typedef struct { const uint8_t *p; uint32_t n, gen, frame; uint64_t px, h; } HCache;
static HCache g_hc[32];
static uint64_t hash_vram_cached(GEState *g, const uint8_t *p, uint32_t n, uint32_t frame) {
    if (n < 16384 || p < g->vram || p + n > g->vram + 0x200000) return hash_bytes(p, n);
    uint32_t i = (uint32_t)(((uintptr_t)p >> 4) ^ n) & 31; HCache *c = &g_hc[i];
    if (c->p == p && c->n == n && c->gen == g_vram_gen && c->frame == frame && c->px == g->pixels) return c->h;
    c->p = p; c->n = n; c->gen = g_vram_gen; c->frame = frame; c->px = g->pixels; c->h = hash_bytes(p, n); return c->h;
}
/* ---- HD texture packs (the PPSSPP kind: textures.ini + PNG files named by a hash of the original texture).
 * The name is PPSSPP's: 8 hex digits address, 8 hex "dim ^ palette hash", 8 hex "quick" hash of the texture's
 * bytes. Python reads textures.ini and gives the names (gl_replace_add); a texture the game uses that is in the
 * pack is asked for (gl_replace_requests); Python decodes the PNG and hands it over (gl_replace_put); from then on
 * it is drawn instead of the original. Until then the original is used, so loading never makes the game wait. */
static void gl_flush(void);
typedef struct { uint32_t a, b, c; int state; GLuint tex; uint32_t last, kb; } GLRep;     /* state 0 in the pack, 1 asked for, 2 loaded, 3 failed */
/* HD pictures use a lot of graphics memory (a 1024x1024 one ~5 MB): past the budget, the ones not seen for longest are
 * let go (and loaded again if they come back), so a long session never fills the graphics card */
static uint64_t g_rep_kb, g_rep_budget_kb = 1536u * 1024u; static int g_rep_evicted, g_rep_live;
GEXPORT void gl_replace_budget(int mb) { if (mb >= 128) g_rep_budget_kb = (uint64_t)mb * 1024u; }

#define REP_MAX 65536
static GLRep g_rep[REP_MAX]; static int g_nrep; static int g_rep_on;
static int g_rep_slot[REP_MAX * 2]; static int g_rep_dump, g_rep_seen; static uint32_t g_rep_seenk[8192][3];
GEXPORT int gl_replace_seen(int on, uint32_t *out) { g_rep_dump = on; if (out) memcpy(out, g_rep_seenk, sizeof(uint32_t) * 3 * (size_t)g_rep_seen); return g_rep_seen; }   /* (test aid) */                               /* hash table of indexes + 1 */
static int g_repq[4096]; static volatile uint32_t g_repq_head, g_repq_tail;
static uint32_t g_clut_max;                                       /* (PPSSPP hashes this much of the palette buffer) */
static uint32_t rep_h(uint32_t a, uint32_t b, uint32_t c) { uint32_t h = a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ c * 0xC2B2AE3Du; return h ^ (h >> 15); }
static int rep_find(uint32_t a, uint32_t b, uint32_t c) {
    for (uint32_t i = rep_h(a, b, c) & (REP_MAX * 2 - 1);; i = (i + 1) & (REP_MAX * 2 - 1)) {
        int k = g_rep_slot[i]; if (!k) return -1;
        GLRep *e = &g_rep[k - 1]; if (e->a == a && e->b == b && e->c == c) return k - 1;
    }
}
/* the same picture by its size and contents alone, whatever the address: the port's memory layout differs from
 * PPSSPP's in places (menus, lettering), so a pack's name can have another address than the one here. The first
 * one added for a picture is the one used (play.py adds the preferred pack first). */
static int g_rep_cslot[REP_MAX * 2];
static int rep_cfind(uint32_t b, uint32_t c) {
    for (uint32_t i = rep_h(0x5EED, b, c) & (REP_MAX * 2 - 1);; i = (i + 1) & (REP_MAX * 2 - 1)) {
        int k = g_rep_cslot[i]; if (!k) return -1;
        GLRep *e = &g_rep[k - 1]; if (e->b == b && e->c == c) return k - 1;
    }
}
static void nb_add(uint32_t b);
GEXPORT int gl_replace_add(const uint32_t *keys, int n) {         /* keys: n x (address, dim^palette, hash) */
    for (int i = 0; i < n && g_nrep < REP_MAX; i++) {
        uint32_t a = keys[3 * i], b = keys[3 * i + 1], c = keys[3 * i + 2];
        if (rep_find(a, b, c) >= 0) continue;
        GLRep *e = &g_rep[g_nrep]; e->a = a; e->b = b; e->c = c; e->state = 0; e->tex = 0;
        uint32_t j = rep_h(a, b, c) & (REP_MAX * 2 - 1); while (g_rep_slot[j]) j = (j + 1) & (REP_MAX * 2 - 1);
        g_rep_slot[j] = ++g_nrep;
        if (rep_cfind(b, c) < 0) {
            j = rep_h(0x5EED, b, c) & (REP_MAX * 2 - 1); while (g_rep_cslot[j]) j = (j + 1) & (REP_MAX * 2 - 1);
            g_rep_cslot[j] = g_nrep;
        }
        nb_add(b);
    }
    g_rep_on = g_nrep > 0; return g_nrep;
}
/* ------------------------------------------------------------------ near matches. A picture the packs have, but with a
 * different name: the game left other bytes after it this time (rows of leftover memory under a codec portrait, a few
 * bytes after the digits), so its hash - taken over all of it - is new. Pictures with no HD version whose size and
 * palette (b) some pack picture has are handed to play.py (decoded), which compares them with those pack pictures;
 * when one is the same picture apart from such a leftover tail, it is added under this name (gl_replace_add +
 * gl_replace_relink) and remembered (learned_matches.ini). */
#define NB_N 32768
static uint32_t g_nb_v[NB_N]; static uint8_t g_nb_u[NB_N];
static void nb_add(uint32_t b) { uint32_t i = (b * 0x9E3779B1u) >> 17; while (g_nb_u[i] && g_nb_v[i] != b) i = (i + 1) & (NB_N - 1); g_nb_u[i] = 1; g_nb_v[i] = b; }
static int nb_has(uint32_t b) { uint32_t i = (b * 0x9E3779B1u) >> 17; while (g_nb_u[i]) { if (g_nb_v[i] == b) return 1; i = (i + 1) & (NB_N - 1); } return 0; }
typedef struct { uint32_t k[3]; int w, h; uint8_t px[256 * 256 * 4]; } NearQ;   /* (no malloc in the Windows build) */
static NearQ g_nq[16]; static int g_nq_n, g_near_on;
static uint32_t g_ns_b[8192], g_ns_c[8192]; static uint8_t g_ns_u[8192];
GEXPORT void gl_near_on(int on) { g_near_on = on; }
static void near_push(uint32_t a, uint32_t b, uint32_t c, int w, int h, const uint8_t *rgba) {
    if (!g_near_on || g_nq_n >= 16 || w * h > 256 * 256 || !nb_has(b)) return;
    uint32_t i = (rep_h(0, b, c)) & 8191;
    while (g_ns_u[i]) { if (g_ns_b[i] == b && g_ns_c[i] == c) return; i = (i + 1) & 8191; }
    g_ns_u[i] = 1; g_ns_b[i] = b; g_ns_c[i] = c;                      /* (each picture once a session) */
    NearQ *q = &g_nq[g_nq_n];
    memcpy(q->px, rgba, (size_t)w * h * 4); q->k[0] = a; q->k[1] = b; q->k[2] = c; q->w = w; q->h = h; g_nq_n++;
}
GEXPORT int gl_near_take(uint32_t *key, int *wh, uint8_t *out, int max) {     /* (drawing thread) */
    if (!g_nq_n) return 0;
    NearQ *q = &g_nq[--g_nq_n]; int n = q->w * q->h * 4;
    key[0] = q->k[0]; key[1] = q->k[1]; key[2] = q->k[2]; wh[0] = q->w; wh[1] = q->h;
    if (n <= max) memcpy(out, q->px, (size_t)n); else wh[0] = wh[1] = 0;
    return 1;
}
/* (log) HD pictures in graphics memory, their MB, how many were let go since the last call */
GEXPORT void gl_replace_stats(int *o) { int n = 0; for (int i = 0; i < g_nrep; i++) n += g_rep[i].state == 2; o[0] = n; o[1] = (int)(g_rep_kb >> 10); o[2] = g_rep_evicted; g_rep_evicted = 0; }
static void rep_request(int i) {
    if (g_rep[i].state == 0 && g_repq_head - g_repq_tail < 4096) { g_rep[i].state = 1; g_repq[g_repq_head & 4095] = i; g_repq_head++; }
}
GEXPORT int gl_replace_requests(int *out, int max) {              /* (any thread) indexes asked for since the last call */
    int n = 0; while (g_repq_tail != g_repq_head && n < max) { out[n++] = g_repq[g_repq_tail & 4095]; g_repq_tail++; }
    return n;
}
GEXPORT void gl_replace_key(int i, uint32_t *out) { if (i >= 0 && i < g_nrep) { out[0] = g_rep[i].a; out[1] = g_rep[i].b; out[2] = g_rep[i].c; } }
/* on the drawing thread: the decoded picture for replacement i (rgba = 0: it could not be loaded) */
static void gl_replace_put__b(int i, int w, int h, const uint8_t *rgba);
GEXPORT void gl_replace_put(int i, int w, int h, const uint8_t *rgba) { uint64_t _t = pf_top_begin(); gl_replace_put__b(i, w, h, rgba); pf_top_end(4, _t); }
static void gl_replace_put__b(int i, int w, int h, const uint8_t *rgba) {
    if (!g_gl_on || i < 0 || i >= g_nrep) return;
    GLRep *e = &g_rep[i];
    if (!rgba || w <= 0 || h <= 0) { e->state = 3; return; }
    if (e->state != 1) return;                                     /* (not wanted any more) */
    gl_flush(); g_batch_gen = 0xFFFFFFFF;
    /* over the budget: let go of the HD pictures not used for longest (none used by the last 4 frames: those may be
     * drawn again for in-between pictures) */
    uint32_t kb = (uint32_t)(((uint64_t)w * (uint64_t)h * 4u * 4u / 3u) >> 10);
    while (g_rep_kb + kb > g_rep_budget_kb || g_rep_live >= 4096) {   /* (and at most 4096 of them: texture names run out) */
        int old = -1;
        for (int j = 0; j < g_nrep; j++) if (g_rep[j].state == 2 && g_rep[j].last + 4 < g_frame_no && (old < 0 || g_rep[j].last < g_rep[old].last)) old = j;
        if (old < 0) break;
        GLRep *o = &g_rep[old]; p_glDeleteTextures(1, &o->tex); o->tex = 0; o->state = 0; g_rep_kb -= o->kb; o->kb = 0; g_rep_evicted++; g_rep_live--;
    }
    if (!e->tex) p_glGenTextures(1, &e->tex);
    p_glBindTexture(GL_TEXTURE_2D, e->tex); ap_forget();
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (g_mips) { p_glGenerateMipmap(GL_TEXTURE_2D); if (g_aniso > 1.f) p_glTexParameterf(GL_TEXTURE_2D, 0x84FE, g_aniso); }
    e->state = 2; e->kb = kb; e->last = g_frame_no; g_rep_kb += kb; g_rep_live++;
}
static uint32_t xxh32(const uint8_t *p, uint32_t len, uint32_t seed) {
    const uint32_t P1 = 2654435761u, P2 = 2246822519u, P3 = 3266489917u, P4 = 668265263u, P5 = 374761393u;
    const uint8_t *end = p + len; uint32_t h;
#define RL(x, r) (((x) << (r)) | ((x) >> (32 - (r))))
    if (len >= 16) {
        uint32_t v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1; const uint8_t *lim = end - 16;
        do {
            uint32_t w; memcpy(&w, p, 4); v1 = RL(v1 + w * P2, 13) * P1; p += 4; memcpy(&w, p, 4); v2 = RL(v2 + w * P2, 13) * P1; p += 4;
            memcpy(&w, p, 4); v3 = RL(v3 + w * P2, 13) * P1; p += 4; memcpy(&w, p, 4); v4 = RL(v4 + w * P2, 13) * P1; p += 4;
        } while (p <= lim);
        h = RL(v1, 1) + RL(v2, 7) + RL(v3, 12) + RL(v4, 18);
    } else h = seed + P5;
    h += len;
    while (p + 4 <= end) { uint32_t w; memcpy(&w, p, 4); h += w * P3; h = RL(h, 17) * P4; p += 4; }
    while (p < end) { h += (*p) * P5; h = RL(h, 11) * P1; p++; }
    h ^= h >> 15; h *= P2; h ^= h >> 13; h *= P3; h ^= h >> 16;
#undef RL
    return h;
}
/* PPSSPP's "quick" texture hash (QuickTexHashNonSSE: the same result as its SSE / NEON versions) */
static uint32_t quick_hash(const uint8_t *p, uint32_t size, uint32_t addr) {
    uint32_t check = 0;
    if ((addr & 0xF) == 0 && (size & 0x3F) == 0) {
        static const uint16_t c2i[8] = {0xc00bU, 0x9bd9U, 0x4b73U, 0xb651U, 0x4d9bU, 0x4309U, 0x0083U, 0x0001U};
        uint16_t cur[8] = {0}, c2[8]; memcpy(c2, c2i, sizeof c2);
        for (uint32_t i = 0; i < size / 16; i += 4) {
            const uint8_t *b = p + (size_t)i * 16; uint16_t w16[8]; uint32_t w32[4], x32[4];
            memcpy(w16, b, 16); for (int j = 0; j < 8; j++) cur[j] = (uint16_t)(cur[j] + (uint16_t)(w16[j] * c2[j]));
            memcpy(x32, cur, 16); memcpy(w32, b + 16, 16); for (int j = 0; j < 4; j++) x32[j] ^= w32[j];
            memcpy(w32, b + 32, 16); for (int j = 0; j < 4; j++) x32[j] += w32[j];
            memcpy(cur, x32, 16);
            memcpy(w16, b + 48, 16); for (int j = 0; j < 8; j++) cur[j] ^= (uint16_t)(w16[j] * c2[j]);
            for (int j = 0; j < 8; j++) c2[j] = (uint16_t)(c2[j] + 0x2455U);
        }
        uint32_t x32[4], y32[4]; memcpy(x32, cur, 16); memcpy(y32, c2, 16);
        for (int j = 0; j < 4; j++) x32[j] += y32[j];
        check = x32[0] + x32[1] + x32[2] + x32[3];
    } else {
        for (uint32_t i = 0; i < size / 8; i++) { uint32_t a, b; memcpy(&a, p + 8 * i, 4); memcpy(&b, p + 8 * i + 4, 4); check += a; check ^= b; }
    }
    return check;
}
/* the PPSSPP name of the texture now set up (0 = can't be worked out) */
static int rep_key(GEState *g, const Tex *t, uint32_t *a, uint32_t *b, uint32_t *c) {
    static const uint8_t BPP[16] = {16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8, 0, 0, 0, 0, 0};
    static const uint32_t AM[16] = {0x7F8, 0x7F8, 0x7F8, 0x7FC, 0x7E0, 0x7F0, 0x7F8, 0x7FC, 0x7FF, 0x7FF, 0x7FF, 0, 0, 0, 0, 0};
    uint32_t *r = g->reg; int fmt = t->fmt; if (fmt > 10) return 0;
    uint32_t addr = ((r[0xA0] & 0xFFFFF0) | ((r[0xA8] << 8) & 0x0F000000)) & 0x3FFFFFFF;
    uint32_t dim = r[0xB8] & 0xF0F; int w = 1 << (dim & 15), h = 1 << ((dim >> 8) & 15);
    uint32_t bufw = r[0xA8] & AM[fmt]; if (bufw == 0) bufw = 128 / BPP[fmt];
    uint32_t hash;
    if ((int)bufw <= w) {
        uint32_t size = (uint32_t)BPP[fmt] * (bufw * (uint32_t)h + (uint32_t)(w - (int)bufw)) / 8;
        const uint8_t *p = gloc(g, addr, size); if (!p) return 0;
        hash = quick_hash(p, size, addr);
    } else {
        uint32_t line = (uint32_t)BPP[fmt] * (uint32_t)w / 8, stride = (uint32_t)BPP[fmt] * bufw / 8;
        const uint8_t *p = gloc(g, addr, (uint32_t)(h - 1) * stride + line); if (!p) return 0;
        hash = 0; for (int y = 0; y < h; y++) hash = (hash * 11) ^ quick_hash(p + (size_t)y * stride, line, addr + (uint32_t)y * stride);
    }
    uint32_t lo = dim;
    if (fmt & 4) {                                                    /* palette textures: the palette's hash too */
        uint32_t cf = r[0xC5] & 0xFFFFFF, base = ((cf >> 16) & 0x1F) << 4, bb = (cf & 3) == 3 ? base * 4 : base * 2;
        uint32_t n = g->clut_len + bb; if (n > g_clut_max) n = g_clut_max; if (n > 2048) n = 2048;
        lo ^= xxh32(g->clut, n, 0xC0108888) ^ (0xC5000000u | cf);
    }
    *a = addr; *b = lo; *c = hash; return 1;
}
/* (Ported Ops) pictures named by a hash of only their first n bytes: the item / weapon icons are unpacked into
 * buffers just big enough for them, so the rest of their (power-of-two) texture is whatever follows in memory -
 * a different hash every time. The pack's textures.ini lists them: [hashbytes] fmt,width,height,bufw = n
 * (fmt,width,height,bufw,vram = n: only pictures in video memory - Snake's face) */
typedef struct { int fmt, w, h, bufw; uint32_t n; } GLHashBytes; static GLHashBytes g_hb[64]; static int g_nhb;
GEXPORT void gl_replace_hashbytes(int fmt, int w, int h, int bufw, int n) {
    for (int i = 0; i < g_nhb; i++) if (g_hb[i].fmt == fmt && g_hb[i].w == w && g_hb[i].h == h && g_hb[i].bufw == bufw) { g_hb[i].n = (uint32_t)n; return; }
    if (g_nhb < 64 && n > 0) { GLHashBytes *e = &g_hb[g_nhb++]; e->fmt = fmt; e->w = w; e->h = h; e->bufw = bufw; e->n = (uint32_t)n; }
}
/* (Ported Ops) a palette picture whose palette sits inside its own texture - the cutscene drawings: the picture is
 * stored with its palette straight after it, and the rest of the (power-of-two) texture is whatever was loaded next.
 * Then only the picture itself (up to the palette) is hashed. 0 = not such a picture */
static uint32_t rep_partial_n(GEState *g, const Tex *t) {
    uint32_t *r = g->reg; int fmt = t->fmt; if (fmt != 4 && fmt != 5) return 0;
    uint32_t addr = ((r[0xA0] & 0xFFFFF0) | ((r[0xA8] << 8) & 0x0F000000)) & 0x3FFFFFFF;
    uint32_t caddr = ((r[0xB0] & 0xFFFFF0) | ((r[0xB1] << 8) & 0x0F000000)) & 0x3FFFFFFF;
    uint32_t dim = r[0xB8] & 0xF0F; int w = 1 << (dim & 15), h = 1 << ((dim >> 8) & 15);
    uint32_t bufw = r[0xA8] & 0x7FF, bpp = fmt == 4 ? 4 : 8;
    if (!bufw || (int)bufw > w) return 0;
    uint32_t row = bpp * bufw / 8, size = bpp * (bufw * (uint32_t)h + (uint32_t)(w - (int)bufw)) / 8;
    if (caddr <= addr || caddr >= addr + size) return 0;
    uint32_t n = caddr - addr;
    if (n < row * 8 || n % row) return 0;                            /* (whole rows of the picture, at least 8) */
    return n;
}
/* the stable hash of a picture with junk after it: 1 = such a picture (c = the hash of its first n bytes) */
static int rep_stable_c(GEState *g, const Tex *t, uint32_t *c, uint32_t *nout) {
    uint32_t *r = g->reg; uint32_t addr = ((r[0xA0] & 0xFFFFF0) | ((r[0xA8] << 8) & 0x0F000000)) & 0x3FFFFFFF;
    uint32_t dim = r[0xB8] & 0xF0F; int w = 1 << (dim & 15), h = 1 << ((dim >> 8) & 15), bufw = (int)(r[0xA8] & 0x7FF);
    uint32_t n = 0;
    for (int i = 0; i < g_nhb; i++) {
        GLHashBytes *e = &g_hb[i];
        if ((e->fmt & 0xFF) == t->fmt && e->w == w && e->h == h && e->bufw == bufw
            && (!(e->fmt & 0x100) || (addr >= 0x04000000 && addr < 0x04200000))) { n = e->n; break; }   /* (0x100: in video memory only) */
    }
    if (!n) n = rep_partial_n(g, t);
    if (!n) return 0;
    const uint8_t *p = gloc(g, addr, n); if (!p) return 0;
    *c = quick_hash(p, n, addr); if (nout) *nout = n; return 1;
}
static int rep_hb_find(GEState *g, const Tex *t, uint32_t b) {
    uint32_t c; if (!rep_stable_c(g, t, &c, 0)) return -1;
    return rep_cfind(b, c);
}
static uint32_t g_lk[3];   /* (the name rep_lookup worked out last, for the texture list of the problem report) */
static int rep_lookup(GEState *g, const Tex *t) {
    uint32_t a, b, c; g_lk[0] = g_lk[1] = g_lk[2] = 0; if (!g_rep_on || !rep_key(g, t, &a, &b, &c)) return -1;
    g_lk[0] = a; g_lk[1] = b; g_lk[2] = c;
    if (g_rep_dump) { static uint32_t seen[8192][3]; static int ns; int dup = 0;
        for (int i = 0; i < ns; i++) if (seen[i][0] == a && seen[i][1] == b && seen[i][2] == c) { dup = 1; break; }
        if (!dup && ns < 8192) { seen[ns][0] = a; seen[ns][1] = b; seen[ns][2] = c; ns++; g_rep_seen = ns; memcpy(g_rep_seenk, seen, sizeof seen); } }
    int i = rep_find(a, b, c); if (i < 0) i = rep_cfind(b, c); if (i < 0) i = rep_hb_find(g, t, b);
    if (i < 0) return -1;
    rep_request(i);
    return i;
}
/* after near matches were added: pictures in use that had no HD version look again */
GEXPORT int gl_replace_relink(void) {
    int n = 0;
    for (int i = 0; i < g_ntex; i++) {
        GLTex *e = &g_tex[i]; if (e->rep >= 0 || !e->kb) continue;
        int j = rep_find(e->ka, e->kb, e->kc); if (j < 0) j = rep_cfind(e->kb, e->kc);
        if (j >= 0) { e->rep = j; rep_request(j); n++; }
    }
    return n;
}
/* (problem report) the textures used in the last few frames: pack name, size, HD state (-1 none in the packs, 0 not asked
 * yet / let go, 1 loading, 2 in use, 3 could not load); 7 numbers each */
GEXPORT int gl_dbg_texlist(uint32_t *o, int max) {
    int n = 0;
    for (int i = 0; i < g_ntex && n < max; i++) {
        const GLTex *e = &g_tex[i]; if (e->last_use + 3 < g_frame_no || !e->kb) continue;
        uint32_t *q = o + 7 * n++; q[0] = e->ka; q[1] = e->kb; q[2] = e->kc; q[3] = (uint32_t)e->w; q[4] = (uint32_t)e->h;
        q[5] = e->rep >= 0 ? (uint32_t)g_rep[e->rep].state : 0xFFFFFFFFu; q[6] = e->rep >= 0 ? g_rep[e->rep].a : 0;
    }
    return n;
}
static uint64_t g_txs[4];   /* (test aid) texture binds, bytes hashed, new textures, their texels */
GEXPORT void gl_dbg_tex(uint64_t *o) { memcpy(o, g_txs, sizeof g_txs); memset(g_txs, 0, sizeof g_txs); }
#ifdef TEX_DUMP
/* (tool, Linux test build only) textures of 2D drawing - menus, lettering, the HUD - handed out once each, decoded,
 * under the name an HD pack uses (address, size/palette, quick hash) */
#include <stdlib.h>
static int g_dump_on, g_dump_missing; GEXPORT void gl_dump_missing(int on) { g_dump_missing = on; } static uint32_t g_dk[65536][3]; static int g_dkn;
typedef struct { uint32_t k[3]; int w, h; uint8_t *px; } DumpTex; static DumpTex g_dq[4096]; static int g_dqn;
GEXPORT void gl_dump(int on) { g_dump_on = on; }
static void tex_dump(GEState *g, const Tex *t) {
    uint32_t a, b, c, sc, sn = 0; if (!rep_key(g, t, &a, &b, &c)) return;
    if (g_dump_missing) {                                             /* (only the ones no HD pack has yet) */
        int i = rep_find(a, b, c); if (i < 0) i = rep_cfind(b, c); if (i < 0) i = rep_hb_find(g, t, b);
        if (i >= 0) return;
    }
    if (rep_stable_c(g, t, &sc, &sn)) { a = 0; c = sc; }               /* (named by the stable part: see rep_stable_c) */
    for (int i = 0; i < g_dkn; i++) if (g_dk[i][0] == a && g_dk[i][1] == b && g_dk[i][2] == c) return;
    if (g_dkn >= 65536 || g_dqn >= 4096) return;
    g_dk[g_dkn][0] = a; g_dk[g_dkn][1] = b; g_dk[g_dkn][2] = c; g_dkn++;
    int w = t->w, h = t->h; DumpTex *d = &g_dq[g_dqn++]; d->k[0] = a; d->k[1] = b; d->k[2] = c; d->w = w; d->h = h;
    d->px = malloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { Col q = texel(t, x, y); uint8_t *o = d->px + ((size_t)y * w + x) * 4; o[0] = q.r; o[1] = q.g; o[2] = q.b; o[3] = q.a; }
    if (sn) {                                                         /* (the junk after the picture: left see-through) */
        uint32_t bufw = g->reg[0xA8] & 0x7FF, row = (t->fmt == 4 ? 4 : t->fmt == 5 ? 8 : 16) * bufw / 8, rows = row ? sn / row : (uint32_t)h;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if ((uint32_t)y >= rows || x >= (int)bufw) memset(d->px + ((size_t)y * w + x) * 4, 0, 4);
    }
}
GEXPORT int gl_dump_take(uint32_t *key, int *wh, uint8_t *out, int max) {
    if (!g_dqn) return 0;
    DumpTex *d = &g_dq[--g_dqn]; int n = d->w * d->h * 4;
    key[0] = d->k[0]; key[1] = d->k[1]; key[2] = d->k[2]; wh[0] = d->w; wh[1] = d->h;
    if (n <= max) memcpy(out, d->px, (size_t)n);
    free(d->px); return 1;
}
#endif

/* ------------------------------------------------------------------ text the game makes while playing (subtitles, codec
 * lines...): it writes the letters into a 16-colour picture with a 4-step edge (see-through, 1/3, 2/3, solid; one
 * group of 4 colours per text colour), so no texture pack can have it. These pictures are made sharp here instead:
 * the edge is smoothed at a higher size, then cut to a clean anti-aliased outline (like the rebuilt font sheets). */
static int g_text_hd = 1;
GEXPORT void gl_text_hd(int on) { g_text_hd = on; }
static int is_gen_text(const Tex *t) {
    if (t->fmt != 4 || t->ncl < 16 || t->w < 64 || t->h < 8) return 0;
    for (int gq = 0; gq < 4; gq++) {
        const Col *p = t->pal + gq * 4;
        if (p[0].a != 0 || p[1].a != 0x55 || p[2].a != 0x9F || p[3].a != 0xFF) return 0;
    }
    return 1;
}
static inline float cubw(float x) { x = x < 0.f ? -x : x; return x < 1.f ? (1.5f * x - 2.5f) * x * x + 1.f : x < 2.f ? ((-0.5f * x + 2.5f) * x - 4.f) * x + 2.f : 0.f; }
#ifdef TEXT_TEST
#include <stdio.h>
#include <time.h>
GEXPORT int g_tt_bad, g_tt_n; GEXPORT double g_tt_old, g_tt_new;
static double tt_now(void) { struct timespec q; clock_gettime(CLOCK_MONOTONIC, &q); return q.tv_sec * 1e3 + q.tv_nsec / 1e6; }
static uint8_t *text_up_ref(const Tex *t, int *ow, int *oh) {
    int S = g_scale < 2 ? 2 : g_scale > 4 ? 4 : g_scale, w = t->w > 1024 ? 1024 : t->w, h = t->h > 512 ? 512 : t->h;
    static uint8_t A[1024 * 512], G[1024 * 512]; static uint8_t out[4096 * 2048 * 4];
    if ((size_t)w * S * h * S * 4 > sizeof out) return 0;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {          /* each texel: its edge step (alpha) and colour group */
        Col c = texel(t, x, y); int k = 0;
        for (int i = 0; i < 16; i++) if (t->pal[i].r == c.r && t->pal[i].g == c.g && t->pal[i].b == c.b && t->pal[i].a == c.a) { k = i; break; }
        A[y * w + x] = (uint8_t)c.a; G[y * w + x] = (uint8_t)(k >> 2);
    }
    int W = w * S, H = h * S;
    for (int Y = 0; Y < H; Y++) {
        float sy = (Y + 0.5f) / S - 0.5f; int y0 = (int)floor(sy); float fy = sy - y0, wy[4];
        for (int j = 0; j < 4; j++) wy[j] = cubw(fy - (j - 1));
        for (int X = 0; X < W; X++) {
            float sx = (X + 0.5f) / S - 0.5f; int x0 = (int)floor(sx); float fx = sx - x0, a = 0.f; int best = -1, bg = 0;
            for (int j = 0; j < 4; j++) {
                int yy = y0 + j - 1; if (yy < 0) yy = 0; if (yy >= h) yy = h - 1;
                for (int i = 0; i < 4; i++) {
                    int xx = x0 + i - 1; if (xx < 0) xx = 0; if (xx >= w) xx = w - 1;
                    int v = A[yy * w + xx]; a += v * cubw(fx - (i - 1)) * wy[j];
                    if ((i == 1 || i == 2) && (j == 1 || j == 2) && v > best) { best = v; bg = G[yy * w + xx]; }
                }
            }
            float e = (a - 96.f) / 64.f; e = e < 0.f ? 0.f : e > 1.f ? 1.f : e; e = e * e * (3.f - 2.f * e);
            int al = (int)(e * 255.f + 0.5f); const Col *f = &t->pal[bg * 4 + 3]; uint8_t *o = out + ((size_t)Y * W + X) * 4;
            o[0] = (uint8_t)f->r; o[1] = (uint8_t)f->g; o[2] = (uint8_t)f->b; o[3] = (uint8_t)al;   /* (full colour: the soft edge fades by alpha alone - the PSP's own colours are dimmed as well, which at this size shows as a dark line around each letter) */
        }
    }
    *ow = W; *oh = H; return out;
}
static uint8_t *text_up_new(const Tex *t, int *ow, int *oh);
static uint8_t *text_up_cmp(const Tex *t, int *ow, int *oh) {
    static uint8_t keep[4096 * 2048 * 4]; int W1, H1, W2, H2;
    double t0 = tt_now(); uint8_t *a = text_up_ref(t, &W1, &H1); double t1 = tt_now(); if (a) memcpy(keep, a, (size_t)W1 * H1 * 4);
    double t2 = tt_now(); uint8_t *b = text_up_new(t, &W2, &H2); double t3 = tt_now();
    g_tt_n++; g_tt_old += t1 - t0; g_tt_new += t3 - t2;
    if (!a != !b || (a && (W1 != W2 || H1 != H2 || memcmp(keep, b, (size_t)W1 * H1 * 4)))) g_tt_bad++;
    *ow = W2; *oh = H2; return b;
}
#define text_up text_up_new
#endif
static uint8_t *text_up(const Tex *t, int *ow, int *oh) {
    int S = g_scale < 2 ? 2 : g_scale > 4 ? 4 : g_scale, w = t->w > 1024 ? 1024 : t->w, h = t->h > 512 ? 512 : t->h;
    static uint8_t A[1024 * 512], G[1024 * 512], M[1024 * 512], R[1024 * 512]; static uint8_t out[4096 * 2048 * 4];
    static int X0[4096]; static float WX[4096][4]; static float HS[512 * 4096];
    if ((size_t)w * S * h * S * 4 > sizeof out) return 0;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {          /* each texel: its edge step (alpha) and colour group */
        Col c = texel(t, x, y); int k = 0;
        for (int i = 0; i < 16; i++) if (t->pal[i].r == c.r && t->pal[i].g == c.g && t->pal[i].b == c.b && t->pal[i].a == c.a) { k = i; break; }
        A[y * w + x] = (uint8_t)c.a; G[y * w + x] = (uint8_t)(k >> 2);
    }
    /* (most of a line of text is empty: M = any ink in the 4 x 4 texels around, there the result is see-through) */
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int m = 0; for (int i = -1; i <= 2; i++) { int xx = x + i; if (xx < 0) xx = 0; if (xx >= w) xx = w - 1; m |= A[y * w + xx]; }
        R[y * w + x] = (uint8_t)(m != 0);
    }
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int m = 0; for (int j = -1; j <= 2; j++) { int yy = y + j; if (yy < 0) yy = 0; if (yy >= h) yy = h - 1; m |= R[yy * w + x]; }
        M[y * w + x] = (uint8_t)m;
    }
    int W = w * S, H = h * S;
    for (int X = 0; X < W; X++) {                                    /* (the same weights for every row) */
        float sx = (X + 0.5f) / S - 0.5f; int x0 = (int)floor(sx); float fx = sx - x0;
        X0[X] = x0; for (int i = 0; i < 4; i++) WX[X][i] = cubw(fx - (i - 1));
    }
    for (int y = 0; y < h; y++) {                                   /* (across first, for each texel row: then only 4 down) */
        const uint8_t *ar = A + y * w; float *hs = HS + (size_t)y * W;
        for (int X = 0; X < W; X++) {
            int x0 = X0[X]; float rs = 0.f;
            for (int i = 0; i < 4; i++) { int xx = x0 + i - 1; if (xx < 0) xx = 0; if (xx >= w) xx = w - 1; rs += ar[xx] * WX[X][i]; }
            hs[X] = rs;
        }
    }
    for (int Y = 0; Y < H; Y++) {
        float sy = (Y + 0.5f) / S - 0.5f; int y0 = (int)floor(sy); float fy = sy - y0, wy[4]; int yr[4];
        for (int j = 0; j < 4; j++) { wy[j] = cubw(fy - (j - 1)); int yy = y0 + j - 1; yr[j] = (yy < 0 ? 0 : yy >= h ? h - 1 : yy) * w; }
        int ym = y0 < 0 ? 0 : y0 >= h ? h - 1 : y0; uint8_t *o = out + (size_t)Y * W * 4;
        for (int X = 0; X < W; X++, o += 4) {
            int x0 = X0[X], xm = x0 < 0 ? 0 : x0 >= w ? w - 1 : x0;
            if (y0 >= 0 && x0 >= 0 && !M[ym * w + xm]) { const Col *f = &t->pal[G[ym * w + xm] * 4 + 3]; o[0] = (uint8_t)f->r; o[1] = (uint8_t)f->g; o[2] = (uint8_t)f->b; o[3] = 0; continue; }
            float a = 0.f; int best = -1, bg = 0;
            for (int j = 0; j < 4; j++) a += HS[(size_t)(yr[j] / w) * W + X] * wy[j];
            for (int j = 1; j <= 2; j++) for (int i = 1; i <= 2; i++) {
                int xx = x0 + i - 1; if (xx < 0) xx = 0; if (xx >= w) xx = w - 1;
                int v = A[yr[j] + xx]; if (v > best) { best = v; bg = G[yr[j] + xx]; }
            }
            float e = (a - 96.f) / 64.f; e = e < 0.f ? 0.f : e > 1.f ? 1.f : e; e = e * e * (3.f - 2.f * e);
            int al = (int)(e * 255.f + 0.5f); const Col *f = &t->pal[bg * 4 + 3];
            o[0] = (uint8_t)f->r; o[1] = (uint8_t)f->g; o[2] = (uint8_t)f->b; o[3] = (uint8_t)al;
        }
    }
    *ow = W; *oh = H; return out;
}
#ifdef TEXT_TEST
#undef text_up
#define text_up text_up_cmp
#endif
static GLuint tex_for__b(GEState *g, Tex *t, int clampu, int clampv);
static GLuint tex_for(GEState *g, Tex *t, int clampu, int clampv) { uint64_t _t = PF_TSC(); GLuint _r = tex_for__b(g, t, clampu, clampv); pf_add(6, _t); return _r; }
static GLuint tex_for__b(GEState *g, Tex *t, int clampu, int clampv) {
    uint32_t *r = g->reg;
    uint64_t key = ((uint64_t)t->addr << 32) ^ ((uint64_t)t->w << 20) ^ ((uint64_t)t->h << 8) ^ (uint64_t)(t->fmt | (t->sw << 4))
                 ^ ((uint64_t)t->rowbytes << 44) ^ ((uint64_t)(r[0xC5] & 0x1FFFFF) * 0x9E3779B97F4A7C15ull);
    uint32_t n = t->fmt >= 8 ? (uint32_t)(((t->w + 3) / 4) * ((t->h + 3) / 4) * (t->fmt == 8 ? 8 : 16)) : (uint32_t)t->rowbytes * t->h;
    if (n > t->avail) n = t->avail;
    uint64_t hsh = hash_vram_cached(g, t->base, n, g_frame_no); g_txs[0]++; g_txs[1] += n;
    if (t->fmt >= 4 && t->fmt <= 7) hsh ^= hash_bytes((const uint8_t *)t->pal, (uint32_t)(t->ncl * sizeof(Col))) * 31;
    /* A texture is "the same" only with the same place AND the same content: when the game puts a different picture
     * at the same place, it gets a new OpenGL texture (the old one may still be needed to draw the last frame again
     * for the in-between pictures). The least recently used one is reused, never one of the last frame. */
    static int quick[4096]; GLTex *e = 0; uint32_t qs = (uint32_t)((key ^ hsh) ^ ((key ^ hsh) >> 29)) & 4095;
    if (quick[qs] < g_ntex && g_tex[quick[qs]].key == key && g_tex[quick[qs]].hash == hsh) e = &g_tex[quick[qs]];
    else for (int i = 0; i < g_ntex; i++) if (g_tex[i].key == key && g_tex[i].hash == hsh) { e = &g_tex[i]; quick[qs] = i; break; }
    if (!e) { g_txs[2]++; g_txs[3] += (uint64_t)t->w * t->h;
        if (g_ntex < MAX_TEX) { e = &g_tex[g_ntex++]; p_glGenTextures(1, &e->id); }
        else {
            GLTex *old = 0;
            for (int i = 0; i < g_ntex; i++) {
                GLTex *c = &g_tex[i];
                if (c->last_use + 2 >= g_frame_no) continue;               /* used by the last frames (may be drawn again) */
                if (!old || c->last_use < old->last_use) old = c;
            }
            if (!old) { old = &g_tex[0]; for (int i = 1; i < g_ntex; i++) if (g_tex[i].last_use < old->last_use) old = &g_tex[i]; }
            e = old;
        }
        e->key = key; quick[qs] = (int)(e - g_tex);
        e->hash = hsh; e->w = t->w; e->h = t->h; e->rep = rep_lookup(g, t); e->raw = 1; e->ka = g_lk[0]; e->kb = g_lk[1]; e->kc = g_lk[2];
    }
    e->last_use = g_frame_no; (void)clampu; (void)clampv;
    if (e->rep >= 0) {
        GLRep *r = &g_rep[e->rep];
        if (r->state == 2) { r->last = g_frame_no; return r->tex; }    /* the HD picture (the PSP one isn't even decoded) */
        if (r->state == 0) rep_request(e->rep);                         /* (let go earlier: load it again) */
    }
    if (e->raw) {
        e->raw = 0;
        if (g_text_hd && e->rep < 0 && is_gen_text(t)) {
            int W, H; uint8_t *up = text_up(t, &W, &H);
            if (up) {
                p_glBindTexture(GL_TEXTURE_2D, e->id); ap_forget();
                p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, up);
                if (g_mips) { p_glGenerateMipmap(GL_TEXTURE_2D); if (g_aniso > 1.f) p_glTexParameterf(GL_TEXTURE_2D, 0x84FE, g_aniso); }
                return e->id;
            }
        }
        static uint8_t buf[1024 * 1024 * 4];
        int w = t->w > 1024 ? 1024 : t->w, h = t->h > 1024 ? 1024 : t->h;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            Col c = texel(t, x, y); uint8_t *o = buf + ((size_t)y * w + x) * 4;
            o[0] = (uint8_t)c.r; o[1] = (uint8_t)c.g; o[2] = (uint8_t)c.b; o[3] = (uint8_t)c.a;
        }
        if (e->rep < 0 && e->kb && g_near_on) near_push(e->ka, e->kb, e->kc, w, h, buf);
        p_glBindTexture(GL_TEXTURE_2D, e->id); ap_forget();
        p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
        if (g_mips) {                                             /* smaller copies for far away: less shimmer */
            p_glGenerateMipmap(GL_TEXTURE_2D);
            if (g_aniso > 1.f) p_glTexParameterf(GL_TEXTURE_2D, 0x84FE /* MAX_ANISOTROPY */, g_aniso);
        }
    }
    return e->id;
}

/* ------------------------------------------------------------------ batches: all triangles of one draw command */
static uint64_t cmd_key(const GLSt *t, int n) {
    /* (not the framebuffer address: the game draws into two buffers in turn) */
    /* (nor the vertex count: triangles behind the camera are left out, so it changes) */
    uint32_t k[9] = {(uint32_t)g_fbs[t->fbi].w, t->tex, t->through, t->bon, t->bs, t->bd, t->zon, (uint32_t)t->texon, (uint32_t)t->tfunc};
    (void)n; return hash_bytes((const uint8_t *)k, sizeof k);
}
static void rec_overflow(void) {                           /* recording full: no in-between pictures for this frame */
    g_rec[g_rcur].over = 1; g_vbuf = g_vtx; g_vcap = MAX_VTX; g_nvtx = g_vbase = 0; g_sbuf = 0;
}
static GLSt g_ap; static int g_ap_ok, g_ap_H;
static void gl_flush__b(void);
static void gl_flush(void) { uint64_t _t = PF_TSC(); gl_flush__b(); pf_add(10, _t); }
static void gl_flush__b(void) {
    if (!g_gl_on) { g_nvtx = g_vbase = 0; return; }
    int n = g_nvtx - g_vbase; GLRec *Rg = &g_rec[g_rcur]; int gn = (g_rec_on && g_vbuf == Rg->v) ? Rg->ngv - Rg->gbase : 0;
    if (n <= 0 && gn <= 0) { if (g_vbuf == g_vtx) g_nvtx = g_vbase = 0; Rg->ngv = Rg->gbase; return; }
    if (n > 0 && g_st.texon && g_st.tfilt && !(g_bt_clu && g_bt_clv)) {
        /* smooth filtering + "repeat": a picture used once across a surface (floor tiles...) would blend in its
           opposite edge along its borders (a dotted seam). If the draw stays inside the picture, clamp instead. */
        const GLVtx *q = g_vbuf + g_vbase; float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
        for (int k = 0; k < n; k++) {
            if (q[k].u < u0) u0 = q[k].u; if (q[k].u > u1) u1 = q[k].u; if (q[k].v < v0) v0 = q[k].v; if (q[k].v > v1) v1 = q[k].v;
        }
        int cu = g_bt_clu || (u0 >= -0.002f && u1 <= 1.002f), cv = g_bt_clv || (v0 >= -0.002f && v1 <= 1.002f);
        if (cu != g_st.clu || cv != g_st.clv || (g_ap_ok && (g_ap.clu != cu || g_ap.clv != cv))) {
            g_st.clu = cu; g_st.clv = cv; g_ap.clu = cu; g_ap.clv = cv;
            if (!g_ap_ok || g_ap.tex != g_st.tex) p_glBindTexture(GL_TEXTURE_2D, g_st.tex);
            p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, cu ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, cv ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        }
    }
    if (n > 0) {
        p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n * sizeof(GLVtx), g_vbuf + g_vbase, GL_STREAM_DRAW);
        p_glDrawArrays(GL_TRIANGLES, 0, n);
    } else n = 0;
    GLRec *R = &g_rec[g_rcur];
    if (g_rec_on && g_vbuf == R->v) {
        if (R->nc >= FR_CMD) rec_overflow();
        else {
            GLCmd *c = &R->c[R->nc++]; c->st = g_st; c->kind = 0; c->v0 = g_vbase; c->n = n; c->match = -1; c->ssw = 0; c->key = cmd_key(&g_st, n);
            c->g0 = R->gbase; c->gn = R->ngv - R->gbase; R->gbase = R->ngv;
            c->key2 = c->key ^ ((uint64_t)g_st.va * 0x9E3779B97F4A7C15ull) ^ 0x5bd1e995u;
            double sx = 0, sy = 0, sw = 0; const GLVtx *q = g_vbuf + g_vbase;
            for (int k = 0; k < n; k++) { sx += (double)q[k].x * q[k].w; sy += (double)q[k].y * q[k].w; sw += q[k].w; }
            c->cx = sw > 0 ? (float)(sx / sw) : 0.f; c->cy = sw > 0 ? (float)(sy / sw) : 0.f;
            R->fbmask |= 1u << g_st.fbi; R->nv = g_nvtx; g_vbase = g_nvtx;
            if (g_st.pj >= 0) {
                GLLit *Lt = &g_lit[g_rcur]; Lt->pj[(int)g_st.pj].nv += n;
                if (g_st.lit) { Lt->lsum[0] += g_st.ldir[0] * n; Lt->lsum[1] += g_st.ldir[1] * n; Lt->lsum[2] += g_st.ldir[2] * n; Lt->lw += n; }
            }
            return;
        }
    }
    g_nvtx = g_vbase = 0;
}
static const GLenum ZF[8] = {GL_NEVER, GL_ALWAYS, 0x0202, 0x0205, 0x0201, 0x0203, 0x0204, 0x0206};   /* never always eq ne lt le gt ge */
static GLenum blend_src(int f, uint32_t fix, int *fixused) {
    switch (f) {
    case 0: return GL_DST_COLOR; case 1: return GL_ONE_MINUS_DST_COLOR; case 2: return GL_SRC_ALPHA; case 3: return GL_ONE_MINUS_SRC_ALPHA;
    case 4: return GL_DST_ALPHA; case 5: return GL_ONE_MINUS_DST_ALPHA; case 6: return GL_SRC_ALPHA; case 7: return GL_ONE_MINUS_SRC_ALPHA;
    case 8: return GL_DST_ALPHA; case 9: return GL_ONE_MINUS_DST_ALPHA;
    default: if ((fix & 0xFFFFFF) == 0xFFFFFF) return GL_ONE; if ((fix & 0xFFFFFF) == 0) return GL_ZERO; *fixused = 1; return GL_CONSTANT_COLOR;
    }
}
static GLenum blend_dst(int f, uint32_t fix, int *fixused) {
    switch (f) {
    case 0: return GL_SRC_COLOR; case 1: return GL_ONE_MINUS_SRC_COLOR; case 2: return GL_SRC_ALPHA; case 3: return GL_ONE_MINUS_SRC_ALPHA;
    case 4: return GL_DST_ALPHA; case 5: return GL_ONE_MINUS_DST_ALPHA; case 6: return GL_SRC_ALPHA; case 7: return GL_ONE_MINUS_SRC_ALPHA;
    case 8: return GL_DST_ALPHA; case 9: return GL_ONE_MINUS_DST_ALPHA;
    default: if ((fix & 0xFFFFFF) == 0xFFFFFF) return GL_ONE; if ((fix & 0xFFFFFF) == 0) return GL_ZERO; *fixused = 2; return GL_CONSTANT_COLOR;
    }
}
/* Movies with subtitles: the game writes each movie picture straight into a framebuffer in video memory (CPU), then
 * draws the subtitle onto it with the graphics chip. Here the graphics chip's drawing goes to the card's copy of that
 * framebuffer, which never saw the picture - so the subtitle stood on black. A framebuffer written by the CPU is
 * marked (gl_vram_written, from the movie code) and gets that picture (the HD one, if there is one) put into its
 * card copy before the first drawing onto it. */
static volatile uint32_t g_vw[8]; static volatile int g_vw_on[8];
/* framebuffers the movie now playing has written a picture into (all 0xFFFFFFFF: none yet) */
static volatile uint32_t g_mvw[8] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
static uint32_t g_vws_addr[8], g_vws_seq[8], g_vws_n;           /* (each framebuffer's latest video-memory picture: a number) */
static uint32_t vws_get(uint32_t addr) { for (int i = 0; i < 8; i++) if (g_vws_seq[i] && g_vws_addr[i] == addr) return g_vws_seq[i]; return 0; }
GEXPORT void gl_vram_written(uint32_t addr) {
    g_vram_gen++; addr &= 0x1FFFFF; int k = -1;
    { int j = -1; for (int i = 0; i < 8; i++) if (g_vws_seq[i] && g_vws_addr[i] == addr) j = i;
      if (j < 0) { j = 0; for (int i = 1; i < 8; i++) if (g_vws_seq[i] < g_vws_seq[j]) j = i; }
      g_vws_addr[j] = addr; g_vws_seq[j] = ++g_vws_n; }
    for (int i = 0; i < 8; i++) if (g_vw_on[i] && g_vw[i] == addr) k = i;
    for (int i = 0; i < 8 && k < 0; i++) if (!g_vw_on[i]) k = i;
    if (k < 0) k = 0;
    g_vw[k] = addr; g_vw_on[k] = 1;
    for (int i = 0; i < 8; i++) if (g_mvw[i] == addr) return;              /* (written during this movie: see gl_movie) */
    for (int i = 0; i < 8; i++) if (g_mvw[i] == 0xFFFFFFFFu) { g_mvw[i] = addr; return; }
}
static int mvw_find(uint32_t addr) { addr &= 0x1FFFFF; for (int i = 0; i < 8; i++) if (g_mvw[i] == addr) return 1; return 0; }
static int vw_find(uint32_t addr) { addr &= 0x1FFFFF; for (int i = 0; i < 8; i++) if (g_vw_on[i] && g_vw[i] == addr) return i; return -1; }
static void vram_into_fb(GLFb *f);
static void use_fb(GLFb *f) {
    int vw = vw_find(f->addr);
    if (vw >= 0) { g_vw_on[vw] = 0; gl_flush(); g_cur_fb = 0; vram_into_fb(f); }
    f->drawn = g_frame_no; f->dirty = 1;
    if (g_cur_fb == f) return;
    gl_flush();
    p_glBindFramebuffer(GL_FRAMEBUFFER, f->msfbo ? f->msfbo : f->fbo); p_glViewport(0, 0, f->w * g_scale, f->h * g_scale);
    p_glUniform2f(u_fbsize, (float)f->w, (float)f->h);
    g_cur_fb = f;
}
/* turn a captured state into OpenGL state (H = framebuffer height in PSP pixels). What OpenGL already has is not
 * sent again (g_ap: the state last applied; ap_forget() whenever other code changes OpenGL state directly). */
static void ap_forget(void) { g_ap_ok = 0; }
static void apply_state(const GLSt *s, int H) {
    const GLSt *o = &g_ap; int all = !g_ap_ok;
    if (all || H != g_ap_H || memcmp(s->sc, o->sc, sizeof s->sc)) {
        if (all) p_glEnable(GL_SCISSOR_TEST);
        p_glScissor(s->sc[0] * g_scale, (H - s->sc[3]) * g_scale, (s->sc[2] - s->sc[0]) * g_scale, (s->sc[3] - s->sc[1]) * g_scale);
    }
    if (all || s->zon != o->zon || (s->zon && (s->zf != o->zf || s->zmask != o->zmask))) {
        if (s->zon) { p_glEnable(GL_DEPTH_TEST); p_glDepthFunc(ZF[s->zf]); p_glDepthMask(s->zmask); }
        else p_glDisable(GL_DEPTH_TEST);
    }
    if (all) p_glColorMask(1, 1, 1, 1);
    if (g_stencil && (all || s->son != o->son || (s->son && (s->sf != o->sf || s->sref != o->sref || s->smask != o->smask ||
                                                             memcmp(s->sop, o->sop, 3) || s->swmask != o->swmask)))) {
        static const GLenum SO[8] = {0x1E00 /* KEEP */, 0 /* ZERO */, 0x1E01 /* REPLACE */, 0x150A /* INVERT */, 0x1E02 /* INCR */, 0x1E03 /* DECR */, 0x1E00, 0x1E00};
        if (s->son) {
            p_glEnable(GL_STENCIL_TEST); p_glStencilFunc(ZF[s->sf], s->sref, s->smask);
            p_glStencilOp(SO[s->sop[0]], SO[s->sop[1]], SO[s->sop[2]]); p_glStencilMask(s->swmask);
        } else p_glDisable(GL_STENCIL_TEST);
    }
    if (all || s->bon != o->bon || (s->bon && (s->bs != o->bs || s->bd != o->bd || s->beq != o->beq || s->bfix != o->bfix ||
                                                memcmp(s->bc, o->bc, sizeof s->bc)))) {
        if (s->bon) {
            p_glEnable(GL_BLEND); p_glBlendFuncSeparate(s->bs, s->bd, GL_ZERO, GL_ONE); p_glBlendEquation(s->beq);
            if (s->bfix) p_glBlendColor(s->bc[0], s->bc[1], s->bc[2], s->bc[3]);
        } else p_glDisable(GL_BLEND);
    }
    if (s->texon && (all || !o->texon || s->tex != o->tex || s->tfilt != o->tfilt || s->clu != o->clu || s->clv != o->clv)) {
        p_glBindTexture(GL_TEXTURE_2D, s->tex);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, s->tfilt ? (g_mips ? 0x2703 /* LINEAR_MIPMAP_LINEAR */ : GL_LINEAR) : GL_NEAREST);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, s->tfilt ? GL_LINEAR : GL_NEAREST);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, s->clu ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, s->clv ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    }
    if (all || s->texon != o->texon) p_glUniform1i(u_texon, s->texon);
    if (all || s->tfunc != o->tfunc) p_glUniform1i(u_tfunc, s->tfunc);
    if (all || s->ta != o->ta) p_glUniform1i(u_ta, s->ta);
    if (all || s->dbl != o->dbl) p_glUniform1i(u_dbl, s->dbl);
    if (all || memcmp(s->env, o->env, sizeof s->env)) p_glUniform3f(u_env, s->env[0], s->env[1], s->env[2]);
    if (all || s->atest != o->atest) p_glUniform1i(u_atest, s->atest);
    if (s->atest >= 0 && (all || o->atest < 0 || s->amask != o->amask)) p_glUniform1i(u_amask, s->amask);
    if (s->atest >= 0 && (all || o->atest < 0 || s->aref != o->aref)) p_glUniform1i(u_aref, s->aref);
    if (all || s->fogon != o->fogon) p_glUniform1i(u_fogon, s->fogon);
    if (all || memcmp(s->fogcol, o->fogcol, sizeof s->fogcol)) p_glUniform3f(u_fogcol, s->fogcol[0], s->fogcol[1], s->fogcol[2]);
    /* what is kept: an atest < 0 keeps the old amask/aref values in OpenGL, so remember those */
    int am = o->amask, ar = o->aref; g_ap = *s; if (s->atest < 0 && !all) { g_ap.amask = am; g_ap.aref = ar; }
    g_ap_H = H; g_ap_ok = 1;
}
/* the camera set of a 3D draw (remaster lighting): found among this frame's, or added */
static int pj_for(GEState *g) {
    GLLit *L = &g_lit[g_rcur]; const uint32_t *r = g->reg;
    double sx = gf32(r[0x42]), sy = gf32(r[0x43]), ox = gf32(r[0x45]) - (r[0x4C] & 0xFFFF) / 16.0, oy = gf32(r[0x46]) - (r[0x4D] & 0xFFFF) / 16.0;
    double sz = gf32(r[0x44]), oz = gf32(r[0x47]);
    for (int i = 0; i < L->npj; i++) {
        GLPj *p = &L->pj[i];
        if (p->sx == sx && p->sy == sy && p->ox == ox && p->oy == oy && p->sz == sz && p->oz == oz && !memcmp(p->P, g->proj, sizeof p->P) && !memcmp(p->V, g->view, sizeof p->V)) return i;
    }
    if (L->npj >= MAX_PJ) return -1;
    GLPj *p = &L->pj[L->npj]; memset(p, 0, sizeof *p);
    p->sx = sx; p->sy = sy; p->ox = ox; p->oy = oy; p->sz = sz; p->oz = oz; memcpy(p->P, g->proj, sizeof p->P); memcpy(p->V, g->view, sizeof p->V);
    /* clip x, y, w = A (view x, y, z) + c: A's inverse gives the place in front of the camera back */
    const double *P = g->proj; double a[9] = {P[0], P[4], P[8], P[1], P[5], P[9], P[3], P[7], P[11]};
    double det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) + a[2] * (a[3] * a[7] - a[4] * a[6]);
    double sc = fabs(a[0] * a[4] * a[8]) + 1e-30;
    if (fabs(det) > 1e-9 * sc && fabs(sx) > 1e-6 && fabs(sy) > 1e-6 && (a[6] != 0 || a[7] != 0 || a[8] != 0)) {
        double id = 1.0 / det, *m = p->Mi;
        m[0] = (a[4] * a[8] - a[5] * a[7]) * id; m[1] = (a[2] * a[7] - a[1] * a[8]) * id; m[2] = (a[1] * a[5] - a[2] * a[4]) * id;
        m[3] = (a[5] * a[6] - a[3] * a[8]) * id; m[4] = (a[0] * a[8] - a[2] * a[6]) * id; m[5] = (a[2] * a[3] - a[0] * a[5]) * id;
        m[6] = (a[3] * a[7] - a[4] * a[6]) * id; m[7] = (a[1] * a[6] - a[0] * a[7]) * id; m[8] = (a[0] * a[4] - a[1] * a[3]) * id;
        p->ok = 1;
    }
    return L->npj++;
}
static double g_lsun[3] = {-0.55, 0.74, 0.39}, g_dmax_s;   /* the sun (towards it), kept from frame to frame */
/* the world place of a recorded vertex (screen x, y + distance w), with camera set p and view matrix V */
static inline void lit_world(const GLPj *p, const double *V, float x, float y, float w, double *o) {
    double a0 = (x - p->ox) / p->sx * w - p->P[12], a1 = (y - p->oy) / p->sy * w - p->P[13], a2 = w - p->P[15];
    const double *m = p->Mi;
    double v0 = m[0] * a0 + m[1] * a1 + m[2] * a2 - V[9], v1 = m[3] * a0 + m[4] * a1 + m[5] * a2 - V[10], v2 = m[6] * a0 + m[7] * a1 + m[8] * a2 - V[11];
    o[0] = V[0] * v0 + V[1] * v1 + V[2] * v2; o[1] = V[3] * v0 + V[4] * v1 + V[5] * v2; o[2] = V[6] * v0 + V[7] * v1 + V[8] * v2;
}
/* how different two projections are: the largest change of one entry, as a share of the entry's size */
static double pj_pdiff(const GLPj *a, const GLPj *b) {
    double md = 0;
    for (int k = 0; k < 16; k++) { double d = fabs(a->P[k] - b->P[k]), m = fabs(a->P[k]) > fabs(b->P[k]) ? fabs(a->P[k]) : fabs(b->P[k]); if (m > 1e-9 && d / m > md) md = d / m; }
    return md;
}
/* a world point as camera set p sees it: X, Y, Z, W (screen place times W) */
static inline void pj_projd(const GLPj *p, const double *w, double *o) {
    const double *V = p->V, *P = p->P;
    double v0 = w[0] * V[0] + w[1] * V[3] + w[2] * V[6] + V[9], v1 = w[0] * V[1] + w[1] * V[4] + w[2] * V[7] + V[10], v2 = w[0] * V[2] + w[1] * V[5] + w[2] * V[8] + V[11];
    double cx = v0 * P[0] + v1 * P[4] + v2 * P[8] + P[12], cy = v0 * P[1] + v1 * P[5] + v2 * P[9] + P[13];
    double cz = v0 * P[2] + v1 * P[6] + v2 * P[10] + P[14], cw = v0 * P[3] + v1 * P[7] + v2 * P[11] + P[15];
    o[0] = cx * p->sx + p->ox * cw; o[1] = cy * p->sy + p->oy * cw; o[2] = cz * p->sz + p->oz * cw; o[3] = cw;
}
/* the same, straight into light space B as one matrix for camera set p: q = K (x w, y w, w) + K[9..11] */
static void lit_mat(const GLPj *p, const double *V, const double *B, double *K) {
    const double *M = p->Mi; double D[9] = {1.0 / p->sx, 0, -p->ox / p->sx, 0, 1.0 / p->sy, -p->oy / p->sy, 0, 0, 1};
    double BV[9], BVM[9];
    for (int a = 0; a < 3; a++) for (int j = 0; j < 3; j++) BV[3 * a + j] = B[3 * a] * V[j] + B[3 * a + 1] * V[3 + j] + B[3 * a + 2] * V[6 + j];
    for (int a = 0; a < 3; a++) for (int j = 0; j < 3; j++) BVM[3 * a + j] = BV[3 * a] * M[j] + BV[3 * a + 1] * M[3 + j] + BV[3 * a + 2] * M[6 + j];
    for (int a = 0; a < 3; a++) for (int j = 0; j < 3; j++) K[3 * a + j] = BVM[3 * a] * D[j] + BVM[3 * a + 1] * D[3 + j] + BVM[3 * a + 2] * D[6 + j];
    double a0[3] = {-p->P[12], -p->P[13], -p->P[15]}, m0[3];
    for (int j = 0; j < 3; j++) m0[j] = M[3 * j] * a0[0] + M[3 * j + 1] * a0[1] + M[3 * j + 2] * a0[2] - V[9 + j];
    for (int a = 0; a < 3; a++) K[9 + a] = BV[3 * a] * m0[0] + BV[3 * a + 1] * m0[1] + BV[3 * a + 2] * m0[2];
}
/* frame i is complete: its sun, and the part of the world its shadow picture covers (around what is near the camera) */
/* ---- draws that lie wholly off the screen (psp_gpu.c ge_prim finds them before their points are worked out: the
 * game hands over whole rooms, most of it out of view). Nothing of them is drawn or recorded; what the rest of the
 * port learns from every draw is kept here: how many points (menus are told apart by that), how far away they are
 * and which light shines on them (the shadow picture is fitted with it), and their texture is looked at so that an
 * HD picture is loaded before it turns into view, and kept. */
static int g_pcull_on = 1; static uint32_t g_pcull_n[4];
GEXPORT void gl_prim_cull(int on) { g_pcull_on = on; }
GEXPORT void gl_prim_cull_stats(uint32_t *o) { memcpy(o, g_pcull_n, sizeof g_pcull_n); memset(g_pcull_n, 0, sizeof g_pcull_n); }
static uint32_t g_pc_hist[4][MAX_PJ][64]; static int g_pc_wv[4];
static int gl_prim_cull_ok(void) {
    if (!g_gl_on || !g_pcull_on || g_cast_all) return 0;          /* (everything casts shadows: also what is out of view) */
#ifdef TEX_DUMP
    if (g_dump_on) return 0;
#endif
    return 1;
}
static void gl_prim_culled(GEState *g, const Layout *L, int n, const float *w3, int nw) {
    uint32_t *r = g->reg; g_pcull_n[0]++; g_pcull_n[1] += (uint32_t)n;
    if (g_rec_on && g_vbuf == g_rec[g_rcur].v) {
        g_pc_wv[g_rcur] += n;
        int pj = pj_for(g);
        if (pj >= 0) {
            GLLit *Lt = &g_lit[g_rcur]; Lt->pj[pj].nv += n; uint32_t *h = g_pc_hist[g_rcur][pj];
            for (int k = 0; k < nw; k++) { float w = w3[k]; if (!(w > 1.f && w < 1e18f)) continue;
                int b = (int)(log2((double)w) * 2); if (b < 0) b = 0; if (b > 63) b = 63; h[b]++; }
            if (g_light_on && (r[0x17] & 1)) for (int i = 0; i < 4; i++) {
                if (!(r[0x18 + i] & 1) || ((r[0x5F + i] >> 8) & 3) != 0) continue;
                double d[3] = {gf32(r[0x63 + 3 * i]), gf32(r[0x64 + 3 * i]), gf32(r[0x65 + 3 * i])}, l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (l > 0) { Lt->lsum[0] += (float)(d[0] / l) * n; Lt->lsum[1] += (float)(d[1] / l) * n; Lt->lsum[2] += (float)(d[2] / l) * n; Lt->lw += n; }
                break;
            }
        }
    }
    if ((r[0x1E] & 1) && L->tc) {                                  /* its texture: once a frame is enough */
        static struct { uint32_t fr, k[6]; } memo[64];
        uint32_t k[6] = {r[0xA0], r[0xA8], r[0xB8], r[0xC3] | (r[0xC2] << 8), r[0xC5], 0};
        if ((r[0xC3] & 15) >= 4 && (r[0xC3] & 15) <= 7) k[5] = (r[0xB0] & 0xFFFFFFu) ^ ((r[0xB1] & 0x0F0000u) << 8) ^ (g->clut_len * 2654435761u) ^ hash_bytes(g->clut, g->clut_len < 64 ? g->clut_len : 64);   /* (which palette) */
        uint32_t q = (k[0] ^ (k[0] >> 7) ^ (k[0] >> 15) ^ k[5]) & 63;
        if (memo[q].fr != g_frame_no + 1 || memcmp(memo[q].k, k, sizeof k)) {
            memo[q].fr = g_frame_no + 1; memcpy(memo[q].k, k, sizeof k);
            static Tex T;
            if (tex_setup(g, &T)) {
                uint32_t ta = T.addr & 0x3FFFFFFFu; int isfb = 0;         /* (not a picture the card drew - an effect reading the screen: that is brought back when it is shown) */
                if (ta >= 0x04000000u && ta < 0x04200000u) { ta -= 0x04000000u;
                    for (int i = 0; i < g_nfb; i++) { GLFb *f = &g_fbs[i]; uint32_t bpp = f->fmt == 3 ? 4 : 2; if (ta >= f->addr && ta < f->addr + (uint32_t)f->w * 272 * bpp) { isfb = 1; break; } } }
                if (!isfb) { tex_for(g, &T, r[0xC7] & 1, (r[0xC7] & 0x100) != 0); g_pcull_n[2]++; }
            }
        }
    }
}
static void lit_fit(int i) {
    GLLit *L = &g_lit[i]; GLRec *R = &g_rec[i]; L->box = 0;
    if (L->main < 0 || R->over) return;
    if (L->lw > 50) { double l = sqrt(L->lsum[0] * L->lsum[0] + L->lsum[1] * L->lsum[1] + L->lsum[2] * L->lsum[2]);
                      if (l > 0.5 * L->lw) for (int k = 0; k < 3; k++) g_lsun[k] = g_lsun[k] * 0.8 + 0.2 * L->lsum[k] / l; }
    { double l = sqrt(g_lsun[0] * g_lsun[0] + g_lsun[1] * g_lsun[1] + g_lsun[2] * g_lsun[2]); for (int k = 0; k < 3; k++) g_lsun[k] /= l; }
    /* the sun: from the side the game's own light comes from, but g_sun_angle from straight overhead. (The world's
     * light is baked into it already; long sideways shadows need the whole level, and only what is on screen is
     * drawn: ceilings and walls out of view would let the sun through) */
    double hx = g_lsun[0], hz = g_lsun[2], hl = sqrt(hx * hx + hz * hz), an = g_sun_angle * 3.14159265358979 / 180.0;
    if (hl > 1e-6) { hx /= hl; hz /= hl; } else { hx = 1; hz = 0; }
    double sun[3] = {hx * sin(an), cos(an), hz * sin(an)};
    double *B = L->B, z[3] = {-sun[0], -sun[1], -sun[2]}, up[3] = {0, 1, 0};
    if (fabs(z[1]) > 0.99) { up[0] = 1; up[1] = 0; }
    double x[3] = {up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]};
    double xl = sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]); x[0] /= xl; x[1] /= xl; x[2] /= xl;
    double y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    for (int k = 0; k < 3; k++) { B[k] = x[k]; B[3 + k] = y[k]; B[6 + k] = z[k]; }
    /* how far the scene goes: the shadow picture covers the nearer part of it */
    static int hist[64]; memset(hist, 0, sizeof hist); int tot = 0; const double *MV = L->pj[L->main].V;
    for (int c = 0; c < R->nc; c++) {
        const GLCmd *cm = &R->c[c]; if (cm->kind || cm->st.pj < 0 || memcmp(L->pj[(int)cm->st.pj].V, MV, sizeof L->pj[0].V)) continue;
        for (int k = 0; k < cm->n; k += 3) { float w = R->v[cm->v0 + k].w; if (!(w > 1.f && w < 1e18f)) continue;   /* (also not-a-number: fades draw odd things) */
            int b = (int)(log2(w) * 2); if (b < 0) b = 0; if (b > 63) b = 63; hist[b]++; tot++; }
    }
    for (int p = 0; p < L->npj; p++) if (!memcmp(L->pj[p].V, MV, sizeof L->pj[0].V)) for (int k = 0; k < 64; k++) { hist[k] += (int)g_pc_hist[i][p][k]; tot += (int)g_pc_hist[i][p][k]; }   /* (and of the draws left out for being off the screen) */
    if (tot < 30) return;
    int acc = 0, b = 0; for (; b < 64; b++) { acc += hist[b]; if (acc * 10 >= tot * 6) break; }
    double dmax = pow(2.0, (b + 1) * 0.5) * 1.3; if (dmax < 3000) dmax = 3000;
    g_dmax_s = g_dmax_s > 0 ? g_dmax_s * 0.7 + dmax * 0.3 : dmax; dmax = g_dmax_s;
    double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30}, zlo = 1e30;
    /* the corners of what the camera sees, from close up to dmax away */
    const GLPj *mp = &L->pj[L->main]; double near = dmax * 0.02;
    for (int cn = 0; cn < 8; cn++) {
        double w = (cn & 4) ? dmax : near, sx = (cn & 1) ? 1.0 : -1.0, sy = (cn & 2) ? 1.0 : -1.0, wp[3], q[3];
        lit_world(mp, MV, (float)(mp->ox + sx * fabs(mp->sx)), (float)(mp->oy + sy * fabs(mp->sy)), (float)w, wp);
        q[0] = B[0] * wp[0] + B[1] * wp[1] + B[2] * wp[2]; q[1] = B[3] * wp[0] + B[4] * wp[1] + B[5] * wp[2]; q[2] = B[6] * wp[0] + B[7] * wp[1] + B[8] * wp[2];
        for (int a = 0; a < 3; a++) { if (q[a] < lo[a]) lo[a] = q[a]; if (q[a] > hi[a]) hi[a] = q[a]; }
    }
    /* (one matrix per camera set: screen place and distance straight to light space) */
    static double PK[MAX_PJ][12]; static signed char pk_ok[MAX_PJ];
    for (int i = 0; i < L->npj; i++) { const GLPj *p = &L->pj[i]; pk_ok[i] = p->ok && !memcmp(p->V, MV, sizeof p->V); if (pk_ok[i]) lit_mat(p, MV, B, PK[i]); }
    for (int c = 0; c < R->nc; c++) {                               /* things between the sun and that: they cast shadows into it */
        const GLCmd *cm = &R->c[c]; if (cm->kind || cm->st.pj < 0 || !pk_ok[(int)cm->st.pj]) continue;
        const double *K = PK[(int)cm->st.pj];
        for (int k = 0; k < cm->n; k++) {
            const GLVtx *v = &R->v[cm->v0 + k]; if (!(v->w > 0)) continue;
            double xw = (double)v->x * v->w, yw = (double)v->y * v->w, w = v->w;
            double q0 = K[0] * xw + K[1] * yw + K[2] * w + K[9], q1 = K[3] * xw + K[4] * yw + K[5] * w + K[10];
            if (q0 >= lo[0] && q0 <= hi[0] && q1 >= lo[1] && q1 <= hi[1]) { double q2 = K[6] * xw + K[7] * yw + K[8] * w + K[11]; if (q2 < zlo) zlo = q2; if (q2 > hi[2]) hi[2] = q2; }
        }
    }
    if (!(hi[0] > lo[0])) return;
    double size = hi[0] - lo[0]; if (hi[1] - lo[1] > size) size = hi[1] - lo[1];
    if (!g_cast_all) {
        /* only characters cast shadows: the shadow picture needs to cover just them (a shadow lies straight "below"
         * its caster as seen from the sun), so it can be much sharper than one covering the whole view */
        /* (characters farther away than dmax count too, up to 3x as far: a soldier lying further off lost his shadows
         *  and got them back as Snake came near - it looked like flicker) */
        double flo[2] = {lo[0], lo[1]}, fhi[2] = {hi[0], hi[1]};
        for (int cn = 4; cn < 8; cn++) {
            double sx = (cn & 1) ? 1.0 : -1.0, sy = (cn & 2) ? 1.0 : -1.0, wp[3], q[2];
            lit_world(mp, MV, (float)(mp->ox + sx * fabs(mp->sx)), (float)(mp->oy + sy * fabs(mp->sy)), (float)(dmax * 3.0), wp);
            q[0] = B[0] * wp[0] + B[1] * wp[1] + B[2] * wp[2]; q[1] = B[3] * wp[0] + B[4] * wp[1] + B[5] * wp[2];
            for (int a = 0; a < 2; a++) { if (q[a] < flo[a]) flo[a] = q[a]; if (q[a] > fhi[a]) fhi[a] = q[a]; }
        }
        double clo[2] = {1e30, 1e30}, chi[2] = {-1e30, -1e30};
        for (int c = 0; c < R->nc; c++) {
            const GLCmd *cm = &R->c[c]; if (cm->kind || cm->st.pj < 0 || !cm->st.skin || !cm->st.zmask || (cm->st.bon && cm->st.bfix)) continue;
            if (!pk_ok[(int)cm->st.pj]) continue;
            const double *K = PK[(int)cm->st.pj];
            for (int k = 0; k < cm->n; k++) {
                const GLVtx *v = &R->v[cm->v0 + k]; double q0, q1; if (!(v->w > 0 && v->w < 1e18f)) continue;
                double xw = (double)v->x * v->w, yw = (double)v->y * v->w, w = v->w;
                q0 = K[0] * xw + K[1] * yw + K[2] * w + K[9]; q1 = K[3] * xw + K[4] * yw + K[5] * w + K[10];
                if (q0 < flo[0] || q0 > fhi[0] || q1 < flo[1] || q1 > fhi[1]) continue;      /* (only those in view) */
                if (q0 < clo[0]) clo[0] = q0; if (q0 > chi[0]) chi[0] = q0; if (q1 < clo[1]) clo[1] = q1; if (q1 > chi[1]) chi[1] = q1;
                { double q2 = K[6] * xw + K[7] * yw + K[8] * w + K[11]; if (q2 < zlo) zlo = q2; if (q2 > hi[2]) hi[2] = q2; }   /* (its depth in the shadow picture too) */
            }
        }
        if (chi[0] > clo[0]) {
            double m = 60.0 + 0.08 * ((chi[0] - clo[0]) > (chi[1] - clo[1]) ? chi[0] - clo[0] : chi[1] - clo[1]);
            double cs = chi[0] - clo[0] + 2 * m; if (chi[1] - clo[1] + 2 * m > cs) cs = chi[1] - clo[1] + 2 * m;
            if (cs < 600) cs = 600;
            if (cs < size || cs < 4.0 * size) { size = cs; lo[0] = clo[0] - m; hi[0] = chi[0] + m; lo[1] = clo[1] - m; hi[1] = chi[1] + m; }   /* (may be bigger than the near view now, within reason) */
        }
    }
    size = pow(2.0, ceil(log2(size * 1.05) * 4) / 4);              /* (in steps: the shadows don't crawl when it changes a little) */
    { static double keep; static int smaller;                     /* (grows at once; shrinks only after 20 frames that need
                                                                   *  less: no back-and-forth between sizes = flicker) */
      if (size >= keep) { keep = size; smaller = 0; }
      else if (++smaller >= 20 || size < keep * 0.25) { keep = size; smaller = 0; }
      else size = keep; }
    double tex = size / 2048, cx = (lo[0] + hi[0]) * 0.5, cy = (lo[1] + hi[1]) * 0.5;
    L->bmin[0] = floor((cx - size * 0.5) / tex) * tex; L->bmin[1] = floor((cy - size * 0.5) / tex) * tex;   /* (on whole texels) */
    if (zlo > lo[2]) zlo = lo[2];
    L->bmin[2] = zlo - size * 0.05; L->zsize = hi[2] + size * 0.05 - L->bmin[2]; L->bsize = size; L->box = 1;
    L->dbg[0] = tot; L->dbg[1] = dmax; L->dbg[2] = size; L->dbg[3] = L->zsize; L->dbg[4] = hi[0] - lo[0]; L->dbg[5] = hi[1] - lo[1];
}
static int g_pm_n[4];
GEXPORT void gl_pm_dbg(int *o) { memcpy(o, g_pm_n, sizeof g_pm_n); memset(g_pm_n, 0, sizeof g_pm_n); }   /* (test aid) draws with colour writes masked */
GEXPORT int gl_light_dbg(double *o) {        /* (test aid) the last frame's camera sets: n, main, light; per set sx sy ox oy P[16] V[12] nv ok */
    GLLit *L = &g_lit[g_rlast]; o[0] = L->npj; o[1] = L->main; o[2] = L->lsum[0]; o[3] = L->lsum[1]; o[4] = L->lsum[2]; o[5] = L->lw;
    o[6] = L->box; o[7] = 0; for (int k = 0; k < 6; k++) o[700 + k] = L->dbg[k]; for (int k = 0; k < 3; k++) o[710 + k] = g_lsun[k];
    for (int i = 0; i < L->npj; i++) { double *e = o + 8 + i * 40; GLPj *p = &L->pj[i];
        e[0] = p->sx; e[1] = p->sy; e[2] = p->ox; e[3] = p->oy; memcpy(e + 4, p->P, sizeof p->P); memcpy(e + 20, p->V, sizeof p->V); e[32] = p->nv; e[33] = p->ok; }
    return L->npj;
}
/* new draw command: read the PSP state once and turn it into OpenGL state */
GEXPORT int gl_fb_download(GEState *g, uint32_t addr, uint32_t n);
static uint32_t g_shown[2] = {0xFFFFFFFF, 0xFFFFFFFF};
/* A draw that uses a framebuffer the graphics card drew as its texture, in the same pixel format (an effect that
 * reads the screen: a glow or blur over it, a death / menu effect): the card's own picture is used as the texture -
 * sharp, and nothing is brought back to the PSP's video memory or decoded again. (Bringing it back for each of
 * several passes a frame made the screen be drawn in software as well: the game slowed down to 8 fps.)
 * Drawing into the same framebuffer it reads: a copy of it taken first (the card can't read and draw one picture). */
static uint32_t g_fbt_n;
GEXPORT uint32_t gl_fbt_count(void) { return g_fbt_n; }
static int g_fbt_on = 1; GEXPORT void gl_fbt(int on) { g_fbt_on = on; }
static GLFb *fb_tex_src(GEState *g, const Tex *T) {
    if (!g_fbt_on || g_sw_on) return 0;
    uint32_t a = T->addr & 0x3FFFFFFF, bufw = g->reg[0xA8] & 0x7FF;
    if (a < 0x04000000u || a >= 0x04200000u || T->fmt < 1 || T->fmt > 3) return 0;
    a -= 0x04000000u;
    for (int i = 0; i < g_nfb; i++) {
        GLFb *f = &g_fbs[i];
        if (f->addr != a) continue;
        if (f->need_sw || f->gpu_skip || f->fmt != T->fmt || (uint32_t)f->w != bufw || f->drawn + 3 < g_frame_no) return 0;
        if (vw_find(f->addr) >= 0 || mvw_find(f->addr)) return 0;
        return f;
    }
    return 0;
}
static GLuint fb_tex_id(GLFb *src, GLFb *cur) {
    fb_resolve(src);
    if (src != cur) { g_cur_fb = 0; ap_forget(); use_fb(cur); return src->color; }
    if (src->cgen != src->ndraw || !src->ccolor) {                  /* (drawn into since the last copy) */
        int W = src->w * g_scale, H = src->h * g_scale;
        if (!src->ccolor) {
            p_glGenTextures(1, &src->ccolor); p_glBindTexture(GL_TEXTURE_2D, src->ccolor);
            p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
            p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            p_glGenFramebuffers(1, &src->cfbo); p_glBindFramebuffer(GL_FRAMEBUFFER, src->cfbo);
            p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, src->ccolor, 0);
        }
        p_glBindFramebuffer(GL_READ_FRAMEBUFFER, src->fbo); p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, src->cfbo);
        p_glDisable(GL_SCISSOR_TEST);
        p_glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        src->cgen = src->ndraw;
    }
    g_cur_fb = 0; ap_forget(); use_fb(cur);
    return src->ccolor;
}
   /* the framebuffers shown in the window lately */
static void batch_begin(GEState *g, const Layout *L, int has_uv) {
    gl_flush();
    uint32_t *r = g->reg; Pipe P; setup(g, &P);
    GLFb *f = fb_for(P.fb_addr, P.fb_fmt, (int)P.fb_stride);
    /* (software drawing for a framebuffer the game has stopped reading - a death or menu effect is over: back to
     *  the graphics card alone, if the card kept drawing it all along) */
    if (f->need_sw && !f->gpu_skip && g_frame_no - f->rd_frame > 60) { f->need_sw = 0; f->dl_cnt = 0; f->dl_gen = 0; g_swq_gen = 0xFFFFFFFF; }
    if (f->need_sw && f->addr != g_shown[0] && f->addr != g_shown[1]) {
        f->gpu_skip = 1;   /* (drawn in software, never shown: the game only reads it from video memory - the graphics card's copy would be unused work) */
        static Tex T0; if (P.texon && has_uv && tex_setup(g, &T0) && !gl_fb_download(g, T0.addr, (uint32_t)T0.rowbytes * T0.h)) gl_note_read(T0.addr, (uint32_t)T0.rowbytes * T0.h);   /* (but the software drawing needs its texture in video memory) */
        g_batch_ok = 0; g_bt_tex = 0; return;
    }
    use_fb(f); f->ndraw++;
    GLSt *s = &g_st; memset(s, 0, sizeof *s);
    s->fbi = (int)(f - g_fbs); s->fbaddr = f->addr; s->va = g->vaddr;
    s->sc[0] = P.sc[0]; s->sc[1] = P.sc[1]; s->sc[2] = P.sc[2] + 1; s->sc[3] = P.sc[3] + 1;
    if (s->sc[2] < s->sc[0]) s->sc[2] = s->sc[0]; if (s->sc[3] < s->sc[1]) s->sc[3] = s->sc[1];
    s->zon = P.ztest_on ? 1 : 0; s->zf = r[0xDE] & 7; s->zmask = P.zwrite ? 1 : 0;
    if (r[0x24] & 1) {
        s->son = 1; s->sf = r[0xDC] & 7; s->sref = (r[0xDC] >> 8) & 255; s->smask = (r[0xDC] >> 16) & 255;
        s->sop[0] = r[0xDD] & 7; s->sop[1] = (r[0xDD] >> 8) & 7; s->sop[2] = (r[0xDD] >> 16) & 7; s->swmask = (uint8_t)~(r[0xE9] & 255);
    }
    if (P.blend_on) {
        int fu = 0, fd = 0; uint32_t bm = r[0xDF];
        s->bon = 1; s->bs = blend_src(bm & 15, r[0xE0], &fu); s->bd = blend_dst((bm >> 4) & 15, r[0xE1], &fd);
        int eq = (bm >> 8) & 7;
        s->beq = eq == 1 ? GL_FUNC_SUBTRACT : eq == 2 ? GL_FUNC_REVERSE_SUBTRACT : eq == 3 ? GL_MIN : eq == 4 ? GL_MAX : GL_FUNC_ADD;
        /* fixed blend amounts: OpenGL has one blend colour (+ its alpha) for both. Source and destination both fixed
         * (see-through characters: 25% + 75%) -> the destination as 1 - the colour when they add up to 1, else the
         * destination amount (grey) in the colour's alpha */
        float fa[3] = {(r[0xE0] & 255) / 255.f, ((r[0xE0] >> 8) & 255) / 255.f, ((r[0xE0] >> 16) & 255) / 255.f};
        float fb[3] = {(r[0xE1] & 255) / 255.f, ((r[0xE1] >> 8) & 255) / 255.f, ((r[0xE1] >> 16) & 255) / 255.f};
        s->bc[3] = 1.f;
        if (fu || fd) { s->bfix = 1; const float *f = fu ? fa : fb; s->bc[0] = f[0]; s->bc[1] = f[1]; s->bc[2] = f[2]; }
        if (fu && fd) {
            float d = 0; for (int k = 0; k < 3; k++) { float e = fa[k] + fb[k] - 1.f; if (e < 0) e = -e; if (e > d) d = e; }
            if (d < 0.02f) s->bd = GL_ONE_MINUS_CONSTANT_COLOR;
            else { s->bd = 0x8003 /* CONSTANT_ALPHA */; s->bc[3] = (fb[0] + fb[1] + fb[2]) / 3.f; }
        }
    }
    /* texture + shading */
    static Tex T; int tex = P.texon && has_uv && tex_setup(g, &T);
    g_bt_tex = tex; g_bt_through = L->through; g_bt_flat = (r[0x50] & 1) == 0;
    tex_params(g, &g_bt_su, &g_bt_sv, &g_bt_ou, &g_bt_ov);
    g_bt_fbt = 0;
    if (tex) {
        GLFb *src = fb_tex_src(g, &T);
        if (src) {                                                    /* a framebuffer's own picture, as the graphics card has it */
            GLuint id = fb_tex_id(src, f);
            if (id) { s->tex = id; g_bt_fbt = 1; g_bt_fu = (float)T.w / (float)src->w; g_bt_fv = (float)T.h / (float)src->h; src->rd_frame = g_frame_no; g_fbt_n++; }
            else src = 0;
        }
        if (!src) {
        if (!gl_fb_download(g, T.addr, (uint32_t)T.rowbytes * T.h)) gl_note_read(T.addr, (uint32_t)T.rowbytes * T.h);   /* (a framebuffer as a texture: its picture from the graphics card) */
        s->tex = tex_for(g, &T, r[0xC7] & 1, (r[0xC7] & 0x100) != 0);
        }
        g_bt_tw = T.w; g_bt_th = T.h;
#ifdef TEX_DUMP
        if (g_dump_on && (L->through || g_dump_on == 2)) tex_dump(g, &T);
#endif
        s->tfilt = g_filter ? 1 : 0; s->clu = (r[0xC7] & 1) ? 1 : 0; s->clv = (r[0xC7] & 0x100) ? 1 : 0;
        g_bt_clu = s->clu; g_bt_clv = s->clv;
    }
    s->through = L->through ? 1 : 0; s->skin = L->wf ? 1 : 0;
    g_pm_n[0]++; if ((r[0xE8] & 0xFFFFFF) == 0xFFFFFF) g_pm_n[1]++; else if (r[0xE8] & 0xFFFFFF) g_pm_n[2]++; if (r[0xE9] & 0xFF) g_pm_n[3]++;
    s->pj = -1;
    if (g_rec_on && !L->through) s->pj = (int8_t)pj_for(g);       /* (the camera: for the exact in-between pictures, and the lighting) */
    if (g_light_on && g_rec_on && !L->through) {
        if (r[0x17] & 1) for (int i = 0; i < 4; i++) {                /* lit by a light from far away (a sun): its direction */
            if (!(r[0x18 + i] & 1) || ((r[0x5F + i] >> 8) & 3) != 0) continue;
            double d[3] = {gf32(r[0x63 + 3 * i]), gf32(r[0x64 + 3 * i]), gf32(r[0x65 + 3 * i])}, l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (l > 0) { s->lit = 1; s->ldir[0] = (float)(d[0] / l); s->ldir[1] = (float)(d[1] / l); s->ldir[2] = (float)(d[2] / l); }
            break;
        }
    }
    s->texon = tex; s->tfunc = r[0xC9] & 7; s->ta = (r[0xC9] & 0x100) != 0; s->dbl = (r[0xC9] & 0x10000) != 0;
    s->env[0] = (r[0xCA] & 255) / 255.f; s->env[1] = ((r[0xCA] >> 8) & 255) / 255.f; s->env[2] = ((r[0xCA] >> 16) & 255) / 255.f;
    if (P.atest_on) { s->atest = r[0xDB] & 7; s->amask = (r[0xDB] >> 16) & 255; s->aref = ((r[0xDB] >> 8) & 255) & ((r[0xDB] >> 16) & 255); }
    else s->atest = -1;
    s->fogon = (r[0x1F] & 1) && !L->through;
    s->fogcol[0] = (r[0xCF] & 255) / 255.f; s->fogcol[1] = ((r[0xCF] >> 8) & 255) / 255.f; s->fogcol[2] = ((r[0xCF] >> 16) & 255) / 255.f;
    apply_state(s, f->h);
    g_batch_ok = !(tex && T.addr == g_skip_tex);
    if (g_dbg_on && g_dbg_n < 2048) {
        uint32_t *e = g_dbg[g_dbg_n++];
        e[0] = P.fb_addr; e[1] = tex ? T.addr : 0; e[2] = tex ? (uint32_t)(T.fmt | (T.w << 8) | (T.h << 20)) : 0; e[3] = P.blend_on ? r[0xDF] : 0xFFFFFFFF;
        e[4] = r[0xE0]; e[5] = r[0xE1]; e[6] = (P.ztest_on ? (r[0xDE] & 7) : 9) | (P.zwrite << 4) | (P.atest_on << 5) | (L->through << 6) | ((r[0xC9] & 0x1FF) << 8);
        e[7] = r[0xDB]; e[8] = r[0x1F] | (r[0xFC] << 8); e[9] = r[0xD7]; e[10] = r[0xD8]; e[11] = r[0xC9];
        uint32_t *e2 = g_dbg2[g_dbg_n - 1]; e2[0] = (r[0x1D] & 1) | ((r[0x9B] & 1) << 1) | ((uint32_t)(L->wf != 0) << 2) | ((r[0x17] & 1) << 3); e2[1] = r[0x58]; e2[2] = (uint32_t)(int)s->pj; e2[3] = g->vaddr;
    }
}
static inline int batch_ready(GEState *g, const Layout *L, int has_uv) {
    if (!g_gl_on) return 0;
    if (g_batch_gen != g_drawgen || g_batch_L != L || g_batch_uv != has_uv) {
        batch_begin(g, L, has_uv); g_batch_gen = g_drawgen; g_batch_L = L; g_batch_uv = has_uv;
    }
    if (g_nvtx + 6 > g_vcap) { gl_flush(); if (g_vbuf != g_vtx) rec_overflow(); }
    return g_batch_ok;
}
static inline void put_vtx(const Vert *v, double u, double vv, Col c) {
    if (g_vbuf != g_vtx) g_sbuf[g_nvtx] = v->src;
    GLVtx *o = &g_vbuf[g_nvtx++];
    o->x = (float)v->x; o->y = (float)v->y; o->z = (float)v->z; o->w = (float)v->w;
    if (g_bt_tex) {
        if (g_bt_through) { o->u = (float)(u / g_bt_tw); o->v = (float)(vv / g_bt_th); }
        else { o->u = (float)(u * g_bt_su + g_bt_ou); o->v = (float)(vv * g_bt_sv + g_bt_ov); }
        if (g_bt_fbt) { o->u *= g_bt_fu; o->v = 1.f - o->v * g_bt_fv; }
    } else { o->u = 0; o->v = 0; }
    o->r = (uint8_t)clamp8(c.r); o->g = (uint8_t)clamp8(c.g); o->b = (uint8_t)clamp8(c.b); o->a = (uint8_t)clamp8(c.a);
    o->fog = (float)v->fog; o->lx = o->ly = 0.f; o->lz = -1.f;
}
static void gl_tri(GEState *g, const Vert *a, const Vert *b, const Vert *c, const Layout *L) {
    int uv = a->has_uv && b->has_uv && c->has_uv;
    if (!batch_ready(g, L, uv)) return;
    double area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
    if (area == 0) return;
    Col ca = g_bt_flat ? c->c : a->c, cb = g_bt_flat ? c->c : b->c;
    put_vtx(a, a->u, a->v, ca); put_vtx(b, b->u, b->v, cb); put_vtx(c, c->u, c->v, c->c);
}
static void gl_ghost(GEState *g, const Vert *a, const Vert *b, const Vert *c, const Layout *L) {
    GLRec *R = &g_rec[g_rcur];
    if (!g_rec_on || L->through || g_vbuf != R->v || R->ngv + 3 > FR_GV) return;
    int uv = a->has_uv && b->has_uv && c->has_uv;
    if (!batch_ready(g, L, uv) || g_vbuf != R->v) return;
    /* (put_vtx writes the frame's own list: borrow its next slots, then move them over) */
    if (g_nvtx + 3 > g_vcap) return;
    int at = g_nvtx; Col ca = g_bt_flat ? c->c : a->c, cb = g_bt_flat ? c->c : b->c;
    put_vtx(a, a->u, a->v, ca); put_vtx(b, b->u, b->v, cb); put_vtx(c, c->u, c->v, c->c);
    memcpy(R->gv + R->ngv, g_vbuf + at, 3 * sizeof(GLVtx)); g_nvtx = at;
    /* which way round a triangle must turn to be drawn (the game's culling: 0 = both, 1 = area > 0, 2 = area < 0) */
    R->gc[R->ngv / 3] = (uint8_t)((g->reg[0x1D] & 1) ? ((g->reg[0x9B] & 1) ? 1 : 2) : 0);
    R->ngv += 3;
}
static void gl_sprite(GEState *g, const Vert *a, const Vert *b, const Layout *L, int clear) {
    if (!g_gl_on) return;
    if (clear) {                                                  /* clear mode: a rectangle of colour and/or depth */
        g_batch_gen = 0xFFFFFFFF; gl_flush();
        uint32_t *r = g->reg; Pipe P; setup(g, &P); use_fb(fb_for(P.fb_addr, P.fb_fmt, (int)P.fb_stride));
        double x0 = a->x, y0 = a->y, x1 = b->x, y1 = b->y;
        if (x1 < x0) { double t = x0; x0 = x1; x1 = t; } if (y1 < y0) { double t = y0; y0 = y1; y1 = t; }
        int ix0 = (int)floor(x0 + 0.5), ix1 = (int)floor(x1 - 0.5 + 1e-6) + 1, iy0 = (int)floor(y0 + 0.5), iy1 = (int)floor(y1 - 0.5 + 1e-6) + 1;
        if (ix0 < P.sc[0]) ix0 = P.sc[0]; if (ix1 > P.sc[2] + 1) ix1 = P.sc[2] + 1; if (iy0 < P.sc[1]) iy0 = P.sc[1]; if (iy1 > P.sc[3] + 1) iy1 = P.sc[3] + 1;
        if (ix1 <= ix0 || iy1 <= iy0) return;
        uint32_t cm = r[0xD3]; int H = g_cur_fb->h; GLbitfield bits = 0; ap_forget();
        p_glEnable(GL_SCISSOR_TEST); p_glScissor(ix0 * g_scale, (H - iy1) * g_scale, (ix1 - ix0) * g_scale, (iy1 - iy0) * g_scale);
        if (cm & 0x100) { Col c = b->c; p_glColorMask(1, 1, 1, 1); p_glClearColor(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f); bits |= GL_COLOR_BUFFER_BIT; }
        if (cm & 0x400) { p_glDepthMask(1); p_glClearDepth((double)((int)((int64_t)b->z & 0xFFFF)) / 65535.0); bits |= GL_DEPTH_BUFFER_BIT; }
        if ((cm & 0x200) && g_stencil) { p_glStencilMask(0xFF); p_glClearStencil(b->c.a); bits |= GL_STENCIL_BUFFER_BIT; }   /* (alpha clear = the stencil) */
        if (bits) p_glClear(bits);
        GLRec *R = &g_rec[g_rcur];
        if (bits && g_rec_on && g_vbuf == R->v) {
            if (R->nc >= FR_CMD) rec_overflow();
            else {
                GLCmd *c = &R->c[R->nc++]; memset(c, 0, sizeof *c); c->kind = 1; c->match = -1; c->bits = bits;
                c->st.fbi = (int)(g_cur_fb - g_fbs); c->st.fbaddr = g_cur_fb->addr;
                c->st.sc[0] = ix0; c->st.sc[1] = iy0; c->st.sc[2] = ix1; c->st.sc[3] = iy1;
                Col cc = b->c; c->cc[0] = cc.r / 255.f; c->cc[1] = cc.g / 255.f; c->cc[2] = cc.b / 255.f; c->cc[3] = cc.a / 255.f;
                c->cd = (double)((int)((int64_t)b->z & 0xFFFF)) / 65535.0; R->fbmask |= 1u << c->st.fbi;
            }
        }
        return;
    }
    int uv = a->has_uv && b->has_uv;
    if (!batch_ready(g, L, uv)) return;
    /* the four corners; colour and depth come from the second vertex, like the software path */
    Vert p0 = *a, p1 = *b; double u0 = a->u, v0 = a->v, u1 = b->u, v1 = b->v;
    Vert q[4]; double qu[4], qv[4];
    q[0] = p1; q[0].x = p0.x; q[0].y = p0.y; qu[0] = u0; qv[0] = v0;
    q[1] = p1; q[1].x = p1.x; q[1].y = p0.y; qu[1] = u1; qv[1] = v0;
    q[2] = p1; q[2].x = p0.x; q[2].y = p1.y; qu[2] = u0; qv[2] = v1;
    q[3] = p1; qu[3] = u1; qv[3] = v1;
    for (int i = 0; i < 4; i++) { q[i].w = 1.0; q[i].src = a->src * 4 + i; }
    Col c = b->c;
    put_vtx(&q[0], qu[0], qv[0], c); put_vtx(&q[1], qu[1], qv[1], c); put_vtx(&q[2], qu[2], qv[2], c);
    put_vtx(&q[1], qu[1], qv[1], c); put_vtx(&q[3], qu[3], qv[3], c); put_vtx(&q[2], qu[2], qv[2], c);
}

/* a line: a thin rectangle one PSP pixel wide */
static void gl_line(GEState *g, const Vert *a, const Vert *b, const Layout *L) {
    int uv = a->has_uv && b->has_uv;
    if (!batch_ready(g, L, uv)) return;
    double dx = b->x - a->x, dy = b->y - a->y, len = sqrt(dx * dx + dy * dy);
    if (len < 1e-9) { dx = 1; dy = 0; len = 1; }
    double nx = -dy / len * 0.5, ny = dx / len * 0.5, ex = dx / len * 0.5, ey = dy / len * 0.5;
    Vert q[4] = {*a, *a, *b, *b};
    q[0].x = a->x + 0.5 - ex + nx; q[0].y = a->y + 0.5 - ey + ny; q[1].x = a->x + 0.5 - ex - nx; q[1].y = a->y + 0.5 - ey - ny;
    q[2].x = b->x + 0.5 + ex + nx; q[2].y = b->y + 0.5 + ey + ny; q[3].x = b->x + 0.5 + ex - nx; q[3].y = b->y + 0.5 + ey - ny;
    for (int i = 0; i < 4; i++) q[i].src = a->src * 4 + i;
    Col ca = g_bt_flat ? b->c : a->c;
    put_vtx(&q[0], a->u, a->v, ca); put_vtx(&q[1], a->u, a->v, ca); put_vtx(&q[2], b->u, b->v, b->c);
    put_vtx(&q[1], a->u, a->v, ca); put_vtx(&q[3], b->u, b->v, b->c); put_vtx(&q[2], b->u, b->v, b->c);
}

/* ------------------------------------------------------------------ output */
GEXPORT void gl_end_list(void) { if (g_gl_on) { gl_flush(); g_batch_gen = 0xFFFFFFFF; } }
/* the picture at a PSP framebuffer address, scaled, as RGB rows top to bottom (w*h*3 bytes); 0 = no such picture */
GEXPORT int gl_read(uint32_t addr, int w, int h, uint8_t *out) {
    if (!g_gl_on) return 0;
    gl_end_list(); addr &= 0x1FFFFF;
    GLFb *f = 0; for (int i = 0; i < g_nfb; i++) if (g_fbs[i].addr == addr) f = &g_fbs[i];
    if (!f) return 0;
    fb_resolve(f);
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, f->fbo);
    static uint8_t tmp[1920 * 1088 * 4];
    int W = w * g_scale, Hh = h * g_scale; if ((size_t)W * Hh * 4 > sizeof tmp) return 0;
    p_glReadPixels(0, (f->h - h) * g_scale, W, Hh, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    for (int y = 0; y < Hh; y++) for (int x = 0; x < W; x++) {
        const uint8_t *s = tmp + ((size_t)(Hh - 1 - y) * W + x) * 4; uint8_t *o = out + ((size_t)y * W + x) * 3;
        o[0] = s[0]; o[1] = s[1]; o[2] = s[2];
    }
    g_cur_fb = 0;
    return g_scale;
}
/* the game copies (block transfer) from a framebuffer - an explosion's heat haze, a screen effect: the picture the
 * graphics card drew there is brought back into PSP video memory (PSP size, its pixel format) for the copy to find.
 * Before, such a framebuffer was drawn a second time in software from then on, for the rest of the session: up to
 * 200 ms more per frame in busy scenes. Once per change of the picture (g_drawgen). 0 = no framebuffer there. */
static uint8_t g_dl_tmp[4096 * 2176 * 4];   /* (a 512x272 framebuffer at resolution 8, the highest. It was 2560x1440: at
                                               * resolution 6 nothing fitted, and every framebuffer an effect read fell back to software drawing) */
/* Vulkan: copies that don't wait for the graphics card (set by vk_setup; OpenGL keeps waiting) */
static int (*p_rb_start)(int slot, GLuint fbo, int x, int y, int w, int h);
static const uint8_t *(*p_rb_done)(int slot);
#define DLS_N 4
static struct { GLuint fbo; int W, H, rows, state; uint32_t gen, used; } g_dls[DLS_N];   /* state 0 free, 1 copying, 2 brought in */
static void dl_store(GEState *g, GLFb *f, int rows, int W, int H, const uint8_t *src) {
    uint32_t bpp = f->fmt == 3 ? 4 : 2; uint8_t *v = g->vram + f->addr; int c = g_scale / 2;
    for (int y = 0; y < rows; y++) {
        const uint8_t *row = src + (size_t)(H - 1 - (y * g_scale + c)) * W * 4;
        uint8_t *o = v + (size_t)y * f->w * bpp;
        for (int x = 0; x < f->w; x++) {
            const uint8_t *s = row + (size_t)(x * g_scale + c) * 4; uint32_t r = s[0], gg = s[1], b = s[2], a = s[3], px;
            if (f->fmt == 3) { o[4 * x] = (uint8_t)r; o[4 * x + 1] = (uint8_t)gg; o[4 * x + 2] = (uint8_t)b; o[4 * x + 3] = (uint8_t)a; continue; }
            if (f->fmt == 0) px = (r >> 3) | ((gg >> 2) << 5) | ((b >> 3) << 11);
            else if (f->fmt == 1) px = (r >> 3) | ((gg >> 3) << 5) | ((b >> 3) << 10) | ((a > 127u) << 15);
            else px = (r >> 4) | ((gg >> 4) << 4) | ((b >> 4) << 8) | ((a >> 4) << 12);
            o[2 * x] = (uint8_t)px; o[2 * x + 1] = (uint8_t)(px >> 8);
        }
    }
}
GEXPORT int gl_fb_download(GEState *g, uint32_t addr, uint32_t n) {
    if (!g_gl_on || g_sw_on || !g) return 0;
    addr &= 0x3FFFFFFF; if (addr < 0x04000000u || addr >= 0x04200000u) return 0;
    uint32_t lo = addr - 0x04000000u, hi = lo + (n ? n : 1); int done = 0;
    for (int i = 0; i < g_nfb; i++) {
        GLFb *f = &g_fbs[i]; uint32_t bpp = f->fmt == 3 ? 4 : 2, flo = f->addr, fhi = f->addr + (uint32_t)f->w * 272 * bpp;
        if (!(lo < fhi && hi > flo)) continue;
        f->rd_frame = g_frame_no;
        if (f->need_sw) continue;
        if (f->drawn + 3 < g_frame_no || vw_find(f->addr) >= 0 || mvw_find(f->addr)) continue;   /* (not drawn by the card lately, or a movie / CPU picture: video memory has it) */
        done++;
        if (f->dl_gen == f->ndraw + 1) continue;                            /* (not drawn into since it was last brought back) */
        if (f->dl_frame != g_frame_no) { f->dl_frame = g_frame_no; f->dl_cnt = 0; }
        if (f->dl_req != f->ndraw + 1) {                                    /* (a new picture asked for) */
            f->dl_req = f->ndraw + 1;
            if (++f->dl_cnt > 3) { f->need_sw = 1; g_swq_gen = 0xFFFFFFFF; continue; }   /* (drawn and read in turns, strip by strip - a small blur picture: drawing it in software as well is cheaper) */
        }
        int rows = f->h < 272 ? f->h : 272, W = f->w * g_scale, H = rows * g_scale;
        if ((size_t)W * H * 4 > sizeof g_dl_tmp || f->addr + (uint32_t)f->w * rows * bpp > 0x200000u) { f->need_sw = 1; g_swq_gen = 0xFFFFFFFF; continue; }
        uint64_t _pt = PF_TSC();
        if (p_rb_start && p_rb_done) {                                      /* Vulkan: the last finished copy, a new one started */
            int k = -1, lru = 0;
            for (int j = 0; j < DLS_N; j++) if (g_dls[j].state && g_dls[j].fbo == f->fbo) { k = j; break; }
            if (k >= 0 && (g_dls[k].W != W || g_dls[k].H != H)) g_dls[k].state = 0;
            if (k >= 0 && g_dls[k].state == 1) {
                const uint8_t *px = p_rb_done(k);
                if (px) { dl_store(g, f, g_dls[k].rows, W, H, px); f->dl_gen = g_dls[k].gen; g_dls[k].state = 2; g_vram_gen++; }
            }
            if (k >= 0 && g_dls[k].state == 2 && g_dls[k].gen != f->ndraw + 1 && f->dl_gen != f->ndraw + 1) {
                GLFb *cur = g_cur_fb; gl_flush(); fb_resolve(f);
                if (p_rb_start(k, f->fbo, 0, (f->h - rows) * g_scale, W, H)) { g_dls[k].state = 1; g_dls[k].gen = f->ndraw + 1; g_dls[k].used = g_frame_no; }
                g_cur_fb = 0; ap_forget(); if (cur) use_fb(cur);
            }
            if (k >= 0) { if (g_dls[k].state) { g_dls[k].used = g_frame_no; pf_add(7, _pt); continue; } }
            else {                                                          /* first time: a slot (the oldest), and this once a waiting copy */
                for (int j = 0; j < DLS_N; j++) { if (!g_dls[j].state) { lru = j; break; } if (g_dls[j].used < g_dls[lru].used) lru = j; }
                k = lru; g_dls[k].fbo = f->fbo; g_dls[k].W = W; g_dls[k].H = H; g_dls[k].rows = rows; g_dls[k].state = 2; g_dls[k].gen = 0; g_dls[k].used = g_frame_no;
            }
        }
        GLFb *cur = g_cur_fb;
        gl_flush(); fb_resolve(f);
        p_glBindFramebuffer(GL_READ_FRAMEBUFFER, f->fbo);
        p_glReadPixels(0, (f->h - rows) * g_scale, W, H, GL_RGBA, GL_UNSIGNED_BYTE, g_dl_tmp);
        g_cur_fb = 0; ap_forget(); if (cur) use_fb(cur);                      /* (back to the framebuffer being drawn) */
        dl_store(g, f, rows, W, H, g_dl_tmp);
        f->dl_gen = f->ndraw + 1; g_vram_gen++; pf_add(7, _pt);
        if (p_rb_start && p_rb_done) for (int j = 0; j < DLS_N; j++) if (g_dls[j].state && g_dls[j].fbo == f->fbo) g_dls[j].gen = f->dl_gen;
    }
    return done;
}
/* show the picture at a framebuffer address in the window (the default framebuffer), letterboxed to win_w x win_h.
 * If nothing was drawn there with OpenGL lately (movies are written straight into video memory by the CPU), the
 * picture is taken from PSP video memory instead. */

/* remaster colour grade (shared with the Vulkan shader): richer colour, deeper shadows, gentle roll-off of highlights,
 * a slight darkening towards the corners */
#define GRADE_FN \
    "vec3 grade(vec3 c, vec2 p) {\n" \
    "  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n" \
    "  c = max(mix(vec3(l), c, 1.08), 0.0);\n" \
    "  c = max((c - 0.3) * 1.1 + 0.32, 0.0);\n" \
    "  vec3 k = max(c - 0.7, 0.0); c = min(c, 0.7) + k / (1.0 + k * 2.2);\n" \
    "  vec2 q = p - 0.5; c *= 1.0 - 0.3 * dot(q, q);\n" \
    "  return min(c, vec3(1.0));\n" \
    "}\n"
static const char *PVS =
    "#version 330 core\n"
    "uniform vec4 u_rect; out vec2 v_uv; out vec2 v_p;\n"         /* u_rect = u0, v0, u1, v1 of the source picture */
    "void main() {\n"
    "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "  v_uv = mix(u_rect.xy, u_rect.zw, p); v_p = p;\n"
    "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
static const char *PFS =                                      /* u_taps x u_taps samples per window pixel: a clean */
    "#version 330 core\n"                                       /* scale-down when the picture is bigger than the window */
    "in vec2 v_uv; in vec2 v_p; out vec4 o; uniform sampler2D u_tex; uniform int u_taps; uniform vec2 u_step; uniform int u_ovl, u_grade;\n"
    GRADE_FN
    "void main() {\n"
    "  if (u_ovl != 0) { o = texture(u_tex, v_uv); return; }\n"
    "  vec3 c = vec3(0.0); float h = 0.5 * float(u_taps - 1);\n"
    "  for (int j = 0; j < u_taps; j++) for (int i = 0; i < u_taps; i++) c += texture(u_tex, v_uv + (vec2(i, j) - h) * u_step).rgb;\n"
    "  c /= float(u_taps * u_taps);\n"
    "  if (u_grade != 0) c = grade(c, v_p);\n"
    "  o = vec4(c, 1.0);\n"
    "}\n";
/* show the picture at a framebuffer address in the window, letterboxed to win_w x win_h. It is drawn as a textured
 * rectangle (works whatever kind of window buffer the driver gave us, e.g. with forced anti-aliasing).
 * If nothing was drawn there with OpenGL lately (movies are written straight into video memory by the CPU), the
 * picture is taken from PSP video memory instead. Returns 1 / 2 (from video memory), or -GL error. */
/* ---- 60 fps: recording, matching, drawing in-between pictures */
static void rec_reset(int i) {
    g_rgen[i]++; memset(g_pc_hist[i], 0, sizeof g_pc_hist[i]); g_pc_wv[i] = 0;
    g_lit[i].npj = 0; g_lit[i].main = -1; g_lit[i].lsum[0] = g_lit[i].lsum[1] = g_lit[i].lsum[2] = 0; g_lit[i].lw = 0;
    GLRec *R = &g_rec[i]; R->v = g_rv[i]; R->c = g_rc[i]; R->src = g_rs[i]; R->pv = g_rp[i]; R->pf = g_rf[i]; R->gv = g_rgv[i]; R->gc = g_rgc[i]; R->ngv = R->gbase = 0;
    R->nv = R->nc = R->over = 0; R->ok = 0; R->fbmask = 0; R->lerped = R->rejected = R->unpaired = R->guessed = 0;
}
/* on = 1: record every frame so in-between pictures can be drawn (gl_commit at each new picture, gl_present_t) */
GEXPORT void gl_interp(int on) {
    if (g_gl_on) { gl_flush(); g_batch_gen = 0xFFFFFFFF; }
    g_rec_on = on ? 1 : 0;
    for (int i = 0; i < 4; i++) rec_reset(i);
    if (g_rec_on) { g_vbuf = g_rec[g_rcur].v; g_sbuf = g_rec[g_rcur].src; g_vcap = FR_VTX; } else { g_vbuf = g_vtx; g_sbuf = 0; g_vcap = MAX_VTX; }
    g_nvtx = g_vbase = 0;
}
#define MH 32768
#define LERP_LIMIT 80.0f                                       /* PSP pixels a vertex may move between two frames */
/* chains of frame-P draws by key: table 0 = key with vertex address, table 1 = key without */
static uint64_t mh_key[2][MH]; static int mh_head[2][MH], mh_tail[2][MH], mh_next[2][FR_CMD]; static uint32_t mh_gen[2][MH], g_mhgen;
static uint8_t mh_used[FR_CMD];
static inline uint32_t mh_slot(int t, uint64_t k) {
    uint32_t h = (uint32_t)(k ^ (k >> 32)) & (MH - 1);
    while (mh_gen[t][h] == g_mhgen && mh_key[t][h] != k) h = (h + 1) & (MH - 1);
    return h;
}
static void mh_add(int t, uint64_t k, int i) {
    uint32_t h = mh_slot(t, k); mh_next[t][i] = -1;
    if (mh_gen[t][h] != g_mhgen) { mh_gen[t][h] = g_mhgen; mh_key[t][h] = k; mh_head[t][h] = i; }
    else mh_next[t][mh_tail[t][h]] = i;
    mh_tail[t][h] = i;
}
/* the not yet paired draw under key k whose middle is nearest to (cx, cy): the same thing drawn many times (floor
 * tiles, crates: one model, many places) must pair with itself, not with its neighbour */
static int g_take_amb; static uint8_t g_amb[FR_CMD];
static int mh_take(int t, uint64_t k, const GLRec *P, float cx, float cy) {
    uint32_t h = mh_slot(t, k);
    if (mh_gen[t][h] != g_mhgen) return -1;
    int j = mh_head[t][h];
    while (j >= 0 && mh_used[j]) j = mh_next[t][j];
    mh_head[t][h] = j;                                         /* (everything before it is used up) */
    int best = -1; float bd = 0; int seen = 0;
    for (; j >= 0 && seen < 1024; j = mh_next[t][j]) {
        if (mh_used[j]) continue;
        float dx = P->c[j].cx - cx, dy = P->c[j].cy - cy, d = dx * dx + dy * dy; seen++;
        if (best < 0 || d < bd) { best = j; bd = d; }
    }
    g_take_amb = seen > 1;                                     /* (more than one look-alike to choose from) */
    if (best >= 0) mh_used[best] = 1;
    return best;
}
static int g_rej[256][6], g_rej_n;
GEXPORT int gl_interp_rejects(int *out) { memcpy(out, g_rej, sizeof(int) * 6 * (size_t)g_rej_n); return g_rej_n; }
#define SRC_TAB 262144
static int st_idx[SRC_TAB]; static uint32_t st_gen[SRC_TAB], g_stgen;
/* pair each draw of frame R with the same draw in frame P (same vertex data address if possible, else the n-th draw
 * with the same texture and state), then each vertex with the same vertex of that draw; and check that nothing
 * jumped (a jump means it was not really the same thing) */
static int g_miss[256][6], g_miss_n;
GEXPORT int gl_interp_miss(int *out) { memcpy(out, g_miss, sizeof(int) * 6 * (size_t)g_miss_n); return g_miss_n; }
/* ---- the camera's movement between the two frames, as one 4x4 map in clip space (exact for everything that
 * stands still in the world). Fitted to the points that have partners; used for the things that have none: parts of
 * the world just coming into view at the screen edges, triangles that were behind the camera. */
#define NSAMP 32768
static int g_samp[NSAMP][2], g_scmd[NSAMP]; static int g_nsamp; static float g_serr[NSAMP];
static int solve4(double A[4][4], double B[4][4], double X[4][4]) {          /* A X = B */
    double a[4][8];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { a[i][j] = A[i][j]; a[i][4 + j] = B[i][j]; }
    for (int c = 0; c < 4; c++) {
        int p = c; for (int r = c + 1; r < 4; r++) if (fabs(a[r][c]) > fabs(a[p][c])) p = r;
        if (fabs(a[p][c]) < 1e-12) return 0;
        if (p != c) for (int j = 0; j < 8; j++) { double t = a[c][j]; a[c][j] = a[p][j]; a[p][j] = t; }
        for (int r = 0; r < 4; r++) if (r != c) { double f = a[r][c] / a[c][c]; for (int j = c; j < 8; j++) a[r][j] -= f * a[c][j]; }
    }
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) X[i][j] = a[i][4 + j] / a[i][i];
    return 1;
}
/* prev = M * cur, fitted with points scaled to w = 1 (screen-like weights); keep[] chooses the points used */
/* prev (X, Y, W) = M * (X, Y, W, 1) of now: W is the distance in front of the camera, so this holds for any
 * camera move and doesn't depend on each draw's depth range (the game draws far scenery with another one).
 * Fitted with every point scaled to w = 1 (screen-like weights). */
static int fit_cam(GLRec *R, const uint8_t *keep, double M[4][4]) {
    double A[4][4] = {{0}}, B[4][4] = {{0}}, X[4][4]; int n = 0;
    for (int i = 0; i < g_nsamp; i++) {
        if (keep && !keep[i]) continue;
        const GLVtx *v = R->v + g_samp[i][0]; const GLPrev *q = R->pv + g_samp[i][1];
        double iw = 1.0 / v->w, c[4] = {v->x, v->y, 1.0, iw}, pr[4] = {q->X * iw, q->Y * iw, q->W * iw, 0};
        for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) { A[r][k] += c[r] * c[k]; B[r][k] += c[r] * pr[k]; }
        n++;
    }
    if (n < 64 || !solve4(A, B, X)) return 0;
    for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) M[k][r] = X[r][k];            /* (X = M transposed) */
    return n;
}
static int cam_apply(double M[4][4], const GLVtx *v, double *e) {        /* e = prev X, Y, W */
    double c[4] = {v->x * v->w, v->y * v->w, v->w, 1.0};
    for (int r = 0; r < 3; r++) e[r] = M[r][0] * c[0] + M[r][1] * c[1] + M[r][2] * c[2] + M[r][3] * c[3];
    return e[2] == e[2];
}
static float cam_err(GLRec *R, int i, double M[4][4]) {                  /* PSP pixels between the guess and the truth */
    const GLVtx *v = R->v + g_samp[i][0]; const GLPrev *q = R->pv + g_samp[i][1]; double e[3];
    if (!cam_apply(M, v, e) || e[2] <= 1e-6) return 1e9f;
    double dx = e[0] / e[2] - q->X / q->W, dy = e[1] / e[2] - q->Y / q->W;
    return (float)sqrt(dx * dx + dy * dy);
}
static uint8_t g_how1[FR_CMD];      /* paired with a draw from another memory place (same look) */
static double g_cam[4][4]; static int g_cam_ok, g_camdbg[4], g_errh[8];
GEXPORT void gl_cam_hist(int *o) { memcpy(o, g_errh, sizeof g_errh); }
GEXPORT int gl_cam_dbg(int *o) { memcpy(o, g_camdbg, sizeof g_camdbg); return g_cam_ok; }
/* the depth of a draw's points as a straight line of their distance: Z = p W + q (one depth range per draw) */
static void draw_depth(const GLVtx *b, int n, double *p, double *q) {
    double sw = 0, sz = 0, sww = 0, swz = 0;
    for (int k = 0; k < n; k++) { double W = b[k].w, Z = (double)b[k].z * b[k].w; sw += W; sz += Z; sww += W * W; swz += W * Z; }
    double d = n * sww - sw * sw;
    if (n < 2 || fabs(d) < 1e-9 * (sww * n + 1)) { *p = n ? sz / (sw ? sw : 1) : 0; *q = 0; return; }
    *p = (n * swz - sw * sz) / d; *q = (sz - *p * sw) / n;
}
/* where point v of the last frame was the frame before, if it stood still in the world: 0 = can't tell */
static int cam_prev(const GLVtx *v, double dp, double dq, GLPrev *o) {
    double e[3];
    if (!cam_apply(g_cam, v, e)) return 0;
    o->X = (float)e[0]; o->Y = (float)e[1]; o->W = (float)e[2]; o->Z = (float)(dp * e[2] + dq); o->fog = v->fog;
    return 1;
}

static int small_tris(const GLVtx *b, int n) {             /* every triangle of the draw under 16 PSP pixels across */
    if (n < 3 || n > 768) return 0;
    for (int k = 0; k + 2 < n; k += 3) {
        float x0 = b[k].x, x1 = x0, y0 = b[k].y, y1 = y0;
        for (int q = 1; q < 3; q++) { float x = b[k + q].x, y = b[k + q].y; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
        if (x1 < -16.f || x0 > 496.f || y1 < -16.f || y0 > 288.f) continue;   /* (off the screen: the game parks unused ones there) */
        if (!(x1 - x0 < 16.f && y1 - y0 < 16.f)) return 0;
    }
    return 1;
}
/* drawn with points the game already put on the screen itself (every w is 1): not a place in the world, so the
 * camera's move can't say where it was before (it threw the stunned soldier's stars hundreds of pixels across) */
static int screen_space(const GLVtx *b, int n) {
    for (int k = 0; k < n; k++) if (b[k].w < 0.9999f || b[k].w > 1.0001f) return 0;
    return n > 0;
}
static int g_exact;
/* which draws' left-out triangles an in-between picture may show: only solid ones (not see-through dust and haze
 * cards), and only where the frame before drew more of that draw - it was seen then and is leaving now. (A card the
 * game left out in both frames showed up in the in-betweens alone: blowing dust flickered) */
static void ghost_ok(GLRec *R, const GLRec *P) {
    static uint64_t hk[1 << 14]; static int hn[1 << 14]; static uint32_t hg[1 << 14], gen; gen++;
    for (int j = 0; j < P->nc; j++) { const GLCmd *c = &P->c[j]; if (c->kind || c->n <= 0) continue; uint64_t k = c->key2; uint32_t h = (uint32_t)(k ^ (k >> 29)) & ((1 << 14) - 1);
        while (hg[h] == gen && hk[h] != k) h = (h + 1) & ((1 << 14) - 1);
        if (hg[h] != gen) { hg[h] = gen; hk[h] = k; hn[h] = 0; } hn[h] += c->n; }
    static uint64_t rk[1 << 14]; static int rn[1 << 14]; static uint32_t rg[1 << 14];
    for (int i = 0; i < R->nc; i++) { const GLCmd *c = &R->c[i]; if (c->kind) continue; uint64_t k = c->key2; uint32_t h = (uint32_t)(k ^ (k >> 29)) & ((1 << 14) - 1);
        while (rg[h] == gen && rk[h] != k) h = (h + 1) & ((1 << 14) - 1);
        if (rg[h] != gen) { rg[h] = gen; rk[h] = k; rn[h] = 0; } rn[h] += c->n; }
    for (int i = 0; i < R->nc; i++) {
        GLCmd *c = &R->c[i]; c->gok = 0;
        if (c->kind || c->gn <= 0 || c->st.bon || !P->ok) continue;
        uint64_t k = c->key2; uint32_t h = (uint32_t)(k ^ (k >> 29)) & ((1 << 14) - 1), h2 = h;
        while (hg[h] == gen && hk[h] != k) h = (h + 1) & ((1 << 14) - 1);
        while (rg[h2] == gen && rk[h2] != k) h2 = (h2 + 1) & ((1 << 14) - 1);
        if (hg[h] == gen && rg[h2] == gen && hn[h] > rn[h2]) c->gok = 1;
    }
}
static void lost_find__b(GLRec *R, GLRec *P);
static void lost_find(GLRec *R, GLRec *P) { uint64_t _t = PF_TSC(); lost_find__b(R, P); pf_add(9, _t); }
static void lost_find__b(GLRec *R, GLRec *P) {
    int ri = (int)(R - g_rec), pi = (int)(P - g_rec); g_rnlost[ri] = 0;
    if (!g_lost_on) return;
    /* where the frame before's points are now: best from the game's own two cameras (exact, and still there when the
     * camera swung so fast the screen fit below gave up - then nothing filled the edge: the pale blocks that came
     * only now and then on a quick spin); else from the screen fit */
    const GLLit *LR = &g_lit[ri], *LP = &g_lit[pi]; const GLPj *pa = 0, *pb = 0;
    if (LR->main >= 0 && LP->main >= 0 && LR->pj[LR->main].ok && LP->pj[LP->main].ok) {
        pa = &LP->pj[LP->main]; pb = &LR->pj[LR->main];
        if (pa->sx != pb->sx || pa->sy != pb->sy || pa->ox != pb->ox || pa->oy != pb->oy || !(pj_pdiff(pa, pb) < 0.25)) pa = pb = 0;
        else {   /* (not a new shot: turned less than ~50 degrees) */
            double za = sqrt(pa->V[2] * pa->V[2] + pa->V[5] * pa->V[5] + pa->V[8] * pa->V[8]), zb = sqrt(pb->V[2] * pb->V[2] + pb->V[5] * pb->V[5] + pb->V[8] * pb->V[8]);
            if (!((pa->V[2] * pb->V[2] + pa->V[5] * pb->V[5] + pa->V[8] * pb->V[8]) > 0.64 * za * zb)) pa = pb = 0;
        }
    }
    if (!pa && !g_cam_ok) return;
    /* the camera's move the other way: the frame before -> this frame (g_cam maps this frame's points to the one before) */
    double A[3][3], T[3], I[3][3];
    for (int r = 0; r < 3; r++) { for (int k = 0; k < 3; k++) A[r][k] = g_cam[r][k]; T[r] = g_cam[r][3]; }
    double det = pa ? 1.0 : A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (!(fabs(det) > 1e-12)) return;
    I[0][0] = (A[1][1] * A[2][2] - A[1][2] * A[2][1]) / det; I[0][1] = (A[0][2] * A[2][1] - A[0][1] * A[2][2]) / det; I[0][2] = (A[0][1] * A[1][2] - A[0][2] * A[1][1]) / det;
    I[1][0] = (A[1][2] * A[2][0] - A[1][0] * A[2][2]) / det; I[1][1] = (A[0][0] * A[2][2] - A[0][2] * A[2][0]) / det; I[1][2] = (A[0][2] * A[1][0] - A[0][0] * A[1][2]) / det;
    I[2][0] = (A[1][0] * A[2][1] - A[1][1] * A[2][0]) / det; I[2][1] = (A[0][1] * A[2][0] - A[0][0] * A[2][1]) / det; I[2][2] = (A[0][0] * A[1][1] - A[0][1] * A[1][0]) / det;
    /* (in the new frame = the very same draw is there - same look, same vertex data - at the place this one went to.
     *  The look alone doesn't do: the game has many look-alike wall pieces. Nor the vertex data: it draws one wall
     *  piece's model at several places, and the piece that left the screen counted as "still there" because its
     *  twin was - its gap showed as a pale block) */
    static int hn[FR_CMD]; static uint64_t hk[1 << 14]; static int hh[1 << 14]; static uint32_t hg[1 << 14], gen; gen++;
    for (int i = R->nc - 1; i >= 0; i--) { if (R->c[i].kind || (R->c[i].n == 0 && !g_exact)) continue; uint64_t k = R->c[i].key2; uint32_t h = (uint32_t)(k ^ (k >> 29)) & ((1 << 14) - 1);
        while (hg[h] == gen && hk[h] != k) h = (h + 1) & ((1 << 14) - 1);
        if (hg[h] == gen) hn[i] = hh[h]; else { hn[i] = -1; hg[h] = gen; hk[h] = k; } hh[h] = i; }
    /* (only what was drawn with the main camera set: far scenery has its own depth range, and drawn after the world
     *  it covered it - pale triangles in the sky) */
    int pmain = g_lit[pi].main;
    /* (and not far scenery - the sky and buildings beyond the walls: filling a gap with it showed pale wedges. The
     *  world's points the camera was fitted to say how far the world itself goes) */
    static float ws[NSAMP]; int nw = 0;
    for (int i = 0; i < g_nsamp; i++) if (g_serr[i] < 1.f) ws[nw++] = R->v[g_samp[i][0]].w;
    float wref = 0;
    if (nw >= 64) { int cnt[64] = {0}; for (int i = 0; i < nw; i++) { int b = ws[i] > 1.f ? (int)(log2((double)ws[i]) * 4) : 0; if (b > 63) b = 63; if (b < 0) b = 0; cnt[b]++; }
      int acc = 0, b = 0; for (; b < 64; b++) { acc += cnt[b]; if (acc * 10 >= nw * 9) break; } wref = (float)(1.5 * pow(2.0, (b + 1) * 0.25)); }
    /* (and the world's own solid pieces on the screen before - the fitted points can all be near the camera, in a
     *  courtyard the far wall is several times farther: left out as "scenery", its gap showed as a pale block) */
    if (pa) {
        nw = 0; int stp = P->nv / NSAMP + 1;
        for (int j = 0; j < P->nc && nw < NSAMP; j++) { const GLCmd *c = &P->c[j];
            if (c->kind || c->st.pj != pmain || c->st.through || c->st.skin || c->st.bon || !c->st.zon || !c->st.zmask) continue;
            for (int k = 0; k < c->n && nw < NSAMP; k += stp) { const GLVtx *v = P->v + c->v0 + k;
                if (v->x > 0.f && v->x < 480.f && v->y > 0.f && v->y < 272.f && v->w > 1.f) ws[nw++] = v->w; } }
        if (nw >= 64) { int cnt[64] = {0}; for (int i = 0; i < nw; i++) { int b = (int)(log2((double)ws[i]) * 4); if (b > 63) b = 63; if (b < 0) b = 0; cnt[b]++; }
            int acc = 0, b = 0; for (; b < 64; b++) { acc += cnt[b]; if (acc * 100 >= nw * 99) break; }
            float w2 = (float)(1.5 * pow(2.0, (b + 1) * 0.25)); if (w2 > wref) wref = w2; }
    }
    if (!(wref > 0)) return;
    for (int j = 0; j < P->nc && g_rnlost[ri] < FR_CMD; j++) {
        const GLCmd *c = &P->c[j];
        if (pmain < 0 || c->st.pj != pmain) continue;
        if (c->kind || c->st.through || c->st.skin || c->st.bon || !c->st.zon || !c->st.zmask || c->ssw || c->n < 3) continue;
        const GLVtx *b = P->v + c->v0; GLPrev *fw = g_rfw[pi] + c->v0;
        if (screen_space(b, c->n)) continue;
        int on_before = 0, on_now = 0, bad = 0;
        /* (speed: the quick checks first - most of the frame before is still on the screen, and one of its points that
         *  was on the screen and still is says so; the full work only for what may really have left it) */
        for (int k = 0; k < c->n; k++) {
            if (!(b[k].w > 1e-3f) || b[k].w > wref) { bad = 1; break; }
            if (b[k].x > -8.f && b[k].x < 488.f && b[k].y > -8.f && b[k].y < 280.f) on_before = 1;
        }
        if (bad || !on_before) continue;
#define LOST_Q(k)  double q[4]; \
            if (pa) { double wp[3]; lit_world(pa, pa->V, b[k].x, b[k].y, b[k].w, wp); pj_projd(pb, wp, q); q[2] = q[3]; } \
            else { double e[3] = {b[k].x * (double)b[k].w - T[0], b[k].y * (double)b[k].w - T[1], b[k].w - T[2]}; \
                   for (int r = 0; r < 3; r++) q[r] = I[r][0] * e[0] + I[r][1] * e[1] + I[r][2] * e[2]; }
        for (int k = 0; k < c->n && !on_now; k++) {
            if (!(b[k].x > -8.f && b[k].x < 488.f && b[k].y > -8.f && b[k].y < 280.f)) continue;
            LOST_Q(k)
            if (q[2] > 1e-3) { float x = (float)(q[0] / q[2]), y = (float)(q[1] / q[2]); if (x > -4.f && x < 484.f && y > -4.f && y < 276.f) on_now = 1; }
        }
        if (on_now) continue;
        double dp, dq; draw_depth(b, c->n, &dp, &dq);
        for (int k = 0; k < c->n; k++) {
            LOST_Q(k)
            if (!(q[2] > 1e-3)) { bad = 1; break; }
            fw[k].X = (float)q[0]; fw[k].Y = (float)q[1]; fw[k].W = (float)q[2]; fw[k].Z = (float)(dp * q[2] + dq); fw[k].fog = b[k].fog;
            float x = (float)(q[0] / q[2]), y = (float)(q[1] / q[2]);
            if (x > -4.f && x < 484.f && y > -4.f && y < 276.f) { on_now = 1; break; }   /* (still on the screen: not lost - no need to look further) */
            /* (much nearer or farther now: not something standing still) */
            if (q[2] > 3.0 * b[k].w + 50.0 || q[2] * 3.0 + 50.0 < b[k].w) { bad = 1; break; }
        }
#undef LOST_Q
        /* only what was on the screen before and has left it now: what is still on it is in the new frame already */
        if (bad || !on_before || on_now) continue;
        {   double sx = 0, sy = 0, sw = 0; for (int k = 0; k < c->n; k++) { sx += fw[k].X; sy += fw[k].Y; sw += fw[k].W; }
            double px = sx / sw, py = sy / sw; int there = 0; uint64_t k2 = c->key2; uint32_t h = (uint32_t)(k2 ^ (k2 >> 29)) & ((1 << 14) - 1);
            while (hg[h] == gen && hk[h] != k2) h = (h + 1) & ((1 << 14) - 1);
            if (hg[h] == gen) for (int i = hh[h]; i >= 0 && !there; i = hn[i]) {
                const GLCmd *rc = &R->c[i]; double dx = rc->cx - px, dy = rc->cy - py, tol = 24.0 + 0.1 * (fabs(px - 240.0) + fabs(py - 136.0));
                if (rc->n == c->n && dx * dx + dy * dy < tol * tol) there = 1;
                else if (rc->n != c->n) {   /* (with triangles left out behind the camera its middle moves: by its points) */
                    int near = 0;
                    for (int part = 0; part < 2 && !near; part++) {
                        const GLVtx *rb = part ? R->gv + rc->g0 : R->v + rc->v0; int rn = part ? rc->gn : rc->n;
                        for (int q = 0; q < rn && !near; q++) for (int k = 0; k < c->n; k += (c->n > 8 ? c->n / 8 : 1)) {
                            double ex = rb[q].x - fw[k].X / fw[k].W, ey = rb[q].y - fw[k].Y / fw[k].W; if (ex * ex + ey * ey < 4.0) { near = 1; break; } }
                    }
                    if (near) there = 1;
                }
            }
            if (there) continue; }
        g_rlost[ri][g_rnlost[ri]++] = j;
    }
}
static int g_lost_tris;
GEXPORT int gl_lost_count(void) { return g_rnlost[g_rlast]; }
GEXPORT int gl_lost_tris(void) { int n = g_lost_tris; g_lost_tris = 0; return n; }   /* (test aid) */   /* (log / test aid) */
static uint8_t g_ssp[FR_CMD];                              /* paired by order on the screen (screen_space) */
static uint8_t g_uvf[FR_CMD];
static int g_uvdraws;   /* (test aid) draws whose texture scrolled since the frame before */
GEXPORT int gl_dbg_uvlist(int *o, int max) { const GLRec *R = &g_rec[g_rlast]; int m = 0, di = 0;   /* (test aid) draw numbers (draws only) that scrolled */
    for (int i = 0; i < R->nc; i++) { if (R->c[i].kind) continue; if (g_uvf[i] && m < max) o[m++] = di; di++; } return m; }
GEXPORT int gl_dbg_uvdraws(void) { int n = g_uvdraws; g_uvdraws = 0; return n; }
/* a point's texture place t of the way from the frame before (scrolling textures; a jump of half the picture or more
 * is a wrap-around or a new picture: no blend) */
static inline float uv_lerp(float a, float b, float t) { float d = b - a; return (d < 0.5f && d > -0.5f) ? a + d * t : b; }
static uint8_t g_scr[FR_CMD]; static uint32_t g_scr_n;   /* (draws that go round with the camera: drawn where they are on the screen) */
GEXPORT uint32_t gl_scr_count(void) { return g_scr_n; }
static void rec_match__b(GLRec *R, GLRec *P);
static void rec_match(GLRec *R, GLRec *P) { uint64_t _t = PF_TSC(); rec_match__b(R, P); pf_add(8, _t); }
static void rec_match__b(GLRec *R, GLRec *P) {
    memset(g_uvf, 0, (size_t)R->nc);
    R->lerped = R->rejected = R->unpaired = R->guessed = R->cut = R->rescued = 0; g_rej_n = 0; g_miss_n = 0; g_nsamp = 0; g_cam_ok = 0;
    if (!P->ok || P->over || R->over) return;
    static uint8_t need[FR_CMD], rejf[FR_CMD]; memset(g_how1, 0, (size_t)R->nc); memset(rejf, 0, (size_t)R->nc); memset(g_ssp, 0, (size_t)R->nc);                 /* 1 = draw without partner (or a bad one): use the camera's movement */
    g_mhgen++;
    for (int i = 0; i < P->nc; i++) {
        GLCmd *c = &P->c[i]; mh_used[i] = 0; if (c->kind) continue;
        mh_add(0, c->key2, i); mh_add(1, c->key, i);
    }
    int step = R->nv / 6000 + 1;                  /* (sample points for the camera fit: ~6000 are plenty for its 12 numbers;
                                                   *  busy rooms gave 15000+ and the fit took several ms) */
    for (int i = 0; i < R->nc; i++) {
        GLCmd *c = &R->c[i]; c->match = -1; need[i] = 0; if (c->kind) continue;
        int j = mh_take(0, c->key2, P, c->cx, c->cy), how = 0; g_amb[i] = (uint8_t)g_take_amb;
        if (j < 0) { j = mh_take(1, c->key, P, c->cx, c->cy); how = 1; g_amb[i] = 1; }
        if (j < 0) { R->unpaired++; need[i] = 1; continue; }
        GLCmd *pc = &P->c[j];
        /* points the game put on the screen itself (the stunned soldier's stars): it rebuilds them every frame in new
         * memory, so they are paired by their order in the draw - the n-th star with the n-th star - and moved on the
         * screen from where they were */
        if (pc->n == c->n && screen_space(R->v + c->v0, c->n) && (pc->ssw || screen_space(P->v + pc->v0, pc->n))) {   /* (ssw: it was given a depth below) */
            const GLVtx *b = R->v + c->v0, *a0 = P->v + pc->v0; GLPrev *pv = R->pv + c->v0; uint8_t *pf = R->pf + c->v0; int ok = 1, moved = 0;
            /* each sprite with the sprite of the same picture part (a letter of the floating item text: by order alone, a
             * word that changed - "KIT(S) x 3" coming in - moved letters into each other's places, scrambled) */
            static int amap[FR_VTX > 65536 ? 65536 : FR_VTX]; int gs = c->n % 6 == 0 ? 6 : 3, nm = c->n <= 3000;
            /* (one letter per draw: the partner is the draw with the same letter - same picture part - nearest to it) */
            if (c->n == gs) {
                int same = 1; for (int q = 0; q < gs; q++) if (a0[q].u != b[q].u || a0[q].v != b[q].v) { same = 0; break; }
                if (!same) {
                    int bj = -1; float bd = 1e30f;
                    for (int j2 = 0; j2 < P->nc; j2++) {
                        const GLCmd *p2 = &P->c[j2]; if (p2->kind || p2->n != c->n || p2->key != c->key) continue;
                        const GLVtx *e = P->v + p2->v0; int eq = 1;
                        for (int q = 0; q < gs; q++) if (e[q].u != b[q].u || e[q].v != b[q].v || e[q].w != 1.f) { eq = 0; break; }
                        if (!eq) continue;
                        float dx = e[0].x - b[0].x, dy = e[0].y - b[0].y, d = dx * dx + dy * dy; if (d < bd) { bd = d; bj = j2; }
                    }
                    if (bj >= 0) { mh_used[j] = 0; j = bj; mh_used[j] = 1; pc = &P->c[j]; a0 = P->v + pc->v0; }
                }
            }
            for (int k = 0; k < c->n && nm; k++) amap[k] = k;
            for (int g0 = 0; g0 < c->n && nm; g0 += gs) {
                int same = 1; for (int q = 0; q < gs; q++) if (a0[g0 + q].u != b[g0 + q].u || a0[g0 + q].v != b[g0 + q].v) { same = 0; break; }
                if (same) continue;
                int best = -1; float bd = 1e30f;
                for (int h0 = 0; h0 < c->n; h0 += gs) {
                    int eq = 1; for (int q = 0; q < gs; q++) if (a0[h0 + q].u != b[g0 + q].u || a0[h0 + q].v != b[g0 + q].v) { eq = 0; break; }
                    if (!eq) continue;
                    float dx = a0[h0].x - b[g0].x, dy = a0[h0].y - b[g0].y, d = dx * dx + dy * dy; if (d < bd) { bd = d; best = h0; }
                }
                if (best < 0) { ok = 0; break; }
                for (int q = 0; q < gs; q++) amap[g0 + q] = best + q;
            }
            for (int k = 0; k < c->n && ok; k++) {
                const GLVtx *a = a0 + (nm ? amap[k] - k : 0);
                float dx = b[k].x - a[k].x, dy = b[k].y - a[k].y;
                if (dx > LERP_LIMIT || dx < -LERP_LIMIT || dy > LERP_LIMIT || dy < -LERP_LIMIT) {
                    /* (a spare one parked off the screen, or put there: no move for it) */
                    int off = b[k].x < -16.f || b[k].x > 496.f || b[k].y < -16.f || b[k].y > 288.f || a[k].x < -16.f || a[k].x > 496.f || a[k].y < -16.f || a[k].y > 288.f;
                    if (!off) { ok = 0; break; }
                    pv[k].X = b[k].x; pv[k].Y = b[k].y; pv[k].Z = b[k].z; pv[k].W = 1.f; pv[k].fog = b[k].fog; pv[k].u = b[k].u; pv[k].v = b[k].v; pf[k] = 0; continue;
                }
                if (dx != 0.f || dy != 0.f) moved = 1;
                pv[k].X = a[k].x; pv[k].Y = a[k].y; pv[k].Z = a[k].z; pv[k].W = 1.f; pv[k].fog = a[k].fog; pv[k].u = a[k].u; pv[k].v = a[k].v; pf[k] = 0;
            }
            if (ok) { g_ssp[i] = 1; if (moved) { c->match = j; R->lerped++; } continue; }
            mh_used[j] = 0; R->unpaired++; need[i] = 1; continue;
        }
        g_stgen++;
        for (int k = 0; k < pc->n; k++) { int sid = P->src[pc->v0 + k]; if ((unsigned)sid < SRC_TAB) { st_gen[sid] = g_stgen; st_idx[sid] = pc->v0 + k; } }
        int ok = 1, moved = 0, uvm = 0, found = 0, miss = 0; const GLVtx *b = R->v + c->v0; GLPrev *pv = R->pv + c->v0; uint8_t *pf = R->pf + c->v0;
        int cstep = c->n / 32 + 1; if (cstep < step) cstep = step;   /* (a few points per draw: every draw has a say in the
                                                                        * camera fit, not mostly the detailed characters) */
        for (int k = 0; k < c->n; k++) {
            int sid = R->src[c->v0 + k];
            if ((unsigned)sid >= SRC_TAB || st_gen[sid] != g_stgen) { pf[k] = 3; miss++; continue; }
            const GLVtx *a = P->v + st_idx[sid];
            float dx = b[k].x - a->x, dy = b[k].y - a->y;
            /* only points on or near the screen are checked: far outside it (seen at a grazing angle) a point moves
             * a long way on the screen for a small turn of the camera */
            float ox = b[k].x - 240.f, oy = b[k].y - 136.f;
            if (ox > -480.f && ox < 480.f && oy > -300.f && oy < 300.f && (dx > LERP_LIMIT || dx < -LERP_LIMIT || dy > LERP_LIMIT || dy < -LERP_LIMIT)) { ok = 0; break; }
            /* the direction from the camera may turn by ~20 degrees at most - on the screen and around it. (Points far
             * outside the picture belong to surfaces right next to the camera - a wall it is pressed against: their
             * direction swings a lot for a tiny move, and rejecting the whole wall made it hop at 20 fps) */
            if (ox > -1440.f && ox < 1440.f && oy > -816.f && oy < 816.f && a->x > -1200.f && a->x < 1680.f && a->y > -680.f && a->y < 952.f) {
                float ax = (a->x - 240.f) / 240.f, ay = (a->y - 136.f) / 240.f, bx = ox / 240.f, by = oy / 240.f;
                float dot = ax * bx + ay * by + 1.f, la = ax * ax + ay * ay + 1.f, lb = bx * bx + by * by + 1.f;
                if (dot <= 0.f || dot * dot < 0.88f * la * lb) { ok = 0; break; }
            }
            /* its distance from the camera may not jump either: a point of something right beside the camera (far off
             * the screen, so the checks above don't look at it) paired with a point far away across the room swept a
             * long streak across the floor in the in-between pictures */
            { float dw = b[k].w - a->w, mw = b[k].w > a->w ? b[k].w : a->w; if (dw < 0) dw = -dw;
              if (a->w > 0.f && b[k].w > 0.f && dw > 1000.f && dw > 0.5f * mw) { ok = 0; break; } }
            if (dx != 0.f || dy != 0.f || a->w != b[k].w || a->z != b[k].z) moved = 1;
            if (a->u != b[k].u || a->v != b[k].v) { moved = 1; uvm = 1; }      /* (a scrolling texture: blowing dust, water) */
            found++; pf[k] = 0;
            pv[k].X = a->x * a->w; pv[k].Y = a->y * a->w; pv[k].Z = a->z * a->w; pv[k].W = a->w; pv[k].fog = a->fog; pv[k].u = a->u; pv[k].v = a->v;
            if (!c->st.through && !c->st.skin && (k % cstep) == 0 && g_nsamp < NSAMP && b[k].z > -2.f && b[k].z < 65537.f && a->z > -2.f && a->z < 65537.f && ox > -720.f && ox < 720.f && oy > -400.f && oy < 400.f) { g_samp[g_nsamp][0] = c->v0 + k; g_samp[g_nsamp][1] = c->v0 + k; g_scmd[g_nsamp] = i; g_nsamp++; }
        }
        if (ok && found * 10 < c->n * 9) {          /* fewer than 9 in 10 points have a partner: not the same thing */
            mh_used[j] = 0; R->unpaired++; need[i] = 1; continue;
        }
        if (!ok) {
            R->rejected++; need[i] = 1; rejf[i] = 1;
            if (g_rej_n < 256) { int *e = g_rej[g_rej_n++]; e[0] = c->n; e[1] = c->st.through; e[2] = (int)c->st.tex; e[3] = i; e[4] = j; e[5] = pc->n; }
            continue;
        }
        if (!found) { need[i] = 1; continue; }
        if (miss && g_miss_n < 256) { int *e = g_miss[g_miss_n++]; e[0] = c->n; e[1] = miss; e[2] = how; e[3] = pc->n; e[4] = (int)c->st.va; e[5] = (int)pc->st.va; }
        if (miss) moved = 1;
        if (moved) { c->match = j; R->lerped++; if (how) g_how1[i] = 1; g_uvdraws += uvm; g_uvf[i] = (uint8_t)uvm; }
    }
    /* the camera's movement: fit, drop the points that moved by themselves (people, doors), fit again */
    double M[4][4]; g_camdbg[0] = g_nsamp; g_camdbg[1] = g_camdbg[2] = g_camdbg[3] = -1;
    static uint8_t uq[NSAMP]; int nuq = 0;
    for (int i = 0; i < g_nsamp; i++) { uq[i] = !g_amb[g_scmd[i]]; nuq += uq[i]; }
    /* (draws that had only one possible partner are surely paired right: fit the camera to them first when there
     * are enough; look-alikes (floor tiles...) may have taken a neighbour) */
    if ((g_camdbg[1] = fit_cam(R, nuq >= 64 ? uq : 0, M))) {
        static uint8_t keep[NSAMP]; static float err[NSAMP]; int nk = 0;
        for (int i = 0; i < g_nsamp; i++) err[i] = cam_err(R, i, M);
        { int h[8] = {0}; for (int i = 0; i < g_nsamp; i++) { float e = err[i]; h[e < 0.25f ? 0 : e < 0.5f ? 1 : e < 1 ? 2 : e < 2 ? 3 : e < 4 ? 4 : e < 8 ? 5 : e < 32 ? 6 : 7]++; } memcpy(g_errh, h, sizeof h); }
        /* the first fit is pulled off by the things that move by themselves: tighten step by step (16 px ... 1 px) */
        static const float LIM[7] = {16.f, 8.f, 4.f, 2.f, 1.5f, 1.f, 1.f};
        for (int i = 0; i < g_nsamp; i++) { keep[i] = err[i] < LIM[0]; nk += keep[i]; }
        g_camdbg[2] = nk;
        for (int round = 1; round < 7 && nk >= 64 && fit_cam(R, keep, M); round++) {
            nk = 0; for (int i = 0; i < g_nsamp; i++) { keep[i] = cam_err(R, i, M) < LIM[round]; nk += keep[i]; }
        }
        if (nk >= 64) fit_cam(R, keep, M);
        nk = 0; for (int i = 0; i < g_nsamp; i++) { g_serr[i] = cam_err(R, i, M); nk += g_serr[i] < 1.f; }
        { int h[8] = {0}; for (int i = 0; i < g_nsamp; i++) { float e = g_serr[i]; h[e < 0.25f ? 0 : e < 0.5f ? 1 : e < 1 ? 2 : e < 2 ? 3 : e < 4 ? 4 : e < 8 ? 5 : e < 32 ? 6 : 7]++; } memcpy(g_errh, h, sizeof h); }
        g_camdbg[3] = nk;
        if (nk >= 96 && nk * 5 >= g_nsamp * 3) { memcpy(g_cam, M, sizeof M); g_cam_ok = 1; }   /* (3 in 5 agree; sparse outdoor scenes have few points) */   /* most points agree */
    }
    /* almost nothing moved the way one camera move would explain: the camera jumped (a cut, or it bumped into a
     * wall). No in-between pictures for this frame */
    if (g_nsamp >= 400 && !g_cam_ok && (g_camdbg[3] < 0 ? g_camdbg[2] : g_camdbg[3]) * 4 < g_nsamp) R->cut = 1;
    /* points / draws without partners: where they were if they stood still in the world; else left out (-1) or kept (-2) */
    int stuck = 0;
    for (int i = 0; i < R->nc; i++) {
        GLCmd *c = &R->c[i]; if (c->kind) continue;
        const GLVtx *b = R->v + c->v0; GLPrev *pv = R->pv + c->v0;
        if (g_cam_ok && (g_how1[i] || g_amb[i]) && !g_ssp[i] && !c->st.skin && !c->st.through && c->match >= 0) {
            /* paired only by its look (its vertices are somewhere else in memory now): if it doesn't move like the
             * world around it, it was probably the wrong partner (one of many look-alikes): use the camera's move */
            double dev = 0; int nd = 0; const uint8_t *pf = R->pf + c->v0; double e[3];
            for (int k = 0; k < c->n; k++) if (pf[k] == 0 && pv[k].W > 1e-4f && cam_apply(g_cam, &b[k], e) && e[2] > 1e-6) {
                dev += fabs(e[0] / e[2] - pv[k].X / pv[k].W) + fabs(e[1] / e[2] - pv[k].Y / pv[k].W); nd++;
            }
            if (nd && dev / nd > 4.0) { need[i] = 1; c->match = -1; R->lerped--; R->rejected++; rejf[i] = 1; }
        }
        /* little see-through effects (the stars circling a stunned soldier's head, sparks): the game rebuilds them every
         * frame, so a star is easily paired with a different star of the frame before and the in-between pictures made
         * them zig-zag. They are not moved by their own motion in between - only with the camera, like the world */
        if (c->match >= 0 && !g_ssp[i] && c->st.bon && !c->st.skin && small_tris(b, c->n)) { c->match = -1; R->lerped--; need[i] = 1; }
        /* plain (untextured) 2D shapes are not moved in between either: the comic cutscenes draw thousands of
         * look-alike little squares that cut the panels out, and paired with the wrong ones of the frame before they
         * let the drawings spill over the panel lines in the in-between pictures */
        if (c->match >= 0 && c->st.through && !c->st.texon) { c->match = -1; R->lerped--; need[i] = 1; }
        if (need[i]) {
            int ss = !c->st.through && screen_space(b, c->n);
            if (!g_cam_ok || c->st.through || ss) { if (!c->st.through && !ss) stuck++; continue; }   /* (drawn where it is now) */
            int bad = 0, far = 0; uint8_t *pf = R->pf + c->v0; double dp, dq; draw_depth(b, c->n, &dp, &dq);
            for (int k = 0; k < c->n; k++) {
                pf[k] = 0; if (!cam_prev(&b[k], dp, dq, &pv[k])) bad = 1;
                /* (behind the camera, or nearly, in the frame before: only the triangles with such a corner are drawn where
                 * they are now - replay() does that. Leaving the whole draw unmoved made walls right next to the camera
                 * hop at 20 fps) */
                /* (much nearer or farther than now: not something standing still - e.g. a haze card that turns with
                 * the camera. The camera's move doesn't apply to it) */
                /* (most of its points: the corners right next to the camera of a wall it slides along change that much too) */
                else if (pv[k].W > 0.05f * b[k].w && (pv[k].W > 3.f * b[k].w + 50.f || pv[k].W * 3.f + 50.f < b[k].w)) far++;
            }
            if (far * 2 > c->n) bad = 1;
            if (!bad) { c->match = -3; R->guessed++; R->rescued += rejf[i]; }
        } else if (c->match >= 0) {
            uint8_t *pf = R->pf + c->v0; double dp = 0, dq = 0; int dd = 0;
            for (int k = 0; k < c->n; k++) if (pf[k] == 3) {
                if (!dd) { draw_depth(b, c->n, &dp, &dq); dd = 1; }
                if (screen_space(b, c->n)) { pv[k].X = b[k].x; pv[k].Y = b[k].y; pv[k].Z = b[k].z; pv[k].W = 1.f; pv[k].fog = b[k].fog; pv[k].u = b[k].u; pv[k].v = b[k].v; pf[k] = 0; continue; }   /* (stays where it is) */
                pf[k] = (g_cam_ok && !c->st.through && cam_prev(&b[k], dp, dq, &pv[k])) ? 0 : 1;
            }
        }
    }
    /* many world pieces that can't be moved (no partner and no camera move to go by) would stay put while their
     * neighbours move: gaps open between them. Better to show this frame without in-between pictures */
    if (!g_cam_ok && stuck * 25 > R->nc) R->cut = 1;
    /* the stars paired by their order move straight across the screen between frames, but the world around them
     * doesn't when the camera turns (it moves in perspective): they wobbled against the soldier's head. They are tied
     * to the nearest thing on the screen at about the same depth (his head) and move with it, plus their own circling */
    /* see-through cards that go round with the camera (the dust haze hanging in front of it): no partner when the
     * camera turns fast, and taken for something standing in the world they were put where the in-between camera
     * would see the world - swung away by half the turn, so the haze came and went with every picture (flickered).
     * One of the frame before's draws of the same kind was about where it is on the screen now, not where the
     * world moved: it is drawn where it is now */
    memset(g_scr, 0, (size_t)R->nc);
    if (g_cam_ok) for (int i = 0; i < R->nc; i++) {
        GLCmd *c = &R->c[i]; if (c->kind || !need[i] || !c->st.bon || c->st.through || c->st.skin || c->n < 3) continue;
        const GLVtx *v = R->v + c->v0; if (screen_space(v, c->n)) continue;
        double sx = 0, sy = 0, wx = 0, wy = 0; int ns = 0;
        for (int k = 0; k < c->n; k++) {
            double e[3]; if (!(v[k].w > 0.f) || v[k].x < -480.f || v[k].x > 960.f || v[k].y < -272.f || v[k].y > 544.f || !cam_apply(g_cam, &v[k], e) || !(e[2] > 1e-6)) continue;
            sx += v[k].x; sy += v[k].y; wx += e[0] / e[2]; wy += e[1] / e[2]; ns++;
        }
        if (ns < 3) continue;
        sx /= ns; sy /= ns; wx /= ns; wy /= ns;
        if (sx < -40.0 || sx > 520.0 || sy < -40.0 || sy > 312.0) continue;          /* (its middle on the screen) */
        double camd = fabs(wx - sx) + fabs(wy - sy); if (camd < 24.0) continue;      /* (the camera hardly moved it) */
        double bc = 1e30, bw = 1e30;
        for (int j = 0; j < P->nc; j++) {
            const GLCmd *d = &P->c[j]; if (d->kind || d->st.tex != c->st.tex || !d->st.bon || d->st.skin || d->st.through) continue;
            const GLVtx *u = P->v + d->v0; double px = 0, py = 0; int np = 0;
            for (int k = 0; k < d->n; k++) if (u[k].w > 0.f && u[k].x > -480.f && u[k].x < 960.f && u[k].y > -272.f && u[k].y < 544.f) { px += u[k].x; py += u[k].y; np++; }
            if (!np) continue; px /= np; py /= np;
            double dc = fabs(px - sx) + fabs(py - sy), dw = fabs(px - wx) + fabs(py - wy);
            if (dc < bc) bc = dc; if (dw < bw) bw = dw;
        }
        if (bc < 1e29 && bc * 4.0 < camd && bc * 2.0 < bw) { g_scr[i] = 1; if (c->match == -3) R->guessed--; c->match = -1; g_scr_n++; }
    }
    lost_find(R, P);
    for (int i = 0; i < R->nc && g_cam_ok; i++) {
        GLCmd *c = &R->c[i]; if (c->kind || c->st.through || c->n > 64) continue;
        GLVtx *b = R->v + c->v0; GLPrev *pv = R->pv + c->v0;
        int paired = g_ssp[i] && c->match >= 0, alone = !g_ssp[i] && c->match == -1 && screen_space(b, c->n);
        if (!paired && !alone) continue;
        double sx = 0, sy = 0, sz = 0; int ns = 0;
        for (int k = 0; k < c->n; k++) if (b[k].x > -16.f && b[k].x < 496.f && b[k].y > -16.f && b[k].y < 288.f) { sx += b[k].x; sy += b[k].y; sz += b[k].z; ns++; }
        if (!ns) continue;
        sx /= ns; sy /= ns; sz /= ns;
        /* its distance from the camera: that of the world near it on the screen at the same depth (z) */
        int best = -1; float bs = 1e30f;
        for (int j = 0; j < R->nc; j++) {
            const GLCmd *d = &R->c[j]; if (d->kind || d->st.through || g_ssp[j] || (d->match < 0 && d->match != -3)) continue;
            const GLVtx *e = R->v + d->v0; const uint8_t *f = R->pf + d->v0;
            for (int k = 0; k < d->n; k++) {
                if (f[k] || !(e[k].w > 1.f) || e[k].z < 0.f || e[k].z > 65535.f) continue;
                float dx = e[k].x - (float)sx, dy = e[k].y - (float)sy, dd = sqrtf(dx * dx + dy * dy);
                if (dd > 200.f) continue;
                float sc = fabsf(e[k].z - (float)sz) + dd * 0.05f;
                if (sc < bs) { bs = sc; best = d->v0 + k; }
            }
        }
        if (best < 0 || bs > 400.f) continue;
        float Wc = R->v[best].w;
        /* each point moves the way a point of the world at that depth moves with the camera, plus (paired) the
         * stars' own circling: the difference between where they were and where that puts them, spread over the frame */
        int okc = 1; static GLPrev tmp[64];
        for (int k = 0; k < c->n && okc; k++) {
            GLVtx v = b[k]; v.w = Wc; double e[3];
            if (!cam_apply(g_cam, &v, e) || !(e[2] > 1e-3)) { okc = 0; break; }
            double ox = paired ? pv[k].X - e[0] / e[2] : 0, oy = paired ? pv[k].Y - e[1] / e[2] : 0;   /* (pv is still on the screen here) */
            tmp[k].X = (float)(e[0] + ox * e[2]); tmp[k].Y = (float)(e[1] + oy * e[2]); tmp[k].Z = (float)((paired ? pv[k].Z : b[k].z) * e[2]); tmp[k].W = (float)e[2]; tmp[k].fog = b[k].fog; tmp[k].u = paired ? pv[k].u : b[k].u; tmp[k].v = paired ? pv[k].v : b[k].v;
        }
        if (!okc) continue;
        c->ssw = 1;
        for (int k = 0; k < c->n; k++) { b[k].w = Wc; pv[k] = tmp[k]; R->pf[c->v0 + k] = 0; }
        if (alone) { c->match = -3; R->guessed++; }
    }
}
static double g_fpos[4][3]; static int g_fok[4];   /* (what the camera follows, per recorded frame) */
static int rec_follow(const GLRec *R, const GLLit *L, int pj, double *o);
/* a new picture is complete (the game showed it): it becomes "the last frame", the one before it "the previous" */
static int g_since_commit;
/* a frame of only flat 2D drawing (the comic-style cutscenes, menus): no in-between pictures - its pieces are cut
 * out and layered in ways that pairing them with the frame before can't follow (drawings spilled over the panel
 * lines and turned see-through) */
static int rec_2d_only(const GLRec *R) {
    if (!R->ok) return 0;
    int n = 0; for (int i = 0; i < R->nc; i++) { const GLCmd *c = &R->c[i]; if (c->kind) continue; if (!c->st.through) return 0; n++; }
    return n > 0;
}
/* the finished picture of a 2D-only frame, kept when the game says the frame is done: the comic cutscenes draw the
 * next frame straight into the framebuffer that is on the screen, so between two frames it holds a half-drawn picture
 * (drawings not yet cut to their panels, a faded layer) - the screen shows this copy instead */
static GLuint g_frz_tex, g_frz_fbo; static int g_frz_w, g_frz_h, g_frz_ok; static uint32_t g_frz_addr;
static void fb_resolve(GLFb *f);
static void frz_keep(const GLRec *R) {
    g_frz_ok = 0; int fi = -1;
    for (int i = R->nc - 1; i >= 0; i--) if (!R->c[i].kind) { fi = R->c[i].st.fbi; break; }
    if (fi < 0 || fi >= g_nfb) return;
    GLFb *f = &g_fbs[fi]; int w = f->w * g_scale, h = f->h * g_scale;
    fb_resolve(f);
    if (!g_frz_fbo || g_frz_w != w || g_frz_h != h) {
        if (g_frz_tex) p_glDeleteTextures(1, &g_frz_tex);
        p_glGenTextures(1, &g_frz_tex); p_glBindTexture(GL_TEXTURE_2D, g_frz_tex);
        p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (!g_frz_fbo) p_glGenFramebuffers(1, &g_frz_fbo);
        p_glBindFramebuffer(GL_FRAMEBUFFER, g_frz_fbo); p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_frz_tex, 0);
        g_frz_w = w; g_frz_h = h;
    }
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, f->fbo); p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_frz_fbo);
    p_glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0); g_cur_fb = 0; ap_forget();
    g_frz_ok = 1; g_frz_addr = f->addr;
}
static void gl_commit__b(void);
GEXPORT void gl_commit(void) { uint64_t _t = pf_top_begin(); gl_commit__b(); pf_top_end(2, _t); }
static void gl_commit__b(void) {
    if (!g_gl_on || !g_rec_on) return;
    g_since_commit = 0;
    gl_flush(); g_batch_gen = 0xFFFFFFFF;
    GLRec *C = &g_rec[g_rcur];
    if (g_vbuf == C->v) C->nv = g_nvtx;
    C->ok = 1;
    { GLLit *Lt = &g_lit[g_rcur]; int best = -1;
      for (int i = 0; i < Lt->npj; i++) if (Lt->pj[i].ok && (best < 0 || Lt->pj[i].nv > Lt->pj[best].nv)) best = i;
      Lt->main = best;
      g_fok[g_rcur] = best >= 0 && rec_follow(C, Lt, best, g_fpos[g_rcur]); }
    lit_fit(g_rcur);
    int old = g_rprev2; g_rprev2 = g_rprev; g_rprev = g_rlast; g_rlast = g_rcur; g_rcur = old;
    rec_match(&g_rec[g_rlast], &g_rec[g_rprev]); ghost_ok(&g_rec[g_rlast], &g_rec[g_rprev]);
    if (rec_2d_only(&g_rec[g_rlast])) frz_keep(&g_rec[g_rlast]); else g_frz_ok = 0;
    rec_reset(g_rcur); g_vbuf = g_rec[g_rcur].v; g_sbuf = g_rec[g_rcur].src; g_vcap = FR_VTX; g_nvtx = g_vbase = 0;
    g_frame_no++;
}
/* (test aid) the frame just drawn is forgotten instead of kept: the next gl_commit then pairs the frame before it with
 * the one after it, and the in-between picture at t = 0.5 can be compared with the real frame that was left out */
GEXPORT void gl_commit_drop(void) {
    if (!g_gl_on || !g_rec_on) return;
    gl_flush(); g_batch_gen = 0xFFFFFFFF;
    rec_reset(g_rcur); g_vbuf = g_rec[g_rcur].v; g_sbuf = g_rec[g_rcur].src; g_vcap = FR_VTX; g_nvtx = g_vbase = 0;
}
GEXPORT int gl_interp_rescued(void) { return g_rec[g_rlast].rescued; }
/* (play.py: menus) vertices of 3D drawing in the last frame - few means a menu screen (the mission / staff hub, the
 * codec), where W A S D should work the D-pad. -1: nothing recorded */
GEXPORT int gl_world_verts(void) {
    const GLRec *R = &g_rec[g_rlast]; if (!g_rec_on || !R->ok) return -1;
    int n = g_pc_wv[g_rlast]; for (int i = 0; i < R->nc; i++) if (!R->c[i].kind && !R->c[i].st.through) n += R->c[i].n;   /* (with the draws left out for being off the screen) */
    return n;
}
GEXPORT int gl_interp_stats(int *out) {   /* last frame: draws, vertices, moved smoothly, too far apart, full, no partner, moved like the camera */
    GLRec *R = &g_rec[g_rlast]; out[0] = R->nc; out[1] = R->nv; out[2] = R->lerped; out[3] = R->rejected; out[4] = R->over;
    out[5] = R->unpaired; out[6] = R->guessed; out[7] = R->cut; return R->ok;
}
static void shadow_for(GLFb *f) {
    if (f->sfbo) return;
    make_target(f->w * g_scale, f->h * g_scale, &f->sfbo, &f->scolor, &f->sdepth, &f->smsfbo, &f->smscolor, &f->smsdepth);
}
/* draw the last frame's picture for framebuffer f again, every vertex moved t of the way from the previous frame
 * (t = 0: previous frame, 1: last frame), into f's shadow copy. Other framebuffers don't matter for f: textures are
 * always taken from PSP video memory. */
/* clip-space point with everything a vertex carries, for cutting triangles at the near plane */
typedef struct { float X, Y, Z, W, u, v, r, g, b, a, fog; } CV;
static CV cv_mix(const CV *p, const CV *q, float s) {
    CV o; const float *a = &p->X, *b = &q->X; float *d = &o.X;
    for (int i = 0; i < 11; i++) d[i] = a[i] + (b[i] - a[i]) * s;
    return o;
}
static int clip_plane(const CV *in, int n, CV *out, int which) {     /* keep the side where the plane value >= 0 */
    int m = 0;
    for (int i = 0; i < n; i++) {
        const CV *p = &in[i], *q = &in[(i + 1) % n];
        float dp = which ? 65535.f * p->W - p->Z : p->W - 1e-4f, dq = which ? 65535.f * q->W - q->Z : q->W - 1e-4f;
        if (dp >= 0) out[m++] = *p;
        if ((dp >= 0) != (dq >= 0)) out[m++] = cv_mix(p, q, dp / (dp - dq));
    }
    return m;
}
static GLVtx cv_vtx(const CV *c) {
    GLVtx o; float iw = 1.f / c->W;
    o.x = c->X * iw; o.y = c->Y * iw; o.z = c->Z * iw; o.w = c->W; o.u = c->u; o.v = c->v; o.fog = c->fog; o.lx = o.ly = 0.f; o.lz = -1.f;
    o.r = (uint8_t)(c->r + 0.5f); o.g = (uint8_t)(c->g + 0.5f); o.b = (uint8_t)(c->b + 0.5f); o.a = (uint8_t)(c->a + 0.5f);
    return o;
}
/* draw the last frame's picture for framebuffer f again, every vertex moved t of the way from the previous frame
 * (t = 0: previous frame, 1: last frame), into f's shadow copy. Other framebuffers don't matter for f: textures are
 * always taken from PSP video memory. */
/* ---- remaster lighting: the shadow picture (seen from the sun; colour = packed distance from it) */
#define SH_SIZE 2048
#define CHAR_SELF 220.0                                      /* world units (about cm x 10) */
static GLuint g_shfbo, g_shtex, g_shdepth;
static GLuint g_vram_tex, g_pprog, g_ovl_tex; static GLint pu_tex, pu_rect, pu_taps, pu_step, pu_ovl, pu_grade; static int g_ovl_on;
static int sh_target(void) {
    if (g_shfbo) return 1;
    p_glGenTextures(1, &g_shtex); p_glBindTexture(GL_TEXTURE_2D, g_shtex);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SH_SIZE, SH_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    p_glGenFramebuffers(1, &g_shfbo); p_glBindFramebuffer(GL_FRAMEBUFFER, g_shfbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_shtex, 0);
    p_glGenRenderbuffers(1, &g_shdepth); p_glBindRenderbuffer(GL_RENDERBUFFER, g_shdepth);
    p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, SH_SIZE, SH_SIZE);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_shdepth);
    if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { g_light_on = 0; return 0; }
    g_cur_fb = 0; ap_forget(); return 1;
}
/* how a draw takes part: 0 = not (2D, other cameras, glows), 1 = world, 3 = character (lit by the game already) */
static int lit_mode(const GLSt *s, const GLLit *L, const double *MV) {
    if (s->pj < 0 || s->through) return 0;
    const GLPj *p = &L->pj[(int)s->pj]; if (!p->ok || memcmp(p->V, MV, sizeof p->V)) return 0;
    if (s->bon && s->bd == GL_ONE) return 0;
    return s->skin ? 3 : 1;
}
static float g_lcam[4];
/* every vertex of framebuffer f's draws (as about to be drawn, t of the way between frames): its place in the shadow
 * picture. 0 = no lighting for this picture */
/* ---- 60 fps, exact: the in-between pictures from the game's own camera and the world's real places.
 * Every draw comes with the camera it was drawn with (view and projection, both frames). A point's place in the world
 * is worked out from where it landed on the screen; things that stand still keep it, things that move go from their
 * old place to the new one; and the camera itself is moved from its old place and angle to the new one. Then each
 * point is drawn again from that in-between camera. Nothing is guessed from how the picture moved. */
typedef struct { double V[12], P[16], sx, sy, ox, oy, sz, oz; int ok; } GLCam;
static int g_exact = 1;
GEXPORT void gl_exact(int on) { g_exact = on; }      /* (F9 in play.py: compare with the old way) */
GEXPORT int gl_exact_on(void) { return g_exact; }
static int inv3(const double *m, double *o) {           /* 3x3, rows */
    double d = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (!(fabs(d) > 1e-30)) return 0;
    double id = 1.0 / d;
    o[0] = (m[4] * m[8] - m[5] * m[7]) * id; o[1] = (m[2] * m[7] - m[1] * m[8]) * id; o[2] = (m[1] * m[5] - m[2] * m[4]) * id;
    o[3] = (m[5] * m[6] - m[3] * m[8]) * id; o[4] = (m[0] * m[8] - m[2] * m[6]) * id; o[5] = (m[2] * m[3] - m[0] * m[5]) * id;
    o[6] = (m[3] * m[7] - m[4] * m[6]) * id; o[7] = (m[1] * m[6] - m[0] * m[7]) * id; o[8] = (m[0] * m[4] - m[1] * m[3]) * id;
    return 1;
}
/* the view matrix V (PSP order: view_i = sum_j world_j V[3j+i] + V[9+i]) as rows R[i][j], and the camera's place */
static int cam_rows(const double *V, double *R, double *c) {
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) R[3 * i + j] = V[3 * j + i];
    double Ri[9]; if (!inv3(R, Ri)) return 0;
    for (int j = 0; j < 3; j++) c[j] = -(Ri[3 * j] * V[9] + Ri[3 * j + 1] * V[10] + Ri[3 * j + 2] * V[11]);
    return 1;
}
/* the camera t of the way from a (the frame before) to b (the last frame): its place moves in a straight line, its
 * angle turns (rows kept at right angles, their lengths - any scale the game put in - kept too), the rest blends */
static int g_use_tgt; static double g_tgt_a[3], g_tgt_b[3];   /* what the camera follows (Snake) in each frame, in the world */
static void cam_lerp(const GLPj *a, const GLPj *b, double t, GLCam *o) {
    double Ra[9], Rb[9], ca[3], cb[3]; o->ok = 0;
    if (!cam_rows(a->V, Ra, ca) || !cam_rows(b->V, Rb, cb)) return;
    /* the camera mostly circles round Snake: its place is moved round the point it follows (on an arc), not straight
     * across (that cut the corner: everything swelled and shifted a little in the in-between pictures) */
    double pa[3], pb[3]; int use_piv = g_use_tgt; if (use_piv) { memcpy(pa, g_tgt_a, sizeof pa); memcpy(pb, g_tgt_b, sizeof pb); }
    double R[9], len[3];
    for (int i = 0; i < 9; i++) R[i] = Ra[i] + (Rb[i] - Ra[i]) * t;
    for (int i = 0; i < 3; i++) {
        double la = sqrt(Ra[3 * i] * Ra[3 * i] + Ra[3 * i + 1] * Ra[3 * i + 1] + Ra[3 * i + 2] * Ra[3 * i + 2]);
        double lb = sqrt(Rb[3 * i] * Rb[3 * i] + Rb[3 * i + 1] * Rb[3 * i + 1] + Rb[3 * i + 2] * Rb[3 * i + 2]);
        len[i] = la + (lb - la) * t;
    }
    for (int i = 0; i < 3; i++) {                            /* (Gram-Schmidt on the rows, then their lengths back) */
        for (int q = 0; q < i; q++) { double d = R[3 * i] * R[3 * q] + R[3 * i + 1] * R[3 * q + 1] + R[3 * i + 2] * R[3 * q + 2];
            for (int k = 0; k < 3; k++) R[3 * i + k] -= d * R[3 * q + k]; }
        double l = sqrt(R[3 * i] * R[3 * i] + R[3 * i + 1] * R[3 * i + 1] + R[3 * i + 2] * R[3 * i + 2]);
        if (!(l > 1e-12)) return;
        for (int k = 0; k < 3; k++) R[3 * i + k] /= l;
    }
    for (int i = 0; i < 3; i++) for (int k = 0; k < 3; k++) R[3 * i + k] *= len[i];
    double c[3]; for (int k = 0; k < 3; k++) c[k] = ca[k] + (cb[k] - ca[k]) * t;
    if (use_piv) {
        double oa[3], ob[3], la = 0, lb = 0, p[3];
        for (int k = 0; k < 3; k++) { oa[k] = ca[k] - pa[k]; ob[k] = cb[k] - pb[k]; la += oa[k] * oa[k]; lb += ob[k] * ob[k]; p[k] = pa[k] + (pb[k] - pa[k]) * t; }
        la = sqrt(la); lb = sqrt(lb);
        /* (only if it is the same thing in both frames: what is "followed" is the nearest character on the screen, and
         *  that can switch - a knocked-out soldier near the camera, then Snake - and swinging round a point that jumped
         *  put the in-between camera far off: the whole picture slid sideways, a wall's gap showed as a pale block) */
        double dt = sqrt((pb[0] - pa[0]) * (pb[0] - pa[0]) + (pb[1] - pa[1]) * (pb[1] - pa[1]) + (pb[2] - pa[2]) * (pb[2] - pa[2]));
        if (!(dt < 0.15 * (la + lb) * 0.5) || !(la < 1.3 * lb && lb < 1.3 * la)) la = 0;
        if (la > 1e-6 && lb > 1e-6) {
            double ua[3], ub[3], dot = 0; for (int k = 0; k < 3; k++) { ua[k] = oa[k] / la; ub[k] = ob[k] / lb; dot += ua[k] * ub[k]; }
            if (dot > 1) dot = 1; if (dot < -1) dot = -1;
            double ang = 1.5707963267948966 - asin(dot), u[3], len = la + (lb - la) * t;
            if (ang > 1e-6 && ang < 3.0) { double s0 = sin((1 - t) * ang) / sin(ang), s1 = sin(t * ang) / sin(ang); for (int k = 0; k < 3; k++) u[k] = ua[k] * s0 + ub[k] * s1; }
            else for (int k = 0; k < 3; k++) u[k] = ua[k] + (ub[k] - ua[k]) * t;
            for (int k = 0; k < 3; k++) c[k] = p[k] + u[k] * len;
        }
    }
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) o->V[3 * j + i] = R[3 * i + j];
    for (int i = 0; i < 3; i++) o->V[9 + i] = -(R[3 * i] * c[0] + R[3 * i + 1] * c[1] + R[3 * i + 2] * c[2]);
    for (int k = 0; k < 16; k++) o->P[k] = a->P[k] + (b->P[k] - a->P[k]) * t;
    o->sx = a->sx + (b->sx - a->sx) * t; o->sy = a->sy + (b->sy - a->sy) * t; o->ox = a->ox + (b->ox - a->ox) * t;
    o->oy = a->oy + (b->oy - a->oy) * t; o->sz = a->sz + (b->sz - a->sz) * t; o->oz = a->oz + (b->oz - a->oz) * t;
    o->ok = 1;
}
/* a world point seen by camera c, as the in-between picture's point (X, Y, Z, W: screen place times W) */
static inline void cam_proj(const GLCam *c, const double *w, CV *d) {
    const double *V = c->V, *P = c->P;
    double v0 = w[0] * V[0] + w[1] * V[3] + w[2] * V[6] + V[9], v1 = w[0] * V[1] + w[1] * V[4] + w[2] * V[7] + V[10], v2 = w[0] * V[2] + w[1] * V[5] + w[2] * V[8] + V[11];
    double cx = v0 * P[0] + v1 * P[4] + v2 * P[8] + P[12], cy = v0 * P[1] + v1 * P[5] + v2 * P[9] + P[13];
    double cz = v0 * P[2] + v1 * P[6] + v2 * P[10] + P[14], cw = v0 * P[3] + v1 * P[7] + v2 * P[11] + P[15];
    d->X = (float)(cx * c->sx + c->ox * cw); d->Y = (float)(cy * c->sy + c->oy * cw); d->Z = (float)(cz * c->sz + c->oz * cw); d->W = (float)cw;
}
/* (speed) both steps are straight-line maps, so per draw they become small matrices: world = Kw (X, Y, W) + k, where
 * X, Y, W = screen x times w, screen y times w, w; and the in-between camera's X, Y, Z, W = Kc world + kc */
static void world_of(const GLPj *p, double X, double Y, double W, double *o) {
    double a0 = (X - p->ox * W) / p->sx - p->P[12], a1 = (Y - p->oy * W) / p->sy - p->P[13], a2 = W - p->P[15];
    const double *m = p->Mi, *V = p->V;
    double v0 = m[0] * a0 + m[1] * a1 + m[2] * a2 - V[9], v1 = m[3] * a0 + m[4] * a1 + m[5] * a2 - V[10], v2 = m[6] * a0 + m[7] * a1 + m[8] * a2 - V[11];
    o[0] = V[0] * v0 + V[1] * v1 + V[2] * v2; o[1] = V[3] * v0 + V[4] * v1 + V[5] * v2; o[2] = V[6] * v0 + V[7] * v1 + V[8] * v2;
}
static void world_mat(const GLPj *p, double *K) {             /* K[0..8] by columns X, Y, W; K[9..11] the offset */
    double o0[3], e[3]; world_of(p, 0, 0, 0, o0);
    for (int c = 0; c < 3; c++) { world_of(p, c == 0, c == 1, c == 2, e); for (int r = 0; r < 3; r++) K[3 * c + r] = e[r] - o0[r]; }
    for (int r = 0; r < 3; r++) K[9 + r] = o0[r];
}
static void cam_projd(const GLCam *c, const double *w, double *o) {    /* cam_proj in full precision: X, Y, Z, W */
    const double *V = c->V, *P = c->P;
    double v0 = w[0] * V[0] + w[1] * V[3] + w[2] * V[6] + V[9], v1 = w[0] * V[1] + w[1] * V[4] + w[2] * V[7] + V[10], v2 = w[0] * V[2] + w[1] * V[5] + w[2] * V[8] + V[11];
    double cx = v0 * P[0] + v1 * P[4] + v2 * P[8] + P[12], cy = v0 * P[1] + v1 * P[5] + v2 * P[9] + P[13];
    double cz = v0 * P[2] + v1 * P[6] + v2 * P[10] + P[14], cw = v0 * P[3] + v1 * P[7] + v2 * P[11] + P[15];
    o[0] = cx * c->sx + c->ox * cw; o[1] = cy * c->sy + c->oy * cw; o[2] = cz * c->sz + c->oz * cw; o[3] = cw;
}
static void cam_mat(const GLCam *c, double *K) {               /* K[0..11] by columns x, y, z of the world; K[12..15] the offset */
    double d0[4], d[4], w0[3] = {0, 0, 0}; cam_projd(c, w0, d0);
    for (int col = 0; col < 3; col++) { double w[3] = {col == 0, col == 1, col == 2}; cam_projd(c, w, d); for (int r = 0; r < 4; r++) K[4 * col + r] = d[r] - d0[r]; }
    for (int r = 0; r < 4; r++) K[12 + r] = d0[r];
}
/* for each camera set of the last frame: the same set in the frame before (same projection and screen area), and the
 * in-between camera for the picture being made */
static GLCam g_ecam[MAX_PJ]; static signed char g_ecam_st[MAX_PJ]; static int g_ecam_pp[MAX_PJ];
/* what the camera follows (Snake: the nearest character on the screen): the middle of it in the world */
static int rec_follow(const GLRec *R, const GLLit *L, int pj, double *o) {
    static float ws[8192]; int n = 0;
    if (pj < 0 || pj >= L->npj || !L->pj[pj].ok) return 0;
    for (int i = 0; i < R->nc && n < 8192; i++) {
        const GLCmd *c = &R->c[i]; if (c->kind || !c->st.skin || c->st.through || c->st.pj != pj) continue;
        const GLVtx *b = R->v + c->v0;
        for (int k = 0; k < c->n && n < 8192; k += 3) if (b[k].x > 0.f && b[k].x < 480.f && b[k].y > 0.f && b[k].y < 272.f && b[k].w > 1.f) ws[n++] = b[k].w;
    }
    if (n < 30) return 0;
    int cnt[128] = {0}; for (int i = 0; i < n; i++) { int b = (int)(log2((double)ws[i]) * 4); if (b < 0) b = 0; if (b > 127) b = 127; cnt[b]++; }
    int acc = 0, bb = 0; for (; bb < 128; bb++) { acc += cnt[bb]; if (acc * 10 >= n) break; }
    double lim = pow(2.0, (bb + 1) * 0.25) * 1.25; double sum[3] = {0, 0, 0}; int m = 0; const GLPj *p = &L->pj[pj];
    for (int i = 0; i < R->nc; i++) {
        const GLCmd *c = &R->c[i]; if (c->kind || !c->st.skin || c->st.through || c->st.pj != pj) continue;
        const GLVtx *b = R->v + c->v0;
        for (int k = 0; k < c->n; k += 3) if (b[k].x > 0.f && b[k].x < 480.f && b[k].y > 0.f && b[k].y < 272.f && b[k].w > 1.f && b[k].w <= lim) {
            double w[3]; lit_world(p, p->V, b[k].x, b[k].y, b[k].w, w); sum[0] += w[0]; sum[1] += w[1]; sum[2] += w[2]; m++; }
    }
    if (m < 20) return 0;
    for (int k = 0; k < 3; k++) o[k] = sum[k] / m;
    return 1;
}
static int g_tgt_ok = -1;
static void ecam_reset(void) { memset(g_ecam_st, 0, sizeof g_ecam_st); g_tgt_ok = -1; }
static const GLCam *ecam(int pi, float t) {
    if (pi < 0 || pi >= MAX_PJ) return 0;
    if (g_ecam_st[pi]) return g_ecam_st[pi] > 0 ? &g_ecam[pi] : 0;
    const GLLit *LR = &g_lit[g_rlast], *LP = &g_lit[g_rprev]; g_ecam_st[pi] = -1;
    if (pi >= LR->npj || !LR->pj[pi].ok) return 0;
    const GLPj *b = &LR->pj[pi]; int best = -1; double bd = 1e30;
    /* (the same projection - or nearly: the game zooms a little as the camera closes in on Snake or a wall, a new
     *  projection every frame. Requiring the very same one called each of those frames a camera cut: no in-between
     *  pictures, a stutter whenever the camera looked down at Snake. The in-between camera blends the two) */
    for (int j = 0; j < LP->npj; j++) {
        const GLPj *a = &LP->pj[j];
        if (!a->ok || a->sx != b->sx || a->sy != b->sy || a->ox != b->ox || a->oy != b->oy || a->sz != b->sz || a->oz != b->oz) continue;
        double d = pj_pdiff(a, b); if (!(d < 0.25)) continue;
        if (best < 0 || d < bd - 1e-9 || (d < bd + 1e-9 && a->nv > LP->pj[best].nv)) { best = j; bd = d; }
    }
    if (best < 0) return 0;
    if (g_tgt_ok < 0) { g_tgt_ok = g_fok[g_rlast] && g_fok[g_rprev];
        if (g_tgt_ok) { memcpy(g_tgt_b, g_fpos[g_rlast], sizeof g_tgt_b); memcpy(g_tgt_a, g_fpos[g_rprev], sizeof g_tgt_a); } }
    g_use_tgt = g_tgt_ok > 0 && pi == LR->main;
    cam_lerp(&LP->pj[best], b, t, &g_ecam[pi]);
    g_ecam_pp[pi] = best;
    if (!g_ecam[pi].ok) return 0;
    g_ecam_st[pi] = 1; return &g_ecam[pi];
}
/* exact: did the camera cut between the two frames (a new shot, not a move)? 1 = cut, 0 = no, -1 = can't tell */
static int exact_cut(void) {
    const GLLit *LR = &g_lit[g_rlast], *LP = &g_lit[g_rprev];
    if (!g_rec[g_rprev].ok || LR->main < 0 || LP->main < 0) return -1;
    ecam_reset(); const GLCam *c = ecam(LR->main, 0.5f); ecam_reset();
    if (!c) return 1;                                            /* (a different projection: a new shot) */
    const GLPj *a = &LP->pj[g_ecam_pp[LR->main]], *b = &LR->pj[LR->main];
    double Ra[9], Rb[9], ca[3], cb[3];
    if (!cam_rows(a->V, Ra, ca) || !cam_rows(b->V, Rb, cb)) return -1;
    /* (turned more than ~50 degrees, or moved further than ~4x the distance to the near world, in one frame) */
    double la = sqrt(Ra[6] * Ra[6] + Ra[7] * Ra[7] + Ra[8] * Ra[8]), lb = sqrt(Rb[6] * Rb[6] + Rb[7] * Rb[7] + Rb[8] * Rb[8]);
    double cosang = (Ra[6] * Rb[6] + Ra[7] * Rb[7] + Ra[8] * Rb[8]) / (la * lb + 1e-30);
    double dx = ca[0] - cb[0], dy = ca[1] - cb[1], dz = ca[2] - cb[2], mv = sqrt(dx * dx + dy * dy + dz * dz);
    return cosang < 0.64 || mv > 3000.0;
}
/* (test aid) why exact_cut decided: o[0] mains R/P, o[1] P-matrix max relative difference x1000, o[2] viewport same,
 * o[3] cos(turn) x1000, o[4] move */
GEXPORT void gl_dbg_cut(int *o) {
    const GLLit *LR = &g_lit[g_rlast], *LP = &g_lit[g_rprev]; memset(o, 0, 5 * sizeof(int));
    o[0] = LR->main * 100 + LP->main; if (LR->main < 0 || LP->main < 0) return;
    const GLPj *a = &LP->pj[LP->main], *b = &LR->pj[LR->main]; double md = 0;
    for (int k = 0; k < 16; k++) { double d = fabs(a->P[k] - b->P[k]), m = fabs(a->P[k]) > fabs(b->P[k]) ? fabs(a->P[k]) : fabs(b->P[k]); if (m > 1e-9 && d / m > md) md = d / m; }
    o[1] = (int)(md * 1000); o[2] = a->sx == b->sx && a->sy == b->sy && a->ox == b->ox && a->oy == b->oy && a->sz == b->sz && a->oz == b->oz;
    double Ra[9], Rb[9], ca[3], cb[3]; if (!cam_rows(a->V, Ra, ca) || !cam_rows(b->V, Rb, cb)) return;
    double la = sqrt(Ra[6] * Ra[6] + Ra[7] * Ra[7] + Ra[8] * Ra[8]), lb = sqrt(Rb[6] * Rb[6] + Rb[7] * Rb[7] + Rb[8] * Rb[8]);
    o[3] = (int)(1000 * (Ra[6] * Rb[6] + Ra[7] * Rb[7] + Ra[8] * Rb[8]) / (la * lb + 1e-30));
    o[4] = (int)sqrt((ca[0] - cb[0]) * (ca[0] - cb[0]) + (ca[1] - cb[1]) * (ca[1] - cb[1]) + (ca[2] - cb[2]) * (ca[2] - cb[2]));
}
/* (speed) An in-between picture only works out, sends and draws what its camera can see. The game hands the PSP whole
 * rooms and lets it throw away what is off the screen - in a busy place 9 of 10 points (250000 of 280000) belong to
 * draws wholly off it. For the world standing still, the box around a draw (worked out once per game frame) is put
 * through the picture's camera: wholly outside the draw's clip rectangle, the draw is left out of the picture - its
 * points aren't worked out, aren't sent to the card, aren't lit. What is kept is packed together (g_o0: where a
 * draw's points start in the picture's list), so only that is sent. */
static uint8_t g_cullf[FR_CMD]; static int g_o0[FR_CMD]; static int g_cull_on = 1; static uint32_t g_cull_n[4];
GEXPORT void gl_cull(int on) { g_cull_on = on; }
GEXPORT void gl_cull_stats(uint32_t *o) { memcpy(o, g_cull_n, sizeof g_cull_n); memset(g_cull_n, 0, sizeof g_cull_n); }   /* draws left out, kept, points left out, kept */
static float g_bb[4][FR_CMD][12]; static uint32_t g_bb_mark[4][FR_CMD];
static double g_eBR[9], g_esc[3], g_emin[3];                      /* (from lit_place: the sun's picture back to the world) */
static int lit_place__b(GLFb *f, float t, int lo, int hi, const int *ext0, const int *extn);
static int lit_place(GLFb *f, float t, int lo, int hi, const int *ext0, const int *extn) { uint64_t _t = PF_TSC(); int _r = lit_place__b(f, t, lo, hi, ext0, extn); pf_add(3, _t); return _r; }
static int lit_place__b(GLFb *f, float t, int lo, int hi, const int *ext0, const int *extn) {
    GLRec *R = &g_rec[g_rlast]; GLLit *L = &g_lit[g_rlast]; int fi = (int)(f - g_fbs);
    if (!g_light_on || !L->box || L->main < 0) return 0;
    (void)lo; (void)hi;
    const double *VR = L->pj[L->main].V; double Vt[12]; memcpy(Vt, VR, sizeof Vt);
    const GLLit *LP = &g_lit[g_rprev];
    const GLCam *ec = (g_exact && t < 1.f && g_rec[g_rprev].ok) ? ecam(L->main, t) : 0;
    if (ec) memcpy(Vt, ec->V, sizeof Vt);                       /* (exact: the very camera the picture was made with) */
    else if (LP->main >= 0 && t < 1.f && g_rec[g_rprev].ok) {   /* the camera t of the way between the two frames */
        const double *VP = LP->pj[LP->main].V;
        for (int k = 0; k < 12; k++) Vt[k] = VP[k] + (VR[k] - VP[k]) * t;
        double r[3][3]; for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) r[j][i] = Vt[3 * i + j];
        for (int j = 0; j < 3; j++) {
            for (int q = 0; q < j; q++) { double d = r[j][0] * r[q][0] + r[j][1] * r[q][1] + r[j][2] * r[q][2]; for (int i = 0; i < 3; i++) r[j][i] -= d * r[q][i]; }
            double l = sqrt(r[j][0] * r[j][0] + r[j][1] * r[j][1] + r[j][2] * r[j][2]); if (l > 0) for (int i = 0; i < 3; i++) r[j][i] /= l;
        }
        for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) Vt[3 * i + j] = r[j][i];
    }
    const double *B = L->B; double sc[3] = {1.0 / L->bsize, 1.0 / L->bsize, 1.0 / L->zsize};
    /* B Rt: from view to light space; the camera's place */
    double BR[9]; for (int a = 0; a < 3; a++) for (int j = 0; j < 3; j++) BR[3 * a + j] = B[3 * a] * Vt[j] + B[3 * a + 1] * Vt[3 + j] + B[3 * a + 2] * Vt[6 + j];
    for (int a = 0; a < 3; a++) {
        double q = -(BR[3 * a] * Vt[9] + BR[3 * a + 1] * Vt[10] + BR[3 * a + 2] * Vt[11]);
        g_lcam[a] = (float)((q - L->bmin[a]) / L->bsize);
    }
    g_lcam[3] = (float)(L->zsize / L->bsize);
    memcpy(g_eBR, BR, sizeof g_eBR); g_esc[0] = g_esc[1] = L->bsize; g_esc[2] = L->zsize; g_emin[0] = L->bmin[0]; g_emin[1] = L->bmin[1]; g_emin[2] = L->bmin[2];   /* (for encl_mark) */
    static float K[MAX_PJ][12]; static int kd[MAX_PJ]; for (int i = 0; i < L->npj; i++) kd[i] = 0;
    for (int i = 0; i < R->nc; i++) {
        GLCmd *c = &R->c[i]; if (c->st.fbi != fi || c->kind || g_cullf[i] || !lit_mode(&c->st, L, VR)) continue;
        int pi = c->st.pj; const GLPj *p = &L->pj[pi];
        if (!kd[pi]) {           /* l = K (x w, y w, w) + k: screen place and distance straight to the shadow picture */
            const double *M = p->Mi; double G[9], D[9] = {1.0 / p->sx, 0, -p->ox / p->sx, 0, 1.0 / p->sy, -p->oy / p->sy, 0, 0, 1};
            for (int a = 0; a < 3; a++) for (int j = 0; j < 3; j++) G[3 * a + j] = BR[3 * a] * M[j] + BR[3 * a + 1] * M[3 + j] + BR[3 * a + 2] * M[6 + j];
            double c0[3] = {-p->P[12], -p->P[13], -p->P[15]}, mc[3];
            for (int j = 0; j < 3; j++) mc[j] = M[3 * j] * c0[0] + M[3 * j + 1] * c0[1] + M[3 * j + 2] * c0[2] - Vt[9 + j];
            for (int a = 0; a < 3; a++) {
                for (int j = 0; j < 3; j++) K[pi][3 * a + j] = (float)((G[3 * a] * D[j] + G[3 * a + 1] * D[3 + j] + G[3 * a + 2] * D[6 + j]) * sc[a]);
                K[pi][9 + a] = (float)(((BR[3 * a] * mc[0] + BR[3 * a + 1] * mc[1] + BR[3 * a + 2] * mc[2]) - L->bmin[a]) * sc[a]);
            }
            kd[pi] = 1;
        }
        const float *k = K[pi];
        for (int part = 0; part < 2; part++) {
            int a0 = part ? ext0[i] : g_o0[i], n = part ? extn[i] : c->n;
            for (GLVtx *v = g_lv + a0, *e = v + n; v < e; v++) {
                float xw = v->x * v->w, yw = v->y * v->w, w = v->w;
                v->lx = k[0] * xw + k[1] * yw + k[2] * w + k[9]; v->ly = k[3] * xw + k[4] * yw + k[5] * w + k[10]; v->lz = k[6] * xw + k[7] * yw + k[8] * w + k[11];
            }
        }
    }
    return 1;
}
/* draw the shadow picture: f's solid draws, seen from the sun */
static int g_shdbg[8];
GEXPORT int gl_shadow_read(uint8_t *out) {   /* (test aid) the shadow picture, SH_SIZE x SH_SIZE RGBA, rows from the bottom */
    if (!g_shfbo) return 0;
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, g_shfbo); p_glReadPixels(0, 0, SH_SIZE, SH_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, out); g_cur_fb = 0; return SH_SIZE;
}
GEXPORT void gl_shadow_dbg(int *o) { memcpy(o, g_shdbg, sizeof g_shdbg); }   /* (test aid) last shadow pass: skinned draws seen, cast, skipped (blend, no z write, not lit) */
/* ---- ambient occlusion: a picture of how far away every point is (half size), then each world pixel looks at its
 * neighbours there: where nearby surfaces stand closer to the camera (a corner, the foot of a wall, under a crate) it
 * gets darker */
static GLuint g_aofbo, g_aotex, g_aodepth; static int g_ao_w, g_ao_h;
static int ao_target(int w, int h) {
    if (g_aofbo && g_ao_w == w && g_ao_h == h) return 1;
    if (g_aotex) p_glDeleteTextures(1, &g_aotex);
    p_glGenTextures(1, &g_aotex); p_glBindTexture(GL_TEXTURE_2D, g_aotex);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!g_aofbo) { p_glGenFramebuffers(1, &g_aofbo); p_glGenRenderbuffers(1, &g_aodepth); }
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_aofbo); p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_aotex, 0);
    p_glBindRenderbuffer(GL_RENDERBUFFER, g_aodepth); p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_aodepth);
    g_ao_w = w; g_ao_h = h; g_cur_fb = 0; ap_forget();
    return p_glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}
static float g_ao_wmax, g_aok;
static int lit_ao_pass__b(GLFb *f, int lo, const int *ext0, const int *extn);
static int lit_ao_pass(GLFb *f, int lo, const int *ext0, const int *extn) { uint64_t _t = PF_TSC(); int _r = lit_ao_pass__b(f, lo, ext0, extn); pf_add(2, _t); return _r; }
static int lit_ao_pass__b(GLFb *f, int lo, const int *ext0, const int *extn) {
    GLRec *R = &g_rec[g_rlast]; GLLit *L = &g_lit[g_rlast]; int fi = (int)(f - g_fbs); const double *VR = L->pj[L->main].V;
    int w = f->w * g_scale, h = f->h * g_scale; if (w < 8 || h < 8 || !p_glUniform1f || !ao_target(w, h)) return 0;   /* (full size: at half size its edges stepped and crawled when the camera moved) */
    g_ao_wmax = (float)(g_dmax_s > 0 ? g_dmax_s * 4.0 : 30000.0);
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_aofbo); p_glViewport(0, 0, w, h); g_cur_fb = 0; ap_forget();
    p_glDisable(GL_SCISSOR_TEST); p_glDisable(GL_BLEND); p_glColorMask(1, 1, 1, 1); p_glDepthMask(1);
    p_glClearColor(1, 1, 1, 1); p_glClearDepth(0.0); p_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    p_glEnable(GL_DEPTH_TEST); p_glDepthFunc(0x0206 /* GEQUAL: the game's nearer = bigger */);
    p_glUniform1i(u_lmode, 4); p_glUniform4f(u_lk, 0.f, 0.f, 1.f / g_ao_wmax, 0.f);
    p_glUniform1i(u_texon, 0); p_glUniform1i(u_atest, -1); p_glUniform1i(u_fogon, 0); p_glUniform1i(u_dbl, 0); int texon = 0, b0 = 0, bn = 0;
    for (int i = 0; i < R->nc; i++) {
        GLCmd *c = &R->c[i]; if (c->st.fbi != fi || c->kind || g_cullf[i] || !c->st.zmask || !lit_mode(&c->st, L, VR)) continue;
        if (c->st.zf != 6 && c->st.zf != 7) continue;
        if (c->st.bon) continue;                                     /* (see-through: hair edges, glass, ghosts) */
        int cut = c->st.texon && c->st.atest >= 0;                    /* (hair strands, fences: only where solid) */
        if (cut) {
            if (bn) { p_glDrawArrays(GL_TRIANGLES, b0 - lo, bn); bn = 0; }
            p_glBindTexture(GL_TEXTURE_2D, c->st.tex); p_glUniform1i(u_texon, 1); p_glUniform1i(u_tfunc, c->st.tfunc); p_glUniform1i(u_ta, c->st.ta);
            p_glUniform1i(u_atest, c->st.atest); p_glUniform1i(u_amask, c->st.amask); p_glUniform1i(u_aref, c->st.aref); texon = 1;
            p_glDrawArrays(GL_TRIANGLES, g_o0[i] - lo, c->n);
        } else {
            if (texon != 0) { if (bn) { p_glDrawArrays(GL_TRIANGLES, b0 - lo, bn); bn = 0; } p_glUniform1i(u_texon, 0); p_glUniform1i(u_atest, -1); texon = 0; }
            /* (untextured: draws next to each other in memory go out as one) */
            if (bn && b0 + bn == g_o0[i]) bn += c->n;
            else { if (bn) p_glDrawArrays(GL_TRIANGLES, b0 - lo, bn); b0 = g_o0[i]; bn = c->n; }
        }
        if (extn[i]) { if (bn) { p_glDrawArrays(GL_TRIANGLES, b0 - lo, bn); bn = 0; } p_glDrawArrays(GL_TRIANGLES, ext0[i] - lo, extn[i]); }
    }
    if (bn) p_glDrawArrays(GL_TRIANGLES, b0 - lo, bn);
    ap_forget(); return 1;
}
/* Characters shut inside something cast no shadow: a body put into a locker is still drawn by the game, inside the
 * closed locker, and its sun shadow came out onto the floor through the locker (only characters cast shadows; the
 * locker doesn't). Such a character lies wholly inside the box around one small closed object of the world - the
 * locker - measured along the world's own directions (so it is the same from every camera angle; measured in the
 * sun's picture it changed with the camera and the shadow came back at some angles): then his shadow is left out
 * (the PSP shows nothing of him either). Rooms, floors and walls are far bigger than a character and don't count. */
static uint8_t g_shide[FR_CMD]; static int g_encl_on = 1; static uint32_t g_encl_n[2];
GEXPORT void gl_enclosed(int on) { g_encl_on = on; }
GEXPORT void gl_enclosed_stats(uint32_t *o) { o[0] = g_encl_n[0]; o[1] = g_encl_n[1]; g_encl_n[0] = g_encl_n[1] = 0; }
static int wbox(int i, int n, float *b) {                          /* the draw's box in the world: x0 x1 y0 y1 z0 z1 */
    b[0] = b[2] = b[4] = 1e30f; b[1] = b[3] = b[5] = -1e30f; int ok = 0;
    for (int k = 0; k < n; k++) {
        const GLVtx *v = &g_lv[i + k]; if (!(v->w > 0.f)) continue;
        double l[3] = {v->lx * g_esc[0] + g_emin[0], v->ly * g_esc[1] + g_emin[1], v->lz * g_esc[2] + g_emin[2]};
        for (int a = 0; a < 3; a++) {
            float q = (float)(g_eBR[a] * l[0] + g_eBR[3 + a] * l[1] + g_eBR[6 + a] * l[2]);
            if (q < b[2 * a]) b[2 * a] = q; if (q > b[2 * a + 1]) b[2 * a + 1] = q;
        }
        ok++;
    }
    return ok;
}
static void encl_mark(const GLRec *R, int fi) {
    memset(g_shide, 0, (size_t)R->nc);
    if (!g_encl_on || g_cast_all) return;
    const GLLit *L = &g_lit[g_rlast]; const double *VR = L->pj[L->main].V;
    /* the characters: runs of draws with bones. A new one where the same body parts begin again, or another model's
     * (far away in the game's memory - what he carries is drawn before him and counted as part of him otherwise:
     * the box around both fitted no locker) */
    enum { MAXG = 96 }; static float gb[MAXG][6]; static int gi0[MAXG], gi1[MAXG]; int ng = 0, g0 = -1; uint32_t pva = 0;
    for (int i = 0; i <= R->nc; i++) {
        const GLCmd *c = i < R->nc ? &R->c[i] : 0;
        int sk = c && !c->kind && c->st.fbi == fi && c->st.skin && !g_cullf[i] && lit_mode(&c->st, L, VR);
        if ((!sk || c->st.va <= pva || c->st.va - pva > 0x40000u) && g0 >= 0) { gi1[ng] = i; if (gb[ng][1] > gb[ng][0]) ng++; g0 = -1; g_encl_n[0]++; }
        if (!sk || ng >= MAXG) continue;
        if (g0 < 0) { g0 = i; gi0[ng] = i; gb[ng][0] = gb[ng][2] = gb[ng][4] = 1e30f; gb[ng][1] = gb[ng][3] = gb[ng][5] = -1e30f; }
        float b[6];
        if (wbox(g_o0[i], c->n, b)) for (int a = 0; a < 3; a++) { if (b[2 * a] < gb[ng][2 * a]) gb[ng][2 * a] = b[2 * a]; if (b[2 * a + 1] > gb[ng][2 * a + 1]) gb[ng][2 * a + 1] = b[2 * a + 1]; }
        pva = c->st.va;
    }
    if (!ng) return;
    static uint8_t gh[MAXG]; memset(gh, 0, (size_t)ng);
    for (int i = 0; i < R->nc; i++) {                             /* the closed things of the world that could hold one */
        const GLCmd *c = &R->c[i];
        if (c->kind || c->st.fbi != fi || c->st.skin || c->st.through || c->st.bon || !c->st.zmask || g_cullf[i] || c->n < 12 || !lit_mode(&c->st, L, VR)) continue;
        float p[6]; if (!wbox(g_o0[i], 1, p)) continue;             /* (its first point: anywhere near a character?) */
        int near = 0;
        for (int g = 0; g < ng && !near; g++) if (!gh[g]) {
            near = 1; for (int a = 0; a < 3; a++) { float d = (gb[g][2 * a + 1] - gb[g][2 * a]) * 2.f + 1.f; if (p[2 * a] < gb[g][2 * a] - d || p[2 * a] > gb[g][2 * a + 1] + d) near = 0; }
        }
        if (!near) continue;
        float e[6]; if (!wbox(g_o0[i], c->n, e)) continue;
        float ed[3] = {e[1] - e[0], e[3] - e[2], e[5] - e[4]}, m = ed[0] < ed[1] ? ed[0] : ed[1]; if (ed[2] < m) m = ed[2]; m *= 0.12f;   /* (an elbow may poke through a little) */
        for (int g = 0; g < ng; g++) if (!gh[g]) {
            const float *q = gb[g]; float gd[3] = {q[1] - q[0], q[3] - q[2], q[5] - q[4]};
            if (e[0] <= q[0] + m && e[1] >= q[1] - m && e[2] <= q[2] + m && e[3] >= q[3] - m && e[4] <= q[4] + m && e[5] >= q[5] - m &&
                ed[0] * ed[1] * ed[2] <= 8.f * gd[0] * gd[1] * gd[2] && ed[0] <= 3.f * gd[0] + 1.f && ed[1] <= 3.f * gd[1] + 1.f && ed[2] <= 3.f * gd[2] + 1.f) {
                gh[g] = 1; for (int j = gi0[g]; j < gi1[g]; j++) g_shide[j] = 1; g_encl_n[1]++;
            }
        }
    }
}
static void lit_shadow_pass__b(GLFb *f, int lo, const int *ext0, const int *extn);
static void lit_shadow_pass(GLFb *f, int lo, const int *ext0, const int *extn) { uint64_t _t = PF_TSC(); lit_shadow_pass__b(f, lo, ext0, extn); pf_add(1, _t); }
static void lit_shadow_pass__b(GLFb *f, int lo, const int *ext0, const int *extn) {
    GLRec *R = &g_rec[g_rlast]; GLLit *L = &g_lit[g_rlast]; int fi = (int)(f - g_fbs); const double *VR = L->pj[L->main].V;
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_shfbo); p_glViewport(0, 0, SH_SIZE, SH_SIZE); g_cur_fb = 0; ap_forget();
    p_glDisable(GL_SCISSOR_TEST); p_glDisable(GL_BLEND); p_glColorMask(1, 1, 1, 1); p_glDepthMask(1);
    p_glClearColor(1, 1, 1, 1); p_glClearDepth(1.0); p_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    p_glEnable(GL_DEPTH_TEST); p_glDepthFunc(0x0201 /* LESS */);
    p_glUniform1i(u_lmode, 2); p_glUniform1i(u_fogon, 0); p_glUniform1i(u_dbl, 0);
    int texon = -1; memset(g_shdbg, 0, sizeof g_shdbg);
    encl_mark(R, fi);
    for (int i = 0; i < R->nc; i++) {
        GLCmd *c = &R->c[i];
        if (!c->kind && c->st.fbi == fi && c->st.skin) { g_shdbg[0]++; if (c->st.bon && c->st.bfix) g_shdbg[2]++; if (!c->st.zmask) g_shdbg[3]++; if (!lit_mode(&c->st, L, VR)) g_shdbg[4]++; if (c->st.pj < 0) g_shdbg[5]++; if (c->st.through) g_shdbg[6]++; }
        if (c->st.fbi != fi || c->kind || g_cullf[i] || !c->st.zmask || !lit_mode(&c->st, L, VR)) continue;
        if (c->st.bon && c->st.bfix) continue;                        /* (see-through: a ghost casts no shadow) */
        if (!g_cast_all && !c->st.skin) continue;                     /* (the world's own shadows are baked into it) */
        if (g_shide[i]) continue;                                     /* (a character shut inside something: a locker) */
        int cut = c->st.texon && c->st.atest >= 0;                    /* (leaves, fences: only where the picture is solid) */
        if (cut) {
            p_glBindTexture(GL_TEXTURE_2D, c->st.tex); p_glUniform1i(u_texon, 1); p_glUniform1i(u_tfunc, c->st.tfunc); p_glUniform1i(u_ta, c->st.ta);
            p_glUniform1i(u_atest, c->st.atest); p_glUniform1i(u_amask, c->st.amask); p_glUniform1i(u_aref, c->st.aref); texon = 1;
        } else if (texon != 0) { p_glUniform1i(u_texon, 0); p_glUniform1i(u_atest, -1); texon = 0; }
        p_glDrawArrays(GL_TRIANGLES, g_o0[i] - lo, c->n); if (c->st.skin) g_shdbg[1]++;
        if (extn[i]) p_glDrawArrays(GL_TRIANGLES, ext0[i] - lo, extn[i]);
    }
    ap_forget();
}
/* the picture shown last (before the window's scaling): in-between pictures start from it instead of the bare
 * background, so a gap at the screen edge while the camera turns (the game no longer draws what just left the
 * screen) shows what was there a moment ago instead of the empty background */
static GLuint g_lastfbo, g_lasttex; static int g_last_w, g_last_h, g_last_ok, g_fill_on = 0;
static void last_keep__b(GLuint srcfbo, int w, int h);
static void last_keep(GLuint srcfbo, int w, int h) { uint64_t _t = PF_TSC(); last_keep__b(srcfbo, w, h); pf_add(14, _t); }
static void last_keep__b(GLuint srcfbo, int w, int h) {
    if (!g_lastfbo || g_last_w != w || g_last_h != h) {
        if (g_lasttex) p_glDeleteTextures(1, &g_lasttex);
        p_glGenTextures(1, &g_lasttex); p_glBindTexture(GL_TEXTURE_2D, g_lasttex);
        p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (!g_lastfbo) p_glGenFramebuffers(1, &g_lastfbo);
        p_glBindFramebuffer(GL_FRAMEBUFFER, g_lastfbo); p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_lasttex, 0);
        g_last_w = w; g_last_h = h; ap_forget();
    }
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, srcfbo); p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_lastfbo);
    p_glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    g_last_ok = 1; g_cur_fb = 0;
}
static int g_dbg_skip0 = -1, g_dbg_skip1 = -1;
GEXPORT void gl_dbg_skipr(int a, int b) { g_dbg_skip0 = a; g_dbg_skip1 = b; }   /* (test aid) leave draws a..b-1 out of in-between pictures */
#if defined(__linux__) && defined(GL_TIMING)
#include <time.h>
static double g_rp_ms[4]; GEXPORT void gl_dbg_rp(double *o) { memcpy(o, g_rp_ms, sizeof g_rp_ms); memset(g_rp_ms, 0, sizeof g_rp_ms); }
static double now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }
#define RP_T(i, t0) (g_rp_ms[i] += now_ms() - (t0))
#else
#define now_ms() 0.0
#define RP_T(i, t0) ((void)(t0))
#endif
static int g_gh_n[4], g_ghost_on = 1;
GEXPORT void gl_ghost_on(int on) { g_ghost_on = on; }   /* (test aid) kept triangles in the in-betweens on/off */   /* (test aid) kept triangles: drawn, still behind, facing away, all */
GEXPORT void gl_dbg_ghost(int *o) { memcpy(o, g_gh_n, sizeof g_gh_n); memset(g_gh_n, 0, sizeof g_gh_n); }
/* where the PSP put a point on the screen (x, y, depth z) minus where this frame's own camera (K1) puts its world place w,
 * in r: the small error of working back from the screen to the world (the PSP rounds its screen places; the camera
 * read from the game is a little off for some models). 0 when it can't tell or it is more than such an error */
static inline void scr_resid(const double *K1, const double *w, const GLVtx *v, double *r) {
    double X = K1[0] * w[0] + K1[4] * w[1] + K1[8] * w[2] + K1[12], Y = K1[1] * w[0] + K1[5] * w[1] + K1[9] * w[2] + K1[13];
    double Z = K1[2] * w[0] + K1[6] * w[1] + K1[10] * w[2] + K1[14], W = K1[3] * w[0] + K1[7] * w[1] + K1[11] * w[2] + K1[15];
    r[0] = r[1] = r[2] = 0.0; if (!(W > 1e-6)) return;
    double rx = (double)v->x - X / W, ry = (double)v->y - Y / W, rz = (double)v->z - Z / W;
    if (rx > -2.0 && rx < 2.0 && ry > -2.0 && ry < 2.0 && rz > -64.0 && rz < 64.0) { r[0] = rx; r[1] = ry; r[2] = rz; }
}
static double pjr_Kr[4][MAX_PJ][12], pjr_K1[4][MAX_PJ][16], pjc_Kc[MAX_PJ][16], pjc_S[MAX_PJ][16]; static uint32_t pjr_mark[4][MAX_PJ], pjc_mark[MAX_PJ], g_replay_n;
static uint32_t cvc_gen[SRC_TAB], cvc_g; static CV cvc_val[SRC_TAB];   /* (replay: points already worked out in this draw) */
static int replay__b(GLFb *f, float t);
static int replay(GLFb *f, float t) { uint64_t _t = PF_TSC(); int _r = replay__b(f, t); pf_add(0, _t); return _r; }
static int replay__b(GLFb *f, float t) {
    GLRec *R = &g_rec[g_rlast]; int fi = (int)(f - g_fbs); double rp0 = now_ms();
    if (!R->ok || R->over || !(R->fbmask >> fi & 1)) return 0;
    static int ext0[FR_CMD], extn[FR_CMD];
    int lo = R->nv, hi = 0;
    for (int i = 0; i < R->nc; i++) { GLCmd *c = &R->c[i]; if (c->st.fbi != fi || c->kind) continue; if (c->v0 < lo) lo = c->v0; if (c->v0 + c->n > hi) hi = c->v0 + c->n; }
    int next = hi;                                              /* extra triangles from cutting go after hi */
    int op = lo;                                                /* (the kept draws' points, packed from lo) */
    ecam_reset(); g_replay_n++;
    const GLRec *Pe = &g_rec[g_rprev]; const GLLit *LRe = &g_lit[g_rlast], *LPe = &g_lit[g_rprev];
    for (int i = 0; i < R->nc; i++) {
        GLCmd *c = &R->c[i]; extn[i] = 0; g_cullf[i] = 0; g_o0[i] = op; if (c->st.fbi != fi || c->kind) continue;
        GLVtx *o = g_lv + op; const GLVtx *b = R->v + c->v0;
        /* ---- exact: from the world places and the game's cameras */
        /* (not for draws with no partner that don't stand still either - the old way keeps them where they are on the
         *  screen, which is right for what the camera follows: Snake and what he holds) */
        const GLCam *cm = (g_exact && Pe->ok && !c->st.through && !c->ssw && t < 0.999f && !(c->match == -1 && (c->st.skin || g_scr[i]))) ? ecam(c->st.pj, t) : 0;
        int exact = cm && !screen_space(b, c->n);
        if (!exact) { memcpy(o, b, sizeof(GLVtx) * (size_t)c->n); op += c->n; }   /* (the exact way writes every point itself) */
        if (exact) {
            const GLPj *pr = &LRe->pj[(int)c->st.pj], *pp = 0; int pjr = c->st.pj, pjp = -1;
            if (c->match >= 0 && c->match < Pe->nc) { int q = Pe->c[c->match].st.pj; if (q >= 0 && q < LPe->npj && LPe->pj[q].ok) { pp = &LPe->pj[q]; pjp = q; } }
            const GLPrev *pv = R->pv + c->v0; const uint8_t *pf = R->pf + c->v0; ext0[i] = next;
            /* (speed: the matrices belong to a camera set, not to a draw - worked out once per set: the recorded
             *  frame's per frame, the in-between camera's per picture. ~1700 draws use a handful of sets) */
            int ri = (int)(R - g_rec), pi_ = (int)(Pe - g_rec);
            if (pjr_mark[ri][pjr] != g_rgen[ri]) {
                world_mat(pr, pjr_Kr[ri][pjr]);
                GLCam c1; memcpy(c1.V, pr->V, sizeof c1.V); memcpy(c1.P, pr->P, sizeof c1.P);
                c1.sx = pr->sx; c1.sy = pr->sy; c1.ox = pr->ox; c1.oy = pr->oy; c1.sz = pr->sz; c1.oz = pr->oz; c1.ok = 1; cam_mat(&c1, pjr_K1[ri][pjr]);
                pjr_mark[ri][pjr] = g_rgen[ri];
            }
            if (pjp >= 0 && pjr_mark[pi_][pjp] != g_rgen[pi_]) {
                const GLPj *q = &LPe->pj[pjp]; world_mat(q, pjr_Kr[pi_][pjp]);
                GLCam c1; memcpy(c1.V, q->V, sizeof c1.V); memcpy(c1.P, q->P, sizeof c1.P);
                c1.sx = q->sx; c1.sy = q->sy; c1.ox = q->ox; c1.oy = q->oy; c1.sz = q->sz; c1.oz = q->oz; c1.ok = 1; cam_mat(&c1, pjr_K1[pi_][pjp]);
                pjr_mark[pi_][pjp] = g_rgen[pi_];
            }
            if (pjc_mark[pjr] != g_replay_n) {
                double *Kc_ = pjc_Kc[pjr], *S_ = pjc_S[pjr], *Kr_ = pjr_Kr[ri][pjr]; cam_mat(cm, Kc_);
                for (int r = 0; r < 4; r++) {
                    for (int col = 0; col < 3; col++) S_[4 * col + r] = Kc_[r] * Kr_[3 * col] + Kc_[4 + r] * Kr_[3 * col + 1] + Kc_[8 + r] * Kr_[3 * col + 2];
                    S_[12 + r] = Kc_[r] * Kr_[9] + Kc_[4 + r] * Kr_[10] + Kc_[8 + r] * Kr_[11] + Kc_[12 + r];
                }
                pjc_mark[pjr] = g_replay_n;
            }
            const double *Kr = pjr_Kr[ri][pjr], *Kp = pjp >= 0 ? pjr_Kr[pi_][pjp] : pjr_Kr[ri][pjr], *Kc = pjc_Kc[pjr], *S = pjc_S[pjr];
            /* (the in-between camera puts each point where its world place is seen from there. Working the world
             *  place back from the PSP's screen place is off by up to ~0.15 PSP pixel for some models, a different
             *  amount for each: neighbouring pieces - floor tiles, the parts of a wall - then met a little apart or
             *  overlapped in the in-between pictures but not in the game's own frames, and lines flickered along
             *  their edges. Each point keeps that error (K1: this frame's own camera), so the pieces meet as the
             *  PSP made them, in every picture) */
            /* (a rigid thing - not a character - either moved or didn't: a few of its points tell) */
            int still = !pp;
            if (pp && !c->st.skin && c->n >= 3) {
                still = 1;
                for (int k = 0; k < c->n && still; k += (c->n - 1) / 2 > 0 ? (c->n - 1) / 2 : 1) {
                    if (pf[k] != 0 || !(pv[k].W > 1e-3f)) continue;
                    double X = (double)b[k].x * b[k].w, Y = (double)b[k].y * b[k].w, W = b[k].w, wc[3], wp[3];
                    for (int a = 0; a < 3; a++) { wc[a] = Kr[a] * X + Kr[3 + a] * Y + Kr[6 + a] * W + Kr[9 + a]; wp[a] = Kp[a] * pv[k].X + Kp[3 + a] * pv[k].Y + Kp[6 + a] * pv[k].W + Kp[9 + a]; }
                    double dx = wp[0] - wc[0], dy = wp[1] - wc[1], dz = wp[2] - wc[2];
                    if (dx * dx + dy * dy + dz * dz > 0.25) still = 0;
                }
            }
            /* (speed) wholly off the screen for this picture's camera: left out. Standing still: the box around it now.
             * Moving (a soldier, a door): each point is somewhere between its place then and its place now, so the
             * box around it then as well - both wholly past the same edge. Characters cast shadows: left out only
             * when well away from the screen (their shadow isn't on it either) */
            if (g_cull_on && c->n >= 3 && !(c->gn > 0 && c->gok && g_ghost_on) && (!c->st.skin || !g_cast_all) && (still ? !c->st.skin || !pp : 1)) {
                float *bb = g_bb[ri][i];
                if (g_bb_mark[ri][i] != g_rgen[ri]) {                    /* (the boxes: once per recorded frame) */
                    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f, w0 = 1e30f, w1 = -1e30f;
                    for (int k = 0; k < c->n; k++) {
                        float x = b[k].x, y = b[k].y, w = b[k].w;
                        if (!(w > 1e-3f) || !(x > -1e9f && x < 1e9f && y > -1e9f && y < 1e9f && w < 1e18f)) { w0 = -1.f; break; }
                        if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; if (w < w0) w0 = w; if (w > w1) w1 = w;
                    }
                    bb[0] = x0; bb[1] = x1; bb[2] = y0; bb[3] = y1; bb[4] = w0; bb[5] = w1;
                    bb[10] = -1.f;
                    if (pp && w0 > 0.f) {                                /* (where its points were: X / W, Y / W, W) */
                        x0 = 1e30f; x1 = -1e30f; y0 = 1e30f; y1 = -1e30f; w0 = 1e30f; w1 = -1e30f;
                        for (int k = 0; k < c->n; k++) {
                            if (pf[k] != 0) continue;                    /* (no partner: only its place now counts) */
                            float w = pv[k].W; if (!(w > 1e-3f) || !(w < 1e18f)) { w0 = -1.f; break; }
                            float x = pv[k].X / w, y = pv[k].Y / w; if (!(x > -1e9f && x < 1e9f && y > -1e9f && y < 1e9f)) { w0 = -1.f; break; }
                            if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; if (w < w0) w0 = w; if (w > w1) w1 = w;
                        }
                        if (w0 > 1e29f) w0 = -2.f;                       /* (-2: none of its points had a partner) */
                        bb[6] = x0; bb[7] = x1; bb[8] = y0; bb[9] = y1; bb[10] = w0; bb[11] = w1;
                    }
                    g_bb_mark[ri][i] = g_rgen[ri];
                }
                int can = bb[4] > 0.f && (still || bb[10] > 0.f || bb[10] == -2.f);
                if (can) {
                    /* (every point of the draw is a mix of the box's 8 corners, and the camera's sum keeps mixes: what
                     *  all 8 corners are on the far side of - an edge of the clip rectangle, the camera itself - all of it is) */
                    /* (ambient occlusion looks at the distances around a pixel, up to 41 PSP pixels to the right and 72
                     *  down - also past the right and bottom edge of the screen, where the framebuffer goes on) */
                    const double m = c->st.skin ? 160.0 : 6.0, ao_m = (g_ao_on && g_light_on) ? 1.0 : 0.0;
                    const double L0 = c->st.sc[0] - m, T0 = c->st.sc[1] - m, R0 = c->st.sc[2] + m + 44.0 * ao_m, B0 = c->st.sc[3] + m + 76.0 * ao_m;
                    int out = 31;
                    for (int q = 0; q < 8 && out; q++) {
                        double W = bb[4 + (q >> 2)], X = bb[q & 1] * W, Y = bb[2 + ((q >> 1) & 1)] * W;
                        double cx = S[0] * X + S[4] * Y + S[8] * W + S[12], cy = S[1] * X + S[5] * Y + S[9] * W + S[13], cw = S[3] * X + S[7] * Y + S[11] * W + S[15];
                        int o2 = 0;
                        if (cx - L0 * cw < 0) o2 |= 1; if (cx - R0 * cw > 0) o2 |= 2; if (cy - T0 * cw < 0) o2 |= 4; if (cy - B0 * cw > 0) o2 |= 8; if (cw < 1e-4) o2 |= 16;
                        out &= o2;
                    }
                    if (out && !still && bb[10] > 0.f) {                 /* (and where it was, through that frame's world) */
                        for (int q = 0; q < 8 && out; q++) {
                            double W = bb[10 + (q >> 2)], X = bb[6 + (q & 1)] * W, Y = bb[8 + ((q >> 1) & 1)] * W, wq[3];
                            for (int a = 0; a < 3; a++) wq[a] = Kp[a] * X + Kp[3 + a] * Y + Kp[6 + a] * W + Kp[9 + a];
                            double cx = Kc[0] * wq[0] + Kc[4] * wq[1] + Kc[8] * wq[2] + Kc[12], cy = Kc[1] * wq[0] + Kc[5] * wq[1] + Kc[9] * wq[2] + Kc[13], cw = Kc[3] * wq[0] + Kc[7] * wq[1] + Kc[11] * wq[2] + Kc[15];
                            int o2 = 0;
                            if (cx - L0 * cw < 0) o2 |= 1; if (cx - R0 * cw > 0) o2 |= 2; if (cy - T0 * cw < 0) o2 |= 4; if (cy - B0 * cw > 0) o2 |= 8; if (cw < 1e-4) o2 |= 16;
                            out &= o2;
                        }
                    }
                    if (out) { g_cullf[i] = 1; extn[i] = 0; c->ex = 1; g_cull_n[0]++; g_cull_n[2] += (uint32_t)c->n; continue; }
                }
            }
            op += c->n; g_cull_n[1]++; g_cull_n[3] += (uint32_t)c->n;
            const double *K1 = pjr_K1[ri][pjr]; GLRes *rres = g_rres[ri] + c->v0;
            if (g_rres_mark[ri][i] != g_rgen[ri]) {                    /* (once per recorded frame) */
                for (int k = 0; k < c->n; k++) {
                    double X = (double)b[k].x * b[k].w, Y = (double)b[k].y * b[k].w, W = b[k].w, wc[3], r[3];
                    for (int a = 0; a < 3; a++) wc[a] = Kr[a] * X + Kr[3 + a] * Y + Kr[6 + a] * W + Kr[9 + a];
                    scr_resid(K1, wc, &b[k], r); rres[k].x = (float)r[0]; rres[k].y = (float)r[1]; rres[k].z = (float)r[2];
                }
                g_rres_mark[ri][i] = g_rgen[ri];
            }
            /* (speed) a point used by several triangles (strips, shared corners: ~3 times each) is worked out once */
            const int *srcs = R->src + c->v0;
            if (++cvc_g == 0) { memset(cvc_gen, 0, sizeof cvc_gen); cvc_g = 1; }
            for (int k = 0; k + 2 < c->n; k += 3) {
                CV tri[3]; int in = 1;
                if (still) {
                    /* (speed: most points of a picture - the world standing still - come this way, ~230000 of 280000
                     *  in a busy place. Written straight into the picture's list, the same arithmetic as before; the
                     *  table of points already worked out cost more to look up (11 MB, all over memory) than the sum) */
                    GLVtx *oo = &o[k];
                    for (int q = 0; q < 3; q++) {
                        const GLVtx *v = &b[k + q]; GLVtx *d = &oo[q];
                        double X = (double)v->x * v->w, Y = (double)v->y * v->w, W = v->w;
                        float cx = (float)(S[0] * X + S[4] * Y + S[8] * W + S[12]), cy = (float)(S[1] * X + S[5] * Y + S[9] * W + S[13]);
                        float cz = (float)(S[2] * X + S[6] * Y + S[10] * W + S[14]), cw = (float)(S[3] * X + S[7] * Y + S[11] * W + S[15]);
                        { const GLRes *rr = &rres[k + q]; cx += rr->x * cw; cy += rr->y * cw; cz += rr->z * cw; }
                        if (!(cw > 1e-4f)) { in = 0; break; }
                        float iw = 1.f / cw;
                        d->x = cx * iw; d->y = cy * iw; d->z = cz * iw; d->w = cw; d->u = v->u; d->v = v->v;
                        d->r = v->r; d->g = v->g; d->b = v->b; d->a = v->a; d->fog = v->fog; d->lx = d->ly = 0.f; d->lz = -1.f;
                        if (pp && pf[k + q] == 0) { d->u = uv_lerp(pv[k + q].u, v->u, t); d->v = uv_lerp(pv[k + q].v, v->v, t); }
                    }
                    if (in) continue;
                    in = 1;                                              /* (a corner behind this camera: cut, the slow way) */
                    for (int q = 0; q < 3; q++) {
                        const GLVtx *v = &b[k + q]; CV *d = &tri[q];
                        double X = (double)v->x * v->w, Y = (double)v->y * v->w, W = v->w;
                        d->X = (float)(S[0] * X + S[4] * Y + S[8] * W + S[12]); d->Y = (float)(S[1] * X + S[5] * Y + S[9] * W + S[13]);
                        d->Z = (float)(S[2] * X + S[6] * Y + S[10] * W + S[14]); d->W = (float)(S[3] * X + S[7] * Y + S[11] * W + S[15]);
                        { const GLRes *rr = &rres[k + q]; d->X += rr->x * d->W; d->Y += rr->y * d->W; d->Z += rr->z * d->W; }
                        d->fog = v->fog; d->u = v->u; d->v = v->v; d->r = v->r; d->g = v->g; d->b = v->b; d->a = v->a;
                        if (pp && pf[k + q] == 0) { d->u = uv_lerp(pv[k + q].u, v->u, t); d->v = uv_lerp(pv[k + q].v, v->v, t); }
                        if (!(d->W > 1e-4f)) in = 0;
                    }
                    goto made;
                }
                for (int q = 0; q < 3; q++) {
                    const GLVtx *v = &b[k + q]; CV *d = &tri[q]; double wc[3], wp[3], wt[3];
                    int sid = srcs[k + q]; if ((unsigned)sid < SRC_TAB && cvc_gen[sid] == cvc_g) { *d = cvc_val[sid]; d->r = v->r; d->g = v->g; d->b = v->b; d->a = v->a; if (!(d->W > 1e-4f)) in = 0; continue; }   /* (colour: per triangle with flat shading) */
                    double X = (double)v->x * v->w, Y = (double)v->y * v->w, W = v->w;
                    for (int a = 0; a < 3; a++) wc[a] = Kr[a] * X + Kr[3 + a] * Y + Kr[6 + a] * W + Kr[9 + a];
                    int moved = 0; float fog0 = v->fog;
                    if (pp && pf[k + q] == 0 && pv[k + q].W > 1e-3f) {      /* (paired: where it was in the world) */
                        double PX = pv[k + q].X, PY = pv[k + q].Y, PW = pv[k + q].W;
                        for (int a = 0; a < 3; a++) wp[a] = Kp[a] * PX + Kp[3 + a] * PY + Kp[6 + a] * PW + Kp[9 + a];
                        double dx = wp[0] - wc[0], dy = wp[1] - wc[1], dz = wp[2] - wc[2], d2 = dx * dx + dy * dy + dz * dz;
                        /* (still - or a jump no runner makes in a frame: a wrong partner) */
                        if (d2 > 0.25 && d2 < 6000.0 * 6000.0) { moved = 1; fog0 = pv[k + q].fog; }
                    }
                    if (moved) for (int a = 0; a < 3; a++) wt[a] = wp[a] + (wc[a] - wp[a]) * t;
                    else { wt[0] = wc[0]; wt[1] = wc[1]; wt[2] = wc[2]; }
                    d->X = (float)(Kc[0] * wt[0] + Kc[4] * wt[1] + Kc[8] * wt[2] + Kc[12]); d->Y = (float)(Kc[1] * wt[0] + Kc[5] * wt[1] + Kc[9] * wt[2] + Kc[13]);
                    d->Z = (float)(Kc[2] * wt[0] + Kc[6] * wt[1] + Kc[10] * wt[2] + Kc[14]); d->W = (float)(Kc[3] * wt[0] + Kc[7] * wt[1] + Kc[11] * wt[2] + Kc[15]);
                    { const GLRes *rr = &rres[k + q]; d->X += rr->x * d->W; d->Y += rr->y * d->W; d->Z += rr->z * d->W; }
                    d->fog = fog0 + (v->fog - fog0) * t;
                    d->u = v->u; d->v = v->v; d->r = v->r; d->g = v->g; d->b = v->b; d->a = v->a;
                    if (pp && pf[k + q] == 0) { d->u = uv_lerp(pv[k + q].u, v->u, t); d->v = uv_lerp(pv[k + q].v, v->v, t); }
                    if ((unsigned)sid < SRC_TAB) { cvc_gen[sid] = cvc_g; cvc_val[sid] = *d; }
                    if (!(d->W > 1e-4f)) in = 0;
                }
              made:
                if (in) { for (int q = 0; q < 3; q++) o[k + q] = cv_vtx(&tri[q]); continue; }
                CV p2[8]; int n2 = clip_plane(tri, 3, p2, 0);
                if (n2 < 3) { o[k + 1] = o[k]; o[k + 2] = o[k]; continue; }
                o[k] = cv_vtx(&p2[0]); o[k + 1] = cv_vtx(&p2[1]); o[k + 2] = cv_vtx(&p2[2]);
                for (int q = 3; q < n2 && next + 3 <= FR_VTX; q++) { g_lv[next++] = cv_vtx(&p2[0]); g_lv[next++] = cv_vtx(&p2[q - 1]); g_lv[next++] = cv_vtx(&p2[q]); }
            }
            /* the triangles the PSP left out (a corner behind the camera in this frame) that this camera sees whole -
             * for the world standing still (a wall beside the camera as it swings: else a pale block at the edge) */
            if (c->gn > 0) g_gh_n[3] += c->gn / 3;
            if (c->gn > 0 && c->gok && g_ghost_on && !c->st.skin && (still || c->n < 3)) {
                const GLVtx *gb = R->gv + c->g0; const uint8_t *gc = R->gc + c->g0 / 3;
                for (int k = 0; k + 2 < c->gn && next + 3 <= FR_VTX; k += 3) {
                    CV tri[3]; int in = 1;
                    for (int q = 0; q < 3 && in; q++) {
                        const GLVtx *v = &gb[k + q]; CV *d = &tri[q];
                        double X = (double)v->x * v->w, Y = (double)v->y * v->w, W = v->w;
                        d->X = (float)(S[0] * X + S[4] * Y + S[8] * W + S[12]); d->Y = (float)(S[1] * X + S[5] * Y + S[9] * W + S[13]);
                        d->Z = (float)(S[2] * X + S[6] * Y + S[10] * W + S[14]); d->W = (float)(S[3] * X + S[7] * Y + S[11] * W + S[15]);
                        d->fog = v->fog; d->u = v->u; d->v = v->v; d->r = v->r; d->g = v->g; d->b = v->b; d->a = v->a;
                        if (!(d->W > 1e-3f)) in = 0;                /* (still partly behind: the PSP would leave it out too) */
                    }
                    if (!in) { g_gh_n[1]++; continue; }
                    if (gc[k / 3]) {                                 /* (the game's back-face rule, seen from this camera) */
                        double x0 = tri[0].X / tri[0].W, y0 = tri[0].Y / tri[0].W, x1 = tri[1].X / tri[1].W, y1 = tri[1].Y / tri[1].W, x2 = tri[2].X / tri[2].W, y2 = tri[2].Y / tri[2].W;
                        double ar = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
                        if (gc[k / 3] == 1 ? !(ar > 0) : !(ar < 0)) { g_gh_n[2]++; continue; }
                    }
                    g_gh_n[0]++;
                    for (int q = 0; q < 3; q++) g_lv[next++] = cv_vtx(&tri[q]);
                }
            }
            extn[i] = next - ext0[i]; c->ex = 1;
            continue;
        }
        c->ex = 0;
        if (c->match < 0 && c->match != -3) continue;          /* (-3: moved like the camera) */
        const GLPrev *pv = R->pv + c->v0; const uint8_t *pf = R->pf + c->v0; ext0[i] = next;
        for (int k = 0; k + 2 < c->n; k += 3) {
            if (pf[k] == 1 || pf[k + 1] == 1 || pf[k + 2] == 1) { o[k + 1] = o[k]; o[k + 2] = o[k]; continue; }  /* wasn't there */
            /* a corner was behind the camera the frame before: moving it from there would sweep the triangle across
             * the screen (a flash). Drawn where it is now */
            if (!(pv[k].W > 0.05f * b[k].w) || !(pv[k + 1].W > 0.05f * b[k + 1].w) || !(pv[k + 2].W > 0.05f * b[k + 2].w)) continue;
            CV tri[3]; int in = 1;
            for (int q = 0; q < 3; q++) {
                const GLPrev *a = &pv[k + q]; const GLVtx *v = &b[k + q]; CV *d = &tri[q];
                /* decided per point, from the point alone: a point shared by two triangles then moves the same way in
                 * both (deciding per triangle opened cracks along shared edges near the camera) */
                int scr = a->W > 1e-4f && (a->Z > 65535.f * a->W || v->z > 65535.f);
                d->W = a->W + (v->w - a->W) * t; d->fog = a->fog + (v->fog - a->fog) * t;
                d->u = v->u; d->v = v->v; d->r = v->r; d->g = v->g; d->b = v->b; d->a = v->a;
                if (c->match >= 0 && pf[k + q] == 0) { d->u = uv_lerp(a->u, v->u, t); d->v = uv_lerp(a->v, v->v, t); }
                if (scr && a->W > 1e-4f) {
                    /* a point past the near plane in one of the frames is far off the screen; moving it in clip space
                     * would sweep it across the screen, so its whole triangle moves on the screen instead */
                    float ia = 1.f / a->W, x = a->X * ia + (v->x - a->X * ia) * t, y = a->Y * ia + (v->y - a->Y * ia) * t, z = a->Z * ia + (v->z - a->Z * ia) * t;
                    d->X = x * d->W; d->Y = y * d->W; d->Z = z * d->W;
                } else {
                    d->X = a->X + (v->x * v->w - a->X) * t; d->Y = a->Y + (v->y * v->w - a->Y) * t; d->Z = a->Z + (v->z * v->w - a->Z) * t;
                }
                if (!(d->W > 1e-4f)) in = 0;                    /* (behind the camera: only from the camera guess) */
            }
            if (in) { for (int q = 0; q < 3; q++) o[k + q] = cv_vtx(&tri[q]); continue; }
            CV p2[8]; int n2 = clip_plane(tri, 3, p2, 0);
            if (n2 < 3) { o[k + 1] = o[k]; o[k + 2] = o[k]; continue; }                    /* all behind it */
            o[k] = cv_vtx(&p2[0]); o[k + 1] = cv_vtx(&p2[1]); o[k + 2] = cv_vtx(&p2[2]);
            for (int q = 3; q < n2 && next + 3 <= FR_VTX; q++) {                           /* the rest as a fan */
                g_lv[next++] = cv_vtx(&p2[0]); g_lv[next++] = cv_vtx(&p2[q - 1]); g_lv[next++] = cv_vtx(&p2[q]);
            }
        }
        extn[i] = next - ext0[i];
    }
    for (int i = 0; i < R->nc; i++) if (R->c[i].kind || R->c[i].st.fbi != fi || (R->c[i].match < 0 && R->c[i].match != -3 && !R->c[i].ex)) extn[i] = 0;
    /* what left the screen in this frame (missing from it): from the frame before, moved on by the camera */
    const GLRec *Pl = &g_rec[g_rprev]; static int lo0[FR_CMD], lon[FR_CMD]; int nl = (t < 0.999f && Pl->ok) ? g_rnlost[g_rlast] : 0;
    for (int L = 0; L < nl; L++) {
        const GLCmd *c = &Pl->c[g_rlost[g_rlast][L]]; lon[L] = 0; lo0[L] = next;
        if (next + c->n > FR_VTX || g_fbs[c->st.fbi].w != f->w || g_fbs[c->st.fbi].h != f->h) continue;   /* (the game draws every other frame into its other framebuffer) */
        const GLVtx *b = Pl->v + c->v0; const GLPrev *fw = g_rfw[g_rprev] + c->v0;
        /* (exact: from its place in the world, seen by the in-between camera) */
        const GLCam *lc = g_exact ? ecam(g_lit[g_rlast].main, t) : 0; const GLPj *lp = 0;
        if (lc && c->st.pj >= 0 && c->st.pj < g_lit[g_rprev].npj && g_lit[g_rprev].pj[(int)c->st.pj].ok) lp = &g_lit[g_rprev].pj[(int)c->st.pj];
        for (int k = 0; k + 2 < c->n; k += 3) {
            CV tri[3]; int in = 1;
            if (lp) {
                for (int q = 0; q < 3; q++) {
                    const GLVtx *a = &b[k + q]; CV *d = &tri[q]; double wp[3];
                    lit_world(lp, lp->V, a->x, a->y, a->w, wp); cam_proj(lc, wp, d);
                    d->fog = a->fog; d->u = a->u; d->v = a->v; d->r = a->r; d->g = a->g; d->b = a->b; d->a = a->a;
                    if (!(d->W > 1e-3f)) in = 0;
                }
                if (!in) continue;
                for (int q = 0; q < 3; q++) g_lv[next++] = cv_vtx(&tri[q]);
                continue;
            }
            for (int q = 0; q < 3; q++) {
                const GLVtx *a = &b[k + q]; const GLPrev *v = &fw[k + q]; CV *d = &tri[q];
                d->W = a->w + (v->W - a->w) * t; d->X = a->x * a->w + (v->X - a->x * a->w) * t; d->Y = a->y * a->w + (v->Y - a->y * a->w) * t;
                d->Z = a->z * a->w + (v->Z - a->z * a->w) * t; d->fog = a->fog + (v->fog - a->fog) * t;
                d->u = a->u; d->v = a->v; d->r = a->r; d->g = a->g; d->b = a->b; d->a = a->a;
                if (!(d->W > 1e-3f)) in = 0;
            }
            if (!in) continue;
            for (int q = 0; q < 3; q++) g_lv[next++] = cv_vtx(&tri[q]);
        }
        lon[L] = next - lo0[L]; g_lost_tris += lon[L] / 3;
    }
    if (op < hi) {                                              /* (the extra triangles: right after the packed points) */
        int sh = hi - op;
        if (next > hi && op < hi) for (int q = 0; q < next - hi; q++) g_lv[op + q] = g_lv[hi + q];   /* (moved down: copied front to back) */
        for (int i = 0; i < R->nc; i++) if (extn[i]) ext0[i] -= sh;
        for (int L = 0; L < nl; L++) lo0[L] -= sh;
        next -= sh;
    }
    RP_T(0, rp0); double rp1 = now_ms();
    int lit = lit_place(f, t, lo, next, ext0, extn) && sh_target();
    RP_T(1, rp1);
    /* the gap filler: the last picture shown, drawn at the end over everything farther away than the nearest solid
     * part of the world (only background: sky, haze cards, nothing) */
    /* (switched off: in some areas the depth test can't tell background from walls and the edges showed an old
     * picture. Kept for a better version: a real coverage test) */
    int fill = g_fill_on && t < 0.999f && g_last_ok && g_last_w == f->w * g_scale && g_last_h == f->h * g_scale && next + 24 <= FR_VTX, fill0 = next;
    if (fill) {
        float zmin = 1e9f;
        for (int i = 0; i < R->nc; i++) {
            const GLCmd *c = &R->c[i]; if (c->st.fbi != fi || c->kind || c->st.through || c->st.bon || !c->st.zmask || !c->st.zon) continue;
            if (c->st.zf != 6 && c->st.zf != 7) { zmin = -1.f; break; }   /* (nearer = smaller here: don't fill at all) */
            for (const GLVtx *v = R->v + c->v0, *e = v + c->n; v < e; v++)
                if (v->w > 0 && v->z >= 0 && v->z <= 65535 && v->z < zmin) zmin = v->z;   /* (all of it, also off the screen: a big wall's corners can all be off it) */
        }
        if (zmin > 200.f && zmin < 1e8f) {
            float z = zmin - 100.f, W = (float)f->w, H = (float)f->h;
            /* only bands along the screen edges (where things leave the screen): never the middle of the picture */
            static const float P[24][2] = {{0, 0}, {72, 0}, {0, 272}, {72, 0}, {72, 272}, {0, 272},
                                           {408, 0}, {480, 0}, {408, 272}, {480, 0}, {480, 272}, {408, 272},
                                           {72, 0}, {408, 0}, {72, 40}, {408, 0}, {408, 40}, {72, 40},
                                           {72, 232}, {408, 232}, {72, 272}, {408, 232}, {408, 272}, {72, 272}};
            for (int k = 0; k < 24; k++) { GLVtx *o = &g_lv[next++]; memset(o, 0, sizeof *o); o->x = P[k][0]; o->y = P[k][1]; o->z = z; o->w = 1.f;
                o->u = P[k][0] / W; o->v = 1.f - P[k][1] / H; o->r = o->g = o->b = o->a = 255; o->fog = 1.f; o->lz = -1.f; }
        } else fill = 0;
    }
    p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    if (next > lo) p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(next - lo) * sizeof(GLVtx), g_lv + lo, GL_STREAM_DRAW);
    double rp2 = now_ms();
    if (lit) lit_shadow_pass(f, lo, ext0, extn);
    int ao = lit && g_ao_on && lit_ao_pass(f, lo, ext0, extn);
    RP_T(2, rp2);
    shadow_for(f);
    p_glBindFramebuffer(GL_FRAMEBUFFER, f->smsfbo ? f->smsfbo : f->sfbo); p_glViewport(0, 0, f->w * g_scale, f->h * g_scale);
    p_glUniform2f(u_fbsize, (float)f->w, (float)f->h);
    int lm = 0, aon = 0; const double *MV = lit ? g_lit[g_rlast].pj[g_lit[g_rlast].main].V : 0;
    if (lit) {
        p_glActiveTexture(GL_TEXTURE0 + 1); p_glBindTexture(GL_TEXTURE_2D, g_shtex); p_glActiveTexture(GL_TEXTURE0); ap_forget();
        GLLit *L = &g_lit[g_rlast];
        { double tz = L->bsize / SH_SIZE; if (tz < 2.5) tz = 2.5;              /* (bias step: a shadow texel, at least 2.5 world units) */
          /* lk.z: on characters, shadows of anything closer than CHAR_SELF along the sun's direction are left out: their
           * own head, arms, gear (low-poly, they gave hard triangles under the chin and blotches on bare skin) */
          p_glUniform4f(u_lk, g_lk_shadow, g_lk_sun, (float)(CHAR_SELF / L->zsize), (float)(tz / L->zsize)); }
        p_glUniform4f(u_lcam, g_lcam[0], g_lcam[1], g_lcam[2], g_lcam[3]);
        p_glUniform1f(u_aos, 0.f);
        if (ao) {                                            /* (radius about 120 world units, as a share of the far distance) */
            p_glActiveTexture(GL_TEXTURE0 + 2); p_glBindTexture(GL_TEXTURE_2D, g_aotex); p_glActiveTexture(GL_TEXTURE0); ap_forget();
            p_glUniform1f(u_aox, 1.f / (float)(f->w * g_scale)); p_glUniform1f(u_aoy, 1.f / (float)(f->h * g_scale)); g_aok = 0.95f * 180.f / g_ao_wmax; p_glUniform1f(u_aos, g_aok); aon = 1;
        }
    }
    p_glUniform1i(u_lmode, 0);
    int lost_done = !nl, world = 0;
    for (int i = 0; i < R->nc; i++) {
        GLCmd *c = &R->c[i]; if (c->st.fbi != fi) continue;
        if (!lost_done && world && (c->kind ? (c->bits & GL_DEPTH_BUFFER_BIT) != 0 : c->st.through)) {   /* (after the world, before the game's 2D on top or a depth clear: things drawn after one aren't hidden by walls) */
            if (lm) { p_glUniform1i(u_lmode, 0); lm = 0; }
            if (aon) { p_glUniform1f(u_aos, 0.f); aon = 0; }
            for (int L = 0; L < nl; L++) if (lon[L]) { GLSt st = Pl->c[g_rlost[g_rlast][L]].st; if (g_lost_on == 2) { st.texon = 0; st.zon = 0; } apply_state(&st, f->h); p_glDrawArrays(GL_TRIANGLES, lo0[L] - lo, lon[L]); }
            lost_done = 1;
        }
        if (!c->kind && !c->st.through) world = 1;
        if (!c->kind && g_cullf[i]) continue;                    /* (off this picture's screen) */
        if (g_dbg_skip0 >= 0 && !c->kind) { int di = 0; for (int q = 0; q < i; q++) di += !R->c[q].kind; if (g_dbg_skip1 > g_dbg_skip0 ? (di >= g_dbg_skip0 && di < g_dbg_skip1) : !(di >= g_dbg_skip1 && di < g_dbg_skip0)) continue; }   /* (test aid; a > b: only b..a-1) */
        if (lit && !c->kind) {
            int m = lit_mode(&c->st, &g_lit[g_rlast], MV); if (m != lm) { p_glUniform1i(u_lmode, m); lm = m; }
            int a = ao && !c->st.bon; if (a != aon) { p_glUniform1f(u_aos, a ? g_aok : 0.f); aon = a; }
        }
        if (c->kind == 1) {
            const int *q = c->st.sc; p_glEnable(GL_SCISSOR_TEST); ap_forget();
            p_glScissor(q[0] * g_scale, (f->h - q[3]) * g_scale, (q[2] - q[0]) * g_scale, (q[3] - q[1]) * g_scale);
            if (c->bits & GL_COLOR_BUFFER_BIT) { p_glColorMask(1, 1, 1, 1); p_glClearColor(c->cc[0], c->cc[1], c->cc[2], c->cc[3]); }
            if (c->bits & GL_DEPTH_BUFFER_BIT) { p_glDepthMask(1); p_glClearDepth(c->cd); }
            if (c->bits & GL_STENCIL_BUFFER_BIT) { p_glStencilMask(0xFF); p_glClearStencil((int)(c->cc[3] * 255.f + 0.5f)); }
            p_glClear(c->bits);
        } else {
            apply_state(&c->st, f->h);
            if (c->n) p_glDrawArrays(GL_TRIANGLES, g_o0[i] - lo, c->n);
            if (extn[i]) p_glDrawArrays(GL_TRIANGLES, ext0[i] - lo, extn[i]);
        }
    }
    if (!lost_done) {
        if (lm) { p_glUniform1i(u_lmode, 0); lm = 0; }
        if (aon) p_glUniform1f(u_aos, 0.f);
        for (int L = 0; L < nl; L++) if (lon[L]) { GLSt st = Pl->c[g_rlost[g_rlast][L]].st; if (g_lost_on == 2) { st.texon = 0; st.zon = 0; } apply_state(&st, f->h); p_glDrawArrays(GL_TRIANGLES, lo0[L] - lo, lon[L]); }
    }
    if (lm) p_glUniform1i(u_lmode, 0);
    if (fill) {
        GLSt st; memset(&st, 0, sizeof st); st.sc[2] = 480; st.sc[3] = 272; st.zon = 1; st.zf = 7; st.zmask = 0;
        st.tex = g_lasttex; st.clu = st.clv = 1; st.texon = 1; st.tfunc = 3; st.atest = -1;
        apply_state(&st, f->h); p_glDrawArrays(GL_TRIANGLES, fill0 - lo, 24);
    }
    resolve(f->smsfbo, f->sfbo, f->w * g_scale, f->h * g_scale);
    g_cur_fb = 0; g_batch_gen = 0xFFFFFFFF;
    return 1;
}

/* (log) in-between pictures: made, not made because the camera cut, nothing moved / no partners, too many rejected */
static int g_pk[4];
GEXPORT void gl_present_kinds(int *o) { for (int i = 0; i < 4; i++) { o[i] = g_pk[i]; g_pk[i] = 0; } }
static int g_older;
static int present_impl2(GEState *g, uint32_t addr, int stride, int fmt, int win_w, int win_h, float t);
/* t < 0: the moment lies between the two frames before the last (-1 = the one before the last, 0 = the last but one):
 * those are drawn instead. (The game's newest frame came early: the pair it replaced is still the right one) */
static int g_movie;
static int present_impl__b(GEState *g, uint32_t addr, int stride, int fmt, int win_w, int win_h, float t);
static int present_impl(GEState *g, uint32_t addr, int stride, int fmt, int win_w, int win_h, float t) { uint64_t _t = pf_top_begin(); int _r = present_impl__b(g, addr, stride, fmt, win_w, win_h, t); pf_top_end(1, _t); return _r; }
static int present_impl__b(GEState *g, uint32_t addr, int stride, int fmt, int win_w, int win_h, float t) {
    { uint32_t a = addr & 0x1FFFFF; if (a != g_shown[0]) { g_shown[1] = g_shown[0]; g_shown[0] = a; } }
    if (g_movie) return present_impl2(g, addr, stride, fmt, win_w, win_h, 1.f);   /* (movies: as they are, no in-between pictures) */
    if (g_rec_on && rec_2d_only(&g_rec[g_rlast])) return present_impl2(g, addr, stride, fmt, win_w, win_h, 1.f);
    if (!(t < 0.f) || !g_rec_on || !g_rec[g_rprev].ok || !g_rec[g_rprev2].ok || g_rec[g_rprev].over) return present_impl2(g, addr, stride, fmt, win_w, win_h, t < 0.f ? 0.f : t);
    if (t < -1.f) t = -1.f;
    /* (a moment right on one of the two older frames - the original frame rate shows those: that frame itself, as the
     *  game drew it. Made from the frame after it, moved back, it lacked what the game had stopped drawing at the
     *  screen edges by then - the pale blocks on camera swings, at the original frame rate too) */
    int whole = 0;
    if (t + 1.f < 0.02f && !g_rec[g_rprev2].over) whole = 2;      /* the frame two back */
    else if (t + 1.f > 0.98f) whole = 1;                          /* the frame before the last */
    /* that frame was drawn into the game's other framebuffer: the one of the same size it drew most into */
    GLFb *f = 0; for (int i = 0; i < g_nfb; i++) if (g_fbs[i].addr == (addr & 0x1FFFFF)) f = &g_fbs[i];
    const GLRec *P = &g_rec[whole == 2 ? g_rprev2 : g_rprev]; static int cnt[MAX_FB]; memset(cnt, 0, sizeof cnt);
    for (int i = 0; i < P->nc; i++) if (!P->c[i].kind && P->c[i].st.fbi < MAX_FB) cnt[P->c[i].st.fbi]++;
    int best = -1; for (int i = 0; i < g_nfb; i++) if (cnt[i] && (!f || (g_fbs[i].w == f->w && g_fbs[i].fmt == f->fmt)) && (best < 0 || cnt[i] > cnt[best])) best = i;
    if (best < 0) return present_impl2(g, addr, stride, fmt, win_w, win_h, 0.f);
    int sl = g_rlast, sp = g_rprev; g_older = 1;
    if (whole == 2) { g_rlast = g_rprev2; g_rprev = sl; }          /* (g_rprev isn't looked at for a whole frame, t = 1) */
    else { g_rlast = g_rprev; g_rprev = g_rprev2; }
    int r = present_impl2(g, (addr & ~0x1FFFFFu) | g_fbs[best].addr, stride, fmt, win_w, win_h, whole ? 1.f : t + 1.f);
    g_rlast = sl; g_rprev = sp; g_older = 0; return r;
}
/* Scene changes: the game sometimes shows a framebuffer it hasn't drawn into yet (while it loads the next scene),
 * so what is in video memory there is leftover data - texture bytes, read as a picture: black on top and coloured
 * horizontal stripes ("TV snow") below. A real picture changes about as much across as it does down; that leftover
 * data changes far more from row to row. Such a picture is shown as black instead (the scene is fading anyway). */
static int g_garbage_hidden, g_movie;
static void mvw_reset(void) { for (int i = 0; i < 8; i++) g_mvw[i] = 0xFFFFFFFFu; }
GEXPORT void gl_movie(int on) { if (on && !g_movie) mvw_reset(); g_movie = on; }   /* play.py: a movie is playing (its pictures are written straight into video memory) */
/* HD movies: play.py hands over the HD version of the movie picture just written into video memory at 'dest'
 * (rgb = 0: none for this one - the original is shown). While the game shows that framebuffer as it is, the HD
 * picture is shown in its place. */
typedef struct { uint32_t addr; GLuint tex; int ok, w, h; uint32_t age; } GLMovieHD;
static GLMovieHD g_mvhd[4]; static uint32_t g_mvhd_age; static int g_mvhd_shown;
GEXPORT void gl_movie_hd(uint32_t dest, int w, int h, const uint8_t *rgb) {
    if (!g_gl_on) return;
    dest &= 0x1FFFFF; GLMovieHD *e = 0;
    for (int i = 0; i < 4; i++) if (g_mvhd[i].tex && g_mvhd[i].addr == dest) e = &g_mvhd[i];
    if (!rgb || w <= 0 || h <= 0) { if (e) e->ok = 0; return; }
    if (!e) { for (int i = 0; i < 4; i++) if (!e || g_mvhd[i].age < e->age) e = &g_mvhd[i]; e->addr = dest; e->ok = 0; }
    gl_flush(); g_batch_gen = 0xFFFFFFFF;
    if (!e->tex) p_glGenTextures(1, &e->tex);
    p_glBindTexture(GL_TEXTURE_2D, e->tex); ap_forget();
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, 0x1907 /* GL_RGB */, GL_UNSIGNED_BYTE, rgb);
    e->ok = 1; e->w = w; e->h = h; e->age = ++g_mvhd_age;
}
GEXPORT void gl_movie_hd_clear(void) { for (int i = 0; i < 4; i++) g_mvhd[i].ok = 0; mvw_reset(); }   /* (a movie starts or ends) */
GEXPORT int gl_movie_hd_shown(void) { int n = g_mvhd_shown; g_mvhd_shown = 0; return n; }   /* (log / test) */
static int vram_garbage(const uint8_t *rgb) {
    uint64_t dx = 0, dy = 0; int n = 0;
    for (int y = 0; y < 271; y += 2) {
        const uint8_t *a = rgb + y * 480 * 3, *b = a + 480 * 3;
        for (int x = 0; x < 479; x++, a += 3, b += 3) {
            int l = a[0] + 2 * a[1] + a[2], r = a[3] + 2 * a[4] + a[5], d = b[0] + 2 * b[1] + b[2];
            dx += (uint32_t)(l > r ? l - r : r - l); dy += (uint32_t)(l > d ? l - d : d - l); n++;
        }
    }
    double mx = (double)dx / (4.0 * n), my = (double)dy / (4.0 * n);   /* average change per pixel, 0..255 */
    int bad = my > 8.0 && my > 1.5 * mx + 2.0;
    if (bad) g_garbage_hidden++;
    return bad;
}
GEXPORT int gl_garbage_hidden(void) { return g_garbage_hidden; }
static int ensure_pprog(void);
static GEState *g_vw_ge;                                           /* (for vram_into_fb: video memory to read) */
static void vram_into_fb(GLFb *f) {
    if (!g_vw_ge || !ensure_pprog()) return;
    GLMovieHD *mv = 0;
    for (int i = 0; i < 4; i++) if (g_mvhd[i].ok && g_mvhd[i].addr == f->addr) mv = &g_mvhd[i];
    p_glActiveTexture(GL_TEXTURE0);
    if (mv) p_glBindTexture(GL_TEXTURE_2D, mv->tex);
    else {
        static uint8_t rgb[480 * 272 * 3];
        fb_to_rgb(g_vw_ge->vram, 0x04000000u | f->addr, f->w, f->fmt, rgb);
        if (!g_vram_tex) p_glGenTextures(1, &g_vram_tex);
        p_glBindTexture(GL_TEXTURE_2D, g_vram_tex);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 480, 272, 0, 0x1907 /* GL_RGB */, GL_UNSIGNED_BYTE, rgb);
    }
    p_glBindFramebuffer(GL_FRAMEBUFFER, f->msfbo ? f->msfbo : f->fbo);
    p_glDisable(GL_SCISSOR_TEST); p_glDisable(GL_DEPTH_TEST); p_glDisable(GL_BLEND); p_glColorMask(1, 1, 1, 1);
    p_glViewport(0, (f->h - 272) * g_scale, 480 * g_scale, 272 * g_scale);
    p_glUseProgram(g_pprog); p_glUniform1i(pu_tex, 0); p_glUniform1i(pu_grade, 0); p_glUniform1i(pu_ovl, 0);
    p_glUniform4f(pu_rect, 0.f, 1.f, 1.f, 0.f);                      /* rows are stored top first */
    float ratio = mv ? (float)mv->w / (480.f * g_scale) : 1.f; int taps = ratio <= 1.05f ? 1 : ratio < 2.f ? 2 : ratio < 3.f ? 3 : 4;
    p_glUniform1i(pu_taps, taps); p_glUniform2f(pu_step, 1.f / (480.f * g_scale) / taps, 1.f / (272.f * g_scale) / taps);
    p_glDrawArrays(0x0005 /* GL_TRIANGLE_STRIP */, 0, 4);
    p_glUseProgram(g_prog); p_glViewport(0, 0, f->w * g_scale, f->h * g_scale);
    f->dirty = 1; g_cur_fb = 0; ap_forget(); g_batch_gen = 0xFFFFFFFF;
}
static int ensure_pprog(void) {
    if (!g_pprog) {
        if (!p_glUniform4f) *(void **)&p_glUniform4f = g_getproc("glUniform4f");
        GLuint vs = compile(GL_VERTEX_SHADER, PVS), fs = compile(GL_FRAGMENT_SHADER, PFS);
        if (!vs || !fs || !p_glUniform4f) { g_gl_on = 0; return 0; }
        g_pprog = p_glCreateProgram(); p_glAttachShader(g_pprog, vs); p_glAttachShader(g_pprog, fs); p_glLinkProgram(g_pprog);
        GLint ok = 0; p_glGetProgramiv(g_pprog, GL_LINK_STATUS, &ok);
        if (!ok) { p_glGetProgramInfoLog(g_pprog, sizeof g_gl_err, 0, g_gl_err); return 0; }
        pu_tex = p_glGetUniformLocation(g_pprog, "u_tex"); pu_rect = p_glGetUniformLocation(g_pprog, "u_rect");
        pu_taps = p_glGetUniformLocation(g_pprog, "u_taps"); pu_step = p_glGetUniformLocation(g_pprog, "u_step");
        pu_ovl = p_glGetUniformLocation(g_pprog, "u_ovl"); pu_grade = p_glGetUniformLocation(g_pprog, "u_grade");
    }
    return g_pprog != 0;
}
/* movies at an even pace: the game makes its movie pictures in uneven bursts (two close together, then a long
 * gap - the emulation of the decoding), and showing each as soon as it comes made the movie judder. Each new movie
 * picture (HD, from video memory, or with the game's subtitles drawn on it) is kept, as shown, in a small ring; the
 * screen steps through the ring one picture per (measured) picture time, a picture or two behind the newest. */
#define MQ_N 6
static struct { GLuint tex, fbo; int w, h; } g_mq[MQ_N];
static uint32_t g_mq_made, g_mq_shown, g_mq_key = 0xFFFFFFFFu; static float g_mq_credit, g_mq_ppf = 2.f; static int g_mq_since, g_mq_on = 1, g_mq_was;
GEXPORT void gl_movie_pace(int on) { g_mq_on = on; }
GEXPORT void gl_movie_pace_dbg(int *o) { o[0] = (int)g_mq_made; o[1] = (int)g_mq_shown; o[2] = (int)(g_mq_ppf * 100.f); }   /* (test aid) */
static int mq_slot(int w, int h) {             /* the slot for a new picture, w x h; its framebuffer is bound */
    int k = (int)(g_mq_made % MQ_N);
    if (!g_mq[k].tex || g_mq[k].w != w || g_mq[k].h != h) {
        if (!g_mq[k].tex) { p_glGenTextures(1, &g_mq[k].tex); p_glGenFramebuffers(1, &g_mq[k].fbo); }
        p_glBindTexture(GL_TEXTURE_2D, g_mq[k].tex); ap_forget();
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
        p_glBindFramebuffer(GL_FRAMEBUFFER, g_mq[k].fbo);
        p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_mq[k].tex, 0);
        g_mq[k].w = w; g_mq[k].h = h;
    } else p_glBindFramebuffer(GL_FRAMEBUFFER, g_mq[k].fbo);
    g_mq_made++; return k;
}
static int mq_pick(void) {                     /* which picture of the ring goes on the screen now */
    uint32_t newest = g_mq_made - 1;
    g_mq_since++;
    if (g_mq_shown + 3 < newest) { g_mq_shown = newest - 2; g_mq_credit = 0.f; }   /* (fell behind: catch up) */
    g_mq_credit += 1.f / g_mq_ppf;
    if (g_mq_credit >= 1.f && g_mq_shown < newest) { g_mq_shown++; g_mq_credit -= 1.f; }
    if (g_mq_shown >= newest && g_mq_credit > 1.f) g_mq_credit = 1.f;
    if (g_mq_credit < -1.f) g_mq_credit = -1.f;
    return (int)(g_mq_shown % MQ_N);
}
/* (log) pictures shown black / taken from video memory / video memory blacked out since the last call, and the last one's
 * details: black, address, frames since drawn, age when switched to, older frame, movie, no new frames, t x 1000 */
static int g_pkx[3], g_pkx_last[8];
GEXPORT void gl_present_odd(int *o) { for (int i = 0; i < 3; i++) { o[i] = g_pkx[i]; g_pkx[i] = 0; } memcpy(o + 3, g_pkx_last, sizeof g_pkx_last); }
static int present_impl2(GEState *g, uint32_t addr, int stride, int fmt, int win_w, int win_h, float t) {
    if (!g_gl_on) return 0;
    gl_end_list(); addr &= 0x1FFFFF; g_vw_ge = g;
    { int r_ = ensure_pprog(); if (!r_) return g_gl_on ? -2 : -1; }
    GLFb *f = 0; for (int i = 0; i < g_nfb; i++) if (g_fbs[i].addr == addr) f = &g_fbs[i];
    /* 60 fps mode counts frames in gl_commit; when the game stops showing new frames (a movie written straight into
     * video memory, a fade, a menu) presents count instead, so a picture nobody draws on the graphics card any more
     * is taken from video memory again */
    int nocommit = g_rec_on && g_since_commit >= 20;
    if (!g_rec_on || nocommit) g_frame_no++;
    int stale = !f || g_frame_no - f->drawn > 2, shadow = 0;
    if (g_movie && vw_find(addr) >= 0) stale = 1;                 /* (a movie picture written since the card last drew there) */
    /* how old the picture in this framebuffer was when the game switched the display to it: a framebuffer last drawn
     * long before (the title screen, seen again when a cutscene starts) holds nothing current */
    static uint32_t s_shown = 0xFFFFFFFFu, s_age;
    if (addr != s_shown) { s_shown = addr; s_age = f ? (uint32_t)(g_frame_no - f->drawn) : 0xFFFFFFFFu; }
    if (g_rec_on && !stale && t >= 0.f) {
        GLRec *R = &g_rec[g_rlast];
        /* (the game keeps drawing without showing new frames - menus, item screens, fades: then what is in the
         * framebuffer now is the picture, not the last recorded frame) */
        int live = nocommit || g_movie || rec_2d_only(R);            /* (a movie: its pictures aren't recorded draws; a 2D-only
                                                                      *  frame - comic cutscenes: shown as the game drew it, never
                                                                      *  drawn again from the recording, which can't redo how its
                                                                      *  panels are cut out) */
        int smooth = !live && t < 0.999f && R->lerped > 0 && (R->rejected - R->rescued) * 2 <= R->lerped && !R->cut;   /* (camera cut: no in-between) */
        if (g_exact) { int ec = exact_cut(); if (ec >= 0) smooth = !live && t < 0.999f && !ec; }   /* (exact: the game's cameras say whether it cut) */
        if (t < 0.999f) {                                         /* (log: in-between pictures that couldn't be made) */
            if (smooth) g_pk[0]++; else if (R->cut) g_pk[1]++; else if (R->lerped <= 0) g_pk[2]++; else g_pk[3]++;
        }
        if (smooth) shadow = replay(f, t);
        else if (!live && g_light_on && g_lit[g_rlast].box) shadow = replay(f, 1.f);   /* (lighting: the last frame as it is, lit) */
        else if (!live && g_older) shadow = replay(f, 1.f);   /* (an older frame: the game is already drawing over its framebuffer -
                                                               *  drawn again from the recording, as it was) */
    }
    GLMovieHD *mv = 0;
    if (stale && g_movie) for (int i = 0; i < 4; i++) if (g_mvhd[i].ok && g_mvhd[i].addr == addr) mv = &g_mvhd[i];
    /* a framebuffer the game shows without drawing into it, and no movie wrote a picture into: the card's own last
     * picture there is what video memory would hold on a PSP (a held picture). Already old when the game switched to
     * it, or never drawn here at all: what video memory holds there is leftover data - the title screen seen again
     * when a cutscene starts, texture bytes read as a picture ("TV snow") while the next scene loads - shown black */
    int held = 0, black = 0;
    if (stale && !mv && !mvw_find(addr)) {
        if (f && s_age <= 30) held = 1;
        else black = 1;
    }
    if (held) stale = 0;
    if (black || (stale && !mv)) {                                /* (log: pictures not drawn by the graphics card - black or from video memory) */
        g_pkx[black ? 0 : 1]++;
        int *e = g_pkx_last; e[0] = black; e[1] = (int)addr; e[2] = f ? (int)(g_frame_no - f->drawn) : -1; e[3] = (int)s_age; e[4] = g_older; e[5] = g_movie; e[6] = nocommit; e[7] = (int)(t * 1000);
    }
    if (g_rec_on) g_since_commit++;
    if (!stale && !shadow) fb_resolve(f);
    if (!stale && g_rec_on && g_fill_on) last_keep(shadow ? f->sfbo : f->fbo, f->w * g_scale, f->h * g_scale);
    int dw = win_w, dh = win_w * 272 / 480;                       /* keep the PSP's shape */
    if (dh > win_h) { dh = win_h; dw = win_h * 480 / 272; }
    /* (a movie: the picture goes into the ring - only when it is a new one - and the ring's picture of the moment
     *  goes on the screen; see g_mq) */
    int paced = g_movie && g_mq_on && dw > 0 && dh > 0, mq_new = 1;
    if (paced) {
        uint32_t key = mv ? (0x80000000u | mv->age) : black ? 1u : stale ? ((addr << 12) ^ (vws_get(addr) << 1) ^ 0x40000000u)
                                                              : ((addr << 12) ^ (uint32_t)f->drawn ^ ((uint32_t)shadow << 30));
        if (!g_mq_was) { g_mq_made = 0; g_mq_shown = 0; g_mq_key = 0xFFFFFFFFu; g_mq_credit = 0.f; g_mq_ppf = 2.f; g_mq_since = 0; }
        if (key == g_mq_key && g_mq_made) mq_new = 0;
        else {
            if (g_mq_made) { float n = (float)g_mq_since; if (n < 1.f) n = 1.f; if (n > 8.f) n = 8.f; g_mq_ppf = 0.92f * g_mq_ppf + 0.08f * n; }
            g_mq_since = 0; g_mq_key = key;
        }
    }
    g_mq_was = paced;
    if (!paced || mq_new) {
    if (paced) mq_slot(dw, dh); else p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_cur_fb = 0; ap_forget();
    p_glDisable(GL_SCISSOR_TEST); p_glDisable(GL_DEPTH_TEST); p_glDisable(GL_BLEND); p_glColorMask(1, 1, 1, 1);
    if (paced) p_glViewport(0, 0, dw, dh); else p_glViewport(0, 0, win_w, win_h);
    p_glClearColor(0, 0, 0, 1); p_glClear(GL_COLOR_BUFFER_BIT);
    if (!paced) p_glViewport((win_w - dw) / 2, (win_h - dh) / 2, dw, dh);
    p_glUseProgram(g_pprog); p_glUniform1i(pu_tex, 0); p_glActiveTexture(GL_TEXTURE0); p_glUniform1i(pu_grade, g_light_on ? 1 : 0);
    if (mv) {                                                     /* an HD movie picture in place of video memory */
        p_glBindTexture(GL_TEXTURE_2D, mv->tex);
        p_glUniform4f(pu_rect, 0.f, 1.f, 1.f, 0.f);                  /* rows are stored top first */
        float ratio = (float)mv->w / (float)dw; int taps = ratio <= 1.05f ? 1 : ratio < 2.f ? 2 : ratio < 3.f ? 3 : 4;
        p_glUniform1i(pu_taps, taps); p_glUniform2f(pu_step, 1.f / (float)dw / taps, 1.f / (float)dh / taps);
        g_mvhd_shown++;
    } else if (stale) {
        static uint8_t rgb[480 * 272 * 3];
        if (black) memset(rgb, 0, sizeof rgb); else {
        fb_to_rgb(g->vram, 0x04000000u | addr, stride, fmt, rgb);
        /* (black: see 'held' above) during a movie: a framebuffer this movie hasn't written a picture into yet holds whatever was left there -
         * an old picture (the title screen) or noise - shown black until its first picture; outside a movie, noise
         * left in a framebuffer the game shows before drawing into it is shown black */
        if (g_movie ? !mvw_find(addr) : vram_garbage(rgb)) { memset(rgb, 0, sizeof rgb); g_pkx[2]++; }
        }
        if (!g_vram_tex) p_glGenTextures(1, &g_vram_tex);
        p_glBindTexture(GL_TEXTURE_2D, g_vram_tex);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 480, 272, 0, 0x1907 /* GL_RGB */, GL_UNSIGNED_BYTE, rgb);
        p_glUniform4f(pu_rect, 0.f, 1.f, 1.f, 0.f);                  /* rows are stored top first */
        p_glUniform1i(pu_taps, 1);
    } else {
        int frz = !shadow && g_frz_ok && g_frz_addr == f->addr && g_rec_on && !nocommit && rec_2d_only(&g_rec[g_rlast]);
        p_glBindTexture(GL_TEXTURE_2D, frz ? g_frz_tex : shadow ? f->scolor : f->color);
        p_glUniform4f(pu_rect, 0.f, (float)(f->h - 272) / f->h, 480.f / f->w, 1.f);
        float ratio = 480.f * g_scale / (float)dw; int taps = ratio <= 1.05f ? 1 : ratio < 2.f ? 2 : ratio < 3.f ? 3 : 4;
        p_glUniform1i(pu_taps, taps);
        p_glUniform2f(pu_step, 480.f / f->w / (float)dw / taps, 272.f / f->h / (float)dh / taps);
    }
    p_glDrawArrays(0x0005 /* GL_TRIANGLE_STRIP */, 0, 4);
    }
    if (paced) {                                                  /* the ring's picture of the moment, on the screen */
        int k = mq_pick();
        p_glBindFramebuffer(GL_FRAMEBUFFER, 0); g_cur_fb = 0; ap_forget();
        p_glDisable(GL_SCISSOR_TEST); p_glDisable(GL_DEPTH_TEST); p_glDisable(GL_BLEND); p_glColorMask(1, 1, 1, 1);
        p_glViewport(0, 0, win_w, win_h); p_glClearColor(0, 0, 0, 1); p_glClear(GL_COLOR_BUFFER_BIT);
        p_glViewport((win_w - dw) / 2, (win_h - dh) / 2, dw, dh);
        p_glUseProgram(g_pprog); p_glUniform1i(pu_tex, 0); p_glActiveTexture(GL_TEXTURE0); p_glUniform1i(pu_grade, 0);
        p_glBindTexture(GL_TEXTURE_2D, g_mq[k].tex);
        p_glUniform4f(pu_rect, 0.f, 0.f, 1.f, 1.f); p_glUniform1i(pu_taps, 1);
        p_glDrawArrays(0x0005, 0, 4);
    }
    if (g_ovl_on && g_ovl_tex) {                                  /* a system dialog (save list, message) on top */
        p_glEnable(GL_BLEND); p_glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE); p_glBlendEquation(GL_FUNC_ADD);
        p_glBindTexture(GL_TEXTURE_2D, g_ovl_tex); p_glUniform1i(pu_ovl, 1); p_glUniform1i(pu_grade, 0); p_glUniform4f(pu_rect, 0.f, 1.f, 1.f, 0.f);   /* (rows top first) */
        p_glDrawArrays(0x0005, 0, 4);
        p_glUniform1i(pu_ovl, 0); p_glDisable(GL_BLEND); ap_forget();
    }
    p_glUseProgram(g_prog);
    GLenum e = p_glGetError();
    if (e) { static const char hx[] = "0123456789abcdef"; char m[] = "OpenGL error 0x0000 while showing the picture";
             m[15] = hx[(e >> 12) & 15]; m[16] = hx[(e >> 8) & 15]; m[17] = hx[(e >> 4) & 15]; m[18] = hx[e & 15];
             snprintf_(g_gl_err, sizeof g_gl_err, "%s", m); return -(int)e; }
    return stale ? 2 : shadow ? 3 : 1;
}
GEXPORT int gl_present(GEState *g, uint32_t addr, int stride, int fmt, int win_w, int win_h) {
    return present_impl(g, addr, stride, fmt, win_w, win_h, -1.f);
}
/* 60 fps mode: show the picture t of the way (0..1) from the previous frame to the last one (1 = the last one) */
GEXPORT int gl_present_t(GEState *g, uint32_t addr, int stride, int fmt, int win_w, int win_h, float t) {
    return present_impl(g, addr, stride, fmt, win_w, win_h, t);
}

/* the window picture just drawn by gl_present (before it is shown), as RGB rows top to bottom: for showing it
 * another way when the window itself can't show OpenGL pictures */
GEXPORT int gl_read_window(int w, int h, uint8_t *out) {
    static uint8_t tmp[2560 * 1600 * 4];
    if (!g_gl_on || w <= 0 || h <= 0 || (size_t)w * h * 4 > sizeof tmp) return 0;
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    p_glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        const uint8_t *s = tmp + ((size_t)(h - 1 - y) * w + x) * 4; uint8_t *o = out + ((size_t)y * w + x) * 3;
        o[0] = s[0]; o[1] = s[1]; o[2] = s[2];
    }
    return 1;
}
/* (test aid) draw i of the last frame and draw j of the frame before: vertex address, count, first vertices x,y,w,src */
GEXPORT int gl_interp_dbg(int i, int j, float *out) {
    GLRec *R = &g_rec[g_rlast], *P = &g_rec[g_rprev];
    if (i < 0 || i >= R->nc || j < 0 || j >= P->nc) return 0;
    GLCmd *a = &R->c[i], *b = &P->c[j];
    out[0] = (float)a->st.va; out[1] = (float)b->st.va; out[2] = (float)a->n; out[3] = (float)b->n;
    for (int k = 0; k < 6; k++) {
        float *o = out + 4 + k * 8;
        if (k < a->n) { const GLVtx *v = R->v + a->v0 + k; o[0] = v->x; o[1] = v->y; o[2] = v->w; o[3] = (float)R->src[a->v0 + k]; }
        if (k < b->n) { const GLVtx *v = P->v + b->v0 + k; o[4] = v->x; o[5] = v->y; o[6] = v->w; o[7] = (float)P->src[b->v0 + k]; }
    }
    return 1;
}
/* (test aid) draw the last frame again at t = 1 into the shadow copy and count pixels that differ from the real
 * picture by more than 'tol' in any colour (should be about 0: the replay must give the same picture) */
GEXPORT int gl_replay_check(uint32_t addr, int tol) {
    addr &= 0x1FFFFF; GLFb *f = 0; for (int i = 0; i < g_nfb; i++) if (g_fbs[i].addr == addr) f = &g_fbs[i];
    if (!f || !g_rec_on) return -1;
    gl_end_list(); g_rec[g_rlast].lerped += 0;
    if (!replay(f, 1.0f)) return -2;
    fb_resolve(f);
    int W = 480 * g_scale, H = 272 * g_scale; static uint8_t a[2880 * 1632 * 4], b[2880 * 1632 * 4];
    if ((size_t)W * H * 4 > sizeof a) return -3;
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, f->fbo); p_glReadPixels(0, (f->h - 272) * g_scale, W, H, GL_RGBA, GL_UNSIGNED_BYTE, a);
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, f->sfbo); p_glReadPixels(0, (f->h - 272) * g_scale, W, H, GL_RGBA, GL_UNSIGNED_BYTE, b);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0); g_cur_fb = 0; ap_forget();
    int n = 0;
    for (int i = 0; i < W * H; i++) {
        int d0 = a[4 * i] - b[4 * i], d1 = a[4 * i + 1] - b[4 * i + 1], d2 = a[4 * i + 2] - b[4 * i + 2];
        if (d0 > tol || d0 < -tol || d1 > tol || d1 < -tol || d2 > tol || d2 < -tol) n++;
    }
    return n;
}
/* (test aid) the lerped draws of the last frame with the biggest point movement: n, through, va, max move, min w */
static float g_topz[1];
GEXPORT int gl_interp_top(float *out, int max) {
    GLRec *R = &g_rec[g_rlast]; int cnt = 0;
    for (int i = 0; i < R->nc; i++) {
        GLCmd *c = &R->c[i]; if (c->kind || c->match < 0) continue;
        float mv = 0, mw = 1e30f, mx = 0; const GLVtx *b = R->v + c->v0; const GLPrev *a = R->pv + c->v0;
        for (int k = 0; k < c->n; k++) {
            float px = a[k].X / a[k].W, py = a[k].Y / a[k].W, d = fabsf(px - b[k].x) + fabsf(py - b[k].y);
            if (d > mv) { mv = d; mx = b[k].z; g_topz[0] = a[k].Z / a[k].W; }
            if (a[k].W < mw) mw = a[k].W; if (b[k].w < mw) mw = b[k].w;
        }
        float *o = 0;
        if (cnt < max) o = out + 6 * cnt++;
        else { int lo = 0; for (int q = 1; q < max; q++) if (out[6 * q + 3] < out[6 * lo + 3]) lo = q; if (out[6 * lo + 3] < mv) o = out + 6 * lo; }
        if (o) { o[0] = (float)c->n; o[1] = (float)c->st.through; o[2] = mx; o[3] = mv; o[4] = mw; o[5] = g_topz[0]; }
    }
    return cnt;
}
/* (test aid) draws per framebuffer in the last recorded frame: out[2*i] = address, out[2*i+1] = count */
GEXPORT int gl_rec_fbs(uint32_t *out) {
    GLRec *R = &g_rec[g_rlast]; int n = 0;
    for (int i = 0; i < g_nfb; i++) {
        int k = 0; for (int j = 0; j < R->nc; j++) if (R->c[j].st.fbi == i) k++;
        out[2 * n] = g_fbs[i].addr; out[2 * n + 1] = (uint32_t)k; n++;
    }
    return n;
}
/* (test aid) moved triangles of the last frame that, at t, cover the point (px, py): per triangle 10 floats */
GEXPORT int gl_dbg_tris(float t, float px, float py, float *out, int max) {
    GLRec *R = &g_rec[g_rlast]; int n = 0;
    for (int i = 0; i < R->nc && n < max; i++) {
        GLCmd *c = &R->c[i]; if (c->kind || (c->match < 0 && c->match != -3)) continue;
        const GLVtx *b = R->v + c->v0; const GLPrev *pv = R->pv + c->v0; const uint8_t *pf = R->pf + c->v0;
        for (int k = 0; k + 2 < c->n && n < max; k += 3) {
            float x[3], y[3];
            for (int q = 0; q < 3; q++) {
                const GLPrev *a = &pv[k + q]; float W = a->W + (b[k + q].w - a->W) * t;
                x[q] = (a->X + (b[k + q].x * b[k + q].w - a->X) * t) / W; y[q] = (a->Y + (b[k + q].y * b[k + q].w - a->Y) * t) / W;
            }
            float d0 = (x[1] - x[0]) * (py - y[0]) - (y[1] - y[0]) * (px - x[0]), d1 = (x[2] - x[1]) * (py - y[1]) - (y[2] - y[1]) * (px - x[1]),
                  d2 = (x[0] - x[2]) * (py - y[2]) - (y[0] - y[2]) * (px - x[2]);
            if (!((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0))) continue;
            float *o = out + 10 * n++;
            o[0] = (float)i; o[1] = (float)c->match; o[2] = (float)(pf[k] | pf[k + 1] << 2 | pf[k + 2] << 4);
            for (int q = 0; q < 3; q++) { o[3 + q] = pv[k + q].W > 0 ? pv[k + q].Z / pv[k + q].W : -1e9f; o[6 + q] = b[k + q].z; }
            o[9] = pv[k].W;
        }
    }
    return n;
}

/* (test aid) camera-fit leftovers per draw: out[3*i] = draw, points, points off by > 2 px */
GEXPORT int gl_cam_draws(int *out, int max) {
    GLRec *R = &g_rec[g_rlast]; static int cnt[FR_CMD], bad[FR_CMD]; memset(cnt, 0, sizeof(int) * (size_t)R->nc); memset(bad, 0, sizeof(int) * (size_t)R->nc);
    for (int i = 0; i < g_nsamp; i++) { cnt[g_scmd[i]]++; if (g_serr[i] > 2.f) bad[g_scmd[i]]++; }
    int n = 0;
    for (int i = 0; i < R->nc && n < max; i++) if (bad[i] > 20) { out[3 * n] = i; out[3 * n + 1] = cnt[i]; out[3 * n + 2] = bad[i]; n++; }
    return n;
}
GEXPORT int gl_dbg_match(int i, float *o) {   /* (test aid) partner of draw i, its middle, mean camera-fit error */
    GLRec *R = &g_rec[g_rlast], *P = &g_rec[g_rprev]; if (i < 0 || i >= R->nc) return -9;
    GLCmd *c = &R->c[i]; o[0] = c->cx; o[1] = c->cy; o[2] = c->match >= 0 ? P->c[c->match].cx : 0; o[3] = c->match >= 0 ? P->c[c->match].cy : 0;
    double e = 0; int n = 0; for (int k = 0; k < g_nsamp; k++) if (g_scmd[k] == i) { e += g_serr[k]; n++; }
    o[4] = n ? (float)(e / n) : -1; o[5] = (float)c->st.va; o[6] = c->match >= 0 ? (float)P->c[c->match].st.va : 0; o[7] = (float)c->st.tex;
    return c->match;
}
GEXPORT int gl_dbg_samples(float *o, int max) {   /* (test aid) camera-fit points: x, y, z, w, prev x, prev y, error, draw */
    GLRec *R = &g_rec[g_rlast]; int n = 0;
    for (int i = 0; i < g_nsamp && n < max; i++, n++) {
        const GLVtx *v = R->v + g_samp[i][0]; const GLPrev *q = R->pv + g_samp[i][1]; float *e = o + 8 * n;
        e[0] = v->x; e[1] = v->y; e[2] = v->z; e[3] = v->w; e[4] = q->X / q->W; e[5] = q->Y / q->W; e[6] = g_serr[i]; e[7] = (float)g_scmd[i];
    }
    return n;
}

/* a picture (RGBA rows, top first) shown over the game's picture; w = 0 removes it. Call on the drawing thread. */
GEXPORT void gl_overlay(const uint8_t *rgba, int w, int h) {
    if (!g_gl_on) return;
    if (w <= 0 || h <= 0 || !rgba) { g_ovl_on = 0; return; }
    if (!g_ovl_tex) p_glGenTextures(1, &g_ovl_tex);
    p_glBindTexture(GL_TEXTURE_2D, g_ovl_tex);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    g_ovl_on = 1; ap_forget();
}
/* (test aid) the last frame's draws: n, blend on, through, skin, match, x0, y0, x1, y1 (bounding box, PSP pixels) */
GEXPORT int gl_dbg_draws(int *o, int max) {
    GLRec *R = &g_rec[g_rlast]; int m = 0;
    for (int i = 0; i < R->nc && m < max; i++) {
        GLCmd *c = &R->c[i]; if (c->kind) continue;
        const GLVtx *b = R->v + c->v0; float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int k = 0; k < c->n; k++) { if (b[k].x < x0) x0 = b[k].x; if (b[k].x > x1) x1 = b[k].x; if (b[k].y < y0) y0 = b[k].y; if (b[k].y > y1) y1 = b[k].y; }
        int *e = o + 9 * m++; e[0] = c->n; e[1] = c->st.bon; e[2] = c->st.through; e[3] = c->st.skin | c->ssw << 1; e[4] = c->match;
        e[5] = (int)x0; e[6] = (int)y0; e[7] = (int)x1; e[8] = (int)y1;
    }
    return m;
}
/* (test aid) vertices of the last frame's i-th draw (counting draws only): x, y, z, w, and where it was the frame before (x, y) per vertex */
GEXPORT int gl_dbg_vtx(int idx, float *o, int max) {
    GLRec *R = &g_rec[g_rlast]; int m = 0;
    for (int i = 0; i < R->nc; i++) {
        GLCmd *c = &R->c[i]; if (c->kind) continue;
        if (m++ != idx) continue;
        int n = c->n < max ? c->n : max; const GLVtx *b = R->v + c->v0;
        for (int k = 0; k < n; k++) { float *e = o + 6 * k; e[0] = b[k].x; e[1] = b[k].y; e[2] = b[k].z; e[3] = b[k].w; const GLPrev *q = R->pv + c->v0 + k; e[4] = q->W != 0.f ? q->X / q->W : -9999.f; e[5] = q->W != 0.f ? q->Y / q->W : -9999.f; }
        return n;
    }
    return 0;
}
/* (test aid) vertices of the last frame's idx-th draw: x, y, z, w, then where it was before in clip space X, Y, Z, W, then pf */
GEXPORT int gl_dbg_vtx2(int idx, float *o, int max) {
    GLRec *R = &g_rec[g_rlast]; int m = 0;
    for (int i = 0; i < R->nc; i++) {
        GLCmd *c = &R->c[i]; if (c->kind) continue;
        if (m++ != idx) continue;
        int n = c->n < max ? c->n : max; const GLVtx *b = R->v + c->v0; const GLPrev *q = R->pv + c->v0; const uint8_t *f = R->pf + c->v0;
        for (int k = 0; k < n; k++) { float *e = o + 9 * k; e[0] = b[k].x; e[1] = b[k].y; e[2] = b[k].z; e[3] = b[k].w; e[4] = q[k].X; e[5] = q[k].Y; e[6] = q[k].Z; e[7] = q[k].W; e[8] = f[k]; }
        return n;
    }
    return 0;
}
/* (test aid) draws of the frame before the last that no draw of the last frame was paired with: n, x0, y0, x1, y1, wmin */
GEXPORT int gl_dbg_lost(int *o, int max) {
    GLRec *R = &g_rec[g_rlast], *P = &g_rec[g_rprev]; static uint8_t used[FR_CMD]; int m = 0;
    memset(used, 0, sizeof used);
    for (int i = 0; i < R->nc; i++) if (R->c[i].match >= 0 && R->c[i].match < P->nc) used[R->c[i].match] = 1;
    for (int j = 0; j < P->nc && m < max; j++) {
        GLCmd *c = &P->c[j]; if (c->kind || used[j] || c->st.through) continue;
        const GLVtx *b = P->v + c->v0; float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, wm = 1e30f;
        for (int k = 0; k < c->n; k++) { if (b[k].x < x0) x0 = b[k].x; if (b[k].x > x1) x1 = b[k].x; if (b[k].y < y0) y0 = b[k].y; if (b[k].y > y1) y1 = b[k].y; if (b[k].w < wm) wm = b[k].w; }
        int *e = o + 6 * m++; e[0] = c->n; e[1] = (int)x0; e[2] = (int)y0; e[3] = (int)x1; e[4] = (int)y1; e[5] = (int)wm;
    }
    return m;
}
/* (test aid) the lost draws of the last frame: n, x0, y0, x1, y1 (in the frame before), wmin, wmax, zmin, zmax */
GEXPORT int gl_dbg_lostlist(int *o, int max) {
    const GLRec *P = &g_rec[g_rprev]; int m = 0;
    for (int L = 0; L < g_rnlost[g_rlast] && m < max; L++) {
        const GLCmd *c = &P->c[g_rlost[g_rlast][L]]; const GLVtx *b = P->v + c->v0;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, w0 = 1e30f, w1 = 0, z0 = 1e30f, z1 = -1e30f;
        for (int k = 0; k < c->n; k++) { if (b[k].x < x0) x0 = b[k].x; if (b[k].x > x1) x1 = b[k].x; if (b[k].y < y0) y0 = b[k].y; if (b[k].y > y1) y1 = b[k].y;
            if (b[k].w < w0) w0 = b[k].w; if (b[k].w > w1) w1 = b[k].w; if (b[k].z < z0) z0 = b[k].z; if (b[k].z > z1) z1 = b[k].z; }
        int *e = o + 9 * m++; e[0] = c->n; e[1] = (int)x0; e[2] = (int)y0; e[3] = (int)x1; e[4] = (int)y1; e[5] = (int)w0; e[6] = (int)w1; e[7] = (int)z0; e[8] = (int)z1;
    }
    return m;
}
/* (test aid) the frame before the last's draws, like gl_dbg_draws: n, blend, through, skin, used by the last frame, x0, y0, x1, y1 */
GEXPORT int gl_dbg_prevdraws(int *o, int max) {
    GLRec *R = &g_rec[g_rlast], *P = &g_rec[g_rprev]; static uint8_t used[FR_CMD]; int m = 0; memset(used, 0, sizeof used);
    for (int i = 0; i < R->nc; i++) if (!R->c[i].kind && R->c[i].match >= 0 && R->c[i].match < P->nc) used[R->c[i].match] = 1;
    for (int i = 0; i < P->nc && m < max; i++) {
        GLCmd *c = &P->c[i]; if (c->kind) continue;
        const GLVtx *b = P->v + c->v0; float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int k = 0; k < c->n; k++) { if (b[k].x < x0) x0 = b[k].x; if (b[k].x > x1) x1 = b[k].x; if (b[k].y < y0) y0 = b[k].y; if (b[k].y > y1) y1 = b[k].y; }
        int *e = o + 9 * m++; e[0] = c->n; e[1] = c->st.bon; e[2] = c->st.through; e[3] = c->st.skin; e[4] = used[i] ? 1 : 0;
        e[5] = (int)x0; e[6] = (int)y0; e[7] = (int)x1; e[8] = (int)y1;
    }
    return m;
}
