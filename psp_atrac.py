"""
Movie sound (ATRAC3+, Sony's audio format inside the .pmf movies).
The raw frames are wrapped in a small in-memory .at3 (RIFF/WAVE) file and decoded with PyAV (pip install av),
or with an 'ffmpeg' program if PyAV isn't there. Each frame gives 2048 stereo samples (8192 bytes, 16-bit).
"""
import struct, io, collections, shutil, subprocess, zlib

GUID_AT3P = bytes.fromhex('BFAA23E958CB7144A119FFFA01E4CE62')
BATCH = 24                                   # frames decoded per go
LEAD = 3                                     # earlier frames decoded again in front, so batches join seamlessly


def at3_file(frames, cfg):
    fs = len(frames[0])
    fmt = struct.pack('<HHIIHHHHI', 0xFFFE, 2, 44100, fs * 44100 // 2048, fs, 0, 34, 0, 3) + GUID_AT3P + struct.pack('<H', 1) + bytes(cfg) + bytes(8)
    data = b''.join(frames)
    return (b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(data)) + b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt +
            b'data' + struct.pack('<I', len(data)) + data)


class Atrac3pStream:
    def __init__(self):
        self.buf = bytearray(); self.frames = collections.deque(); self.pcm = collections.deque(); self.prev = None
        self.cfg = None; self.mode = None; self.err = None; self.decoded = 0
        try:
            import av; self.av = av; self.mode = 'PyAV'
        except Exception:
            if shutil.which('ffmpeg'): self.mode = 'ffmpeg program'

    def __getstate__(self):                               # save states: no module objects inside
        d = dict(self.__dict__)
        for k in ('av', '_thr', '_ev', '_lock'): d.pop(k, None)
        return d
    def __setstate__(self, d):
        self.__dict__.update(d)
        if self.mode == 'PyAV':
            try: import av; self.av = av
            except Exception: self.mode = None

    # Movie sound is decoded on a helper thread a little ahead of time: decoding a batch on the game's own thread
    # made the game (and so the sound) stall for a moment about once a second - choppy movie sound.
    def _lockobj(self):
        if self.__dict__.get('_lock') is None:
            import threading; self._lock = threading.Lock(); self._ev = threading.Event()
        return self._lock
    def _worker_on(self):
        t = self.__dict__.get('_thr')
        if t is None or not t.is_alive():
            import threading; self._lockobj()
            self._thr = threading.Thread(target=self._work, daemon=True); self._thr.start()
    def _work(self):
        import time
        idle = 0.0
        while idle < 20.0:                                  # (ends by itself once the movie has stopped feeding it)
            if self.frames and (len(self.frames) >= BATCH or len(self.pcm) < 8):
                with self._lock: self._decode_batch()
                idle = 0.0
            else:
                self._ev.wait(0.05); self._ev.clear(); idle += 0.05

    def feed(self, payload):
        """payload of one audio packet, after its 4-byte header. Returns how many whole sound frames it completed."""
        self.buf += payload; b = self.buf; p = 0; added = 0
        while p + 8 <= len(b):
            if b[p] != 0x0F or b[p + 1] != 0xD0: p += 1; continue
            fs = (((b[p + 2] & 3) << 8) | b[p + 3]) * 8 + 8
            if p + 8 + fs > len(b): break
            if self.cfg is None: self.cfg = bytes(b[p + 2:p + 4])
            self.frames.append(bytes(b[p + 8:p + 8 + fs])); p += 8 + fs; added += 1
        del b[:p]
        if self.mode is not None:
            self._worker_on(); self._ev.set()
        return added

    def _decode_batch(self):
        n = min(BATCH, len(self.frames))
        if n == 0 or self.mode is None: return
        batch = [self.frames.popleft() for _ in range(n)]
        lead = [f for f in (self.prev or []) if len(f) == len(batch[0])]       # last frames again: the decoder needs their history
        self.prev = (list(self.prev or []) + batch)[-LEAD:]
        try:
            blob = at3_file(lead + batch, self.cfg or b'\x28\x5c'); raw = b''
            if self.mode == 'PyAV':
                with self.av.open(io.BytesIO(blob), format='wav') as c:
                    rs = self.av.AudioResampler(format='s16', layout='stereo', rate=44100); out = []
                    for fr in c.decode(audio=0):
                        res = rs.resample(fr)
                        for o in (res if isinstance(res, list) else [res]):
                            if o is None: continue
                            out.append(bytes(o.planes[0])[:o.samples * 4])
                    raw = b''.join(out)
            else:
                raw = subprocess.run(['ffmpeg', '-loglevel', 'quiet', '-f', 'wav', '-i', 'pipe:0', '-f', 's16le', '-ac', '2', 'pipe:1'],
                                     input=blob, capture_output=True).stdout
            raw = raw[len(lead) * 8192:]
            for i in range(0, len(raw) - 8191, 8192): self.pcm.append(raw[i:i + 8192])
            self.decoded += n
        except Exception as e:
            self.err = f"{e.__class__.__name__}: {e}"
            for _ in range(n): self.pcm.append(bytes(8192))

    def next_pcm(self):
        """8192 bytes of 16-bit stereo for the next frame (silence if nothing is there yet)"""
        if not self.pcm and self.frames:
            import time
            t = self.__dict__.get('_thr')
            if t is not None and t.is_alive():              # the helper is on it: give it a moment
                self._ev.set(); end = time.perf_counter() + 0.1
                while not self.pcm and time.perf_counter() < end: time.sleep(0.002)
            if not self.pcm:
                with self._lockobj(): self._decode_batch()
        return self.pcm.popleft() if self.pcm else bytes(8192)


