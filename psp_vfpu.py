#!/usr/bin/env python3
"""
PSP VFPU (vector unit) for the MGS Portable Ops interpreter - EXPERIMENTAL.
Written from the public PPSSPP register-addressing / prefix rules.  Maths uses ordinary
float32 arithmetic (not the PSP's bit-exact approximations), which is fine for running a game.
Hook:  psp_cpu.CPU.exec() calls  vfpu.execute(cpu, ins, pc)  for VFPU opcodes.
"""
import math, struct
from psp_cpu import Stop, s16

M32 = 0xFFFFFFFF
PFX_CONST = [0.0, 1.0, 2.0, 0.5, 3.0, 1.0/3.0, 0.25, 1.0/6.0]
CST = [0.0, 3.4028234663852886e+38, math.sqrt(2), math.sqrt(0.5), 2/math.sqrt(math.pi), 2/math.pi, 1/math.pi,
       math.pi/4, math.pi/2, math.pi, math.e, math.log2(math.e), math.log10(math.e), math.log(2), math.log(10),
       2*math.pi, math.pi/6, math.log10(2), math.log2(10), math.sqrt(3)/2]
CTRL_S, CTRL_T, CTRL_D, CTRL_CC = 0, 1, 2, 3

def f32(b): return struct.unpack('<f', struct.pack('<I', b & M32))[0]
def bits(f):
    try: return struct.unpack('<I', struct.pack('<f', f))[0]
    except OverflowError: return 0x7F800000 if f > 0 else 0xFF800000
def rnd(f):                                   # round to float32 precision, keep as python float
    try: return struct.unpack('<f', struct.pack('<f', f))[0]
    except OverflowError: return math.inf if f > 0 else -math.inf

def init(cpu):
    cpu.v = [0] * 128                         # raw 32-bit patterns, index = mtx*16 + col*4 + row
    cpu.vc = [0] * 16
    cpu.vc[CTRL_S] = 0xE4; cpu.vc[CTRL_T] = 0xE4; cpu.vc[CTRL_D] = 0
    cpu.vc[7] = 0x7772CEAB                    # revision register
    for i in range(8): cpu.vc[8 + i] = 0x3F800001

# ------------------------------------------------------------------ register addressing
def vec_idx(reg, n):
    mtx = (reg >> 2) & 7; col = reg & 3; tr = (reg >> 5) & 1
    if n == 1: tr = 0; row = (reg >> 5) & 3
    elif n == 3: row = (reg >> 6) & 1
    else: row = (reg >> 5) & 2
    if tr: return [mtx*16 + ((row+i) & 3)*4 + col for i in range(n)]
    return [mtx*16 + col*4 + ((row+i) & 3) for i in range(n)]

def mat_idx(reg, n):                           # returns list of n*n indices laid out [j*4+i]
    mtx = (reg >> 2) & 7; col = reg & 3; tr = (reg >> 5) & 1
    if n == 1: tr = 0; row = (reg >> 5) & 3
    elif n == 3: row = (reg >> 6) & 1
    else: row = (reg >> 5) & 2
    out = [0] * 16
    for i in range(n):
        for j in range(n):
            if tr: out[j*4+i] = mtx*16 + ((row+i) & 3)*4 + ((col+j) & 3)
            else:  out[j*4+i] = mtx*16 + ((col+j) & 3)*4 + ((row+i) & 3)
    return out

def rdv(cpu, reg, n):  return [f32(cpu.v[i]) for i in vec_idx(reg, n)]
def rdvb(cpu, reg, n): return [cpu.v[i] for i in vec_idx(reg, n)]
def rdm(cpu, reg, n):
    idx = mat_idx(reg, n); out = [0.0] * 16
    for j in range(n):
        for i in range(n): out[j*4+i] = f32(cpu.v[idx[j*4+i]])
    return out

def wr_masked(cpu, idx, vals_bits, n, mask_ok=True):
    d = cpu.vc[CTRL_D]
    for i, ix in enumerate(idx):
        if mask_ok and (d >> (8 + i)) & 1: continue
        cpu.v[ix] = vals_bits[i] & M32
def wrv(cpu, reg, vals, n):
    wr_masked(cpu, vec_idx(reg, n), [bits(x) for x in vals[:n]], n)
