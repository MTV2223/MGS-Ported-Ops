#!/usr/bin/env python3
"""
Minimal Allegrex (PSP MIPS) interpreter - EXPERIMENTAL, built to run MGS Portable Ops startup code.
Implements: integer ISA, delay slots / likely branches, unaligned loads/stores, Allegrex extras
(clz/clo, ext/ins, seb/seh, wsbh, bitrev, rotr, min/max, movz/movn), single-precision FPU.
NOT implemented (stops with a report): VFPU (vector unit), COP0 and anything unknown.
"""
import struct

RAM_LO, RAM_HI = 0x08000000, 0x0A000000      # 32 MB user RAM window (kernel part unused)
VRAM_LO, VRAM_HI = 0x04000000, 0x04200000
SCR_LO, SCR_HI = 0x00010000, 0x00014000

class Yield(Exception):
    pass

class Stop(Exception):
    def __init__(self, reason, pc=None): super().__init__(reason); self.reason = reason; self.pc = pc

class Limit(Stop): pass
class Returned(Stop): pass

def s32(x): x &= 0xFFFFFFFF; return x - 0x100000000 if x & 0x80000000 else x
def s16(x): x &= 0xFFFF; return x - 0x10000 if x & 0x8000 else x
def f2i(f):
    try: return struct.unpack('<I', struct.pack('<f', f))[0]
    except OverflowError: return 0x7F800000 if f > 0 else 0xFF800000
def i2f(i): return struct.unpack('<f', struct.pack('<I', i & 0xFFFFFFFF))[0]

# ---------------------------------------------------------------- optional fast native core (psp_core.c)
import os as _os, ctypes as _ct
_CORE = None
VFPU_OPS = (0x12, 0x18, 0x19, 0x1A, 0x1B, 0x32, 0x34, 0x35, 0x36, 0x37, 0x3A, 0x3C, 0x3D, 0x3E, 0x3F)

class _CState(_ct.Structure):
    _fields_ = [('r', _ct.c_uint32 * 32), ('hi', _ct.c_uint32), ('lo', _ct.c_uint32), ('pc', _ct.c_uint32),
                ('f', _ct.c_uint32 * 32), ('fcr31', _ct.c_uint32), ('cc', _ct.c_uint32),
                ('v', _ct.c_uint32 * 128), ('vc', _ct.c_uint32 * 16), ('halt', _ct.c_uint32),
                ('fault_addr', _ct.c_uint32), ('icount', _ct.c_uint64),
                ('ram', _ct.c_void_p), ('vram', _ct.c_void_p), ('scr', _ct.c_void_p), ('stubmap', _ct.c_void_p)]

def core():
    """the compiled CPU core, or None (then the slower Python one is used)"""
    global _CORE
    if _CORE is None:
        _CORE = False
        if _os.environ.get('PSP_NOCORE'): return None
        here = _os.path.dirname(_os.path.abspath(__file__))
        for n in ('psp_core.dll', 'psp_core.so', 'psp_core.dylib'):
            p = _os.path.join(here, n)
            if _os.path.exists(p):
                try:
                    lib = _ct.CDLL(p); lib.core_run.restype = _ct.c_int; lib.core_run.argtypes = [_ct.c_void_p, _ct.c_int64]
                    if lib.core_version() >= 4 and lib.core_state_size() == _ct.sizeof(_CState): _CORE = lib; break
                except OSError: pass
    return _CORE or None

import array as _array, struct as _struct
_REGS = _struct.Struct('<69I')                    # r[32] hi lo pc f[32] fcr31 cc: the start of _CState
_pack = _REGS.pack_into; _unpack = _REGS.unpack_from
def _put(dst, lst, n):
    """python list of 32-bit values -> ctypes array, as one block copy"""
    try: _ct.memmove(dst, _array.array('I', lst).tobytes(), 4 * n)
    except (OverflowError, TypeError): dst[:] = [int(x) & 0xFFFFFFFF for x in lst]
def _get(src): return _array.array('I', bytes(src)).tolist()