# ------------------------------------------------------------------ sceAtrac3plus: the game's music, voices and some sound effects
# The game hands over a whole .at3 file (RIFF/WAVE, ATRAC3 or ATRAC3+) that sits in memory. It is decoded in one go into
# 16-bit stereo, then handed back 1024 (ATRAC3) or 2048 (ATRAC3+) samples at a time, like the PSP does.
ERR_NO_ID = 0x80630004          # wrong / unused atrac ID
ERR_ALL_DECODED = 0x80630024    # nothing left to decode
ERR_NO_LOOP = 0x80630021        # file has no loop points
ERR_BAD_DATA = 0x80630002


class WholeDecode:
    """decodes a whole .at3 file in a background thread, so a long music track doesn't freeze the game while it decodes.
    pcm grows as it goes (16-bit stereo 44.1 kHz); done is set at the end."""
    def __init__(self, blob):
        import threading
        self.pcm = bytearray(); self.done = False; self.lock = threading.Lock(); self.need = 0; self.waiting = 0
        self.t = threading.Thread(target=self._work, args=(blob,), daemon=True); self.t.start()
    def _add(self, b):
        with self.lock: self.pcm += b
        # Paced: a few seconds ahead of the player is plenty. Decoding a whole track flat out (the alert music starts)
        # kept Python busy for seconds, and the drawing thread had to queue for Python after every step: the 60 fps
        # pictures came late and stuttered while being chased. A short sleep hands Python over at once.
        if not self.waiting and len(self.pcm) > self.need + 44100 * 4 * 4:
            import time; time.sleep(0.002)
    def _work(self, blob):
        try:
            try:
                import av
            except ImportError: av = None
            if av is not None:
                with av.open(io.BytesIO(blob), format='wav') as c:
                    rs = av.AudioResampler(format='s16', layout='stereo', rate=44100)
                    for fr in c.decode(audio=0):
                        res = rs.resample(fr)
                        for o in (res if isinstance(res, list) else [res]):
                            if o is not None: self._add(bytes(o.planes[0])[:o.samples * 4])
                    try:
                        res = rs.resample(None)
                        for o in (res if isinstance(res, list) else [res]):
                            if o is not None: self._add(bytes(o.planes[0])[:o.samples * 4])
                    except Exception: pass
            elif shutil.which('ffmpeg'):
                p = subprocess.Popen(['ffmpeg', '-loglevel', 'quiet', '-f', 'wav', '-i', 'pipe:0', '-f', 's16le', '-ac', '2', '-ar', '44100', 'pipe:1'],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE)
                import threading
                def feed():
                    try: p.stdin.write(blob); p.stdin.close()
                    except Exception: pass
                threading.Thread(target=feed, daemon=True).start()
                while True:
                    b = p.stdout.read(65536)
                    if not b: break
                    self._add(b)
        except Exception: pass
        self.done = True
    def get(self, start, end, wait=3.0):
        """bytes [start, end) of the pcm, waiting (a little) for the decoder to get there"""
        import time
        t = time.time()
        if end > self.need: self.need = end
        self.waiting += 1
        try:
            while len(self.pcm) < end and not self.done and time.time() - t < wait: time.sleep(0.002)
        finally: self.waiting -= 1
        with self.lock: return bytes(self.pcm[start:end])
    def length(self, wait=10.0):
        import time
        t = time.time()
        self.waiting += 1
        try:
            while not self.done and time.time() - t < wait: time.sleep(0.005)
        finally: self.waiting -= 1
        return len(self.pcm)