def wrvb(cpu, reg, b, n): wr_masked(cpu, vec_idx(reg, n), b, n)
def wrm(cpu, reg, vals, n):
    idx = mat_idx(reg, n)
    for j in range(n):
        for i in range(n): cpu.v[idx[j*4+i]] = bits(vals[j*4+i])

# ------------------------------------------------------------------ prefixes
def apply_st(p, v, n, invalid=0.0):
    if p == 0xE4: return v
    orig = [v[i] if i < n and i < len(v) else invalid for i in range(4)]
    out = list(v) + [0.0] * (4 - len(v))
    for i in range(n):
        sw = (p >> (2*i)) & 3; ab = (p >> (8+i)) & 1; cs = (p >> (12+i)) & 1; ng = (p >> (16+i)) & 1
        x = PFX_CONST[sw + (ab << 2)] if cs else (abs(orig[sw]) if ab else orig[sw])
        out[i] = -x if ng else x
    return out
def sw(x, y, z, w): return x | y << 2 | z << 4 | w << 6
def consts(*cs):                                # each: None or 0..7 index in PFX_CONST
    r = 0
    for i, c in enumerate(cs):
        if c is not None: r |= ((c & 3) << (2*i)) | ((c & 4) << (6 + i)) | (1 << (12 + i))
    return r
def S(cpu, v, n, inv=0.0): return apply_st(cpu.vc[CTRL_S], v, n, inv)
def T(cpu, v, n, inv=0.0): return apply_st(cpu.vc[CTRL_T], v, n, inv)
def rewrite(p, remove, add): return (p & ~remove) | add

def dsat(cpu, v, n):
    d = cpu.vc[CTRL_D]
    if not (d & 0xFF): return v
    v = list(v)
    for i in range(n):
        s = (d >> (2*i)) & 3
        if s == 1: v[i] = 0.0 if v[i] <= 0 else (1.0 if v[i] > 1 else v[i])
        elif s == 3: v[i] = -1.0 if v[i] < -1 else (1.0 if v[i] > 1 else v[i])
    return v
def eat(cpu): cpu.vc[CTRL_S] = 0xE4; cpu.vc[CTRL_T] = 0xE4; cpu.vc[CTRL_D] = 0
def last_only_d(cpu, n):                        # ops that apply D only to the last lane
    d = cpu.vc[CTRL_D]
    cpu.vc[CTRL_D] = ((d & (1 << 8)) << (n-1)) | ((d & 3) << (2*n-2))

def apply_st_bits(p, b, n):
    """Same as apply_st but on raw 32-bit patterns (for integer vector ops)."""
    if p == 0xE4: return list(b) + [0] * (4 - len(b))
    orig = [b[i] if i < len(b) else 0 for i in range(4)]; out = list(orig)
    for i in range(n):
        sw_ = (p >> (2*i)) & 3; ab = (p >> (8+i)) & 1; cs = (p >> (12+i)) & 1; ng = (p >> (16+i)) & 1
        x = bits(PFX_CONST[sw_ + (ab << 2)]) if cs else ((orig[sw_] & 0x7FFFFFFF) if ab else orig[sw_])
        out[i] = x ^ 0x80000000 if ng else x
    return out
def sgn32(x): return x - 0x100000000 if x & 0x80000000 else x
def h2f(h):
    try: return struct.unpack('<e', struct.pack('<H', h & 0xFFFF))[0]
    except Exception: return math.nan
def f2h(f):
    if f != f: return 0x7FFF
    try: return struct.unpack('<H', struct.pack('<e', f))[0]
    except OverflowError: return 0xFC00 if f < 0 else 0x7C00
def wr_ints(cpu, reg, vals, n): wr_masked(cpu, vec_idx(reg, n), [v & M32 for v in vals[:n]], n)

def fin(x):                                     # float32 round + PSP-ish NaN/denormal cleanup
    x = rnd(x)
    return x
def dot(a, b):
    s = 0.0
    for x, y in zip(a, b): s = rnd(s + rnd(x * y))
    return s
def div(a, b):
    if b == 0.0:
        if a == 0.0 or a != a: return math.nan
        neg = (a < 0) != (math.copysign(1, b) < 0)
        return -math.inf if neg else math.inf
    return rnd(a / b)

def size_of(ins): return ((ins >> 7) & 1) + ((ins >> 14) & 2) + 1

