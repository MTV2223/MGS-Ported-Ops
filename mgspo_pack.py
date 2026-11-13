"""Packs locked to the game disc.

The launcher's art (the FOXHOUND intro and logo) and the HD packs (upscaled movies and textures) are kept in .pak files
that can only be opened with your own copy of the game: the key is made from the game's program file on the disc
(PSP_GAME/SYSDIR/EBOOT.BIN, exactly as it is on the USA disc ULUS10202). Without the disc the packs are noise.

A .pak file:  b"MGSPOPK1" | 16-byte check of the key | u32 length of the list | the list (JSON, locked) | the files (locked)
The list says each file's place in the port's folder ("HD movies/s01_01.mp4"), its size and where its bytes start.
"""
import os, io, json, struct, hashlib

MAGIC = b"MGSPOPK1"
CHUNK = 4 << 20


# ------------------------------------------------------------------------------------------------ the disc
class Iso:
    """just enough ISO 9660 to find and read files on the disc image"""
    def __init__(self, path):
        self.f = open(path, "rb"); self.f.seek(16 * 2048); pvd = self.f.read(2048)
        if pvd[1:6] != b"CD001": self.f.close(); raise ValueError("not an ISO 9660 disc image")
        self.root = (struct.unpack_from("<I", pvd, 158)[0], struct.unpack_from("<I", pvd, 166)[0])
    def close(self): self.f.close()
    def entries(self, lba, size):
        self.f.seek(lba * 2048); data = self.f.read(size); i = 0
        while i < len(data):
            ln = data[i]
            if ln == 0: i = (i // 2048 + 1) * 2048; continue
            rec = data[i:i + ln]; i += ln
            nm = rec[33:33 + rec[32]]
            if nm in (b"\x00", b"\x01"): continue
            yield nm.decode("latin1").split(";")[0].rstrip("."), struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0], bool(rec[25] & 2)
    def find(self, path):
        lba, size = self.root; isdir = True
        for part in path.strip("/").split("/"):
            for name, l, s, d in self.entries(lba, size):
                if name.lower() == part.lower(): lba, size, isdir = l, s, d; break
            else: return None
        return lba, size, isdir
    def read(self, path):
        e = self.find(path)
        if e is None or e[2]: return None
        self.f.seek(e[0] * 2048); return self.f.read(e[1])
    def count_files(self, lba=None, size=None, depth=0):
        if lba is None: lba, size = self.root
        n = 0
        for name, l, s, d in self.entries(lba, size):
            n += self.count_files(l, s, depth + 1) if d and depth < 12 else (0 if d else 1)
        return n


def disc_key(iso_path):
    """the key from your disc (None if the disc image can't be read)"""
    try:
        iso = Iso(iso_path)
        try: data = iso.read("PSP_GAME/SYSDIR/EBOOT.BIN")
        finally: iso.close()
    except Exception: return None
    if not data: return None
    return hashlib.sha256(b"mgspo-disc-key\x00" + data).digest()


def key_check(key): return hashlib.sha256(b"mgspo-check\x00" + key).digest()[:16]


# ------------------------------------------------------------------------------------------------ locking
def _xor(data, key, name, start):
    """lock / unlock (the same thing) a run of bytes of one file, starting at byte 'start' of that file"""
    out = bytearray(); pos = 0
    while pos < len(data):
        j = (start + pos) // CHUNK; o = (start + pos) % CHUNK
        n = min(len(data) - pos, CHUNK - o)
        ks = hashlib.shake_256(key + name.encode("utf-8") + j.to_bytes(8, "little")).digest(o + n)[o:]
        a = data[pos:pos + n]
        out += (int.from_bytes(a, "little") ^ int.from_bytes(ks, "little")).to_bytes(n, "little")
        pos += n
    return bytes(out)


