"""
Sound for the port.
  - Sas: the PSP sound chip (sceSasCore). Decodes the game's VAG/ADPCM sound effects and music voices,
    applies pitch, volume and a simple envelope, and mixes them into the buffers the game asks for.
  - AudioOut: what the game sends to the speakers (sceAudioOutput...). It is written to  sound_out.wav  next to
    the EBOOT so you can listen to it. (Live playback comes with the fast native version; Python is too slow
    to keep a speaker fed in real time.)
"""
import struct, wave, os

VAG_F = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60), (0, 0), (0, 0), (0, 0),
         (0, 0), (0, 0), (0, 0), (0, 0), (0, 0), (0, 0), (0, 0), (0, 0)]
ENV_MAX = 0x40000000

def clamp16(v): return -32768 if v < -32768 else 32767 if v > 32767 else v

# ---- the sound chip's volume envelope (attack, decay, sustain, release), as PPSSPP has it (sceSas.cpp)
LIN_INC, LIN_DEC, LIN_BENT, EXP_DEC, EXP_INC, DIRECT = range(6)
ATTACK, DECAY, SUSTAIN, RELEASE, OFF = range(5)
def _simple_rate(n):
    n &= 0x7F
    if n == 0x7F: return 0
    r = ((7 - (n & 3)) << 26) >> (n >> 2); return r or 1
def _exp_rate(n):
    n &= 0x7F
    if n == 0x7F: return 0
    r = ((7 - (n & 3)) << 24) >> (n >> 2); return r or 1