# ------------------------------------------------------------------ the executor
def execute(cpu, ins, pc):
    op = ins >> 26
    r = cpu.r
    if op == 0x3F and (ins >> 16) == 0xFFFF: return      # vnop / vsync / vflush
    vd = ins & 0x7F; vs = (ins >> 8) & 0x7F; vt = (ins >> 16) & 0x7F

    if op == 0x12:                                # COP2: mfv/mtv/mfvc/mtvc, bvf/bvt
        sub = (ins >> 21) & 31; rt = (ins >> 16) & 31; imm = ins & 0xFF
        if sub == 3:
            if rt:
                if imm < 128: r[rt] = cpu.v[vec_idx(imm, 1)[0]]
                elif imm < 144: r[rt] = cpu.vc[imm - 128]
        elif sub == 7:
            if imm < 128: cpu.v[vec_idx(imm, 1)[0]] = r[rt]
            elif imm < 144: cpu.vc[imm - 128] = r[rt] & (0xFFFFF if imm-128 < 2 else 0xFFF if imm-128 == 2 else M32)
        elif sub == 8:
            t = (ins >> 16) & 3; ccbit = (cpu.vc[CTRL_CC] >> ((ins >> 18) & 7)) & 1
            taken = (ccbit == 1) if t in (1, 3) else (ccbit == 0)
            cpu.branch(pc, taken, (pc + 4 + (s16(ins & 0xFFFF) << 2)) & M32, bool(t & 2))
        else: raise Stop(f"unimplemented COP2 sub {sub:#x} (insn {ins:#010x})", pc)
        return

    if op in (0x32, 0x3A):                        # lv.s / sv.s
        vt5 = ((ins >> 16) & 31) | ((ins & 3) << 5)
        a = (r[(ins >> 21) & 31] + s16(ins & 0xFFFC)) & M32
        ix = vec_idx(vt5, 1)[0]                   # register number -> storage slot (same mapping as every other op)
        if op == 0x32: cpu.v[ix] = cpu.r32(a)
        else: cpu.w32(a, cpu.v[ix])
        return
    if op in (0x36, 0x3E):                        # lv.q / sv.q
        vt6 = ((ins >> 16) & 31) | ((ins & 1) << 5)
        a = (r[(ins >> 21) & 31] + s16(ins & 0xFFFC)) & M32
        idx = vec_idx(vt6, 4)
        if op == 0x36:
            for i in range(4): cpu.v[idx[i]] = cpu.r32(a + 4*i)
        else:
            for i in range(4): cpu.w32(a + 4*i, cpu.v[idx[i]])
        return
    if op in (0x35, 0x3D):                        # lvl.q lvr.q svl.q svr.q
        vt6 = ((ins >> 16) & 31) | ((ins & 1) << 5)
        a = (r[(ins >> 21) & 31] + s16(ins & 0xFFFC)) & M32
        idx = vec_idx(vt6, 4); off = (a >> 2) & 3; right = bool(ins & 2)
        if op == 0x35:
            if not right:
                for i in range(off + 1): cpu.v[idx[3 - i]] = cpu.r32(a - 4*i)
            else:
                for i in range(4 - off): cpu.v[idx[i]] = cpu.r32(a + 4*i)
        else:
            if not right:
                for i in range(off + 1): cpu.w32(a - 4*i, cpu.v[idx[3 - i]])
            else:
                for i in range(4 - off): cpu.w32(a + 4*i, cpu.v[idx[i]])
        return

    if op == 0x37:                                # prefixes / viim / vfim
        k = (ins >> 23) & 7
        if k < 6:
            reg = k >> 1; data = ins & 0xFFFFF
            if reg == 2: data &= 0xFFF
            cpu.vc[reg] = data; return
        vtt = vt
        if k == 6: val = float(s16(ins & 0xFFFF))
        else:
            h = ins & 0xFFFF; e = (h >> 10) & 31; m = h & 0x3FF; sg = -1.0 if h & 0x8000 else 1.0
            val = sg * (math.ldexp(m, -24) if e == 0 else (math.inf if e == 31 and m == 0 else math.nan if e == 31 else math.ldexp(1024 + m, e - 25)))
        wrv(cpu, vtt, dsat(cpu, [val], 1), 1); eat(cpu); return

    n = size_of(ins)

    if op == 0x18 or op == 0x19 or op == 0x1B:
        sub = (ins >> 23) & 7
        if op == 0x18 and sub in (0, 1, 7) or op == 0x19 and sub == 0:
            s = rdv(cpu, vs, n); t = rdv(cpu, vt, n)
            if op == 0x18 and sub == 7:
                s[n-1:] = S(cpu, s[n-1:], 1, -math.inf)[:1] if True else s[n-1:]
                t[n-1:] = T(cpu, t[n-1:], 1, -math.inf)[:1]
            else:
                s = S(cpu, s, n)[:n]; t = T(cpu, t, n)[:n]
            if op == 0x18 and sub == 0: d = [rnd(a + b) for a, b in zip(s, t)]
            elif op == 0x18 and sub == 1: d = [rnd(a - b) for a, b in zip(s, t)]
            elif op == 0x18: d = [div(a, b) for a, b in zip(s, t)]
            else: d = [rnd(a * b) for a, b in zip(s, t)]
            if op == 0x18 and sub == 7: last_only_d(cpu, n)
            wrv(cpu, vd, dsat(cpu, d, n), n); eat(cpu); return
        if op == 0x19 and sub == 1:               # vdot
            s = S(cpu, rdv(cpu, vs, n), n)[:n]; t = T(cpu, rdv(cpu, vt, n), n)[:n]
            wrv(cpu, vd, dsat(cpu, [dot(s, t)], 1), 1); eat(cpu); return
        if op == 0x19 and sub == 4:               # vhdp
            s = rdv(cpu, vs, n); t = T(cpu, rdv(cpu, vt, n), n)[:n]
            s = apply_st(rewrite(cpu.vc[CTRL_S], sw(*(3 if i == n-1 else 0 for i in range(4))), consts(*(1 if i == n-1 else None for i in range(4)))), s, 4)[:n]
            wrv(cpu, vd, dsat(cpu, [dot(s, t)], 1), 1); eat(cpu); return
        if op == 0x19 and sub == 2:               # vscl
            s = S(cpu, rdv(cpu, vs, n), n)[:n]
            tl = (vt >> 5) & 3
            tv = [0.0] * 4; tv[tl] = f32(cpu.v[vec_idx(vt, 1)[0]])
            tv = apply_st(rewrite(cpu.vc[CTRL_T], 0xFF, sw(tl, tl, tl, tl)), tv, 4)
            wrv(cpu, vd, dsat(cpu, [rnd(s[i] * tv[i]) for i in range(n)], n), n); eat(cpu); return
        if op == 0x19 and sub == 5:               # vcrs
            s = apply_st(rewrite(cpu.vc[CTRL_S], sw(3, 3, 3, 0), sw(1, 2, 0, 0)), rdv(cpu, vs, n), n)
            t = apply_st(rewrite(cpu.vc[CTRL_T], sw(3, 3, 3, 0), sw(2, 0, 1, 0)), rdv(cpu, vt, n), n)
            wrv(cpu, vd, dsat(cpu, [rnd(s[i] * t[i]) for i in range(n)], n), n); eat(cpu); return
        if op == 0x1B and sub in (2, 3):          # vmin / vmax
            s = S(cpu, rdv(cpu, vs, n), n)[:n]; t = T(cpu, rdv(cpu, vt, n), n)[:n]
            d = [min(a, b) if sub == 2 else max(a, b) for a, b in zip(s, t)]
            wrv(cpu, vd, dsat(cpu, d, n), n); eat(cpu); return
        if op == 0x1B and sub == 0:               # vcmp
            cond = ins & 15
            s = S(cpu, rdv(cpu, vs, n), n)[:n]; t = T(cpu, rdv(cpu, vt, n), n)[:n]
            cc = 0; any_ = 0; all_ = 1; aff = 0x30
            for i in range(n):
                a, b = s[i], t[i]
                c = (0, a == b, a < b, a <= b, 1, a != b, a >= b, a > b, a == 0, a != a, math.isinf(a),
                     a != a or math.isinf(a), a != 0, a == a, not math.isinf(a), not (a != a or math.isinf(a)))[cond]
                c = int(bool(c)); cc |= c << i; any_ |= c; all_ &= c; aff |= 1 << i
            cpu.vc[CTRL_CC] = (cpu.vc[CTRL_CC] & ~aff) | ((cc | (any_ << 4) | (all_ << 5)) & aff)
            eat(cpu); return
        if op == 0x1B and sub in (6, 7):          # vsge / vslt
            s = S(cpu, rdv(cpu, vs, n), n)[:n]; t = T(cpu, rdv(cpu, vt, n), n)[:n]
            d = [1.0 if ((a >= b) if sub == 6 else (a < b)) else 0.0 for a, b in zip(s, t)]
            wrv(cpu, vd, dsat(cpu, d, n), n); eat(cpu); return
        raise Stop(f"unimplemented VFPU op {op:#x} sub {sub} (insn {ins:#010x})", pc)

    if op == 0x34:
        sub = (ins >> 21) & 31; rt = (ins >> 16) & 31
        if sub == 0:                               # VFPU4: unary ops
            if rt == 3 or rt in (6, 7):            # vidt / vzero / vone
                if rt == 3:
                    offmask = 3 if n >= 3 else 1; off = vd & offmask
                    d = [1.0 if off == (i & offmask) else 0.0 for i in range(n)]
                else: d = [1.0 if rt == 7 else 0.0] * n
                wrv(cpu, vd, dsat(cpu, d, n), n); eat(cpu); return
            s = rdv(cpu, vs, n)
            if rt in (1, 2):
                s = apply_st(rewrite(cpu.vc[CTRL_S], 0, (0xF << 8) if rt == 1 else (0xF << 16)), s, n)[:n]
            elif rt >= 16:
                s = list(s); s[n-1:] = S(cpu, s[n-1:], 1, math.inf)[:1]
            else:
                s = S(cpu, s, n)[:n]
            def un(x):
                try:
                    if rt == 0: return x
                    if rt == 1: return x
                    if rt == 2: return x
                    if rt == 4: return 0.0 if x <= 0 else (1.0 if x > 1 else x)
                    if rt == 5: return -1.0 if x < -1 else (1.0 if x > 1 else x)
                    if rt == 16: return div(1.0, x)
                    if rt == 17: return div(1.0, math.sqrt(x)) if x > 0 else (math.inf if x == 0 else math.nan)
                    if rt == 18: return math.sin(x * math.pi / 2)
                    if rt == 19: return math.cos(x * math.pi / 2)
                    if rt == 20: return 2.0 ** x
                    if rt == 21: return math.log2(x) if x > 0 else (-math.inf if x == 0 else math.nan)
                    if rt == 22: return math.sqrt(x) if x >= 0 else math.nan
                    if rt == 23: return math.asin(x) * 2 / math.pi
                    if rt == 24: return -div(1.0, x)
                    if rt == 26: return -math.sin(x * math.pi / 2)
                    if rt == 28: return 2.0 ** -x
                except (ValueError, OverflowError, ZeroDivisionError): return math.nan
                raise Stop(f"unimplemented VFPU4 unary {rt} (insn {ins:#010x})", pc)
            d = [rnd(un(x)) for x in s]
            if rt >= 16: last_only_d(cpu, n)
            wrv(cpu, vd, dsat(cpu, d, n), n); eat(cpu); return
        if sub == 1:                               # VFPU7: random numbers, half floats, int<->packed conversions
            if rt == 0:                            # vrnds: seed
                import random; random.seed(cpu.v[vec_idx(vs, 1)[0]]); eat(cpu); return
            if rt in (1, 2, 3):                    # vrndi / vrndf1 / vrndf2
                import random
                if rt == 1: d = [random.getrandbits(32) for _ in range(n)]
                else:
                    base = 0x3F800000 if rt == 2 else 0x40000000
                    d = [base | random.getrandbits(23) for _ in range(n)]
                wr_ints(cpu, vd, d, n); eat(cpu); return
            if rt == 19:                           # vh2f
                sb = apply_st_bits(cpu.vc[CTRL_S], rdvb(cpu, vs, n), n)
                if n == 1: out = [h2f(sb[0] & 0xFFFF), h2f(sb[0] >> 16)]; on = 2
                else: out = [h2f(sb[0] & 0xFFFF), h2f(sb[0] >> 16), h2f(sb[1] & 0xFFFF), h2f(sb[1] >> 16)]; on = 4
                wrv(cpu, vd, dsat(cpu, out, on), on); eat(cpu); return
            if rt == 18:                           # vf2h
                sf = apply_st(cpu.vc[CTRL_S], rdv(cpu, vs, n) + [0.0] * (4 - n), 4)
                if n <= 2: out = [f2h(sf[0]) | f2h(sf[1]) << 16]; on = 1
                else: out = [f2h(sf[0]) | f2h(sf[1]) << 16, f2h(sf[2]) | f2h(sf[3]) << 16]; on = 2
                wr_ints(cpu, vd, out, on); eat(cpu); return
            if 24 <= rt <= 27:                     # vuc2i / vc2i / vus2i / vs2i
                sel = rt & 3; sb = apply_st_bits(cpu.vc[CTRL_S], rdvb(cpu, vs, n), n); d = [0] * 4; on = n
                if sel == 0:
                    v = sb[0]
                    for k in range(4): d[k] = (((v & 0xFF) * 0x01010101) & M32) >> 1; v >>= 8
                    on = 4
                elif sel == 1:
                    v = sb[0]; d = [(v & 0xFF) << 24, (v & 0xFF00) << 16, (v & 0xFF0000) << 8, v & 0xFF000000]; on = 4
                else:
                    on = 2; m_ = n
                    if n in (3, 4): m_ = 2
                    if m_ == 2: on = 4
                    for k in range(m_):
                        v = sb[k]
                        if sel == 2: d[2*k] = (v & 0xFFFF) << 15; d[2*k+1] = (v & 0xFFFF0000) >> 1
                        else: d[2*k] = (v & 0xFFFF) << 16; d[2*k+1] = v & 0xFFFF0000
                wr_ints(cpu, vd, d, on); eat(cpu); return
            if 28 <= rt <= 31:                     # vi2uc / vi2c / vi2us / vi2s
                sel = rt & 3; sb = apply_st_bits(cpu.vc[CTRL_S], rdvb(cpu, vs, n), 4); d = [0, 0]
                if sel == 0:
                    for k in range(4):
                        v = sgn32(sb[k]); v = 0 if v < 0 else v; d[0] |= ((v >> 23) & 0xFF) << (8*k)
                    on = 1
                elif sel == 1:
                    for k in range(4): d[0] |= (sb[k] >> 24) << (8*k)
                    on = 1
                else:
                    for k in range((n + 1) // 2):
                        lo, hi = sb[2*k], sb[2*k+1]
                        if sel == 2:
                            lo = max(sgn32(lo), 0) >> 15; hi = max(sgn32(hi), 0) >> 15
                            d[k] = (lo & 0xFFFF) | ((hi & 0xFFFF) << 16)
                        else: d[k] = (lo >> 16) | ((hi >> 16) << 16)
                    on = 2 if n >= 3 else 1
                wr_ints(cpu, vd, d, on); eat(cpu); return
            raise Stop(f"unimplemented VFPU7 op {rt} (insn {ins:#010x})", pc)
        if sub == 2:                               # VFPU9
            if rt == 6 or rt == 7:                 # vfad / vavg
                s = S(cpu, rdv(cpu, vs, n), n)[:n]
                tot = 0.0
                for x in s: tot = rnd(tot + x)
                if rt == 7: tot = rnd(tot / n)
                wrv(cpu, vd, dsat(cpu, [tot], 1), 1); eat(cpu); return
            if rt == 4:                            # vocp  (1 - x)
                sf = apply_st(rewrite(cpu.vc[CTRL_S], 0, 0xF << 16), rdv(cpu, vs, n), n)[:n]
                wrv(cpu, vd, dsat(cpu, [rnd(1.0 + x) for x in sf], n), n); eat(cpu); return
            if rt == 10:                           # vsgn
                sf = S(cpu, rdv(cpu, vs, n), n)[:n]
                d = [0.0 if (x != x or abs(x) < 1.1754943508222875e-38) else (1.0 if x > 0 else -1.0) for x in sf]
                wrv(cpu, vd, dsat(cpu, d, n), n); eat(cpu); return
            if rt in (2, 3):                       # vbfy1 / vbfy2 (butterflies)
                sf = rdv(cpu, vs, n) + [0.0] * (4 - n)
                d = [sf[0] + sf[1], sf[0] - sf[1], sf[2] + sf[3], sf[2] - sf[3]] if rt == 2 else \
                    [sf[0] + sf[2], sf[1] + sf[3], sf[0] - sf[2], sf[1] - sf[3]]
                wrv(cpu, vd, dsat(cpu, [rnd(x) for x in d], n), n); eat(cpu); return
            if rt in (25, 26, 27):                 # vt4444 / vt5551 / vt5650
                sb = apply_st_bits(cpu.vc[CTRL_S], rdvb(cpu, vs, 4), 4); cols = []
                for v in sb:
                    a = (v >> 24) & 0xFF; b_ = (v >> 16) & 0xFF; g = (v >> 8) & 0xFF; r_ = v & 0xFF
                    if rt == 25: cols.append((a >> 4) << 12 | (b_ >> 4) << 8 | (g >> 4) << 4 | (r_ >> 4))
                    elif rt == 26: cols.append((a >> 7) << 15 | (b_ >> 3) << 10 | (g >> 3) << 5 | (r_ >> 3))
                    else: cols.append((b_ >> 3) << 11 | (g >> 2) << 5 | (r_ >> 3))
                on = 1 if n == 1 else 2
                wr_ints(cpu, vd, [cols[0] | cols[1] << 16, cols[2] | cols[3] << 16], on); eat(cpu); return
            if rt == 16:                           # vmfvc
                imm = (ins >> 8) & 0x7F; cpu.v[vec_idx(vd, 1)[0]] = cpu.vc[imm] if imm < 16 else 0; return
            if rt == 17:                           # vmtvc
                imm = ins & 0x7F
                if imm < 16: cpu.vc[imm] = cpu.v[vec_idx(vs, 1)[0]]
                return
            raise Stop(f"unimplemented VFPU9 op {rt} (insn {ins:#010x})", pc)
        if sub == 3:                               # vcst
            c = CST[rt] if rt < len(CST) else 0.0
            wrv(cpu, vd, dsat(cpu, [rnd(c)] * n, n), n); eat(cpu); return
        if 16 <= sub <= 19 or sub == 20:           # vf2in/z/u/d, vi2f
            imm = (ins >> 16) & 31
            s = rdv(cpu, vs, n) if sub != 20 else rdvb(cpu, vs, n)
            if sub == 20:
                s = [float(x - 0x100000000 if x & 0x80000000 else x) for x in s]
                s = apply_st(cpu.vc[CTRL_S], s, n)[:n]
                wrv(cpu, vd, dsat(cpu, [rnd(x / (1 << imm)) for x in s], n), n); eat(cpu); return
            s = S(cpu, s, n)[:n]; out = []
            for x in s:
                if x != x: out.append(0x7FFFFFFF); continue
                v = x * (1 << imm)
                if v >= 2147483647.0: out.append(0x7FFFFFFF)
                elif v <= -2147483648.0: out.append(0x80000000)
                else:
                    if sub == 16: iv = int(math.floor(v + 0.5)) if v >= 0 or v != math.floor(v) + 0.5 else int(math.floor(v + 0.5))
                    elif sub == 17: iv = int(v)
                    elif sub == 18: iv = math.ceil(v)
                    else: iv = math.floor(v)
                    out.append(iv & M32)
            wrvb(cpu, vd, out, n); eat(cpu); return
        if sub == 21:                              # vcmov
            tf = (ins >> 19) & 1; imm3 = (ins >> 16) & 7
            s = S(cpu, rdv(cpu, vs, n), n)[:n]; d = T(cpu, rdv(cpu, vd, n), n)[:n]; cc = cpu.vc[CTRL_CC]
            if imm3 == 6:
                for i in range(n):
                    if ((cc >> i) & 1) == (not tf): d[i] = s[i]
            elif ((cc >> imm3) & 1) == (not tf): d = list(s)
            wrv(cpu, vd, dsat(cpu, d, n), n); eat(cpu); return
        raise Stop(f"unimplemented VFPU(0x34) sub {sub} (insn {ins:#010x})", pc)

    if op == 0x3C:
        sub = (ins >> 21) & 31
        if sub < 4:                                # vmmul
            s = rdm(cpu, vs, n); t = rdm(cpu, vt, n); d = [0.0] * 16
            for a in range(n):
                for b in range(n):
                    sa = s[b*4:b*4+4] if (a == n-1 and b == n-1) is False else S(cpu, s[b*4:b*4+4], 4)
                    ta = t[a*4:a*4+4] if (a == n-1 and b == n-1) is False else T(cpu, t[a*4:a*4+4], 4)
                    d[a*4+b] = dot(sa[:n], ta[:n])
            last_only_d(cpu, n)
            row = dsat(cpu, d[4*(n-1):4*n], n); d[4*(n-1):4*n] = row
            wrm(cpu, vd, d, n); eat(cpu); return
        if 4 <= sub < 16:                          # vtfm / vhtfm
            ins2 = (ins >> 23) & 3
            msz = ins2 + 1
            s = rdm(cpu, vs, msz); t = rdv(cpu, vt, msz)
            tn = min(n, ins2 + 1)
            t2 = [t[i] if i < tn else (1.0 if i == ins2 else 0.0) for i in range(4)]
            d = [0.0] * 4
            for i in range(ins2): d[i] = dot(s[i*4:i*4+msz], t2[:msz])
            sl = S(cpu, s[ins2*4:ins2*4+4], 4)
            cn = [None, 0 if n < 2 else None, 0 if n < 3 else None, 0 if n < 4 else None]
            if ins2 >= n and ins2 in (1, 2, 3): cn[ins2] = 1
            tp = apply_st(rewrite(cpu.vc[CTRL_T], sw(0, 3 if n < 2 else 0, 3 if n < 3 else 0, 3 if n < 4 else 0), consts(*cn)), t, 4)
            d[ins2] = dot(sl[:msz], tp[:msz])
            last_only_d(cpu, msz)
            wrv(cpu, vd, dsat(cpu, d, msz), msz); eat(cpu); return
        if 16 <= sub < 20:                         # vmscl
            s = rdm(cpu, vs, n); sc = f32(cpu.v[vec_idx(vt, 1)[0]]); d = [0.0] * 16
            for a in range(n):
                for b in range(n): d[a*4+b] = rnd(s[a*4+b] * sc)
            wrm(cpu, vd, d, n); eat(cpu); return
        if 20 <= sub < 24:                         # vcrsp.t / vqmul.q
            s = rdv(cpu, vs, n) + [0.0] * (4 - n); t = rdv(cpu, vt, n) + [0.0] * (4 - n); d = [0.0] * 4
            if n == 3:
                d[0] = dot(s, [0.0, t[2], -t[1], 0.0]); d[1] = dot(s, [-t[2], 0.0, t[0], 0.0])
                tp = apply_st(rewrite(cpu.vc[CTRL_T], 0xFF | (0xF << 16), sw(1, 0, 3, 2) | (2 << 16)), t, 4)
                d[2] = dot(S(cpu, s, 4), tp)
            elif n == 4:
                d[0] = dot(s, [t[3], t[2], -t[1], t[0]]); d[1] = dot(s, [-t[2], t[3], t[0], t[1]])
                d[2] = dot(s, [t[1], -t[0], t[3], t[2]])
                tp = apply_st(rewrite(cpu.vc[CTRL_T], 0xFF | (0xF << 16), sw(0, 1, 2, 3) | (7 << 16)), t, 4)
                d[3] = dot(S(cpu, s, 4), tp)
            if n > 1: last_only_d(cpu, n)
            wrv(cpu, vd, dsat(cpu, d, n), n); eat(cpu); return
        if sub == 29:                              # vrot
            imm = (ins >> 16) & 31; neg = bool(imm & 16); sl = (imm >> 2) & 3; cl = imm & 3
            x = f32(cpu.v[vec_idx(vs, 1)[0]])
            sine = math.sin(x * math.pi / 2); cosine = math.cos(x * math.pi / 2)
            if neg: sine = -sine
            d = [0.0] * 4
            if sl == cl: d = [sine] * 4
            else: d[sl] = sine
            d[cl] = cosine
            wrv(cpu, vd, dsat(cpu, [rnd(q) for q in d], n), n); eat(cpu); return
        if sub == 28:                              # vmmov / vmidt / vmzero / vmone
            k = (ins >> 16) & 15
            if k == 0:
                s = rdm(cpu, vs, n); wrm(cpu, vd, s, n); eat(cpu); return
            if k in (3, 6, 7):
                d = [0.0] * 16
                for a in range(n):
                    for b in range(n): d[a*4+b] = (1.0 if a == b else 0.0) if k == 3 else (1.0 if k == 7 else 0.0)
                wrm(cpu, vd, d, n); eat(cpu); return
        raise Stop(f"unimplemented VFPU6 sub {sub} (insn {ins:#010x})", pc)

    if op == 0x3F and (ins >> 16) == 0xFFFF: return      # vnop / vsync / vflush: nothing to do here
    names = {0x1A: 'VFPU2', 0x3F: 'VFPU7'}
    raise Stop(f"unimplemented VFPU {names.get(op, hex(op))} (insn {ins:#010x})", pc)