def parse_riff(b):
    """-> dict(fmt=tag, ch, block, total, offset, loops, datapos, datalen) or None"""
    if len(b) < 12 or b[:4] != b'RIFF' or b[8:12] != b'WAVE': return None
    info = {'total': None, 'offset': 0, 'loops': []}; p = 12
    while p + 8 <= len(b):
        cid = b[p:p + 4]; n = struct.unpack_from('<I', b, p + 4)[0]; d = p + 8
        if cid == b'fmt ' and n >= 16:
            info['fmt'], info['ch'] = struct.unpack_from('<HH', b, d); info['block'] = struct.unpack_from('<H', b, d + 12)[0]
        elif cid == b'fact' and n >= 4:
            info['total'] = struct.unpack_from('<I', b, d)[0]
            if n >= 8: info['offset'] = struct.unpack_from('<I', b, d + 4)[0]
        elif cid == b'smpl' and n >= 36:
            cnt = struct.unpack_from('<I', b, d + 28)[0]
            for i in range(min(cnt, 8)):
                if d + 36 + i * 24 + 24 <= len(b):
                    _, _, st, en, _, _ = struct.unpack_from('<6I', b, d + 36 + i * 24); info['loops'].append((st, en))
        elif cid == b'data':
            info['datapos'] = d; info['datalen'] = n; break
        p = d + n + (n & 1)
    return info if 'fmt' in info else None