class Voice:
    # (defaults for voices from older save states, made before the envelope was there)
    adsr = False; st = OFF; ar = dr = sr = rr = sl = 0; at = LIN_INC; dt = stp = rt = LIN_DEC
    def __init__(self):
        self.addr = 0; self.size = 0; self.loop = 0; self.pcm = False; self.pitch = 0x1000
        self.lv = self.rv = 0x1000; self.on = False; self.playing = False; self.env = 0; self.release = False
        self.paused = False; self.loopflag = False; self.rel_step = ENV_MAX // 2205          # ~50 ms release
        # the envelope: only once the game has set one (before that: full volume, as before)
        self.adsr = False; self.st = OFF
        self.ar, self.at, self.dr, self.dt, self.sr, self.stp, self.rr, self.rt, self.sl = 0, LIN_INC, 0, LIN_DEC, 0, LIN_DEC, 0, LIN_DEC, 0
        self.reset()
    def simple_adsr(self, e1, e2):
        """SetSimpleADSR: the two packed words"""
        self.adsr = True
        self.ar = _simple_rate(e1 >> 8); self.at = LIN_INC if not (e1 & 0x8000) else LIN_BENT
        n = (e1 >> 4) & 0xF; self.dr = 0x7FFFFFFF if n == 0 else 0x80000000 >> n; self.dt = EXP_DEC
        self.sl = ((e1 & 0xF) + 1) << 26
        self.stp = (e2 >> 14) & 3
        self.sr = _exp_rate(e2 >> 6) if self.stp == EXP_DEC else _simple_rate(e2 >> 6)
        self.rt = EXP_DEC if (e2 & 0x20) else LIN_DEC; n = e2 & 0x1F
        if n == 31: self.rr = 0
        elif self.rt == LIN_DEC: self.rr = 0x40000000 if n == 30 else 1 if n == 29 else 0x10000000 >> n
        else: self.rr = 0x7FFFFFFF if n == 0 else 0x80000000 >> n
    def walk(self, typ, rate):
        h = self.env
        if typ == LIN_INC: h += rate
        elif typ == LIN_DEC: h -= rate
        elif typ == LIN_BENT: h += rate if h <= ENV_MAX * 3 // 4 else rate // 4
        elif typ == EXP_DEC:
            d = h - ENV_MAX; d += (-d * rate) >> 32; h = d + ENV_MAX - (rate + 3) // 4
        elif typ == EXP_INC:
            d = h - ENV_MAX; d += (-d * rate) >> 32; h = d + 0x4000 + ENV_MAX
        else: h = rate
        self.env = h
    def env_step(self):
        st = self.st
        if st == ATTACK:
            self.walk(self.at, self.ar)
            if self.env >= ENV_MAX or self.env < 0: self.env = min(max(self.env, 0), ENV_MAX); self.st = DECAY
        elif st == DECAY:
            self.walk(self.dt, self.dr)
            if self.env < self.sl: self.env = min(max(self.env, 0), ENV_MAX); self.st = SUSTAIN
        elif st == SUSTAIN:
            self.walk(self.stp, self.sr)
            if self.env <= 0: self.env = 0; self.st = RELEASE
        elif st == RELEASE:
            self.walk(self.rt, self.rr)
            if self.env <= 0: self.env = 0; self.st = OFF
    def reset(self):
        self.blk = 0; self.s1 = self.s2 = 0; self.buf = []; self.bi = 0; self.frac = 0; self.loop_blk = None; self.done = False
        self.cur = 0; self.prev = 0

    def next_sample(self, cpu):
        """one source sample at the voice's own rate"""
        if self.pcm:
            i = self.blk
            if i >= self.size // 2:
                if self.loop >= 0 and self.loop < self.size // 2 and self.loopflag: i = self.blk = self.loop
                else: self.done = True; return 0
            self.blk += 1
            v = cpu.r16(self.addr + 2 * i); return v - 0x10000 if v & 0x8000 else v
        if self.bi >= len(self.buf):
            if self.done: return 0
            self.decode_block(cpu)
            if not self.buf: self.done = True; return 0
        v = self.buf[self.bi]; self.bi += 1; return v

    def decode_block(self, cpu):
        a = self.addr + self.blk * 16
        if self.size and self.blk * 16 >= self.size: self.buf = []; self.done = True; return
        try: mb, mo = cpu._loc(a, 16); b = bytes(mb[mo:mo + 16])
        except Exception: self.buf = []; self.done = True; return
        pred, shift, flags = b[0] >> 4, b[0] & 15, b[1]
        if flags & 4: self.loop_blk = self.blk
        if flags == 7: self.buf = []; self.done = True; return               # end marker block
        f0, f1 = VAG_F[pred]; s1, s2 = self.s1, self.s2; out = []
        sh = 12 - shift if shift <= 12 else 0
        for i in range(28):
            n = (b[2 + (i >> 1)] >> (4 if i & 1 else 0)) & 15
            n = (n - 16 if n & 8 else n) << sh
            s = clamp16(n + ((s1 * f0 + s2 * f1) >> 6)); out.append(s); s2, s1 = s1, s
        self.s1, self.s2 = s1, s2; self.buf = out; self.bi = 0
        if flags & 1:                                                           # end of sound
            # (loop back only if the game asked for a looping voice - SetVoice's loop argument. The loop marks are
            #  also in one-shot sounds: they repeated until the game keyed them off - the item pickup sound dragged on)
            if (flags & 2) and self.loop_blk is not None and self.loop: self.blk = self.loop_blk
            else: self.blk += 1; self.endnext = True; self.size = self.blk * 16  # play this block, then stop
        else: self.blk += 1

# ---- the mixing done natively (psp_core: sas_mix), the same steps as Sas.mix_py. Only the playing voices are copied
# in and out around each call. (Python's per-sample loop took up to half the game thread with many sounds playing.)
import ctypes as _ct
class _SasV(_ct.Structure):
    _fields_ = [(n, _ct.c_int64) for n in ('addr', 'size', 'loop', 'pitch', 'lv', 'rv', 'env', 'rel_step', 'ar', 'dr', 'sr', 'rr', 'sl')] + \
               [(n, _ct.c_int32) for n in ('at', 'dt', 'stp', 'rt', 'st', 'pcm', 'playing', 'adsr', 'release', 'loopflag', 'done', 'endnext')] + \
               [('blk', _ct.c_int64), ('loop_blk', _ct.c_int64)] + \
               [(n, _ct.c_int32) for n in ('s1', 's2', 'bi', 'nbuf', 'cur', 'prev')] + [('frac', _ct.c_double), ('buf', _ct.c_int16 * 28)]