def write_pak(out_path, key, files, progress=None):
    """files: list of (place in the port's folder, file on disk) -> one .pak"""
    items = []; off = 0
    for rel, src in files:
        sz = os.path.getsize(src); items.append({"path": rel.replace("\\", "/"), "size": sz, "off": off}); off += sz
    lst = _xor(json.dumps(items).encode("utf-8"), key, "\x00list", 0)
    tmp = out_path + ".part"
    with open(tmp, "wb") as f:
        f.write(MAGIC + key_check(key) + struct.pack("<I", len(lst)) + lst)
        done = 0
        for (rel, src), it in zip(files, items):
            with open(src, "rb") as s:
                at = 0
                while True:
                    b = s.read(CHUNK)
                    if not b: break
                    f.write(_xor(b, key, it["path"], at)); at += len(b); done += len(b)
                    if progress: progress(done, off, it["path"])
    os.replace(tmp, out_path)
    return off


class Pak:
    def __init__(self, path):
        self.path = path; self.f = open(path, "rb")
        head = self.f.read(28)
        if len(head) < 28 or head[:8] != MAGIC: self.f.close(); raise ValueError("not a Ported Ops pack")
        self.check = head[8:24]; self.ll = struct.unpack_from("<I", head, 24)[0]; self.items = None; self.base = 28 + self.ll
    def opens_with(self, key): return key is not None and key_check(key) == self.check
    def unlock(self, key):
        if not self.opens_with(key): raise ValueError("this pack doesn't open with this disc")
        self.f.seek(28); self.items = json.loads(_xor(self.f.read(self.ll), key, "\x00list", 0)); self.key = key
        return self.items
    def total(self): return sum(i["size"] for i in self.items)
    def read(self, it):
        self.f.seek(self.base + it["off"]); return _xor(self.f.read(it["size"]), self.key, it["path"], 0)
    def extract(self, root, progress=None, done0=0):
        """write every file into the port's folder. -> bytes written"""
        root = os.path.abspath(root); done = done0
        for it in self.items:
            dst = os.path.abspath(os.path.join(root, *it["path"].split("/")))
            if not dst.startswith(root + os.sep): continue                         # (never outside the port's folder)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if os.path.exists(dst) and os.path.getsize(dst) == it["size"]:         # (already there)
                done += it["size"]
                if progress: progress(done, it["path"])
                continue
            tmp = dst + ".installing"
            with open(tmp, "wb") as o:
                self.f.seek(self.base + it["off"]); at = 0
                while at < it["size"]:
                    b = self.f.read(min(CHUNK, it["size"] - at))
                    if not b: raise IOError("the pack is cut short (download it again)")
                    o.write(_xor(b, self.key, it["path"], at)); at += len(b); done += len(b)
                    if progress: progress(done, it["path"])
            os.replace(tmp, dst)
        return done - done0
    def close(self): self.f.close()


# ------------------------------------------------------------------------------------------------ the game's sounds
_VAG_F = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))


def vag_to_pcm(data):
    """the PSP's sound format (16-byte blocks of 28 samples) -> 16-bit samples (bytes), up to the end mark"""
    out = bytearray(); s1 = s2 = 0
    for o in range(0, len(data) - 15, 16):
        b = data[o:o + 16]; pred, shift, flags = b[0] >> 4, b[0] & 15, b[1]
        if flags == 7: break
        f0, f1 = _VAG_F[pred if pred < 5 else 0]; sh = 12 - shift if shift <= 12 else 0
        for i in range(28):
            n = (b[2 + (i >> 1)] >> (4 if i & 1 else 0)) & 15
            n = (n - 16 if n & 8 else n) << sh
            s = n + ((s1 * f0 + s2 * f1) >> 6); s = -32768 if s < -32768 else 32767 if s > 32767 else s
            out += struct.pack("<h", s); s2, s1 = s1, s
        if flags & 1: break
    return bytes(out)