class CPU:
    def __getstate__(self):                        # save states: leave out the native core's bookkeeping
        d = dict(self.__dict__)
        for k in ('_cst', '_crefs', '_stubmap', '_stubkey', '_vlast', '_vclast'): d.pop(k, None)
        return d

    def _native_setup(self):
        st = _CState(); self._cst = st; refs = []
        for name, ba in (('ram', self.ram), ('vram', self.vram), ('scr', self.scr)):
            arr = (_ct.c_uint8 * len(ba)).from_buffer(ba); refs.append(arr); setattr(st, name, _ct.addressof(arr))
        self._stubmap = bytearray((RAM_HI - RAM_LO) // 4); arr = (_ct.c_uint8 * len(self._stubmap)).from_buffer(self._stubmap)
        refs.append(arr); st.stubmap = _ct.addressof(arr); self._crefs = refs; self._stubkey = None

    def _native_stubs(self):
        key = (len(self.stubs), sum(self.stubs))
        if key == self._stubkey: return
        sm = self._stubmap; sm[:] = bytes(len(sm))
        for a in self.stubs:
            a &= 0x3FFFFFFF
            if RAM_LO <= a < RAM_HI: sm[(a - RAM_LO) >> 2] = 1
        self._stubkey = key

    def __init__(self):
        self.r = [0]*32; self.hi = self.lo = 0; self.pc = 0
        self.f = [0]*32; self.fcr31 = 0; self.cc = False
        self.ram = bytearray(RAM_HI - RAM_LO); self.vram = bytearray(VRAM_HI - VRAM_LO); self.scr = bytearray(SCR_HI - SCR_LO)
        self.stubs = {}; self.hle = None; self.icount = 0; self.vfpu_seen = {}; self.halt = 0x08800000
        import psp_vfpu; self._vfpu = psp_vfpu; psp_vfpu.init(self)

    # ---------------------------------------------------------------- memory
    def _loc(self, a, n):
        a &= 0x3FFFFFFF
        if RAM_LO <= a and a + n <= RAM_HI: return self.ram, a - RAM_LO
        if VRAM_LO <= a and a + n <= VRAM_HI: return self.vram, a - VRAM_LO
        if SCR_LO <= a and a + n <= SCR_HI: return self.scr, a - SCR_LO
        raise Stop(f"bad memory access at {a:#010x}", self.pc)
    def r8(self, a): b, o = self._loc(a, 1); return b[o]
    def r16(self, a): b, o = self._loc(a, 2); return b[o] | b[o+1] << 8
    def r32(self, a): b, o = self._loc(a, 4); return b[o] | b[o+1] << 8 | b[o+2] << 16 | b[o+3] << 24
    def w8(self, a, v): b, o = self._loc(a, 1); b[o] = v & 0xFF
    def w16(self, a, v): b, o = self._loc(a, 2); b[o] = v & 0xFF; b[o+1] = (v >> 8) & 0xFF
    def w32(self, a, v):
        b, o = self._loc(a, 4); b[o] = v & 0xFF; b[o+1] = (v >> 8) & 0xFF; b[o+2] = (v >> 16) & 0xFF; b[o+3] = (v >> 24) & 0xFF
    def cstr(self, a, n=256):
        s = bytearray()
        while len(s) < n:
            c = self.r8(a + len(s))
            if not c: break
            s.append(c)
        return s.decode('latin1')
    def load_bytes(self, a, data): b, o = self._loc(a, len(data)); b[o:o+len(data)] = data

    # ---------------------------------------------------------------- run loop
    def run(self, max_instr=5_000_000):
        lib = core()
        if lib is not None: return self._run_native(lib, max_instr)
        end = self.icount + max_instr
        while self.icount < end:
            pc = self.pc
            if pc == self.halt: raise Returned("returned", pc)
            if pc in self.stubs:
                lib, nid = self.stubs[pc]
                jump = self.hle(self, lib, nid)   # may raise Stop; returns a jump target for module-provided functions
                self.pc = jump if jump else self.r[31]; self.icount += 1
                continue
            self.step()
        raise Limit("instruction limit reached", self.pc)

    def _run_native(self, lib, max_instr):
        if getattr(self, '_cst', None) is None: self._native_setup()
        self._native_stubs(); st = self._cst; run = lib.core_run; stubs = self.stubs
        vdirty = getattr(lib, 'core_vdirty', None); vfast = False         # vfast: st.v already holds self.v
        sta = _ct.addressof(st); mv = memoryview(st).cast('B'); M = 0xFFFFFFFF   # r, hi, lo, pc, f, fcr31, cc sit together
        if vdirty is not None: vdirty.restype = _ct.c_uint32
        end = self.icount + max_instr
        while self.icount < end:
            pc = self.pc
            if pc == self.halt: raise Returned("returned", pc)
            if pc in stubs:
                lib_, nid = stubs[pc]
                jump = self.hle(self, lib_, nid)
                self.pc = jump if jump else self.r[31]; self.icount += 1
                if len(stubs) != self._stubkey[0]: self._native_stubs()
                continue
            # hand the registers to the native core, run, take them back (in place: system calls keep a reference to r).
            # Copied as raw bytes (much faster than item by item); the 144 vector registers only when they changed.
            r, f = self.r, self.f
            try: _pack(mv, 0, *r, self.hi, self.lo, pc, *f, self.fcr31, 1 if self.cc else 0)
            except _struct.error:
                _put(st.r, r, 32); _put(st.f, f, 32); st.hi = self.hi & M; st.lo = self.lo & M; st.pc = pc & M
                st.fcr31 = self.fcr31 & M; st.cc = 1 if self.cc else 0
            st.halt = self.halt; st.icount = self.icount
            if vfast and (self.v != self._vlast or self.vc != self._vclast): vfast = False
            if not vfast: _put(st.v, self.v, 128); _put(st.vc, self.vc, 16)
            code = run(sta, end - self.icount)
            w = _unpack(mv, 0); r[:] = w[0:32]; f[:] = w[35:67]
            self.hi, self.lo, self.pc, self.fcr31 = w[32], w[33], w[34], w[67]; self.cc = bool(w[68]); self.icount = st.icount
            if vdirty is None or vdirty() or not vfast:
                self.v[:] = _get(st.v); self.vc[:] = _get(st.vc); self._vlast = list(self.v); self._vclast = list(self.vc)
            vfast = vdirty is not None
            if code in (0, 1, 2): continue
            self.step()                                  # something only the Python version does: one step, then back
        raise Limit("instruction limit reached", self.pc)

    def step(self):
        pc = self.pc
        ins = self.r32(pc)
        self.icount += 1
        self.pc = pc + 4
        self.exec(ins, pc)

    def delay(self, pc):                           # execute the delay-slot instruction at pc+4
        self.icount += 1
        self.exec(self.r32(pc + 4), pc + 4)

    def branch(self, pc, taken, target, likely=False, link=False):
        if link: self.r[31] = (pc + 8) & 0xFFFFFFFF
        if likely and not taken: self.pc = pc + 8; return
        self.delay(pc)
        self.pc = target if taken else pc + 8

    # ---------------------------------------------------------------- decode/execute
    def exec(self, ins, pc):
        r = self.r
        op = ins >> 26; rs = (ins >> 21) & 31; rt = (ins >> 16) & 31; rd = (ins >> 11) & 31
        sa = (ins >> 6) & 31; fn = ins & 63; imm = ins & 0xFFFF; simm = s16(imm)
        M = 0xFFFFFFFF
        if op == 0:
            if fn == 0x21: r[rd] = (r[rs] + r[rt]) & M
            elif fn == 0x25: r[rd] = r[rs] | r[rt]
            elif fn == 0x00: r[rd] = (r[rt] << sa) & M
            elif fn == 0x02:
                r[rd] = ((r[rt] >> sa) | (r[rt] << (32 - sa)) & M) & M if rs == 1 and sa else r[rt] >> sa
            elif fn == 0x03: r[rd] = (s32(r[rt]) >> sa) & M
            elif fn == 0x04: r[rd] = (r[rt] << (r[rs] & 31)) & M
            elif fn == 0x06:
                n = r[rs] & 31
                r[rd] = (((r[rt] >> n) | (r[rt] << (32 - n))) & M) if (ins >> 6) & 1 and n else r[rt] >> n
            elif fn == 0x07: r[rd] = (s32(r[rt]) >> (r[rs] & 31)) & M
            elif fn == 0x08: t = r[rs]; self.delay(pc); self.pc = t
            elif fn == 0x09:
                t = r[rs]; r[rd] = (pc + 8) & M; self.delay(pc); self.pc = t
            elif fn == 0x0A:
                if r[rt] == 0: r[rd] = r[rs]
            elif fn == 0x0B:
                if r[rt] != 0: r[rd] = r[rs]
            elif fn == 0x0C: raise Stop(f"raw syscall {ins >> 6 & 0xFFFFF:#x}", pc)
            elif fn == 0x0D: raise Stop("break instruction", pc)
            elif fn == 0x0F: pass
            elif fn == 0x10: r[rd] = self.hi
            elif fn == 0x11: self.hi = r[rs]
            elif fn == 0x12: r[rd] = self.lo
            elif fn == 0x13: self.lo = r[rs]
            elif fn == 0x16:
                v = r[rs]; r[rd] = 32 - v.bit_length()
            elif fn == 0x17:
                v = (~r[rs]) & M; r[rd] = 32 - v.bit_length()
            elif fn == 0x18:
                p = s32(r[rs]) * s32(r[rt]); self.lo = p & M; self.hi = (p >> 32) & M
            elif fn == 0x19:
                p = r[rs] * r[rt]; self.lo = p & M; self.hi = (p >> 32) & M
            elif fn == 0x1A:
                a, b = s32(r[rs]), s32(r[rt])
                if b: q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1); self.lo = q & M; self.hi = (a - q*b) & M
            elif fn == 0x1B:
                if r[rt]: self.lo = r[rs] // r[rt]; self.hi = r[rs] % r[rt]
            elif fn in (0x1C, 0x1D, 0x2E, 0x2F):
                acc = (self.hi << 32) | self.lo
                p = (s32(r[rs]) * s32(r[rt])) if fn in (0x1C, 0x2E) else r[rs] * r[rt]
                acc = acc + p if fn in (0x1C, 0x1D) else acc - p
                acc &= (1 << 64) - 1; self.lo = acc & M; self.hi = acc >> 32
            elif fn == 0x20 or fn == 0x21: r[rd] = (r[rs] + r[rt]) & M
            elif fn == 0x22 or fn == 0x23: r[rd] = (r[rs] - r[rt]) & M
            elif fn == 0x24: r[rd] = r[rs] & r[rt]
            elif fn == 0x26: r[rd] = r[rs] ^ r[rt]
            elif fn == 0x27: r[rd] = (~(r[rs] | r[rt])) & M
            elif fn == 0x2A: r[rd] = 1 if s32(r[rs]) < s32(r[rt]) else 0
            elif fn == 0x2B: r[rd] = 1 if r[rs] < r[rt] else 0
            elif fn == 0x2C: r[rd] = r[rs] if s32(r[rs]) > s32(r[rt]) else r[rt]
            elif fn == 0x2D: r[rd] = r[rs] if s32(r[rs]) < s32(r[rt]) else r[rt]
            else: raise Stop(f"unimplemented SPECIAL funct {fn:#x} (insn {ins:#010x})", pc)
        elif op == 1:
            v = s32(r[rs]); tgt = (pc + 4 + (simm << 2)) & M
            if rt == 0: self.branch(pc, v < 0, tgt)
            elif rt == 1: self.branch(pc, v >= 0, tgt)
            elif rt == 2: self.branch(pc, v < 0, tgt, True)
            elif rt == 3: self.branch(pc, v >= 0, tgt, True)
            elif rt == 0x10: self.branch(pc, v < 0, tgt, False, True)
            elif rt == 0x11: self.branch(pc, v >= 0, tgt, False, True)
            else: raise Stop(f"unimplemented REGIMM {rt:#x}", pc)
        elif op == 2: self.delay(pc); self.pc = ((pc + 4) & 0xF0000000) | ((ins & 0x3FFFFFF) << 2)
        elif op == 3:
            self.r[31] = (pc + 8) & M; self.delay(pc); self.pc = ((pc + 4) & 0xF0000000) | ((ins & 0x3FFFFFF) << 2)
        elif op in (4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17):
            a, b = s32(r[rs]), s32(r[rt]); tgt = (pc + 4 + (simm << 2)) & M
            k = op & 7; likely = op >= 0x14
            taken = (a == b) if k == 4 else (a != b) if k == 5 else (a <= 0) if k == 6 else (a > 0)
            self.branch(pc, taken, tgt, likely)
        elif op == 8 or op == 9: r[rt] = (r[rs] + simm) & M
        elif op == 0xA: r[rt] = 1 if s32(r[rs]) < simm else 0
        elif op == 0xB: r[rt] = 1 if r[rs] < (simm & M) else 0
        elif op == 0xC: r[rt] = r[rs] & imm
        elif op == 0xD: r[rt] = r[rs] | imm
        elif op == 0xE: r[rt] = r[rs] ^ imm
        elif op == 0xF: r[rt] = (imm << 16) & M
        elif op == 0x11: self.cop1(ins, pc, rs, rt, rd, sa, fn, simm)
        elif op == 0x1F:
            if fn == 0:   # ext
                size = rd + 1; r[rt] = (r[rs] >> sa) & ((1 << size) - 1)
            elif fn == 4: # ins
                msb = rd; lsb = sa; size = msb - lsb + 1; mask = ((1 << size) - 1) << lsb
                r[rt] = (r[rt] & ~mask & M) | ((r[rs] << lsb) & mask)
            elif fn == 0x20:
                if sa == 0x10: v = r[rt] & 0xFF; r[rd] = (v | 0xFFFFFF00) if v & 0x80 else v
                elif sa == 0x18: v = r[rt] & 0xFFFF; r[rd] = (v | 0xFFFF0000) if v & 0x8000 else v
                elif sa == 0x02: v = r[rt]; r[rd] = ((v & 0x00FF00FF) << 8 | (v >> 8) & 0x00FF00FF) & M
                elif sa == 0x03: v = r[rt]; r[rd] = int.from_bytes(v.to_bytes(4, 'little'), 'big')
                elif sa == 0x14: r[rd] = int(format(r[rt], '032b')[::-1], 2)
                else: raise Stop(f"unimplemented BSHFL {sa:#x}", pc)
            else: raise Stop(f"unimplemented SPECIAL3 funct {fn:#x}", pc)
        elif op == 0x20: v = self.r8(r[rs] + simm); r[rt] = (v | 0xFFFFFF00) if v & 0x80 else v
        elif op == 0x21: v = self.r16(r[rs] + simm); r[rt] = (v | 0xFFFF0000) if v & 0x8000 else v
        elif op == 0x23: r[rt] = self.r32(r[rs] + simm)
        elif op == 0x24: r[rt] = self.r8(r[rs] + simm)
        elif op == 0x25: r[rt] = self.r16(r[rs] + simm)
        elif op == 0x22:   # lwl
            a = (r[rs] + simm) & M; al = a & ~3; sh = (a & 3); w = self.r32(al)
            r[rt] = ((w << (8 * (3 - sh))) & M) | (r[rt] & ((1 << (8 * (3 - sh))) - 1))
        elif op == 0x26:   # lwr
            a = (r[rs] + simm) & M; al = a & ~3; sh = (a & 3); w = self.r32(al)
            keep = (M << (32 - 8 * sh)) & M if sh else 0
            r[rt] = (w >> (8 * sh)) | (r[rt] & keep)
        elif op == 0x28: self.w8(r[rs] + simm, r[rt])
        elif op == 0x29: self.w16(r[rs] + simm, r[rt])
        elif op == 0x2B: self.w32(r[rs] + simm, r[rt])
        elif op == 0x2A:   # swl
            a = (r[rs] + simm) & M; al = a & ~3; sh = a & 3; w = self.r32(al)
            m = (1 << (8 * (sh + 1))) - 1
            self.w32(al, (w & ~m & M) | (r[rt] >> (8 * (3 - sh))) & m)
        elif op == 0x2E:   # swr
            a = (r[rs] + simm) & M; al = a & ~3; sh = a & 3; w = self.r32(al)
            m = (M << (8 * sh)) & M
            self.w32(al, (w & ~m & M) | (r[rt] << (8 * sh)) & m)
        elif op == 0x2F: pass                                  # cache
        elif op == 0x30: r[rt] = self.r32(r[rs] + simm)         # ll
        elif op == 0x38: self.w32(r[rs] + simm, r[rt]); r[rt] = 1  # sc
        elif op == 0x31: self.f[rt] = self.r32(r[rs] + simm)
        elif op == 0x39: self.w32(r[rs] + simm, self.f[rt])
        elif op in (0x12, 0x18, 0x19, 0x1A, 0x1B, 0x32, 0x34, 0x35, 0x36, 0x37, 0x3A, 0x3C, 0x3D, 0x3E, 0x3F):
            self._vfpu.execute(self, ins, pc)
        else:
            name = {0x12: 'COP2/VFPU move', 0x18: 'VFPU0', 0x19: 'VFPU1', 0x1A: 'VFPU(vpfxs)', 0x1B: 'VFPU3', 0x32: 'lv.s', 0x34: 'VFPU4',
                    0x35: 'VFPU5', 0x36: 'lv.q', 0x37: 'VFPU(vpfx)', 0x3A: 'sv.s', 0x3C: 'VFPU6', 0x3E: 'sv.q', 0x3F: 'VFPU7', 0x10: 'COP0'}.get(op, f'opcode {op:#x}')
            raise Stop(f"unimplemented: {name} (insn {ins:#010x})", pc)
        r[0] = 0

    # ---------------------------------------------------------------- FPU
    def cop1(self, ins, pc, rs, rt, rd, sa, fn, simm):
        r, f = self.r, self.f; M = 0xFFFFFFFF
        if rs == 0: r[rt] = f[rd]
        elif rs == 2: r[rt] = self.fcr31 if rd == 31 else 0
        elif rs == 4: f[rd] = r[rt]
        elif rs == 6:
            if rd == 31: self.fcr31 = r[rt]
        elif rs == 8:
            tgt = (pc + 4 + (simm << 2)) & M; t = (rt & 1) == 1; likely = bool(rt & 2)
            self.branch(pc, self.cc == t, tgt, likely)
        elif rs == 16:   # single
            a = i2f(f[rd]); b = i2f(f[rt]); d = sa
            if fn == 0: f[d] = f2i(a + b)
            elif fn == 1: f[d] = f2i(a - b)
            elif fn == 2: f[d] = f2i(a * b)
            elif fn == 3: f[d] = f2i(a / b) if b != 0 else (0x7F800000 if a >= 0 else 0xFF800000)
            elif fn == 4: f[d] = f2i(a ** 0.5) if a >= 0 else 0x7FC00000
            elif fn == 5: f[d] = f2i(abs(a))
            elif fn == 6: f[d] = f[rd]
            elif fn == 7: f[d] = f[rd] ^ 0x80000000
            elif fn in (0xC, 0xD, 0xE, 0xF, 0x24):
                import math
                try:
                    v = {0xC: round, 0xD: math.trunc, 0xE: math.ceil, 0xF: math.floor, 0x24: round}[fn](a)
                    f[d] = int(max(-2**31, min(2**31 - 1, v))) & M
                except (OverflowError, ValueError): f[d] = 0x7FFFFFFF
            elif fn >= 0x30:
                c = fn & 15; un = a != a or b != b
                res = ((c & 1) and un) or ((c & 2) and a == b) or ((c & 4) and a < b)
                self.cc = bool(res) if not (un and not (c & 1)) else False
            else: raise Stop(f"unimplemented FPU.S funct {fn:#x}", pc)
        elif rs == 20:   # word
            if fn == 0x20: f[sa] = f2i(float(s32(f[rd])))
            else: raise Stop(f"unimplemented FPU.W funct {fn:#x}", pc)
        else: raise Stop(f"unimplemented COP1 sub-op {rs:#x}", pc)