_I64 = ('addr', 'size', 'loop', 'pitch', 'lv', 'rv', 'env', 'rel_step', 'ar', 'dr', 'sr', 'rr', 'sl', 'at', 'dt', 'stp', 'rt', 'st', 'blk', 's1', 's2', 'cur', 'prev')
_BOOL = ('pcm', 'playing', 'adsr', 'release', 'loopflag', 'done')
_nat = {'lib': None, 'tried': False, 'mem': None}
def _native_lib():
    if not _nat['tried']:
        _nat['tried'] = True
        try:
            import psp_cpu; lib = psp_cpu.core()
            if lib is not None and hasattr(lib, 'sas_mix'):
                lib.sas_mix.restype = None; lib.sas_mix.argtypes = [_ct.c_void_p] * 4 + [_ct.c_int, _ct.c_int, _ct.c_void_p, _ct.c_void_p]
                lib.sas_out.restype = None; lib.sas_out.argtypes = [_ct.c_void_p, _ct.c_void_p, _ct.c_void_p, _ct.c_int, _ct.c_int, _ct.c_int64, _ct.c_int64]
                _nat['lib'] = lib
        except Exception: _nat['lib'] = None
    return _nat['lib']
def _mem_ptrs(cpu):
    m = _nat['mem']
    if m is None or m[0] is not cpu.ram or m[1] is not cpu.vram or m[2] is not cpu.scr:
        arrs = [(_ct.c_uint8 * len(b)).from_buffer(b) for b in (cpu.ram, cpu.vram, cpu.scr)]
        m = _nat['mem'] = (cpu.ram, cpu.vram, cpu.scr, arrs, [_ct.addressof(a) for a in arrs])
    return m[4]
def _native_mix(sas, out, withmix, lvol, rvol):
    lib = _native_lib()
    if lib is None: return False
    try:
        cpu = sas.m.cpu; g = sas.grain; ptrs = _mem_ptrs(cpu)
        if g <= 0 or g > 4096: return False
        act = [v for v in sas.v if v.playing and not v.paused]
        arr = (_SasV * max(1, len(act)))()
        for k, v in enumerate(act):
            c = arr[k]
            for n in _I64: setattr(c, n, int(getattr(v, n)))
            for n in _BOOL: setattr(c, n, 1 if getattr(v, n) else 0)
            c.endnext = 1 if getattr(v, 'endnext', False) else 0
            c.loop_blk = -1 if v.loop_blk is None else v.loop_blk
            c.frac = v.frac; b = v.buf; c.nbuf = len(b); c.bi = v.bi
            if b: c.buf[:len(b)] = b
        L = (_ct.c_int64 * g)(); R = (_ct.c_int64 * g)()
        lib.sas_mix(ptrs[0], ptrs[1], ptrs[2], arr, len(act), g, L, R)
        for k, v in enumerate(act):
            c = arr[k]
            for n in _I64: setattr(v, n, getattr(c, n))
            for n in _BOOL: setattr(v, n, bool(getattr(c, n)))
            if c.endnext: v.endnext = True
            v.loop_blk = None if c.loop_blk < 0 else c.loop_blk
            v.frac = c.frac; v.bi = c.bi; v.buf = list(c.buf[:c.nbuf])
        sas.calls += 1
        if out:
            b, o = cpu._loc(out, g * 4)
            base = ptrs[0] if b is cpu.ram else ptrs[1] if b is cpu.vram else ptrs[2]
            lib.sas_out(base + o, L, R, g, 1 if withmix else 0, sas.s(lvol), sas.s(rvol))
        return True
    except Exception:
        return False

