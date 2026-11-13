#!/usr/bin/env python3
"""
PSP graphics engine (GE) - a slow, simple SOFTWARE renderer for the MGS Portable Ops port prototype.
The game sends display lists (32-bit commands: top 8 bits = command, low 24 bits = argument).
This reads them, keeps the GE state, draws primitives into the emulated video memory and can save a
frame as a PNG so we can SEE what the game draws.  Written from the public PPSSPP command numbering.
Not implemented yet (ignored, listed in the report): lighting, fog, bezier/spline patches, stencil,
logic ops, texture matrices / environment maps, mip-maps, bilinear filtering.
"""
import struct, zlib, binascii, math, traceback, time
def _NOP(): pass

M32 = 0xFFFFFFFF
def ct_addr(x):
    import ctypes; return ctypes.addressof(x)
def f32(data24): return struct.unpack('<f', struct.pack('<I', (data24 << 8) & M32))[0]

# ---------------------------------------------------------------------------- PNG writer (no extra libraries)
def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + bytes(rgb[y*w*3:(y+1)*w*3]) for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', binascii.crc32(t + d) & M32)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def unpack_color(v, fmt):
    """-> (r,g,b,a) 0..255 from a packed pixel; fmt 0=565 1=5551 2=4444 3=8888"""
    if fmt == 3: return v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >> 24) & 255
    if fmt == 0:
        r, g, b = v & 31, (v >> 5) & 63, (v >> 11) & 31
        return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255
    if fmt == 1:
        r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
        return (r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 255 if v & 0x8000 else 0
    r, g, b, a = v & 15, (v >> 4) & 15, (v >> 8) & 15, (v >> 12) & 15
    return r * 17, g * 17, b * 17, a * 17

def pack_color(r, g, b, a, fmt):
    if fmt == 3: return r | (g << 8) | (b << 16) | (a << 24)
    if fmt == 0: return (r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11)
    if fmt == 1: return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | (0x8000 if a >= 128 else 0)
    return (r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8) | ((a >> 4) << 12)

def clamp255(x): return 0 if x < 0 else 255 if x > 255 else int(x)

ELEM = [0, 1, 2, 4]

class VLayout:
    """Where each part of a vertex sits for a given 'vertex type' word."""
    def __init__(self, vt):
        self.vt = vt
        self.tc, self.col, self.nrm, self.pos, self.wf = vt & 3, (vt >> 2) & 7, (vt >> 5) & 3, (vt >> 7) & 3, (vt >> 9) & 3
        self.idx, self.wc, self.through = (vt >> 11) & 3, ((vt >> 14) & 7) + 1, bool(vt & (1 << 23))
        off = 0; mx = 1
        def al(o, a): return (o + a - 1) // a * a
        self.woff = self.toff = self.coff = self.noff = self.poff = None
        if self.wf: s = ELEM[self.wf]; off = al(off, s); self.woff = off; off += s * self.wc; mx = max(mx, s)
        if self.tc: s = ELEM[self.tc]; off = al(off, s); self.toff = off; off += s * 2; mx = max(mx, s)
        if self.col:
            s = 4 if self.col == 7 else 2; off = al(off, s); self.coff = off; off += s; mx = max(mx, s)
        if self.nrm: s = ELEM[self.nrm]; off = al(off, s); self.noff = off; off += s * 3; mx = max(mx, s)
        if self.pos: s = ELEM[self.pos]; off = al(off, s); self.poff = off; off += s * 3; mx = max(mx, s)
        self.size = al(off, mx)

class GE:
    def __init__(self, cpu, machine=None):
        self.cpu, self.m = cpu, machine
        self.reg = [0] * 256
        self.world = [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0]; self.view = list(self.world)
        self.proj = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        self.bones = [0.0] * 96; self.mi = {0x3A: 0, 0x3C: 0, 0x3E: 0, 0x2A: 0}
        self.base = 0; self.offset_addr = 0; self.vaddr = 0; self.iaddr = 0
        self.lists = {}; self.next_id = 1; self.render = True
        self.clut = b''; self.tex_cache = {}
        self.stats = {'commands': 0, 'prims': 0, 'prims_drawn': 0, 'lists': 0, 'errors': 0, 'cmd_counts': {}, 'ignored': {}}
        self.vtypes = {}; self.textures = {}; self.first_error = None
        self.callbacks = {}                                   # callback id -> (signal func, signal arg, finish func, finish arg)
        import collections; self.trace = collections.deque(maxlen=20000)   # last commands run, for the report

    # ------------------------------------------------------------------ memory helpers
    def mem(self, addr, n=4):
        return self.cpu._loc(addr & 0x3FFFFFFF if addr >= 0x40000000 else addr, n)
    def r32(self, a): return self.cpu.r32(a)
    def rel(self, data): return (self.offset_addr + (((self.base & 0x000F0000) << 8) | data)) & 0x0FFFFFFF | (0x08000000 if (((self.base & 0xF0000) << 8) | data) < 0x04000000 and False else 0)
    def addr_of(self, data):
        a = (self.offset_addr + (((self.base & 0x000F0000) << 8) | data)) & 0x0FFFFFFF
        return a

    # ------------------------------------------------------------------ list handling
    def enqueue(self, start, stall, cbid=-1, head=False):
        lid = self.next_id; self.next_id += 1
        self.lists[lid] = {'pc': start & 0x0FFFFFFF, 'stall': stall & 0x0FFFFFFF, 'stack': [], 'done': False, 'cb': cbid, 'start': start}
        self.stats['lists'] += 1; self.trace.append(('list', lid, start, stall))
        if len(self.lists) > 256:                                           # forget lists that finished long ago
            for k in [k for k, v in list(self.lists.items())[:-128] if v['done']]: self.lists.pop(k, None)
        self.kick(lid)
        return lid
    def update_stall(self, lid, stall):
        L = self.lists.get(lid)
        if L: L['stall'] = stall & 0x0FFFFFFF; self.kick(lid)

    # ------------------------------------------------------------------ drawing on a second thread
    # Like the real PSP, whose graphics chip works while the CPU goes on: the game's display lists are drawn by a
    # helper thread (the drawing itself runs in psp_core.dll, so both really run at the same time). The game thread
    # waits for it where the real game waits for the chip (sceGeDrawSync), and the chip's "done" callbacks are run
    # on the game thread, like an interrupt.
    def start_async(self):
        import threading, queue
        global time
        import time
        if self.__dict__.get('_worker') is not None: return
        import collections
        self._q = queue.Queue(); self._pending = []; self._urgent = collections.deque()
        self._worker = threading.Thread(target=self._work, name='GE', daemon=True); self._worker.start()
    def _work(self):
        q = self._q
        while True:
            item = q.get()
            try:
                if item is None: return
                if self._urgent: self._run_urgent()
                lid, tpost = item
                if lid is _NOP: continue
                gate = self.__dict__.get('job_gate')
                if gate is not None and callable(lid):                      # (a long job that would make the next picture late:
                    try: until = gate(lid)                                  #  first that picture, then the job)
                    except Exception: traceback.print_exc(); until = 0; self.job_gate = None
                    while until and time.perf_counter() < until:
                        if self._urgent: self._run_urgent(); break
                        time.sleep(0.0005)
                t = time.thread_time(); w0 = time.perf_counter()
                if callable(lid):                                           # a job for the drawing thread (OpenGL)
                    try: lid()
                    except Exception: traceback.print_exc()
                else: self.run(lid)
                c = time.thread_time() - t
                self.busy_cpu = self.__dict__.get('busy_cpu', 0.0) + c
                tr = self.__dict__.get('jobtrace')                          # (perf_log: what the drawing thread did, when)
                if tr is not None: tr.append((getattr(lid, 'kind', 'job') if callable(lid) else 'game drawing', tpost, w0, time.perf_counter(), c))
            finally: q.task_done()
    def post_urgent(self, fn):
        """run fn() on the drawing thread as soon as possible: before what is waiting, and even in the middle of a
        display list (between two draw commands)"""
        q = self.__dict__.get('_q')
        if q is None: fn(); return
        self._urgent.append((fn, time.perf_counter()))
        lib = self.__dict__.get('_glib')
        if lib is not None and hasattr(lib, 'ge_yield'): lib.ge_yield(1)
        q.put((_NOP, time.perf_counter()))                               # (wakes the drawing thread if it waits)
    def _run_urgent(self):
        lib = self.__dict__.get('_glib')
        if lib is not None and hasattr(lib, 'ge_yield'): lib.ge_yield(0)
        u = self._urgent; tr = self.__dict__.get('jobtrace')
        while u:
            fn, tpost = u.popleft(); t = time.thread_time(); w0 = time.perf_counter()
            try: fn()
            except Exception: traceback.print_exc()
            c = time.thread_time() - t; self.busy_cpu = self.__dict__.get('busy_cpu', 0.0) + c
            if tr is not None: tr.append((getattr(fn, 'kind', 'job'), tpost, w0, time.perf_counter(), c))
    def post(self, fn):
        """run fn() on the drawing thread, after everything already handed to it"""
        q = self.__dict__.get('_q')
        if q is None: fn()
        else: q.put((fn, time.perf_counter()))
    def call(self, fn, timeout=30):
        """run fn() on the drawing thread and wait for its result"""
        import threading
        box = {}; ev = threading.Event()
        def job():
            try: box['r'] = fn()
            except Exception as e: box['e'] = e
            finally: ev.set()
        self.post(job)
        if not ev.wait(timeout): raise TimeoutError("drawing thread did not answer")
        if 'e' in box: raise box['e']
        return box.get('r')
    def kick(self, lid):
        q = self.__dict__.get('_q')
        if q is None: self.run(lid)
        else: q.put((lid, time.perf_counter()))
    def busy(self):
        q = self.__dict__.get('_q'); return q is not None and q.unfinished_tasks > 0
    def sync(self):
        """wait until the helper thread has drawn everything handed to it so far"""
        q = self.__dict__.get('_q')
        if q is not None: q.join()
    def pending(self): return bool(self.__dict__.get('_pending'))
    def deliver(self):
        """run the callbacks the helper thread collected (game thread only)"""
        p = self.__dict__.get('_pending')
        while p:
            func, a0, a1 = p.pop(0)
            if self.m is not None: self.m.call_nested(func, a0, a1)
    def _callback(self, func, a0, a1):
        import threading
        if self.__dict__.get('_worker') is not None and threading.current_thread() is self._worker: self._pending.append((func, a0, a1))
        elif self.m is not None: self.m.call_nested(func, a0, a1)
    def run(self, lid):
        L = self.lists[lid]
        if L['done']: return
        try: self._run(L)
        except Exception:
            self.stats['errors'] += 1
            if self.first_error is None: self.first_error = traceback.format_exc()
            L['done'] = True

    def _run(self, L):
        if self._gpu() is not None and self._glist() is not None: return self._run_native(L)
        reg = self.reg; st = self.stats; cnt = st['cmd_counts']; r32 = self.cpu.r32; steps = 0
        while True:
            pc = L['pc']
            if L['stall'] and pc == L['stall']: return
            w = r32(pc); L['pc'] = pc + 4
            c = w >> 24; d = w & 0xFFFFFF; self.trace.append((pc, w))
            st['commands'] += 1; cnt[c] = cnt.get(c, 0) + 1; steps += 1
            if steps > 3_000_000: raise RuntimeError("display list runs forever?")
            prev = L.get('prev'); L['prev'] = c
            if c == 0x0F: L['fin'] = True; continue                             # FINISH: takes effect at the END after it
            if c == 0x0E: L['sig'] = d; continue                                # SIGNAL: always paired with the END after it
            if c == 0x0C:                                                       # END
                if prev == 0x0E:                                                # SIGNAL+END: the list keeps going
                    sd = L.pop('sig', 0); beh = (sd >> 16) & 0xFF
                    if beh in (0x10, 0x11, 0x13, 0x14):                         # jump / call (absolute, relative)
                        tgt = ((sd & 0xFFFF) << 16) | (d & 0xFFFF)
                        if beh in (0x13, 0x14): tgt += pc - 4
                        if beh in (0x11, 0x14): L['stack'].append(L['pc'])
                        L['pc'] = tgt & 0x0FFFFFFC
                    elif beh == 0x12:                                           # return
                        if L['stack']: L['pc'] = L['stack'].pop()
                    else: self.signal(L, sd)                                    # handler suspend / continue / pause / sync
                    continue
                if L.pop('fin', False): self.finish(L)
                L['done'] = True; return
            if c == 0x08: L['pc'] = self.addr_of(d) & ~3; continue         # JUMP
            if c == 0x0A: L['stack'].append(L['pc']); L['pc'] = self.addr_of(d) & ~3; continue   # CALL
            if c == 0x0B:                                                   # RET
                if L['stack']: L['pc'] = L['stack'].pop()
                continue

            reg[c] = d
            if c == 0x10: self.base = d
            elif c == 0x13: self.offset_addr = (d << 8) & 0x0FFFFFFF
            elif c == 0x14: self.offset_addr = pc & 0x0FFFFFFF
            elif c == 0x01: self.vaddr = self.addr_of(d)
            elif c == 0x02: self.iaddr = self.addr_of(d)
            elif c in (0x04, 0x05, 0x06):
                if c == 0x04: self.prim(d)
                else: st['ignored']['patches (bezier/spline)'] = st['ignored'].get('patches (bezier/spline)', 0) + 1
                lay = self.layout(reg[0x12]); cnt = (d & 0xFFFF) if c == 0x04 else (d & 0xFF) * ((d >> 8) & 0xFF)
                if lay.idx: self.iaddr += cnt * ELEM[lay.idx]                 # the GE moves on past the vertices it used
                else: self.vaddr += cnt * lay.size
            elif c in (0x3A, 0x3C, 0x3E, 0x2A): self.mi[c] = d & 0x7F
            elif c == 0x3B: i = self.mi[0x3A]; self.world[i % 12] = f32(d); self.mi[0x3A] = i + 1
            elif c == 0x3D: i = self.mi[0x3C]; self.view[i % 12] = f32(d); self.mi[0x3C] = i + 1
            elif c == 0x3F: i = self.mi[0x3E]; self.proj[i % 16] = f32(d); self.mi[0x3E] = i + 1
            elif c == 0x2B: i = self.mi[0x2A]; self.bones[i % 96] = f32(d); self.mi[0x2A] = i + 1
            elif c == 0xC4: self.load_clut(d)
            elif c == 0xEA: self.transfer()

    # the same loop in C (psp_gpu.c ge_run): about 20x faster. Rare commands come back here.
    def _glist(self):
        gl = self.__dict__.get('_gl')
        if gl is not None: return gl
        if self.__dict__.get('_gl_failed'): return None
        import ctypes as ct
        lib = self._glib
        if not hasattr(lib, 'ge_run'): self._gl_failed = True; return None
        class GL(ct.Structure):
            _fields_ = [('pc', ct.c_uint32), ('stall', ct.c_uint32), ('base', ct.c_uint32), ('offset_addr', ct.c_uint32),
                        ('stack', ct.c_uint32 * 32), ('sp', ct.c_int32), ('mi', ct.c_int32 * 4), ('prev', ct.c_int32),
                        ('has_sig', ct.c_int32), ('sig', ct.c_uint32), ('fin', ct.c_int32), ('render', ct.c_int32),
                        ('clut_changed', ct.c_int32), ('commands', ct.c_uint64), ('prims', ct.c_uint64),
                        ('stop_cmd', ct.c_uint32), ('stop_data', ct.c_uint32)]
        if lib.ge_list_size() != ct.sizeof(GL): self._gl_failed = True; return None
        lib.ge_run.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_int]; lib.ge_run.restype = ct.c_int
        self._gl = gl = GL(); return gl

    def _run_native(self, L):
        g = self._gst; gl = self._gl; lib = self._glib; st = self.stats; import ctypes as ct
        MI = (0x3A, 0x3C, 0x3E, 0x2A)
        def front():                                          # python state -> C (also after python handled something:
            g.reg[:] = self.reg; g.world[:] = self.world; g.view[:] = self.view    # a callback may have run another list)
            g.proj[:] = self.proj; g.bones[:] = self.bones; g.vaddr = self.vaddr; g.iaddr = self.iaddr
            if self._gclut is not self.clut:
                n = min(len(self.clut), 2048); g.clut[:n] = self.clut[:n]; g.clut_len = n; self._gclut = self.clut
            gl.pc = L['pc']; gl.stall = L['stall']; gl.base = self.base; gl.offset_addr = self.offset_addr
            stk = L['stack'][-32:]; gl.sp = len(stk); gl.stack[:len(stk)] = stk
            gl.mi[:] = [self.mi[k] for k in MI]; prev = L.get('prev'); gl.prev = -1 if prev is None else prev
            gl.has_sig = 1 if 'sig' in L else 0; gl.sig = L.get('sig', 0); gl.fin = 1 if L.get('fin') else 0
            gl.render = 1 if self.render else 0; gl.clut_changed = 0
        front(); gl.commands = 0; gl.prims = 0
        ga, la = ct.addressof(g), ct.addressof(gl); budget = 3_000_000; err = None
        def back():                                           # C state -> python
            self.reg[:] = list(g.reg); self.world[:] = list(g.world); self.view[:] = list(g.view); self.proj[:] = list(g.proj)
            self.bones[:] = list(g.bones); self.vaddr = g.vaddr; self.iaddr = g.iaddr
            self.base = gl.base; self.offset_addr = gl.offset_addr
            for k, v in zip(MI, gl.mi): self.mi[k] = v
            if gl.clut_changed:
                self.clut = bytes(g.clut[:g.clut_len]); self._gclut = self.clut; self.tex_cache.clear(); gl.clut_changed = 0
            L['pc'] = gl.pc; L['stack'] = list(gl.stack[:gl.sp]); L['prev'] = gl.prev if gl.prev >= 0 else None
            if gl.has_sig: L['sig'] = gl.sig
            else: L.pop('sig', None)
            if gl.fin: L['fin'] = True
            else: L.pop('fin', None)
        try:
            while True:
                res = lib.ge_run(ga, la, budget)
                if res == 0: back(); return
                if res == 1:
                    back(); fin = L.pop('fin', False); L.pop('sig', None)
                    if fin: self.finish(L)
                    L['done'] = True; return
                if res == 2: back(); self.signal(L, gl.stop_data); front(); continue
                if res == 3:
                    back()
                    if gl.stop_cmd == 0xEA: self.transfer()
                    front(); continue
                if res == 7:                                  # (a picture is due: drawn now, then the list goes on)
                    back(); self._run_urgent(); front(); continue
                if res == 4: err = f"display list read outside memory at {g.err_addr:#x}"
                elif res == 5: err = "display list runs forever?"
                else: err = f"draw command reads outside memory at {g.err_addr:#x}"
                back(); raise RuntimeError(err)
        finally:
            st['commands'] += gl.commands; st['prims'] += gl.prims; st['prims_drawn'] += gl.prims if self.render else 0
            gl.commands = 0; gl.prims = 0

    def finish(self, L):
        cb = self.callbacks.get(L['cb'])
        if cb and cb[2] and self.m is not None: self._callback(cb[2], 0, cb[3])
    def signal(self, L, d):
        beh = (d >> 16) & 0xFF
        if beh == 0x01 and False: pass                                      # SIGNAL_SYNC etc: nothing to wait for
        cb = self.callbacks.get(L['cb'])
        if cb and cb[0] and self.m is not None: self._callback(cb[0], d & 0xFFFF, cb[1])

    # ------------------------------------------------------------------ copies / clut
    def load_clut(self, d):
        a = (self.reg[0xB0] & 0xFFFFFF) | ((self.reg[0xB1] & 0x0F0000) << 8)
        n = (d & 0x3F) * 32
        try: b, o = self.mem(a, max(n, 1)); self.clut = bytes(b[o:o + n])
        except Exception: self.clut = b''
        self.tex_cache.clear()
    def transfer(self):
        r = self.reg
        src = (r[0xB2] & 0xFFFFFF) | ((r[0xB3] & 0xFF0000) << 8); dst = (r[0xB4] & 0xFFFFFF) | ((r[0xB5] & 0xFF0000) << 8)
        sw, dw = r[0xB3] & 0x7FF, r[0xB5] & 0x7FF
        sx, sy = r[0xEB] & 0x3FF, (r[0xEB] >> 10) & 0x3FF; dx, dy = r[0xEC] & 0x3FF, (r[0xEC] >> 10) & 0x3FF
        w, h = (r[0xEE] & 0x3FF) + 1, ((r[0xEE] >> 10) & 0x3FF) + 1; bpp = 4 if r[0xEA] & 1 else 2
        lib = self.__dict__.get('_glib')                                    # graphics-card drawing: this picture is read
        if lib is not None and hasattr(lib, 'gl_fb_download'):           # (the graphics card's picture, brought back)
            try:
                import ctypes as _ct
                lib.gl_fb_download(_ct.c_void_p(_ct.addressof(self._gpu())), _ct.c_uint32(src + (sy * sw + sx) * bpp), _ct.c_uint32(h * sw * bpp))
            except Exception:
                try: lib.gl_note_read(src + (sy * sw + sx) * bpp, h * sw * bpp)
                except Exception: pass
        elif lib is not None and hasattr(lib, 'gl_note_read'):
            try: lib.gl_note_read(src + (sy * sw + sx) * bpp, h * sw * bpp)
            except Exception: pass
        for y in range(h):
            s = src + ((sy + y) * sw + sx) * bpp; t = dst + ((dy + y) * dw + dx) * bpp
            sb, so = self.mem(s, w * bpp); db, do = self.mem(t, w * bpp); db[do:do + w * bpp] = sb[so:so + w * bpp]
        self.tex_cache.clear()
        if lib is not None and hasattr(lib, 'gl_vram_touch'): lib.gl_vram_touch()     # (video memory changed: hashes kept for it are out of date)

    # ------------------------------------------------------------------ vertices
    def layout(self, vt):
        L = self.vtypes.get(vt)
        if L is None: L = self.vtypes[vt] = VLayout(vt)
        return L

    def decode_vertex(self, lay, buf, o):
        pos = None; uv = None; col = None; wts = None; th = lay.through
        if lay.pos:
            fmt = lay.pos; p = o + lay.poff
            if fmt == 3: x, y, z = struct.unpack_from('<3f', buf, p)
            elif fmt == 2:
                x, y, z = struct.unpack_from('<3h' if not th else '<3H', buf, p)
                if not th: x, y, z = x / 32768.0, y / 32768.0, z / 32768.0
            else:
                x, y, z = struct.unpack_from('<3b' if not th else '<3B', buf, p)
                if not th: x, y, z = x / 128.0, y / 128.0, z / 128.0
            pos = (x, y, z)
        if lay.tc:
            p = o + lay.toff
            if lay.tc == 3: u, v = struct.unpack_from('<2f', buf, p)
            elif lay.tc == 2:
                u, v = struct.unpack_from('<2H', buf, p)
                if not th: u, v = u / 32768.0, v / 32768.0
            else:
                u, v = buf[p], buf[p + 1]
                if not th: u, v = u / 128.0, v / 128.0
            uv = (u, v)
        if lay.col:
            p = o + lay.coff
            if lay.col == 7: col = unpack_color(struct.unpack_from('<I', buf, p)[0], 3)
            else: col = unpack_color(struct.unpack_from('<H', buf, p)[0], {4: 0, 5: 1, 6: 2}[lay.col])
        if lay.wf:
            p = o + lay.woff
            if lay.wf == 3: wts = struct.unpack_from('<%df' % lay.wc, buf, p)
            elif lay.wf == 2: wts = [x / 32768.0 for x in struct.unpack_from('<%dH' % lay.wc, buf, p)]
            else: wts = [x / 128.0 for x in struct.unpack_from('<%dB' % lay.wc, buf, p)]
        return pos, uv, col, wts

    # ------------------------------------------------------------------ transform
    def mul43(self, m, x, y, z):
        return (x*m[0] + y*m[3] + z*m[6] + m[9], x*m[1] + y*m[4] + z*m[7] + m[10], x*m[2] + y*m[5] + z*m[8] + m[11])

    def project(self, pos, wts, lay):
        """-> (sx, sy, depth, w) screen position"""
        r = self.reg; x, y, z = pos
        if lay.through: return float(x), float(y), float(z), 1.0
        if wts is not None:                                                  # skinning
            ax = ay = az = 0.0
            for i, wgt in enumerate(wts):
                if wgt == 0.0: continue
                bx, by, bz = self.mul43(self.bones[i*12:i*12+12], x, y, z); ax += bx*wgt; ay += by*wgt; az += bz*wgt
            x, y, z = ax, ay, az
        x, y, z = self.mul43(self.world, x, y, z); x, y, z = self.mul43(self.view, x, y, z)
        p = self.proj
        cx = x*p[0] + y*p[4] + z*p[8] + p[12]; cy = x*p[1] + y*p[5] + z*p[9] + p[13]
        cz = x*p[2] + y*p[6] + z*p[10] + p[14]; cw = x*p[3] + y*p[7] + z*p[11] + p[15]
        if cw <= 1e-6: return None
        ix, iy, iz = cx / cw, cy / cw, cz / cw
        sx = ix * f32(r[0x42]) + f32(r[0x45]) - (r[0x4C] & 0xFFFF) / 16.0
        sy = iy * f32(r[0x43]) + f32(r[0x46]) - (r[0x4D] & 0xFFFF) / 16.0
        sz = iz * f32(r[0x44]) + f32(r[0x47])
        return sx, sy, sz, cw

    # ------------------------------------------------------------------ primitives
    def prim(self, d):
        r = self.reg; st = self.stats; st['prims'] += 1
        count, ptype = d & 0xFFFF, (d >> 16) & 7
        lay = self.layout(r[0x12]); key = (r[0x12] & 0xFFFFFF, ptype); self.vtypes_used = getattr(self, 'vtypes_used', {})
        self.vtypes_used[key] = self.vtypes_used.get(key, 0) + 1
        if not self.render or count == 0 or lay.size == 0 or lay.poff is None: return
        if self._gpu() is not None: st['prims_drawn'] += 1; return self._prim_native(d, ptype)
        # fetch vertices
        if lay.idx:
            isz = ELEM[lay.idx]; ib, io = self.mem(self.iaddr, count * isz)
            fmt = {1: '<%dB', 2: '<%dH', 3: '<%dI'}[lay.idx]; idxs = struct.unpack_from(fmt % count, ib, io)
        else: idxs = range(count)
        mx = (max(idxs) + 1) if lay.idx else count
        vb, vo = self.mem(self.vaddr, mx * lay.size)
        cache = {}; verts = []
        ambient = (r[0x5C] & 255, (r[0x5C] >> 8) & 255, (r[0x5C] >> 16) & 255, r[0x5D] & 255)
        for i in idxs:
            v = cache.get(i)
            if v is None:
                pos, uv, col, wts = self.decode_vertex(lay, vb, vo + i * lay.size)
                sp = self.project(pos, wts, lay)
                v = cache[i] = None if sp is None else (sp, uv, col if col is not None else ambient)
            verts.append(v)
        st['prims_drawn'] += 1
        clear = r[0xD3] & 1
        if ptype == 6:                                                         # sprites (rects)
            for k in range(0, len(verts) - 1, 2):
                a, b = verts[k], verts[k + 1]
                if a and b: self.sprite(a, b, lay, clear)
        elif ptype in (3, 4, 5):
            n = len(verts)
            tris = [(verts[k], verts[k+1], verts[k+2]) for k in range(0, n - 2, 3)] if ptype == 3 else \
                   [((verts[k], verts[k+1], verts[k+2]) if k % 2 == 0 else (verts[k+1], verts[k], verts[k+2])) for k in range(n - 2)] if ptype == 4 else \
                   [(verts[0], verts[k], verts[k+1]) for k in range(1, n - 1)]
            for a, b, c in tris:
                if a and b and c: self.triangle(a, b, c, lay, clear)
        else: st['ignored']['points/lines'] = st['ignored'].get('points/lines', 0) + 1

    # ------------------------------------------------------------------ fast drawing (psp_gpu.c, built into psp_core)
    def __getstate__(self):
        d = dict(self.__dict__)
        for k in ('_gst', '_grefs', '_glib', '_gclut', '_gst_failed', '_gl', '_gl_failed', '_q', '_worker', '_pending', 'busy_cpu', 'jobtrace', '_urgent', 'job_gate'): d.pop(k, None)
        return d
    def _gpu(self):
        g = self.__dict__.get('_gst')
        if g is not None: return g
        if self.__dict__.get('_gst_failed'): return None
        import os, ctypes as ct, psp_cpu
        lib = psp_cpu.core()
        if lib is None or os.environ.get('PSP_NOGPU') or not hasattr(lib, 'ge_prim'):
            self._gst_failed = True; return None
        class GS(ct.Structure):
            _fields_ = [('reg', ct.c_uint32 * 256), ('world', ct.c_double * 12), ('view', ct.c_double * 12), ('proj', ct.c_double * 16),
                        ('bones', ct.c_double * 96), ('vaddr', ct.c_uint32), ('iaddr', ct.c_uint32), ('clut_len', ct.c_uint32),
                        ('through_dummy', ct.c_uint32), ('clut', ct.c_uint8 * 2048), ('ram', ct.c_void_p), ('vram', ct.c_void_p),
                        ('scr', ct.c_void_p), ('pixels', ct.c_uint64), ('err_addr', ct.c_uint32), ('pad', ct.c_uint32), ('zram', ct.c_void_p)]
        if lib.ge_state_size() != ct.sizeof(GS): self._gst_failed = True; return None
        g = GS(); refs = []
        for name in ('ram', 'vram', 'scr'):
            ba = getattr(self.cpu, name); arr = (ct.c_uint8 * len(ba)).from_buffer(ba); refs.append(arr); setattr(g, name, ct.addressof(arr))
        zm, _ = self.zmem_at(0x04000000); arr = (ct.c_uint8 * len(zm)).from_buffer(zm); refs.append(arr); g.zram = ct.addressof(arr)
        lib.ge_prim.argtypes = [ct.c_void_p, ct.c_uint32]; lib.ge_prim.restype = ct.c_int
        self._glib = lib; self._grefs = refs; self._gst = g; self._gclut = None
        return g
    def _prim_native(self, d, ptype):
        g = self._gst; r = self.reg
        g.reg[:] = r; g.world[:] = self.world; g.view[:] = self.view; g.proj[:] = self.proj; g.bones[:] = self.bones
        g.vaddr = self.vaddr; g.iaddr = self.iaddr
        if self._gclut is not self.clut:
            n = min(len(self.clut), 2048); g.clut[:n] = self.clut[:n]; g.clut_len = n; self._gclut = self.clut
        if r[0x1E] & 1:
            k = (1 << (r[0xB8] & 15), 1 << ((r[0xB8] >> 8) & 15), r[0xC3] & 15, r[0xC2] & 1, bool(self.clut) and (r[0xC3] & 15) >= 4)
            self.textures[k] = self.textures.get(k, 0) + 1
        res = self._glib.ge_prim(ct_addr(g), d)
        if res == 1: raise RuntimeError(f"draw command reads outside memory at {g.err_addr:#x}")
        if res == 2: self.stats['ignored']['points/lines'] = self.stats['ignored'].get('points/lines', 0) + 1

    # ------------------------------------------------------------------ texture
    def get_texture(self):
        r = self.reg
        w, h = 1 << (r[0xB8] & 15), 1 << ((r[0xB8] >> 8) & 15); fmt = r[0xC3] & 15
        addr = (r[0xA0] & 0xFFFFFF) | ((r[0xA8] & 0x0F0000) << 8); stride = r[0xA8] & 0xFFFF; sw = r[0xC2] & 1
        key = (addr, w, h, fmt, stride, sw, r[0xC5], self.clut)
        bpp_bits = {0: 16, 1: 16, 2: 16, 3: 32, 4: 4, 5: 8, 6: 16, 7: 32, 8: 4, 9: 8, 10: 8}.get(fmt, 32)
        stride = stride or w; rowbytes = stride * bpp_bits // 8           # the buffer width can be narrower than the texture size
        nbytes = rowbytes * h + max(0, (w - stride)) * bpp_bits // 8 + 16
        try: b, o = self.mem(addr, nbytes)
        except Exception: nbytes = rowbytes * h; b, o = self.mem(addr, nbytes)
        raw = bytes(b[o:o + nbytes])
        t = self.tex_cache.get(key)
        if t is not None and t[0] == raw: return t[1]          # the game may rewrite a texture in place (text, render-to-texture)
        if len(self.tex_cache) > 256: self.tex_cache.clear()
        self.textures[(w, h, fmt, sw, bool(self.clut) and fmt >= 4)] = self.textures.get((w, h, fmt, sw, bool(self.clut) and fmt >= 4), 0) + 1
        key_raw = raw
        if sw and fmt < 8:                                                     # unswizzle: 16-byte x 8-row blocks
            out = bytearray(nbytes); bw = rowbytes // 16; src = 0
            for by in range(0, h, 8):
                for bx in range(bw):
                    for yy in range(8):
                        if by + yy >= h: break
                        d0 = (by + yy) * rowbytes + bx * 16; out[d0:d0 + 16] = raw[src:src + 16]; src += 16
            raw = bytes(out)
        px = [None] * (w * h)
        if fmt >= 8:
            self.decode_dxt(raw, w, h, fmt, px)
        elif fmt < 4 or fmt in (6, 7):
            if fmt >= 4: pass
            bpp = 4 if fmt in (3, 7) else 2
            for y in range(h):
                for x in range(w):
                    p = y * rowbytes + x * bpp
                    px[y * w + x] = int.from_bytes(raw[p:p + bpp], 'little')
        else:
            for y in range(h):
                for x in range(w):
                    if fmt == 4: v = raw[y * rowbytes + (x >> 1)]; v = (v >> 4) if x & 1 else (v & 15)
                    else: v = raw[y * rowbytes + x]
                    px[y * w + x] = v
        if 4 <= fmt <= 7 or fmt < 4 or fmt == 6:
            cf = r[0xC5] & 3; shift = (r[0xC5] >> 2) & 31; mask = (r[0xC5] >> 8) & 255; start = ((r[0xC5] >> 16) & 31) * 16
            if fmt in (4, 5, 6, 7) and fmt != 6 and fmt != 7 or fmt == 6 or fmt == 7:
                csz = 4 if cf == 3 else 2; cl = self.clut; ncl = len(cl) // csz
                pal = [unpack_color(int.from_bytes(cl[i*csz:(i+1)*csz], 'little'), cf) for i in range(ncl)]
                if fmt in (4, 5, 6, 7):
                    if fmt in (6, 7): shift_only = True
                    def lookup(v):
                        i = (((v >> shift) & mask) + start) if True else v
                        return pal[i] if i < ncl else (0, 0, 0, 0)
                    px = [lookup(v) for v in px]
            if fmt < 4: px = [unpack_color(v, fmt) for v in px]
        self.tex_cache[key] = (key_raw, (w, h, px)); return (w, h, px)

    def decode_dxt(self, raw, w, h, fmt, px):
        def c565(v): return unpack_color(v, 0)
        bsz = 8 if fmt == 8 else 16; p = 0
        for by in range(0, h, 4):
            for bx in range(0, w, 4):
                blk = raw[p:p + bsz]; p += bsz
                cdat = blk[-8:]; c0, c1 = struct.unpack_from('<HH', cdat, 0); idx = struct.unpack_from('<I', cdat, 4)[0]
                a, b = c565(c0), c565(c1)
                if c0 > c1 or fmt != 8: pal = [a, b, tuple((2*a[i] + b[i]) // 3 for i in range(3)) + (255,), tuple((a[i] + 2*b[i]) // 3 for i in range(3)) + (255,)]
                else: pal = [a, b, tuple((a[i] + b[i]) // 2 for i in range(3)) + (255,), (0, 0, 0, 0)]
                for yy in range(4):
                    for xx in range(4):
                        col = pal[(idx >> (2 * (yy * 4 + xx))) & 3]
                        if fmt == 9: al = ((struct.unpack_from('<Q', blk, 0)[0] >> (4 * (yy * 4 + xx))) & 15) * 17; col = col[:3] + (al,)
                        elif fmt == 10:
                            a0, a1 = blk[0], blk[1]; bits = int.from_bytes(blk[2:8], 'little'); ai = (bits >> (3 * (yy * 4 + xx))) & 7
                            if a0 > a1: al = a0 if ai == 0 else a1 if ai == 1 else ((8 - ai) * a0 + (ai - 1) * a1) // 7
                            else: al = a0 if ai == 0 else a1 if ai == 1 else 0 if ai == 6 else 255 if ai == 7 else ((6 - ai) * a0 + (ai - 1) * a1) // 5
                            col = col[:3] + (al,)
                        if bx + xx < w and by + yy < h: px[(by + yy) * w + bx + xx] = col

    # ------------------------------------------------------------------ pixel pipeline
    def setup(self):
        r = self.reg
        self.fb_addr = 0x04000000 | (r[0x9C] & 0x1FFFFF); self.fb_stride = r[0x9D] & 0xFFFF or 512
        self.fb_fmt = r[0xD2] & 3; self.bpp = 4 if self.fb_fmt == 3 else 2
        self.fbuf, self.fbo = self.mem(self.fb_addr, 4)
        self.z_addr = 0x04000000 | (r[0x9E] & 0x1FFFFF); self.z_stride = r[0x9F] & 0xFFFF or 512
        self.sc = (r[0xD4] & 0x3FF, (r[0xD4] >> 10) & 0x3FF, r[0xD5] & 0x3FF, (r[0xD5] >> 10) & 0x3FF)
        self.texon = bool(r[0x1E] & 1); self.blend_on = bool(r[0x21] & 1); self.atest_on = bool(r[0x22] & 1)
        self.ztest_on = bool(r[0x23] & 1); self.zwrite = not (r[0xE7] & 1)
        if self.ztest_on:
            self.zbuf, self.zo = self.zmem_at(self.z_addr)

    def zmem_at(self, z_addr):
        """The depth buffer is kept apart from VRAM (as PC emulators do). Some scenes put it on top of a picture that
        is on screen (the story intro does), and writing depth into VRAM there showed up as two black boxes."""
        zm = self.__dict__.get('zmem')
        if zm is None: zm = self.zmem = bytearray(0x200000)
        return zm, (z_addr - 0x04000000) & 0x1FFFFF

    def sample(self, tex, u, v):
        w, h, px = tex; r = self.reg
        x = int(math.floor(u * w)) if not self.cur_through else int(math.floor(u)); y = int(math.floor(v * h)) if not self.cur_through else int(math.floor(v))
        wrap = r[0xC7]
        x = (0 if x < 0 else w - 1 if x >= w else x) if wrap & 1 else x % w
        y = (0 if y < 0 else h - 1 if y >= h else y) if wrap & 0x100 else y % h
        return px[y * w + x]

    def shade(self, col, texel):
        r = self.reg; func = r[0xC9] & 7; ta = r[0xC9] & 0x100
        if texel is None: return col
        cr, cg, cb, ca = col; tr, tg, tb, tA = texel
        if func == 0: return (tr * cr // 255, tg * cg // 255, tb * cb // 255, (tA * ca // 255) if ta else ca)
        if func == 1:
            if ta: return ((tr * tA + cr * (255 - tA)) // 255, (tg * tA + cg * (255 - tA)) // 255, (tb * tA + cb * (255 - tA)) // 255, ca)
            return (tr, tg, tb, ca)
        if func == 2:
            e = r[0xCA]; er, eg, eb = e & 255, (e >> 8) & 255, (e >> 16) & 255
            return ((cr * (255 - tr) + er * tr) // 255, (cg * (255 - tg) + eg * tg) // 255, (cb * (255 - tb) + eb * tb) // 255, (tA * ca // 255) if ta else ca)
        if func == 3: return (tr, tg, tb, tA if ta else ca)
        return (clamp255(cr + tr), clamp255(cg + tg), clamp255(cb + tb), (tA * ca // 255) if ta else ca)

    def blend_factor(self, f, src, dst, is_src):
        r = self.reg
        sa, da = src[3] / 255.0, dst[3] / 255.0
        if is_src:
            if f == 0: return tuple(c / 255.0 for c in dst[:3])
            if f == 1: return tuple(1 - c / 255.0 for c in dst[:3])
        else:
            if f == 0: return tuple(c / 255.0 for c in src[:3])
            if f == 1: return tuple(1 - c / 255.0 for c in src[:3])
        a = {2: sa, 3: 1 - sa, 4: da, 5: 1 - da, 6: min(1, 2 * sa), 7: max(0, 1 - 2 * sa), 8: min(1, 2 * da), 9: max(0, 1 - 2 * da)}.get(f)
        if a is not None: return (a, a, a)
        fx = r[0xE0] if is_src else r[0xE1]
        return ((fx & 255) / 255.0, ((fx >> 8) & 255) / 255.0, ((fx >> 16) & 255) / 255.0)

    def put(self, x, y, z, col):
        r = self.reg; sc = self.sc
        if x < sc[0] or x > sc[2] or y < sc[1] or y > sc[3]: return
        off = self.fbo + (y * self.fb_stride + x) * self.bpp
        if self.ztest_on:
            zo = self.zo + (y * self.z_stride + x) * 2; zv = int(z) & 0xFFFF; old = self.zbuf[zo] | (self.zbuf[zo + 1] << 8)
            f = r[0xDE] & 7
            if not (f == 1 or (f == 2 and zv == old) or (f == 3 and zv != old) or (f == 4 and zv < old) or (f == 5 and zv <= old) or (f == 6 and zv > old) or (f == 7 and zv >= old)): return
        if self.atest_on:
            f = r[0xDB] & 7; ref = (r[0xDB] >> 8) & 255; msk = (r[0xDB] >> 16) & 255; a = col[3] & msk; rf = ref & msk
            if not (f == 1 or (f == 2 and a == rf) or (f == 3 and a != rf) or (f == 4 and a < rf) or (f == 5 and a <= rf) or (f == 6 and a > rf) or (f == 7 and a >= rf)): return
        if self.blend_on:
            fb = self.fbuf
            old = unpack_color(int.from_bytes(fb[off:off + self.bpp], 'little'), self.fb_fmt)
            bm = r[0xDF]; sf, df, eq = bm & 15, (bm >> 4) & 15, (bm >> 8) & 7
            S = self.blend_factor(sf, col, old, True); D = self.blend_factor(df, col, old, False)
            out = []
            for i in range(3):
                s_, d_ = col[i] * S[i], old[i] * D[i]
                v = s_ + d_ if eq == 0 else s_ - d_ if eq == 1 else d_ - s_ if eq == 2 else min(col[i], old[i]) if eq == 3 else max(col[i], old[i]) if eq == 4 else abs(s_ - d_)
                out.append(clamp255(v))
            col = (out[0], out[1], out[2], col[3])
        v = pack_color(clamp255(col[0]), clamp255(col[1]), clamp255(col[2]), clamp255(col[3]), self.fb_fmt)
        self.fbuf[off:off + self.bpp] = v.to_bytes(self.bpp, 'little')
        if self.ztest_on and self.zwrite:
            zo = self.zo + (y * self.z_stride + x) * 2; zv = int(z) & 0xFFFF; self.zbuf[zo] = zv & 255; self.zbuf[zo + 1] = zv >> 8

    def tex_params(self):
        r = self.reg
        return (f32(r[0x48]), f32(r[0x49]), f32(r[0x4A]), f32(r[0x4B]))

    def invisible(self, *cols):
        """True when every pixel would fail the alpha test anyway (fully transparent fades/overlays): skip drawing them."""
        r = self.reg
        if not self.atest_on or (r[0xDB] & 7) != 6 or ((r[0xDB] >> 16) & 255) != 255 or ((r[0xDB] >> 8) & 255) != 0: return False
        if self.texon and not ((r[0xC9] & 7) in (0, 2, 4) and (r[0xC9] & 0x100)): return False   # texture alpha not multiplied by vertex alpha
        return all(c is not None and c[3] == 0 for c in cols)

    def sprite(self, a, b, lay, clear):
        self.setup(); self.cur_through = lay.through
        (x0, y0, z0, w0), uv0, c0 = a; (x1, y1, z1, w1), uv1, c1 = b
        col = c1 if c1 else c0
        if not clear and self.invisible(col): return
        tex = self.get_texture() if (self.texon and uv0 and uv1 and not clear) else None
        su, sv, ou, ov = self.tex_params()
        if not lay.through and uv0 and uv1: uv0 = (uv0[0] * su + ou, uv0[1] * sv + ov); uv1 = (uv1[0] * su + ou, uv1[1] * sv + ov)
        if x1 < x0: x0, x1, uv0, uv1 = x1, x0, ((uv1[0], uv0[1]) if uv0 else None), ((uv0[0], uv1[1]) if uv0 else None)
        if y1 < y0: y0, y1, uv0, uv1 = y1, y0, ((uv0[0], uv1[1]) if uv0 else None), ((uv1[0], uv0[1]) if uv0 else None)
        ix0, ix1, iy0, iy1 = int(math.floor(x0 + 0.5)), int(math.floor(x1 - 0.5 + 1e-6)), int(math.floor(y0 + 0.5)), int(math.floor(y1 - 0.5 + 1e-6))
        if x1 - x0 <= 0 or y1 - y0 <= 0: return
        sc = self.sc; ix0, ix1, iy0, iy1 = max(ix0, sc[0]), min(ix1, sc[2]), max(iy0, sc[1]), min(iy1, sc[3])
        clr = clear and True
        if clr:
            cm = self.reg[0xD3]
            for y in range(iy0, iy1 + 1):
                for x in range(ix0, ix1 + 1):
                    off = self.fbo + (y * self.fb_stride + x) * self.bpp
                    if cm & 0x100:
                        v = pack_color(col[0], col[1], col[2], col[3], self.fb_fmt); self.fbuf[off:off + self.bpp] = v.to_bytes(self.bpp, 'little')
                    if cm & 0x400:
                        zb, zbo = self.zmem_at(self.z_addr); zp = zbo + (y * self.z_stride + x) * 2; zv = int(z1) & 0xFFFF; zb[zp] = zv & 255; zb[zp + 1] = zv >> 8
            return
        for y in range(iy0, iy1 + 1):
            tv = (uv0[1] + (uv1[1] - uv0[1]) * ((y + 0.5 - y0) / (y1 - y0))) if tex else 0
            for x in range(ix0, ix1 + 1):
                texel = None
                if tex: texel = self.sample(tex, uv0[0] + (uv1[0] - uv0[0]) * ((x + 0.5 - x0) / (x1 - x0)), tv)
                self.put(x, y, z1, self.shade(col, texel))

    def triangle(self, a, b, c, lay, clear):
        self.setup(); self.cur_through = lay.through
        (x0, y0, z0, w0), uv0, c0 = a; (x1, y1, z1, w1), uv1, c1 = b; (x2, y2, z2, w2), uv2, c2 = c
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if area == 0: return
        if clear:                                                              # clear mode with triangles: fill bounding box
            self.sprite(a, c if abs(x2 - x0) > 0 else b, lay, clear); return
        if self.invisible(c0, c1, c2): return
        tex = self.get_texture() if (self.texon and uv0 and uv1 and uv2) else None
        su, sv, ou, ov = self.tex_params()
        if tex and not lay.through:
            uv0 = (uv0[0] * su + ou, uv0[1] * sv + ov); uv1 = (uv1[0] * su + ou, uv1[1] * sv + ov); uv2 = (uv2[0] * su + ou, uv2[1] * sv + ov)
        sc = self.sc
        minx = max(int(math.floor(min(x0, x1, x2))), sc[0]); maxx = min(int(math.ceil(max(x0, x1, x2))), sc[2])
        miny = max(int(math.floor(min(y0, y1, y2))), sc[1]); maxy = min(int(math.ceil(max(y0, y1, y2))), sc[3])
        if maxx < minx or maxy < miny: return
        iw0, iw1, iw2 = 1.0 / w0, 1.0 / w1, 1.0 / w2; inv = 1.0 / area; flat = (self.reg[0x50] & 1) == 0
        for y in range(miny, maxy + 1):
            py = y + 0.5
            for x in range(minx, maxx + 1):
                px_ = x + 0.5
                l0 = ((x1 - px_) * (y2 - py) - (x2 - px_) * (y1 - py)) * inv
                l1 = ((x2 - px_) * (y0 - py) - (x0 - px_) * (y2 - py)) * inv
                l2 = 1.0 - l0 - l1
                if l0 < -1e-9 or l1 < -1e-9 or l2 < -1e-9: continue
                # perspective-correct weights
                p0, p1, p2 = l0 * iw0, l1 * iw1, l2 * iw2; ps = p0 + p1 + p2
                if ps == 0: continue
                q0, q1, q2 = p0 / ps, p1 / ps, p2 / ps
                col = tuple(c0[i] * q0 + c1[i] * q1 + c2[i] * q2 for i in range(4)) if not flat else c2
                texel = None
                if tex: texel = self.sample(tex, uv0[0] * q0 + uv1[0] * q1 + uv2[0] * q2, uv0[1] * q0 + uv1[1] * q1 + uv2[1] * q2)
                z = z0 * l0 + z1 * l1 + z2 * l2
                self.put(x, y, z, self.shade(tuple(int(v) for v in col), texel))

    # ------------------------------------------------------------------ output
    def dump_frame(self, path, addr, stride, fmt):
        w, h = 480, 272; bpp = 4 if fmt == 3 else 2
        b, o = self.mem(addr, stride * h * bpp); rgb = bytearray(w * h * 3); k = 0
        for y in range(h):
            row = o + y * stride * bpp
            for x in range(w):
                v = int.from_bytes(b[row + x * bpp: row + (x + 1) * bpp], 'little'); c = unpack_color(v, fmt)
                rgb[k] = c[0]; rgb[k + 1] = c[1]; rgb[k + 2] = c[2]; k += 3
        write_png(path, w, h, rgb)

    NAMES = {0x00: 'NOP', 0x01: 'VADDR', 0x02: 'IADDR', 0x04: 'PRIM', 0x05: 'BEZIER', 0x06: 'SPLINE', 0x07: 'BBOX', 0x08: 'JUMP', 0x09: 'BJUMP',
             0x0A: 'CALL', 0x0B: 'RET', 0x0C: 'END', 0x0E: 'SIGNAL', 0x0F: 'FINISH', 0x10: 'BASE', 0x12: 'VTYPE', 0x13: 'OFFSET', 0x14: 'ORIGIN',
             0x15: 'REGION1', 0x16: 'REGION2', 0x17: 'LIGHTING', 0x1D: 'BACKFACE', 0x1E: 'TEXTURE_ON', 0x1F: 'FOG_ON', 0x20: 'DITHER_ON', 0x21: 'BLEND_ON',
             0x22: 'ALPHATEST_ON', 0x23: 'ZTEST_ON', 0x24: 'STENCIL_ON', 0x25: 'ANTIALIAS', 0x26: 'PATCHCULL', 0x27: 'COLORTEST_ON', 0x28: 'LOGICOP_ON',
             0x2A: 'BONE_NUM', 0x2B: 'BONE_DATA', 0x2C: 'MORPH0', 0x36: 'PATCHDIV', 0x37: 'PATCHPRIM', 0x38: 'FRONTFACE', 0x3A: 'WORLD_NUM', 0x3B: 'WORLD_DATA',
             0x3C: 'VIEW_NUM', 0x3D: 'VIEW_DATA', 0x3E: 'PROJ_NUM', 0x3F: 'PROJ_DATA', 0x40: 'TGEN_NUM', 0x41: 'TGEN_DATA', 0x42: 'VSCALEX', 0x43: 'VSCALEY',
             0x44: 'VSCALEZ', 0x45: 'VTRANSX', 0x46: 'VTRANSY', 0x47: 'VTRANSZ', 0x48: 'TEXSCALEU', 0x49: 'TEXSCALEV', 0x4A: 'TEXOFFU', 0x4B: 'TEXOFFV',
             0x4C: 'OFFSETX', 0x4D: 'OFFSETY', 0x50: 'SHADEMODE', 0x51: 'REVNORM', 0x53: 'MATCOLORSRC', 0x54: 'MATEMISSIVE', 0x55: 'MATAMBIENT',
             0x56: 'MATDIFFUSE', 0x57: 'MATSPECULAR', 0x58: 'MATALPHA', 0x5B: 'MATSPECPOW', 0x5C: 'AMBIENTCOLOR', 0x5D: 'AMBIENTALPHA',
             0x9B: 'REVERSENORMAL', 0x9C: 'FBPTR', 0x9D: 'FBWIDTH', 0x9E: 'ZBPTR', 0x9F: 'ZBWIDTH', 0xA0: 'TEXADDR0', 0xA8: 'TEXBUFW0',
             0xB0: 'CLUTADDR', 0xB1: 'CLUTADDRUPPER', 0xB2: 'TRXSRC', 0xB3: 'TRXSRCW', 0xB4: 'TRXDST', 0xB5: 'TRXDSTW', 0xB8: 'TEXSIZE0',
             0xC0: 'TEXMAPMODE', 0xC1: 'TEXSHADELS', 0xC2: 'TEXMODE', 0xC3: 'TEXFORMAT', 0xC4: 'LOADCLUT', 0xC5: 'CLUTFORMAT', 0xC6: 'TEXFILTER',
             0xC7: 'TEXWRAP', 0xC8: 'TEXLEVEL', 0xC9: 'TEXFUNC', 0xCA: 'TEXENVCOLOR', 0xCB: 'TEXFLUSH', 0xCC: 'TEXSYNC', 0xD2: 'PSM', 0xD3: 'CLEARMODE',
             0xD4: 'SCISSOR1', 0xD5: 'SCISSOR2', 0xD6: 'MINZ', 0xD7: 'MAXZ', 0xD8: 'COLORTEST', 0xDB: 'ALPHATEST', 0xDC: 'STENCILTEST', 0xDD: 'STENCILOP',
             0xDE: 'ZTEST', 0xDF: 'BLENDMODE', 0xE0: 'BLENDFIXA', 0xE1: 'BLENDFIXB', 0xE2: 'DITH0', 0xE6: 'LOGICOP', 0xE7: 'ZWRITEDISABLE',
             0xE8: 'MASKRGB', 0xE9: 'MASKALPHA', 0xEA: 'TRXKICK', 0xEB: 'TRXSRCPOS', 0xEC: 'TRXDSTPOS', 0xEE: 'TRXSIZE'}
    def write_trace(self, path):
        with open(path, 'w', encoding='utf-8') as f:
            for e in self.trace:
                if e[0] == 'list': f.write(f"----- list {e[1]} enqueued: start {e[2]:#x} stall {e[3]:#x}\n"); continue
                pc, w = e; c = w >> 24
                f.write(f"{pc:#010x}: {w:08x}  {self.NAMES.get(c, f'cmd {c:#04x}')} {w & 0xFFFFFF:#x}\n")

    def report(self):
        s = self.stats; names = {0x04: 'PRIM', 0x0F: 'FINISH', 0x0C: 'END', 0x1E: 'TEXTUREMAPENABLE'}
        top = sorted(s['cmd_counts'].items(), key=lambda kv: -kv[1])[:12]
        out = [f"graphics engine: {s['lists']} display lists, {s['commands']} commands, {s['prims']} draw calls ({s['prims_drawn']} rendered), {s['errors']} errors",
               "   most used commands: " + ", ".join(f"{k:#04x}x{v}" for k, v in top)]
        vu = sorted(getattr(self, 'vtypes_used', {}).items(), key=lambda kv: -kv[1])[:10]
        if vu: out.append("   vertex types (type, primitive) x count: " + ", ".join(f"({k[0]:#08x},{k[1]})x{v}" for k, v in vu))
        if self.textures: out.append("   textures used (w,h,format,swizzled,palette): " + ", ".join(f"{k}x{v}" for k, v in list(self.textures.items())[:12]))
        if s['ignored']: out.append("   ignored features: " + str(s['ignored']))
        if self.first_error: out.append("   first error:\n" + self.first_error)
        return "\n".join(out)