class AtracLib:
    """sceAtrac3plus system calls (only the ones this game imports)."""
    _cache = {}

    def __init__(self, mach):
        self.m = mach; self.ids = {}; self.log = []

    def __getstate__(self):                               # save states: keep only where each player is, decode again after loading
        d = dict(self.__dict__); d['ids'] = {k: {kk: vv for kk, vv in v.items() if kk != 'pcm'} for k, v in self.ids.items()}; return d
    def __setstate__(self, d):
        self.__dict__.update(d)
        for k, v in self.ids.items(): v['pcm'] = None

    def _dec(self, a):
        if a.get('pcm') is None:
            key = a['key']
            if key not in AtracLib._cache:
                if len(AtracLib._cache) > 24: AtracLib._cache.pop(next(iter(AtracLib._cache)))
                AtracLib._cache[key] = WholeDecode(a['blob'])
            a['pcm'] = AtracLib._cache[key]
        return a['pcm']
    def _chunk(self, a, pos, n):
        """n samples from sample pos (after the encoder delay), as bytes"""
        d = self._dec(a); skip = a['offset'] * 4
        return d.get(skip + pos * 4, skip + (pos + n) * 4)

    def call(self, nid, r):
        cpu = self.m.cpu; a0, a1, a2, a3, t0 = r[4], r[5], r[6], r[7], r[8]
        a = self.ids.get(a0)
        if nid == 0x7A20E7AF:                                              # SetDataAndGetID(buf, size)
            try: b, o = cpu._loc(a0, 12); head = bytes(b[o:o + 12])
            except Exception: r[2] = ERR_BAD_DATA; return True
            size = struct.unpack_from('<I', head, 4)[0] + 8 if head[:4] == b'RIFF' else a1
            size = min(size, a1) if a1 else size
            try: b, o = cpu._loc(a0, size); blob = bytes(b[o:o + size])
            except Exception: r[2] = ERR_BAD_DATA; return True
            info = parse_riff(blob)
            if info is None: r[2] = ERR_BAD_DATA; self.log.append(f"not an .at3 file at {a0:#x}"); return True
            uid = next(i for i in range(64) if i not in self.ids)
            per = 2048 if info['fmt'] == 0xFFFE else 1024
            off0 = info['offset'] if info['total'] else 0
            # loop points in the file count the encoder's lead-in samples too; positions here start after it
            info['loops'] = [(max(0, st - off0), max(0, en - off0)) for st, en in info['loops']]
            self.ids[uid] = {'blob': blob, 'key': (len(blob), zlib.crc32(blob)), 'pos': 0, 'per': per, 'loopnum': 0, 'pcm': None,
                             'total': info['total'], 'offset': info['offset'] if info['total'] else 0, 'loops': info['loops'], 'ended': False}
            if self.ids[uid]['total'] is None:                            # no length in the file: decode it all first
                self.ids[uid]['total'] = max(0, self._dec(self.ids[uid]).length() // 4 - self.ids[uid]['offset'])
            else: self._dec(self.ids[uid])                                 # start decoding now, in the background
            r[2] = uid
        elif nid == 0x61EB33F5:                                            # ReleaseAtracID
            r[2] = 0 if self.ids.pop(a0, None) is not None else ERR_NO_ID
        elif a is None and nid in (0x6A8C3CD5, 0x868120B5, 0xA2BBA8BE, 0xD6A5F2F7, 0xFAA4F89B, 0x644E5607):
            r[2] = ERR_NO_ID
        elif nid == 0x6A8C3CD5:                                            # DecodeData(id, out, *samples, *end, *remainFrames)
            total = a['total']; per = a['per']; pos = a['pos']
            if pos >= total and not (a['loops'] and a['loopnum'] != 0):
                if a1: cpu.w32(a1, 0)
                if a2: cpu.w32(a2, 0)
                if a3: cpu.w32(a3, 1)
                if t0: cpu.w32(t0, 0xFFFFFFFF)
                r[2] = ERR_ALL_DECODED; return True
            looping = bool(a['loops']) and a['loopnum'] != 0
            limit = min(total, a['loops'][0][1] + 1) if looping else total
            if looping and pos >= limit:                                    # (never hand back 0 samples while looping:
                pos = min(a['loops'][0][0], max(0, limit - 1))             #  the game's music player then waits forever)
                if a['loopnum'] > 0: a['loopnum'] -= 1
            n = max(0, min(per, limit - pos)); chunk = self._chunk(a, pos, n); pos += n; end = 0
            if len(chunk) < n * 4: chunk += bytes(n * 4 - len(chunk))      # decoder fell behind / file shorter: silence
            if looping and pos >= limit:
                pos = a['loops'][0][0]
                if a['loopnum'] > 0: a['loopnum'] -= 1
            elif pos >= total: end = 1
            a['pos'] = pos
            if a0 is not None and a1:
                try: cpu.load_bytes(a1, chunk)
                except Exception: pass
            if a2: cpu.w32(a2, n)
            if a3: cpu.w32(a3, end)
            if t0: cpu.w32(t0, 0xFFFFFFFF)                                 # -1: the whole file is in memory
            r[2] = 0
        elif nid == 0x868120B5:                                            # SetLoopNum
            if not a['loops']: r[2] = ERR_NO_LOOP
            else: a['loopnum'] = a1 - (1 << 32) if a1 & 0x80000000 else a1; r[2] = 0
        elif nid == 0xA2BBA8BE:                                            # GetSoundSample(id, *end, *loopStart, *loopEnd)
            if a1: cpu.w32(a1, (a['total'] - 1) & 0xFFFFFFFF)
            if a2: cpu.w32(a2, a['loops'][0][0] if a['loops'] else 0xFFFFFFFF)
            if a3: cpu.w32(a3, a['loops'][0][1] if a['loops'] else 0xFFFFFFFF)
            r[2] = 0
        elif nid == 0xD6A5F2F7:                                            # GetMaxSample
            if a1: cpu.w32(a1, a['per'])
            r[2] = 0
        elif nid == 0xFAA4F89B:                                            # GetLoopStatus(id, *loopNum, *status)
            if a1: cpu.w32(a1, a['loopnum'] & 0xFFFFFFFF)
            if a2: cpu.w32(a2, 1 if a['loops'] else 0)
            r[2] = 0
        elif nid == 0x644E5607:                                            # ResetPlayPosition(id, sample, ...)
            a['pos'] = a1; r[2] = 0
        elif nid == 0x132F1ECA: r[2] = 0                                   # Reinit
        else: return False
        return True