class Sas:
    def __init__(self, mach):
        self.m = mach; self.v = [Voice() for _ in range(32)]; self.grain = 256; self.outmode = 0; self.calls = 0

    def call(self, nid, r):
        cpu = self.m.cpu; a1, a2, a3, t0, t1 = r[5], r[6], r[7], r[8], r[9]
        V = self.v[a1 & 31] if nid not in (0xA3589D81, 0x50A14DFC, 0x68A46B95, 0x07F58C24, 0x2C8E6AB3) else None
        if nid == 0x42778A9F:                                                   # Init
            self.grain = a1 or 256; self.outmode = a3; self.v = [Voice() for _ in range(32)]; r[2] = 0
        elif nid == 0xD1E0A01E: self.grain = a1 or 256; r[2] = 0               # SetGrain
        elif nid == 0xBD11B7C2: r[2] = self.grain                              # GetGrain
        elif nid == 0xE855BF76: self.outmode = a1; r[2] = 0                     # SetOutputmode
        elif nid == 0xE175EF66: r[2] = self.outmode
        elif nid == 0x99944089:                                                 # SetVoice (VAG)
            V.addr, V.size, V.loop, V.pcm = a2, a3, t0, False; r[2] = 0
        elif nid == 0xE1CD9561:                                                 # SetVoicePCM
            V.addr, V.size, V.loop, V.pcm = a2, a3 * 2, t0, True; V.loopflag = t0 >= 0 and t0 != 0xFFFFFFFF; r[2] = 0
        elif nid == 0xAD84D37F: V.pitch = a2 & 0xFFFF; r[2] = 0                # SetPitch
        elif nid == 0x440CA7D8:                                                 # SetVolume
            V.lv = self.s(a2); V.rv = self.s(a3); r[2] = 0
        elif nid == 0x76F01ACA:                                                 # KeyOn
            V.reset(); V.on = V.playing = True; V.release = False
            if V.adsr: V.env = 0; V.st = ATTACK
            else: V.env = ENV_MAX
            r[2] = 0
        elif nid == 0xA0CF2FA4:                                                 # KeyOff
            V.on = False; V.release = True
            if V.adsr and V.st != OFF: V.st = RELEASE
            r[2] = 0
        elif nid == 0xCBCD4F79: V.simple_adsr(a2, a3); r[2] = 0               # SetSimpleADSR
        elif nid == 0x019B25EB:                                                 # SetADSR(flags, attack, decay, sustain, release)
            V.adsr = True; t2 = r[10]
            if a2 & 1: V.ar = a3
            if a2 & 2: V.dr = t0
            if a2 & 4: V.sr = t1
            if a2 & 8: V.rr = t2
            r[2] = 0
        elif nid == 0x9EC3676A:                                                 # SetADSRMode(flags, modes)
            V.adsr = True; t2 = r[10]
            if a2 & 1: V.at = a3 & 7
            if a2 & 2: V.dt = t0 & 7
            if a2 & 4: V.stp = t1 & 7
            if a2 & 8: V.rt = t2 & 7
            r[2] = 0
        elif nid == 0x5F9529F6: V.adsr = True; V.sl = a2; r[2] = 0             # SetSL
        elif nid == 0x68A46B95:                                                 # GetEndFlag
            m = 0
            for i, v in enumerate(self.v):
                if not v.playing: m |= 1 << i
            r[2] = m
        elif nid == 0x74AE582A: r[2] = V.env if V.playing else 0               # GetEnvelopeHeight
        elif nid == 0x07F58C24:
            for i, v in enumerate(self.v): cpu.w32(a1 + 4 * i, v.env if v.playing else 0)
            r[2] = 0
        elif nid == 0x787D04D5:                                                 # SetPause(voice bits, pause)
            for i in range(32):
                if a1 & (1 << i): self.v[i].paused = bool(a2)
            r[2] = 0
        elif nid == 0x2C8E6AB3:
            m = 0
            for i, v in enumerate(self.v):
                if v.paused: m |= 1 << i
            r[2] = m
        elif nid in (0xA3589D81, 0x50A14DFC):                                   # Core / CoreWithMix
            self.mix(a1, nid == 0x50A14DFC, a2, a3); r[2] = 0
        elif nid in (0xB7660A23, 0xF983B186, 0x33D4AB37,
                     0x267A6DD2, 0xD5A229C9, 0xA232CBE6, 0xD5EBBBCD, 0x4AA9EAD6): r[2] = 0     # envelope shapes, reverb, ...
        else: return False
        return True

    @staticmethod
    def s(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v

    def mix(self, out, withmix, lvol, rvol):
        if _native_mix(self, out, withmix, lvol, rvol): return
        self.mix_py(out, withmix, lvol, rvol)

    def mix_py(self, out, withmix, lvol, rvol):
        cpu = self.m.cpu; g = self.grain; L = [0] * g; R = [0] * g; self.calls += 1
        for v in self.v:
            if not v.playing or v.paused: continue
            step = v.pitch / 4096.0; lv, rv = v.lv, v.rv
            for i in range(g):
                v.frac += step
                while v.frac >= 1.0:
                    v.frac -= 1.0; v.prev = v.cur; v.cur = v.next_sample(cpu)
                if v.adsr:
                    v.env_step()
                    if v.st == OFF: v.env = 0; v.playing = False; break
                elif v.release:
                    v.env -= v.rel_step
                    if v.env <= 0: v.env = 0; v.playing = False; break
                if v.done and v.bi >= len(v.buf): v.playing = False; v.env = 0; break
                e = v.env if v.env < ENV_MAX else ENV_MAX
                s = (v.cur * (e >> 15)) >> 15
                L[i] += (s * lv) >> 12; R[i] += (s * rv) >> 12
        if not out: return
        n = g * 2 * 2; data = [0] * (2 * g)
        if withmix:
            b, o = cpu._loc(out, n); old = struct.unpack_from('<%dh' % (g * 2), b, o)
            lv, rv = self.s(lvol), self.s(rvol)
            data[0::2] = [x + ((y * lv) >> 12) for x, y in zip(L, old[0::2])]
            data[1::2] = [x + ((y * rv) >> 12) for x, y in zip(R, old[1::2])]
        else: data[0::2] = L; data[1::2] = R
        data = [-32768 if x < -32768 else 32767 if x > 32767 else x for x in data]
        cpu.load_bytes(out, struct.pack('<%dh' % (g * 2), *data))

class AudioOut:
    """What the game plays (sceAudioOutput...): the channels are MIXED together (music, voices and sound effects can
    each come on their own channel), then go to the speakers (player) and, for the first 3 minutes, to a WAV file."""
    def __init__(self, mach, path):
        self.m = mach; self.path = path; self.ch = {}; self.w = None; self.frames = 0; self.limit = 44100 * 60 * 3
        self.acc = []; self.F = 0; self.cur = {}; self.last = {}
    def reserve(self, ch, samples, fmt):
        if ch < 0 or ch > 7:
            ch = next((i for i in range(8) if i not in self.ch), 0)
        self.ch[ch] = [samples or 1024, fmt]; return ch
    def release(self, ch):
        self.ch.pop(ch, None); self.cur.pop(ch, None); self.last.pop(ch, None)
    def setlen(self, ch, n):
        if ch in self.ch: self.ch[ch][0] = n
    def output(self, ch, lv, rv, buf):
        c = self.ch.get(ch)
        if c is None: c = self.ch[ch] = [256, 0]                      # channel opened before a save state was made
        n, fmt = c
        if not buf: return n
        try:
            b, o = self.m.cpu._loc(buf, n * (4 if fmt == 0 else 2))
            if fmt == 0: s = struct.unpack_from('<%dh' % (2 * n), b, o)
            else:                                                               # mono -> stereo
                mono = struct.unpack_from('<%dh' % n, b, o); s = [x for v in mono for x in (v, v)]
            if lv != 0x8000 or rv != 0x8000:
                s = [(s[i] * (lv if i % 2 == 0 else rv)) >> 15 for i in range(2 * n)]
            self._mix(ch, s)
        except Exception: pass
        return n
    def _mix(self, ch, s):
        if not hasattr(self, 'acc'): self.acc = []; self.F = 0; self.cur = {}; self.last = {}   # older save states
        now = self.m.now(); n = len(s) // 2; F = self.F
        c = max(self.cur.get(ch, F), F); off = (c - F) * 2; acc = self.acc
        if len(acc) < off + 2 * n: acc.extend([0] * (off + 2 * n - len(acc)))
        if off == 0 and len([k for k, t in self.last.items() if now - t < 100000 and k != ch]) == 0 and not any(acc[:2 * n]):
            acc[0:2 * n] = s                                                    # only one channel playing: no adding needed
        else:
            for i, v in enumerate(s): acc[off + i] += v
        self.cur[ch] = c + n; self.last[ch] = now
        active = [self.cur[k] for k, t in self.last.items() if now - t < 100000 and k in self.cur]
        newF = min(active) if active else c + n
        hi = max(self.cur.values())
        if hi - newF > 11025: newF = hi - 11025                               # a channel that fell silent can't hold the rest up
        if newF > F:
            take = (newF - F) * 2; out = acc[:take]; del acc[:take]; self.F = newF
            self._emit(struct.pack('<%dh' % take, *[-32768 if v < -32768 else 32767 if v > 32767 else v for v in out]))
    def _emit(self, raw):
        player = getattr(self, 'player', None)
        if player is not None: player.write(raw)
        if self.path and self.frames < self.limit:
            try:
                if self.w is None:
                    self.w = wave.open(self.path, 'wb'); self.w.setnchannels(2); self.w.setsampwidth(2); self.w.setframerate(44100)
                self.w.writeframesraw(raw)
            except Exception: pass
        self.frames += len(raw) // 4
    def close(self):
        if self.w is not None:
            try: self.w.close()
            except Exception: pass
            self.w = None
    def __getstate__(self):                                                     # save states: the open WAV file is not saved
        d = dict(self.__dict__); d['w'] = None; d.pop('player', None); return d


class WaveOut:
    """Live sound on Windows through winmm (built into Windows, nothing to install).
    Collects the game's sound into ~46 ms blocks and keeps a few of them queued."""
    BLOCK = 8192                                                                  # bytes = 2048 stereo samples
    def __init__(self, queue=6):
        import ctypes as ct
        self.ct = ct; self.winmm = ct.WinDLL('winmm')
        class WFX(ct.Structure):
            _fields_ = [('wFormatTag', ct.c_ushort), ('nChannels', ct.c_ushort), ('nSamplesPerSec', ct.c_uint),
                        ('nAvgBytesPerSec', ct.c_uint), ('nBlockAlign', ct.c_ushort), ('wBitsPerSample', ct.c_ushort), ('cbSize', ct.c_ushort)]
        class HDR(ct.Structure):
            _fields_ = [('lpData', ct.c_void_p), ('dwBufferLength', ct.c_uint), ('dwBytesRecorded', ct.c_uint), ('dwUser', ct.c_size_t),
                        ('dwFlags', ct.c_uint), ('dwLoops', ct.c_uint), ('lpNext', ct.c_void_p), ('reserved', ct.c_size_t)]
        self.HDR = HDR
        fmt = WFX(1, 2, 44100, 176400, 4, 16, 0); self.h = ct.c_void_p()
        if self.winmm.waveOutOpen(ct.byref(self.h), ct.c_uint(0xFFFFFFFF), ct.byref(fmt), 0, 0, 0) != 0: raise OSError("no sound device")
        self.pending = bytearray(); self.busy = []; self.queue = queue
    def write(self, raw):
        self.pending += raw
        while len(self.pending) >= self.BLOCK:
            self._reap()
            if not self.busy and len(self.pending) < 3 * self.BLOCK: break   # speakers ran dry: gather a little first
            blk = bytes(self.pending[:self.BLOCK]); del self.pending[:self.BLOCK]
            if len(self.busy) >= self.queue: continue                             # game running faster than the speakers: drop
            ct = self.ct; buf = ct.create_string_buffer(blk, len(blk)); hdr = self.HDR()
            hdr.lpData = ct.cast(buf, ct.c_void_p); hdr.dwBufferLength = len(blk)
            self.winmm.waveOutPrepareHeader(self.h, ct.byref(hdr), ct.sizeof(hdr))
            self.winmm.waveOutWrite(self.h, ct.byref(hdr), ct.sizeof(hdr)); self.busy.append((hdr, buf))
    def _reap(self):
        keep = []
        for hdr, buf in self.busy:
            if hdr.dwFlags & 1: self.winmm.waveOutUnprepareHeader(self.h, self.ct.byref(hdr), self.ct.sizeof(hdr))
            else: keep.append((hdr, buf))
        self.busy = keep
    def close(self):
        try: self.winmm.waveOutReset(self.h); self._reap(); self.winmm.waveOutClose(self.h)
        except Exception: pass