def wav_bytes(pcm, rate, gain=1.0):
    import array
    a = array.array("h"); a.frombytes(pcm)
    if gain != 1.0: a = array.array("h", (max(-32768, min(32767, int(v * gain))) for v in a))
    n = len(a); fade = min(n // 8, int(rate * 0.002))                        # (a few ms fade at both ends: no clicks)
    for i in range(fade): a[i] = int(a[i] * i / fade); a[n - 1 - i] = int(a[n - 1 - i] * i / fade)
    raw = a.tobytes()
    return (b"RIFF" + struct.pack("<I", 36 + len(raw)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
            + b"data" + struct.pack("<I", len(raw)) + raw)


# the game's own menu sounds (title menu, after PRESS START): where they are on the disc - inside the stage archive
# r_main01 (u32 size + one zlib stream). Each sound: the samples the game plays together, (offset, length, pitch, volume)
# exactly as the game asks its sound chip for them (pitch 0x1000 = 44100 Hz).
SOUND_SRC = "PSP_GAME/USRDIR/stage/r_main01/_zar"
# the game's own menu sounds - the Mother Base hub menu, after PRESS START: what the game asks its sound chip for, each
# part = (offset, length, pitch, volume, start in ms) (pitch 0x1000 = 44100 Hz)
GAME_SOUNDS = {
    "move": [(0x1D4F48, 1504, 2295, 1221, 0), (0x1E1E10, 2416, 4591, 398, 98)],          # to the next tab
    "ok":   [(0x1D4960, 1504, 5460, 810, 0), (0x1D4960, 1504, 5460, 810, 34),             # a tab chosen
             (0x1C4530, 48, 7888, 398, 92), (0x1D5530, 2928, 6878, 1072, 133)],
}
# the hub's music: an ATRAC3+ track (.at3, with its loop points) inside the briefing stage's archive
MUSIC_SRC = "PSP_GAME/USRDIR/stage/briefing/_zar"
MUSIC_AT = (0x4193C4, 925876)


def _resample(pcm, pitch):
    """the voice played at its pitch -> 44100 Hz samples (linear)"""
    import array
    a = array.array("h"); a.frombytes(pcm); step = pitch / 4096.0; out = array.array("h"); x = 0.0; n = len(a)
    while x < n - 1:
        i = int(x); f = x - i; out.append(int(a[i] * (1 - f) + a[i + 1] * f)); x += step
    return out


def _read_inflated(iso_path, path):
    iso = Iso(iso_path)
    try: raw = iso.read(path)
    finally: iso.close()
    return zlib_inflate(raw)


def game_sounds_pcm(iso_path):
    """-> {name: mono 16-bit samples at 44100 Hz (array)} of the game's menu sounds ({} if they can't be read)"""
    import array
    try: blob = _read_inflated(iso_path, SOUND_SRC)
    except Exception: return {}
    out = {}
    for name, parts in GAME_SOUNDS.items():
        mix = []
        for off, ln, pitch, vol, ms in parts:
            s = _resample(vag_to_pcm(blob[off:off + ln]), pitch); d = int(44100 * ms / 1000)
            if d + len(s) > len(mix): mix += [0.0] * (d + len(s) - len(mix))
            for i, v in enumerate(s): mix[d + i] += v * vol
        if not mix: continue
        peak = max(1.0, max(abs(v) for v in mix)); g = 0.55 * 32767 / peak     # (about the loudness of the game's menu)
        a = array.array("h", (int(v * g) for v in mix)); n = len(a); fade = min(n // 8, 88)
        for i in range(fade): a[i] = int(a[i] * i / fade); a[n - 1 - i] = int(a[n - 1 - i] * i / fade)
        out[name] = a
    return out


def game_sounds(iso_path, names=None):
    """-> {name: WAV bytes} of the game's menu sounds ({} if they can't be read)"""
    return {k: wav_bytes(v.tobytes(), 44100) for k, v in game_sounds_pcm(iso_path).items() if not names or k in names}


def game_music(iso_path):
    """-> the hub music as an .at3 file (bytes), or None"""
    try:
        blob = _read_inflated(iso_path, MUSIC_SRC); off, ln = MUSIC_AT; at3 = blob[off:off + ln]
        return at3 if at3[:4] == b"RIFF" and at3[8:12] == b"WAVE" else None
    except Exception: return None


def zlib_inflate(raw):
    import zlib
    return zlib.decompress(raw[4:])
