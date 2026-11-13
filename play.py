# MGS Portable Ops PC port - PLAY WINDOW
# Opens a window, shows the game and lets you play it with the keyboard.
#
# How to start (any of these):
#   - double-click  play.bat
#   - or paste into a fresh Python window:   exec(open(r"<the port's folder>\play.py").read())
#
# Keys:
#   W A S D = analog stick (walking)          mouse = camera (the D-pad: left / right turns, up / down = the views)
#   Space = Cross (X)    R = Circle (O)    left mouse button = Square    Shift = Triangle
#   right mouse button = L button    middle mouse button (press the wheel) = R button    Tab = START    C = SELECT
#   Arrow keys = D-pad too
#   F10 = let go of the mouse (click in the window to take it again)
#   Holding R (Circle, weapon / item select) and on menu screens (mission / staff hub, codec): W A S D = the D-pad.
#   Holding the right mouse button (L, auto aim) + left Ctrl: W A S D strafe (the game's D-pad then).
#   Holding the middle mouse button (R, first-person aiming): W A S D walk and the mouse aims (the game swaps stick and
#   D-pad there; this swaps them back so it feels like a PC shooter). Also holding Shift: W A S D aim instead. port_settings.txt: aim_swap, aim_sensitivity
#   F5 = save a snapshot (state_quick.pkl.gz; the next start continues from it)
#   F11 = bigger / smaller window (full screen)   F2 = sound on/off   F3 = how the picture is shown   Esc = quit
#   F4 = 60 fps on/off (graphics-card drawing only)
#   F6 = remaster lighting on/off (shadows, sun shading, colour grade)
#   F9 = 60 fps in-between pictures: exact (new, default) or the old way - to compare
#   F7 = 10 seconds of exact timings -> perf_log.txt     F8 = problem report: a snapshot, a screenshot and the logs in
#        one zip in the folder "problem_reports" - send that zip to Claude (press it right after something looks wrong)
#   An Xbox-style controller works too: A = Cross, B = Circle, X = Square, Y = Triangle, LB/RB = L/R,
#   Start = START, Back = SELECT, left stick = analog stick, D-pad = D-pad.
#   Controller: LB (auto aim) + LT held = left stick strafes. RB (first-person aim): left stick walks, right stick aims.
#
# It starts from the newest snapshot it finds: state_quick, then state_menu, then state_movie2, else from power-on.
# Speed: needs the fast core (psp_core.dll, made by build_core.bat). Without it the game runs, but very slowly.
import sys, os, time

FOLDER = os.getcwd()                                        # (the port's folder: where this file is, see below)
try:
    _here = os.path.dirname(os.path.abspath(__file__))
    if os.path.exists(os.path.join(_here, "psp_run.py")): FOLDER = _here
except NameError: pass
sys.path.insert(0, FOLDER)
PIPES_FILE = os.path.join(FOLDER, "vk_pipelines.bin")       # (Vulkan) drawing setups kept from earlier sessions

class _Tee:                                     # everything printed also goes to play_log.txt (to send to Claude)
    def __init__(self, a, b): self.a, self.b = a, b
    def write(self, s):
        for x in (self.a, self.b):
            try: x.write(s); x.flush()
            except Exception: pass
    def flush(self): pass
try:
    _log = open(os.path.join(FOLDER, "play_log.txt"), "w", encoding="utf-8")
    sys.stdout = _Tee(sys.stdout, _log) if sys.stdout is not None else _log
    sys.stderr = _Tee(sys.stderr, _log) if sys.stderr is not None else _log
except OSError: pass
print("Python", sys.version.split()[0], "-", sys.executable)
for _m in [k for k in list(sys.modules) if k.startswith("psp_") or k == "prx_loader"]: del sys.modules[_m]   # always use the newest files
import psp_run, psp_cpu, psp_lockers

KEYS = {'Up': 0x10, 'Right': 0x20, 'Down': 0x40, 'Left': 0x80,
        'space': 0x4000, 'r': 0x2000, 'Shift_L': 0x1000, 'Shift_R': 0x1000, 'Tab': 0x8, 'c': 0x1,
        'MOUSE1': 0x8000, 'MOUSE3': 0x100, 'MOUSE2': 0x200,                  # left mouse = Square, right = L, middle (wheel press) = R
        'MCAM_L': 0x80, 'MCAM_R': 0x20, 'MCAM_U': 0x10, 'MCAM_D': 0x40}      # (mouse camera: the D-pad)
STICK = {'w': (0, -1), 's': (0, 1), 'a': (-1, 0), 'd': (1, 0)}
SNAPS = ["state_quick.pkl.gz", "state_menu.pkl.gz", "state_movie2.pkl.gz"]


PACE_MARGIN_US = 12_000          # 60 fps: shown this much later than the newest game picture allows (drawing time)


def find_texture_packs():
    """textures.ini files of HD texture packs: "HD textures" (or "TEXTURES", PPSSPP's name) next to play.bat, each
    pack in a folder of its own inside (or right inside). Where packs have the same texture, the one listed first
    wins: your own packs (any folder) come before the downloaded PPSSPP pack (the folder ULUS10202)."""
    found = []
    for top in ("HD textures", "HD Textures", "TEXTURES", "textures"):
        d = os.path.join(FOLDER, top)
        if not os.path.isdir(d): continue
        p = os.path.join(d, "textures.ini")
        if os.path.exists(p): found.append(p)
        for name in sorted(os.listdir(d), key=lambda n: (n.upper() == "ULUS10202", n.lower())):
            p = os.path.join(d, name, "textures.ini")
            if os.path.exists(p): found.append(p)
    out = []
    for p in found:
        if os.path.normcase(os.path.abspath(p)) not in [os.path.normcase(os.path.abspath(q)) for q in out]: out.append(p)
    return out


hd_state = {}                        # (HD texture loading: for the logs)


def hd_alpha_cleanup(im):
    """cut-out pictures (bars, railings, fences, leaves): on the PSP each pixel is either solid or see-through. The
    upscaler leaves the solid parts speckled with slightly see-through pixels (alpha 160-250), and the game draws only
    fully solid pixels there - so the HD version came out full of holes and glittered (table frames, cell bars).
    A picture whose pixels are nearly all either clearly see-through or nearly solid is made exactly that again;
    pictures with real soft see-through parts (smoke, glow, glass) are left as they are."""
    a = im.getchannel('A'); hist = a.histogram(); n = im.size[0] * im.size[1]
    soft = sum(hist[17:160]); near = sum(hist[160:255])
    if near == 0 or soft > 0.03 * n: return im
    # (a picture that is evenly a little see-through - window glass, a dirt layer: most of it nearly solid, hardly any
    #  pixel fully solid - is no cut-out either. Made solid, the windows of the base could not be seen through.)
    if near > 0.5 * n and hist[255] < 0.02 * n: return im
    im = im.copy(); im.putalpha(a.point(lambda v: 255 if v >= 160 else (0 if v <= 16 else v))); return im


def start_hd_textures(mach, lib):
    """read the packs' lists (textures.ini [hashes]: name = file), tell the renderer the names, and load each PNG the
    first time the game uses that texture (on a thread of its own: the game never waits for it)"""
    import ctypes as ct, threading
    files = {}; used = []; hashbytes = set(); premul = set()
    for ini in find_texture_packs():
        base = os.path.dirname(ini); section = ''; quick = True; mine = {}; pm = False
        for line in open(ini, encoding='utf-8-sig', errors='replace'):
            line = line.split('#', 1)[0].strip()
            if not line: continue
            if line.startswith('['): section = line.strip('[]').strip().lower(); continue
            if '=' not in line: continue
            k, v = (x.strip() for x in line.split('=', 1))
            if section == 'options' and k.lower() == 'hash' and v.lower() != 'quick': quick = False
            # (Ported Ops) the pack's pictures came out of the upscaler with their colours multiplied by their
            # see-through-ness (chaiNNer's "separate alpha"): undone as each one is loaded
            if section == 'options' and k.lower() == 'premultiplied': pm = v.lower() in ('true', 'yes', 'on', '1')
            if section == 'hashbytes':                       # (Ported Ops) fmt,width,height,bufw = bytes hashed
                try:
                    f = [x.strip().lower() for x in k.split(',')]; vram = f[-1] == 'vram'
                    if vram: f = f[:-1]
                    f = [int(x, 0) for x in f]; f[0] |= 0x100 if vram else 0
                    hashbytes.add(tuple(f) + (int(v, 0),))
                except ValueError: pass
            if section == 'hashes' and len(k) == 24 and v:
                try: mine[(int(k[0:8], 16), int(k[8:16], 16), int(k[16:24], 16))] = os.path.join(base, v.split('|')[0].replace('/', os.sep))
                except ValueError: pass
        if not quick: print(f"HD textures: {os.path.relpath(ini, FOLDER)} uses a hash kind the port doesn't know yet (only 'quick') - skipped"); continue
        # (a picture an earlier pack already has - same size and contents, any address - stays the earlier pack's)
        have = {(k[1], k[2]) for k in files}; n0 = len(files)
        for k, v in mine.items():
            if k not in files and (k[1], k[2]) not in have:
                files[k] = v
                if pm: premul.add(v)
        used.append(f"{os.path.relpath(ini, FOLDER)} ({len(files) - n0})")
    if not files: return
    # pictures matched by near_matcher() in earlier sessions (see there): name = file, or "-" for none
    packs = find_texture_packs(); top = os.path.dirname(os.path.dirname(packs[0])) if packs else FOLDER
    learned_path = os.path.join(top, "learned_matches.ini"); learned = {}; nl = 0
    try:
        for line in open(learned_path, encoding='utf-8', errors='replace'):
            line = line.split('#', 1)[0].strip()
            if '=' not in line: continue
            k, v = (x.strip() for x in line.split('=', 1))
            try: kk = (int(k[0:8], 16), int(k[8:16], 16), int(k[16:24], 16))
            except ValueError: continue
            learned[kk] = v
            if v != '-':
                full = os.path.join(top, v.replace('/', os.sep))
                if os.path.exists(full) and kk not in files: files[kk] = full; nl += 1
    except OSError: pass
    if nl: used.append(f"learned_matches.ini ({nl})")
    keys = list(files)
    arr = (ct.c_uint32 * (3 * len(keys)))(*[x for k in keys for x in k])
    lib.gl_replace_add.argtypes = [ct.c_void_p, ct.c_int]; lib.gl_replace_put.argtypes = [ct.c_int, ct.c_int, ct.c_int, ct.c_char_p]
    lib.gl_replace_requests.argtypes = [ct.c_void_p, ct.c_int]; lib.gl_replace_key.argtypes = [ct.c_int, ct.c_void_p]
    n = mach.ge.call(lambda: lib.gl_replace_add(arr, len(keys)))
    for hb in hashbytes:
        if len(hb) == 5: mach.ge.call(lambda hb=hb: lib.gl_replace_hashbytes(*hb))
    print(f"HD textures: on - {', '.join(used)} - {n} textures, loaded when first seen", flush=True)
    try: mach.ge.call(lambda: lib.gl_replace_budget(max(128, int(load_settings().get('hd_texture_memory', '1536')))))
    except (ValueError, AttributeError): pass
    from PIL import Image
    # (three loaders, the smallest pictures first: lettering and menu pictures are small and wanted at once - behind a
    #  queue of big world pictures, walking into a new area, they showed the blurry PSP version for a while)
    import queue as _queue
    todo = _queue.PriorityQueue(); seqn = [0]; hd_state['files'] = files
    def poller():
        req = (ct.c_int * 256)(); key = (ct.c_uint32 * 3)()
        while True:
            k = lib.gl_replace_requests(req, 256)
            if not k: time.sleep(0.02); continue
            for i in range(k):
                idx = req[i]; lib.gl_replace_key(idx, key); path = files.get((key[0], key[1], key[2]))
                try: size = os.path.getsize(path)
                except (OSError, TypeError): size = 0
                seqn[0] += 1; todo.put((size, seqn[0], idx, path)); hd_state['waiting'] = todo.qsize()
    def loader():
        ev = threading.Event()
        while True:
            _, _, idx, path = todo.get(); hd_state['waiting'] = todo.qsize()
            if True:
                try:
                    im = Image.open(path).convert('RGBA')
                    if path in premul: im = Image.frombytes('RGBa', im.size, im.tobytes()).convert('RGBA')
                    im = hd_alpha_cleanup(im)
                    w, h = im.size; data = im.tobytes()
                except Exception as e:
                    print("HD textures: could not load", path, "-", e, flush=True); w = h = 0; data = None
                # handed to the drawing thread one at a time, a few ms apart: a burst of new textures (walking into a
                # new area) is spread over several pictures instead of holding up one of them
                ev.clear()
                def put(idx=idx, w=w, h=h, data=data):
                    try: lib.gl_replace_put(idx, w, h, data)
                    finally: ev.set()
                put.kind = 'HD texture'
                mach.ge.post(put); ev.wait(1.0); time.sleep(0.004)
                hd_state['loaded'] = hd_state.get('loaded', 0) + 1
    threading.Thread(target=poller, daemon=True).start()
    for _ in range(3): threading.Thread(target=loader, daemon=True).start()
    if hasattr(lib, 'gl_near_take') and load_settings().get('hd_near_match', 'on') != 'off':
        threading.Thread(target=near_matcher, args=(mach, lib, files, premul, top, learned_path, learned), daemon=True).start()
        mach.ge.call(lambda: lib.gl_near_on(1))


def near_matcher(mach, lib, files, premul, top, learned_path, learned):
    """HD pictures that the packs have under another name. The packs name a picture by a hash of all its bytes; when
    the game leaves different leftover memory after a picture (rows under a codec portrait, bytes after a strip of
    digits), the same picture gets a new name and the blurry PSP version is shown. The renderer hands over each picture
    with no HD version whose size and palette some pack picture has; it is compared with those (shrunk to its size):
    the same picture - apart from a tail of rows at the bottom, where leftovers are - gets that HD picture, now and
    in later sessions (learned_matches.ini; "-" = none). Anything else that differs (other digits, another face) is
    not a match."""
    import ctypes as ct
    from PIL import Image, ImageChops, ImageStat
    byb = {}
    for k, v in files.items(): byb.setdefault(k[1], []).append(v)
    shrunk = {}
    def small(path, w, h):
        key = (path, w, h)
        if key not in shrunk:
            im = Image.open(path).convert('RGBA')
            if im.size[0] < w or im.size[1] < h or abs(im.size[0] * h - im.size[1] * w) > im.size[0]: shrunk[key] = None
            else:
                pm = Image.frombytes('RGBa', im.size, im.tobytes()) if path in premul else im.convert('RGBa')
                shrunk[key] = Image.frombytes('RGBA', (w, h), pm.resize((w, h), Image.BOX).tobytes())
            if len(shrunk) > 400: shrunk.pop(next(iter(shrunk)))
        return shrunk[key]
    def judge(sd, hd, w, h):
        d = ImageChops.difference(sd, hd); r, g, b, a = d.split()
        m = ImageChops.lighter(ImageChops.lighter(r, g), ImageChops.lighter(b, a))
        bad = m.point(lambda v: 255 if v > 64 else 0)
        rows = list(bad.resize((1, h), Image.BOX).tobytes())               # (share of bad pixels per row, 0-255)
        tot = 0.0; good = 0
        for y in range(h):
            tot += rows[y] / 255.0 * w
            if tot <= max(2.0, 0.003 * (y + 1) * w): good = y + 1
        if good < max(1, int(h * 0.75 + 0.5)): return None
        e = ImageStat.Stat(m.crop((0, 0, w, good))).mean[0]
        r, g, b, a = sd.crop((0, 0, w, good)).split()
        ink = ImageStat.Stat(ImageChops.lighter(ImageChops.lighter(r, g), ImageChops.lighter(b, a))).mean[0]
        # (a nearly empty picture - a few dots, or all see-through - looks like any other nearly empty one: never a match)
        if ink < 12 or e > 0.15 * ink: return None
        return e
    def remember(kk, v):
        learned[kk] = v
        try:
            with open(learned_path, 'a', encoding='utf-8') as f:
                if f.tell() == 0: f.write("# HD pictures the port matched by looking (made by itself; delete a line to have it look again)\n")
                f.write("%08x%08x%08x = %s\n" % (kk + (v,)))
        except OSError: pass
    key = (ct.c_uint32 * 3)(); wh = (ct.c_int * 2)(); buf = (ct.c_uint8 * (256 * 256 * 4))()
    while True:
        time.sleep(0.5)
        try:
            while mach.ge.call(lambda: lib.gl_near_take(key, wh, buf, len(buf))):
                kk = (0, key[1], key[2]); w, h = wh[0], wh[1]
                if not w or kk in learned: continue
                cands = byb.get(key[1], [])
                if not cands or len(cands) > 300: continue
                sd = Image.frombytes('RGBA', (w, h), bytes(buf)[:w * h * 4]).convert('RGBa')
                sd = Image.frombytes('RGBA', (w, h), sd.tobytes())
                best = None
                for path in set(cands):
                    try: hd = small(path, w, h)
                    except Exception: continue
                    if hd is None: continue
                    e = judge(sd, hd, w, h)
                    if e is not None and e < 14 and (best is None or e < best[0]): best = (e, path)
                if best is None: remember(kk, '-'); continue
                rel = os.path.relpath(best[1], top).replace(os.sep, '/'); remember(kk, rel)
                files[kk] = best[1]; byb.setdefault(key[1], []).append(best[1])
                arr = (ct.c_uint32 * 3)(*kk)
                mach.ge.call(lambda: (lib.gl_replace_add(arr, 1), lib.gl_replace_relink()))
                print(f"HD textures: matched {key[0]:08x}{key[1]:08x}{key[2]:08x} {w}x{h} to {rel}", flush=True)
        except Exception as ex:
            print("HD textures: near match stopped -", ex, flush=True); return


class VBlankClock:
    """when the screen refreshes, from Windows' compositor (DwmGetCompositionTimingInfo), in perf_counter seconds"""
    def __init__(self):
        self.ok = False; self.period = None
        try:
            import ctypes as ct
            class TI(ct.Structure):
                _pack_ = 1
                _fields_ = [('cbSize', ct.c_uint32), ('rateRefresh', ct.c_uint32 * 2), ('qpcRefreshPeriod', ct.c_uint64),
                            ('rateCompose', ct.c_uint32 * 2), ('qpcVBlank', ct.c_uint64), ('rest', ct.c_uint8 * 400)]
            self.ct = ct; self.TI = TI; self.dwm = ct.WinDLL('dwmapi'); k = ct.WinDLL('kernel32')
            f = ct.c_int64(); k.QueryPerformanceFrequency(ct.byref(f)); self.freq = float(f.value)
            c = ct.c_int64(); t0 = time.perf_counter(); k.QueryPerformanceCounter(ct.byref(c)); t1 = time.perf_counter()
            self.off = c.value / self.freq - (t0 + t1) / 2                        # QPC seconds - perf_counter
            ti = None
            for self.size in (292, 312, 304):                                    # sizeof(DWM_TIMING_INFO), packed
                ti = self._get()
                if ti: break
            if ti and ti.qpcRefreshPeriod: self.period = ti.qpcRefreshPeriod / self.freq; self.ok = 0.002 < self.period < 0.05
        except Exception: self.ok = False
    def _get(self):
        ti = self.TI(); ti.cbSize = self.size
        return ti if self.dwm.DwmGetCompositionTimingInfo(None, self.ct.byref(ti)) == 0 else None
    def nearest(self, t):
        """the screen refresh closest to time t (before or after it)"""
        ti = self._get()
        if not ti or not ti.qpcVBlank: return None
        if ti.qpcRefreshPeriod: self.period = ti.qpcRefreshPeriod / self.freq
        v = ti.qpcVBlank / self.freq - self.off
        return v + round((t - v) / self.period) * self.period
    def next_after(self, t):
        ti = self._get()
        if not ti or not ti.qpcVBlank: return None
        if ti.qpcRefreshPeriod: self.period = ti.qpcRefreshPeriod / self.freq
        v = ti.qpcVBlank / self.freq - self.off
        if v <= t: v += (int((t - v) / self.period) + 1) * self.period
        return v


def screen_refresh():
    """the monitor's refresh rate (Hz), 60 if unknown"""
    try:
        import ctypes as ct
        u = ct.WinDLL('user32'); g = ct.WinDLL('gdi32'); u.GetDC.restype = ct.c_void_p; u.GetDC.argtypes = [ct.c_void_p]
        g.GetDeviceCaps.argtypes = [ct.c_void_p, ct.c_int]; hdc = u.GetDC(None); hz = g.GetDeviceCaps(hdc, 116)   # VREFRESH
        u.ReleaseDC.argtypes = [ct.c_void_p, ct.c_void_p]; u.ReleaseDC(None, hdc)
        return float(hz) if hz and hz > 1 else 60.0
    except Exception: return 60.0


PAD_EXTRA = {'rt': False, 'rb': False}                                    # (the controller's R2 and R1, for the locker capture)


def pad_mapper(right_stick_camera=True, aim_swap=True):
    """a controller's state in XInput terms (buttons, triggers 0-255, sticks -32768..32767, up = +) -> (PSP buttons, analog)"""
    MAP = {0x1: 0x10, 0x2: 0x40, 0x4: 0x80, 0x8: 0x20, 0x10: 0x8, 0x20: 0x1, 0x100: 0x100, 0x200: 0x200,
           0x1000: 0x4000, 0x2000: 0x2000, 0x4000: 0x8000, 0x8000: 0x1000}     # A=Cross B=Circle X=Square Y=Triangle
    swap_aim = [aim_swap]
    def stick(x, y):
        if abs(x) < 7849 and abs(y) < 7849: x = y = 0
        return max(0, min(255, 128 + x // 256)) | (max(0, min(255, 128 - y // 256)) << 8)
    def stick_dpad(x, y):                                              # a stick pushed = the D-pad
        d = 0
        if x < -16000: d |= 0x80
        elif x > 16000: d |= 0x20
        if y > 16000: d |= 0x10
        elif y < -16000: d |= 0x40
        return d
    def conv(buttons, lt, rt, lx, ly, rx, ry):
        b = 0
        PAD_EXTRA['rt'] = rt > 100; PAD_EXTRA['rb'] = bool(buttons & 0x200)
        for k, v in MAP.items():
            if buttons & k: b |= v
        dp = b & 0xF0                                                      # (the controller's own D-pad)
        if lt > 100: b |= 0x100
        # (R2 / the right trigger is not the game's R: it is the locker capture button - psp_lockers.py. R1 = R)
        if right_stick_camera:                                             # right stick = the D-pad (this game's camera)
            if rx < -16000: b |= 0x80
            elif rx > 16000: b |= 0x20
            if ry > 16000: b |= 0x10
            elif ry < -16000: b |= 0x40
        # auto aim (LB) + LT held: the left stick strafes (the game strafes with the D-pad while auto aiming)
        if (buttons & 0x100) and lt > 100:
            return (b & ~0xF0) | dp | stick_dpad(lx, ly), 0x8080
        # first-person aim (RB = R): the game walks with the D-pad and aims with the analog stick, so swap them -
        # left stick walks, right stick aims (like a PC shooter). port_settings.txt: aim_swap = off turns this off
        if (b & 0x200) and swap_aim[0]:
            return (b & ~0xF0) | dp | stick_dpad(lx, ly), stick(rx, ry)
        return b, stick(lx, ly)
    return conv


def xinput_reader(right_stick_camera=True, aim_swap=True):
    """Xbox-style controller through Windows' own XInput (no install needed). Returns a function or None."""
    import ctypes as ct
    dll = None
    for n in ("xinput1_4", "xinput1_3", "xinput9_1_0"):
        try: dll = ct.WinDLL(n); break
        except (OSError, AttributeError): pass
    if dll is None: return None
    class GP(ct.Structure):
        _fields_ = [('buttons', ct.c_ushort), ('lt', ct.c_ubyte), ('rt', ct.c_ubyte),
                    ('lx', ct.c_short), ('ly', ct.c_short), ('rx', ct.c_short), ('ry', ct.c_short)]
    class ST(ct.Structure): _fields_ = [('packet', ct.c_uint), ('gp', GP)]
    st = ST(); conv = pad_mapper(right_stick_camera, aim_swap)
    def read():
        for i in range(4):
            if dll.XInputGetState(i, ct.byref(st)) == 0: break
        else: return None
        g = st.gp
        return conv(g.buttons, g.lt, g.rt, g.lx, g.ly, g.rx, g.ry)
    return read


def sdl_reader(right_stick_camera=True, aim_swap=True, xinput_has=lambda: False):
    """PlayStation (DualShock 4, DualSense, DualShock 3), Nintendo Switch Pro and other controllers that aren't
    Xbox-style, through SDL2.dll (next to play.py). Buttons go by where they are, not by their names: the bottom face
    button is Cross on every controller (on a Switch Pro that is B), so all controllers play the same.
    Xbox controllers are left to XInput. Returns a function or None"""
    import ctypes as ct
    p = os.path.join(FOLDER, "SDL2.dll")
    if not os.path.exists(p): return None
    try: sdl = ct.CDLL(p)
    except OSError as e: print("Controller support: SDL2.dll could not be loaded -", e); return None
    for k, v in ((b"SDL_GAMECONTROLLER_USE_BUTTON_LABELS", b"0"), (b"SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", b"1"),
                 (b"SDL_JOYSTICK_HIDAPI_PS4", b"1"), (b"SDL_JOYSTICK_HIDAPI_PS5", b"1"), (b"SDL_JOYSTICK_HIDAPI_SWITCH", b"1"),
                 (b"SDL_JOYSTICK_HIDAPI_PS4_RUMBLE", b"0"), (b"SDL_JOYSTICK_HIDAPI_PS5_RUMBLE", b"0"),
                 (b"SDL_XINPUT_ENABLED", b"0")):
        sdl.SDL_SetHint(k, v)
    if sdl.SDL_Init(0x2000) != 0:                                          # SDL_INIT_GAMECONTROLLER (+ joystick)
        sdl.SDL_GetError.restype = ct.c_char_p; print("Controller support: SDL could not start -", sdl.SDL_GetError()); return None
    sdl.SDL_GameControllerOpen.restype = ct.c_void_p; sdl.SDL_GameControllerOpen.argtypes = [ct.c_int]
    for f in ('SDL_GameControllerGetAttached', 'SDL_GameControllerClose', 'SDL_GameControllerGetType'): getattr(sdl, f).argtypes = [ct.c_void_p]
    sdl.SDL_GameControllerGetButton.argtypes = [ct.c_void_p, ct.c_int]; sdl.SDL_GameControllerGetButton.restype = ct.c_uint8
    sdl.SDL_GameControllerGetAxis.argtypes = [ct.c_void_p, ct.c_int]; sdl.SDL_GameControllerGetAxis.restype = ct.c_int16
    sdl.SDL_GameControllerName.argtypes = [ct.c_void_p]; sdl.SDL_GameControllerName.restype = ct.c_char_p
    sdl.SDL_FlushEvents.argtypes = [ct.c_uint32, ct.c_uint32]
    conv = pad_mapper(right_stick_camera, aim_swap)
    # SDL button -> XInput button: A B X Y, Back, (Guide), Start, sticks, shoulders, D-pad; the PS touchpad = Back (Select)
    BTN = ((0, 0x1000), (1, 0x2000), (2, 0x4000), (3, 0x8000), (4, 0x20), (6, 0x10), (7, 0x40), (8, 0x80), (9, 0x100),
           (10, 0x200), (11, 0x1), (12, 0x2), (13, 0x4), (14, 0x8), (20, 0x20))
    XBOX = (1, 2)                                                          # SDL_CONTROLLER_TYPE_XBOX360 / XBOXONE: XInput has them
    cur = {'c': None, 'scan': 0.0}
    sdl.SDL_JoystickNameForIndex.restype = ct.c_char_p; sdl.SDL_JoystickNameForIndex.argtypes = [ct.c_int]
    TYPES = {0: 'unknown', 1: 'Xbox 360', 2: 'Xbox One', 3: 'PS3', 4: 'PS4', 5: 'Switch Pro', 6: 'virtual', 7: 'PS5',
             8: 'Amazon Luna', 9: 'Google Stadia', 10: 'NVIDIA Shield', 11: 'Joy-Con (L)', 12: 'Joy-Con (R)', 13: 'Joy-Con pair'}
    def find():
        n = sdl.SDL_NumJoysticks()
        if n != cur.get('n'):                                              # (the log says what Windows / SDL can see)
            cur['n'] = n; seen = []
            for i in range(n):
                nm = (sdl.SDL_JoystickNameForIndex(i) or b"?").decode('utf-8', 'replace')
                seen.append(f"{nm} [{TYPES.get(sdl.SDL_GameControllerTypeForIndex(i), '?')}"
                            f"{'' if sdl.SDL_IsGameController(i) else ', no button layout known'}]")
            print("Controllers SDL can see: " + (", ".join(seen) if seen else "none"), flush=True)
        # Xbox-type ones are XInput's - unless XInput has none (then it is probably a converted controller XInput can't see)
        for xbox_ok in (False, True):
            if xbox_ok and xinput_has(): break
            for i in range(n):
                if not sdl.SDL_IsGameController(i) or (sdl.SDL_GameControllerTypeForIndex(i) in XBOX and not xbox_ok): continue
                c = sdl.SDL_GameControllerOpen(i)
                if c:
                    name = (sdl.SDL_GameControllerName(c) or b"controller").decode('utf-8', 'replace')
                    print(f"Controller connected: {name} (through SDL)", flush=True); return c
                sdl.SDL_GetError.restype = ct.c_char_p; print("Controller could not be opened:", sdl.SDL_GetError(), flush=True)
        return None
    def read():
        sdl.SDL_GameControllerUpdate(); sdl.SDL_FlushEvents(0, 0xFFFFFFFF)  # (nothing reads SDL's event queue: keep it empty)
        c = cur['c']
        if c is not None and not sdl.SDL_GameControllerGetAttached(c):
            sdl.SDL_GameControllerClose(c); cur['c'] = c = None; print("Controller disconnected", flush=True)
        if c is None:
            now = time.perf_counter()
            if now - cur['scan'] < 1.0: return None                       # (look for a new one once a second)
            cur['scan'] = now; cur['c'] = c = find()
            if c is None: return None
        buttons = 0
        for sb, xb in BTN:
            if sdl.SDL_GameControllerGetButton(c, sb): buttons |= xb
        ax = [sdl.SDL_GameControllerGetAxis(c, i) for i in range(6)]
        lt, rt = max(0, ax[4]) >> 7, max(0, ax[5]) >> 7                   # (0..32767 -> 0..255)
        return conv(buttons, lt, rt, ax[0], -max(-32767, ax[1]), ax[2], -max(-32767, ax[3]))   # (SDL: down = +)
    return read


def pad_reader(right_stick_camera=True, aim_swap=True):
    """Xbox controllers through XInput, PlayStation / Switch Pro / others through SDL: whichever is plugged in"""
    xi = xinput_reader(right_stick_camera, aim_swap); sd = None
    last = [None]
    try: sd = sdl_reader(right_stick_camera, aim_swap, lambda: last[0] is not None)
    except Exception as e: print("Controller support: SDL -", e)
    if xi is None and sd is None: return None
    kinds = "Xbox-style through XInput" + (", PlayStation / Switch Pro / others through SDL" if sd else " (SDL2.dll not found: no PlayStation / Switch Pro)")
    print(f"Controller support: on ({kinds})" + ("   right stick: camera" if right_stick_camera else ""))
    def read():
        r = xi() if xi else None
        if (r is None) != (last[0] is None): print("Xbox-style controller (XInput): " + ("connected" if r is not None else "gone"), flush=True)
        last[0] = r
        if r is None and sd: r = sd()
        return r
    return read


SETTINGS_TEXT = """# MGS Portable Ops PC port - settings. Change a value, save this file, start play.bat again.
renderer = opengl        # opengl = draw on the graphics card (sharp, any size)   vulkan = the same with Vulkan
                         #   (falls back to opengl if Vulkan can't start)   software = the old way
resolution = 3           # opengl only: 1 = PSP size 480x272, 2 = 960x544, 3 = 1440x816, 4 = 1920x1088, 6 = 2880x1632
texture_filter = off     # on = smooth textures (and less shimmer far away), off = sharp pixels like on the PSP
text_hd = on             # on = the text the game writes while playing (subtitles, codec lines) is drawn sharp at HD size
window = 1280x720        # starting window size (F11 = full screen)
fullscreen = off         # on = start in full screen right away (no switch - and its hitch - while playing)
locker_capture = on      # hold Shift (keyboard) or R2 (controller) while dragging an enemy into an open locker: he is captured, as at the truck
display = window         # window = the graphics card shows the picture itself, copy = it is copied into the window (F3)
software_drawing = auto  # auto = only where the game needs it, always = also draw everything the old way (slower)
anti_aliasing = 4        # opengl only: smooth edges. off, 2, 4 or 8 (higher = smoother, more work for the graphics card)
mouse_camera = on        # on = the mouse turns the camera (like the D-pad), off = mouse not used for the camera
mouse_sensitivity = 1.0  # higher = the camera turns for less mouse movement
aim_swap = on            # on = while the middle mouse button (R) is held - first-person aiming - W A S D walk (the game's
                         #   D-pad then) and the mouse aims (the game's analog stick then). off = no swap
aim_sensitivity = 1.0    # first-person aiming with the mouse: higher = aims faster
right_stick = camera     # controller right stick: camera = turns the camera like the D-pad does in this game
                         #   (left / right = turn around Snake, up = close behind him, down = view from above), off = unused
hd_textures = on         # on = use an HD texture pack (PPSSPP kind) from the folder "HD textures" next to play.bat
                         #   (e.g. HD textures/ULUS10202/textures.ini + the PNG files), off = the game's own textures
hd_near_match = on       # on = a picture the packs have under another name (the game left other leftover bytes after it)
                         #   is found by comparing pictures and remembered in "HD textures/learned_matches.ini"
hd_movies = on           # on = upscaled movies (.mp4 named like the game's movie, e.g. MPO_OP.mp4) from the folder
                         #   "HD movies" (or SD_Dump\\Movies(HD)) next to play.bat are shown instead of the originals
hd_texture_memory = 1536 # graphics card memory (MB) for HD textures. Past it, the ones not seen for longest are let go
                         #   (and loaded again when they come back). 8 GB card: 1536 is safe, 12 GB+: 3072
frame_rate = 60          # opengl only: 60 = smooth 60 fps (the graphics card draws in-between pictures; the game itself
                         #   keeps its own pace), original = 20 fps in play / 30 in cut scenes, like the PSP. F4 switches.
lighting = remaster      # remaster = modern lighting: real-time shadows from the game's main light (the sun), surfaces
                         #   facing away from it darker, a richer colour grade. Needs frame_rate = 60. off = as on the PSP.
                         #   F6 switches while playing.
shadow_strength = 35     # with lighting = remaster: how dark shadows are, 0 to 100
sunlight = 15            # with lighting = remaster: how much brighter sunlit surfaces get, 0 to 100
shadows = characters     # with lighting = remaster: characters = Snake, guards etc. cast real-time shadows (the world's
                         #   own shadows are already painted into it), everything = walls and buildings too (long
                         #   outdoor shadows; indoors they can let light through ceilings that are off screen)
ambient_occlusion = on   # with lighting = remaster: soft darkening in corners, where walls meet the floor, under and
                         #   around objects (on / off)
sun_angle = 25           # with lighting = remaster: the sun's angle from straight overhead, 0 to 80 (higher = longer
                         #   shadows, more dramatic; lower = shadows close under characters)
stutter_log = on         # on = choppy moments are noticed and written to stutter_logs\\ by themselves (F7 = the last 12 s now)
present_timing = held    # vulkan, frame_rate = 60: held = each picture is drawn a little early and handed to the screen at
                         #   the exact refresh it is for (smoother when the game is busy drawing), direct = the old way
fps_cap = off            # with frame_rate = 60: the most pictures per second. off = one for every refresh of your screen,
                         #   or a number like 60. The picture changes every 1, 2, 3... screen refreshes (always the same
                         #   number, which is what looks smooth), so the cap is rounded to fit your screen: on a 165 Hz
                         #   screen 60 gives 55 fps (every 3rd refresh), 80 gives 82 fps (every 2nd). On 120 Hz: 60 = 60.
"""

def load_settings():
    p = os.path.join(FOLDER, "port_settings.txt")
    if not os.path.exists(p):
        try: open(p, "w").write(SETTINGS_TEXT)
        except OSError: pass
    else:                                                              # settings added by newer versions: add them to the file
        try:
            have = open(p, encoding="utf-8", errors="replace").read()
            keys = {l.split('#', 1)[0].split('=', 1)[0].strip().lower() for l in have.splitlines() if '=' in l.split('#', 1)[0]}
            add = []; lines = SETTINGS_TEXT.splitlines()
            for i, l in enumerate(lines):
                k = l.split('#', 1)[0].split('=', 1)[0].strip().lower() if '=' in l.split('#', 1)[0] else None
                if k and k not in keys:
                    add.append(l); j = i + 1
                    while j < len(lines) and lines[j].lstrip().startswith('#') and lines[j].startswith(' '): add.append(lines[j]); j += 1
            if add:
                with open(p, "a", encoding="utf-8") as f: f.write(("" if have.endswith("\n") else "\n") + "\n".join(add) + "\n")
        except OSError: pass
    st = {'renderer': 'opengl', 'resolution': '3', 'texture_filter': 'off', 'window': '1280x720', 'frame_rate': '60', 'anti_aliasing': '4'}
    try:
        for line in open(p, encoding="utf-8", errors="replace"):
            line = line.split('#', 1)[0].strip()
            if '=' in line: k, v = line.split('=', 1); st[k.strip().lower()] = v.strip().lower()
    except OSError: pass
    return st


def start_machine():
    eboot = os.path.join(FOLDER, "EBOOT.BIN.dec")
    iso = os.environ.get("MGSPO_ISO") or None                         # (the disc image chosen in the launcher)
    if not iso or not os.path.exists(iso): iso = psp_run.find_iso(FOLDER)
    fresh = os.environ.get("MGSPO_FRESH") == "1"                       # (launcher: NEW MISSION = from power-on)
    for name in ([] if fresh else SNAPS):
        p = os.path.join(FOLDER, name)
        if os.path.exists(p):
            mach = psp_run.Machine.load_state(p, FOLDER); mach.outdir = FOLDER
            mi = getattr(mach, 'iso', None)
            if iso and mi is not None and os.path.normcase(os.path.abspath(getattr(mi, 'path', ''))) != os.path.normcase(os.path.abspath(iso)):
                mi.path = iso; mi._f = None                                # (the disc image is somewhere else now)
            if iso and getattr(mach, 'iso', None) is None:
                print("Skipping", name, "- it was made before the game was read from the .iso"); continue
            if getattr(mach, 'port_version', 0) < getattr(psp_run, 'PORT_VERSION', 0):
                print("Skipping", name, "- it was made by an older version of the port (delete it if you like)"); continue
            print("Starting from snapshot", name)
            return mach, name
    print("No snapshot found: starting from power-on (press Enter to skip the intro movies).")
    mach, cpu = psp_run.power_on(eboot, FOLDER, iso)
    return mach, "power-on" + (" (from the .iso)" if iso else "")


def clean_old_temp_modules():
    """Older versions of the port copied every stage module to a temporary file (tmpXXXXXXXX.prx in Windows' temp
    folder) and never removed them - and because of a size mix-up each one was about 2 GB. Remove those leftovers:
    only files named tmp*.prx that hold a PSP module, nothing else."""
    import tempfile, glob, threading
    def work():
        n = 0; total = 0
        for p in glob.glob(os.path.join(tempfile.gettempdir(), 'tmp*.prx')):
            try:
                with open(p, 'rb') as fh: head = fh.read(4)
                if head not in (b'\x7fELF', b'~PSP', b'\x00PSP'): continue
                sz = os.path.getsize(p); os.remove(p); n += 1; total += sz
            except OSError: pass
        if n: print(f"Cleaned up {n} leftover temporary files from older versions of the port ({total / 2**30:.1f} GB freed)", flush=True)
    threading.Thread(target=work, daemon=True).start()


def main():
    import threading
    import tkinter as tk
    clean_old_temp_modules()
    mach, source = start_machine(); cpu = mach.cpu
    mach.live = True; mach.live_buttons = 0; mach.live_analog = 0x8080
    mach.render_mode = True; mach.ge.render = True; mach.menu_state_path = None; mach.pad_script = []
    try: import PIL.ImageDraw; mach.dialogs_shown = True                 # the game's system dialogs are shown and answered by you
    except ImportError: mach.dialogs_shown = False; print("NOTE: Pillow is missing: the game's save / message dialogs are answered automatically")
    try:                                                                  # live sound (Windows); F2 turns it on/off
        import psp_sound
        if getattr(mach, 'aout', None) is None: mach.aout = psp_sound.AudioOut(mach, None)
        mach.aout.path = None; mach.aout.player = psp_sound.WaveOut()
        print("Sound: on (F2 = sound on/off)")
    except Exception as e:
        print("Sound: off -", e)
    lib = psp_cpu.core()
    if lib is not None and not os.environ.get('PSP_SYNCGE'):              # drawing on its own thread (PSP_SYNCGE=1 turns this off)
        try: mach.ge.start_async(); print("Drawing: on its own thread")
        except Exception as e: print("Drawing thread: off -", e)
    if lib is None: print("NOTE: the fast core (psp_core.dll) was not found - run build_core.bat once. The game will be very slow.")
    try:
        from PIL import Image, ImageTk
    except ImportError: Image = None

    root = tk.Tk(); root.title("MGS Portable Ops - PC port"); root.configure(bg='black')
    scale = [2]
    label = tk.Label(root, bg='black', bd=0); label.pack()
    status = tk.Label(root, text="", fg='#aaa', bg='black', anchor='w', font=('Consolas', 9)); status.pack(fill='x', side='bottom')
    settings = load_settings(); gl = {'on': False}
    # F7: 10 seconds of exact timings -> perf_log.txt (what each part did, when; why pictures came late)
    perf = {'on': False, 'P': [], 'C': [], 'G': [], 'J': [], 'T': {}}
    pace = {'on': False, 'E': (None, None)}; clk = {'wall0': time.perf_counter(), 'game0': mach.now()}
    want_vk = settings.get('renderer') == 'vulkan' and lib is not None and hasattr(lib, 'vk_setup')
    if settings.get('renderer') in ('opengl', 'vulkan') and lib is not None and hasattr(lib, 'gl_setup') and mach.ge.__dict__.get('_worker') is not None:
        frame = None
        try:                                                              # drawing on the graphics card
            import psp_glwin, ctypes as ct
            try: W, H = (int(x) for x in settings.get('window', '1280x720').split('x'))
            except ValueError: W, H = 1280, 720
            res = max(1, min(8, int(settings.get('resolution', '3') or 3)))
            frame = tk.Frame(root, width=W, height=H, bg='', bd=0, highlightthickness=0)   # no background: Tk must not paint over OpenGL
            label.pack_forget(); frame.pack(fill='both', expand=True); root.update()
            hwnd = frame.winfo_id(); gsize = [W, H]
            lib.gl_setup.argtypes = [psp_glwin.GETPROC_T, ct.c_int, ct.c_int]; lib.gl_error.restype = ct.c_char_p
            lib.gl_present.argtypes = [ct.c_void_p, ct.c_uint32, ct.c_int, ct.c_int, ct.c_int, ct.c_int]
            gst = mach.ge._gpu(); gaddr = ct.addressof(gst)
            def gl_init():
                ctx = None
                if want_vk:
                    try: ctx = psp_glwin.VKWindow(hwnd, lib, res, 1 if settings.get('software_drawing') == 'always' else 0)
                    except Exception as e: print("Vulkan:", e, "- using OpenGL instead", flush=True); ctx = None
                    if ctx is not None and hasattr(lib, 'vk_pipes_load'):     # the drawing setups used before: made now,
                        try:                                                   # not the first time something new is seen
                            data = open(PIPES_FILE, 'rb').read()
                            lib.vk_pipes_load.argtypes = [ct.c_char_p, ct.c_int]; n = lib.vk_pipes_load(data, len(data))
                            if n >= 0: gl['pipes_loaded'] = n
                        except OSError: pass
                if ctx is None:
                    ctx = psp_glwin.GLWindow(hwnd)
                    r = lib.gl_setup(ctx.getproc, res, 1 if settings.get('software_drawing') == 'always' else 0)
                    if r: raise RuntimeError(f"OpenGL setup failed ({r}): {(lib.gl_error() or b'').decode('latin1', 'replace')}")
                filt = settings.get('texture_filter') == 'on'
                if hasattr(lib, 'gl_quality'):
                    aa = settings.get('anti_aliasing', '4'); aa = int(aa) if aa.isdigit() else 1
                    gl['quality'] = lib.gl_quality(aa, 1 if filt else 0)
                lib.gl_options(1 if filt else 0)
                if hasattr(lib, 'gl_text_hd'): lib.gl_text_hd(0 if settings.get('text_hd', 'on') == 'off' else 1)
                if hasattr(lib, 'gl_interp'): lib.gl_interp(1 if gl['fps60'] else 0)
                return ctx
            gl['fps60'] = settings.get('frame_rate', '60') == '60' and hasattr(lib, 'gl_present_t')
            ctx = mach.ge.call(gl_init, timeout=120)
            gl['ctx'] = ctx
            if 'pipes_loaded' in gl: print(f"Graphics setups made ahead from vk_pipelines.bin: {gl['pipes_loaded']}", flush=True)
            def save_pipes(wait=False):
                """(Vulkan) keep the drawing setups made so far for the next start (vk_pipelines.bin)"""
                if not hasattr(lib, 'vk_pipes_save') or 'Vulkan' not in getattr(ctx, 'info', ''): return
                def job():
                    lib.vk_pipes_save.argtypes = [ct.c_void_p, ct.c_int]; n = lib.vk_pipes_save(None, 0)
                    if n <= 16: return
                    b = ct.create_string_buffer(n); n = lib.vk_pipes_save(b, n); data = b.raw[:n]
                    def write():
                        try:
                            with open(PIPES_FILE + '.tmp', 'wb') as f: f.write(data)
                            os.replace(PIPES_FILE + '.tmp', PIPES_FILE)
                        except OSError: pass
                    if wait: write()
                    else: threading.Thread(target=write, daemon=True).start()
                if wait: mach.ge.call(job)
                else: mach.ge.post(job)
            gl['save_pipes'] = save_pipes
            lib.gl_read_window.argtypes = [ct.c_int, ct.c_int, ct.c_void_p]
            if hasattr(lib, 'gl_overlay'): lib.gl_overlay.argtypes = [ct.c_char_p, ct.c_int, ct.c_int]
            gl.update(mode=settings.get('display', 'window'), errors=0, pic=None)
            gl['shown'] = 0
            if hasattr(lib, 'gl_present_t'):
                lib.gl_present_t.argtypes = [ct.c_void_p, ct.c_uint32, ct.c_int, ct.c_int, ct.c_int, ct.c_int, ct.c_float]
            def present(addr, stride, fmt, t=-2.0, seq=None, tfun=None):
                def job():
                    nonlocal addr, stride, fmt, t
                    if tfun is not None:                           # (worked out now, on the drawing thread: with the game
                        got = tfun()                               #  frames that are really there at this moment)
                        if got is None: return                     # (nothing to show yet)
                        addr, stride, fmt, t = got
                    w, h = gsize[0], gsize[1]; tj = time.perf_counter()
                    tm0 = None
                    if perf['on'] and perf.get('tm'): tm0 = (ct.c_uint64 * 8)(); lib.vk_timers(tm0)
                    r = lib.gl_present(gaddr, addr, stride, fmt, w, h) if t < -1.5 else lib.gl_present_t(gaddr, addr, stride, fmt, w, h, t)
                    dj = time.perf_counter() - tj; gl['pic_dt'] = dj if dj > gl.get('pic_dt', 0.0) else 0.95 * gl.get('pic_dt', 0.0) + 0.05 * dj
                    if dj > gl.get('period', 1 / 60):                      # (the log: pictures that took longer than a refresh)
                        gl['slow'] = gl.get('slow', 0) + 1; gl['slowest'] = max(gl.get('slowest', 0.0), dj)
                    gl['shown'] += 1
                    if r < 0 and gl['errors'] < 3:
                        gl['errors'] += 1; print("OpenGL:", (lib.gl_error() or b'').decode('latin1', 'replace'), flush=True)
                    if gl.get('grab') is True:                     # (F8: this picture, for the problem report)
                        gb = ct.create_string_buffer(w * h * 3)
                        gl['grab'] = (gb.raw, w, h) if lib.gl_read_window(w, h, gb) else None
                    if gl['mode'] == 'copy':                       # read the picture back and let the window show it
                        buf = ct.create_string_buffer(w * h * 3)
                        if lib.gl_read_window(w, h, buf): gl['pic'] = (buf.raw, w, h)
                    else:
                        t0 = time.perf_counter()
                        if seq is not None: lib.vk_swap_hold(seq)          # (kept: the pacer shows it at its refresh)
                        else: ctx.swap()
                        gl['swap_t'] = gl.get('swap_t', 0.0) + time.perf_counter() - t0
                        if perf['on']:
                            tm = None
                            if tm0 is not None: tm1 = (ct.c_uint64 * 8)(); lib.vk_timers(tm1); tm = tuple(tm1[i] - tm0[i] for i in range(8))
                            perf['J'].append((seq, tj, t0, time.perf_counter(), tm))
                job.kind = 'picture'
                if seq is not None and hasattr(mach.ge, 'post_urgent'): mach.ge.post_urgent(job)   # (ahead of the game's drawing)
                else: mach.ge.post(job)
            mach.gl_present = present; gl.update(on=True, frame=frame, size=gsize)
            # 60 fps: every new game picture is "committed" with the game-clock time it was made at. Pictures are shown
            # by the pacer thread at an even rate in real time, each one placed exactly where that moment falls
            # between the last two game pictures (so uneven timing of the game thread doesn't show as stutter)
            def paced_commit():
                E = mach.now(); tpost = time.perf_counter(); fbc = getattr(mach, 'cur_fb', None)
                def job():
                    tj0 = time.perf_counter()
                    lib.gl_commit(); pace['E'] = (pace['E'][1], pace['E'][2], E)
                    dj = time.perf_counter() - tj0; pace['nf'] = dj if dj > pace.get('nf', 0.0) else 0.9 * pace.get('nf', 0.0) + 0.1 * dj   # (how long it takes: up fast, down slowly)
                    pace['fb'] = fbc                                 # (the framebuffer of the newest frame taken in - the game
                                                                     #  switches to it before the frame is taken in here)    # (the last three: in-between pictures
                                                                                    #  can still use the pair before)
                    # how late (game-clock microseconds) this picture became ready compared with when it was made: the
                    # pictures are shown that much later (averaged), so the newest one is just ready when it's needed
                    late = clk['game0'] + (time.perf_counter() - clk['wall0']) * 1_000_000 - E
                    if -100_000 < late < 200_000:                           # (rises fast, falls slowly: covers the late ones)
                        h = pace.setdefault('lates', []); h.append(late); del h[:-40]
                        pace['late'] = sorted(h)[len(h) * 9 // 10]              # (9 in 10 of the recent ones are ready in time)
                    if perf['on']: perf['C'].append((E, tpost, time.perf_counter(), late))
                job.kind = 'new game frame'
                mach.ge.post(job)
            def job_gate(fn):
                """(drawing thread, before a job) taking in a new game frame takes 3-12 ms and can't be interrupted: started
                just before a picture is due, that picture waited behind it and came late (the stutters in busy places).
                If it wouldn't fit before the next picture, the drawing thread first waits for that picture (it is made the
                moment it is asked for), then takes the frame in. Returns until when to wait at most (0 = start now)"""
                if getattr(fn, 'kind', '') != 'new game frame' or not gl.get('hold') or not pace.get('on'): return 0
                nxt = pace.get('next_post'); lat, margin = perf.get('lat') or 0.015, perf.get('margin') or 0.004
                if not nxt: return 0
                now = time.perf_counter(); need = pace.get('nf', 0.006) * 1.2 + 0.001 + gl.get('pic_dt', 0.004)
                if nxt - now + (lat - margin) >= need: return 0           # (fits: the next picture won't come late)
                # (not when it would hold back nearly every frame: the drawing thread is then busy all the time - a big
                #  fight, the hospital in an alert - and its waits only took time away from the game's drawing, so the
                #  game itself slowed down to 14 fps. Then the frame is taken in at once, and now and then a picture comes late)
                gts = pace.setdefault('gate_ts', []); gts[:] = [x for x in gts if now - x < 1.0]
                if len(gts) >= 6: return 0
                gts.append(now)
                pace['gated'] = pace.get('gated', 0) + 1
                return min(nxt + 0.010, now + 0.030)
            def set_fps60(on):
                # both ways the pictures are timed by the pacer (the game's frames come out of the emulator unevenly
                # in real time - up to ~35 ms late, more while things move): 60 fps blends between frames, original
                # shows each game frame whole, at its own moment (evenly, like on the PSP)
                gl['fps60'] = on; gl['snap'] = not on
                mach.ge.call(lambda: lib.gl_interp(1)); pace['E'] = (None, None, None); pace.pop('m', None); pace.pop('fb', None)
                mach.gl_commit = paced_commit; mach.gl_present_t = lambda *a: None; pace['on'] = True
            def paced_present(when=None, seq=None):
                """show the picture for real time 'when' (perf_counter seconds: the moment the screen will show it).
                Which game frames and where between them is decided when the picture is drawn (a new frame taken in
                between asking and drawing would otherwise move it a whole frame)"""
                when = time.perf_counter() if when is None else when
                if not (pace.get('fb') or getattr(mach, 'cur_fb', None)): return
                present(0, 0, 0, 1.0, seq if gl.get('hold') and gl['mode'] != 'copy' else None, tfun=lambda: pick(when, seq))
            def pick(when, seq):
                fb = pace.get('fb') or getattr(mach, 'cur_fb', None)
                if not fb or not fb[0]: return None
                Em, E0, E1 = pace['E']; traw = None
                if E0 is None or E1 is None or E1 <= E0: t = 1.0
                else:
                    now_e = clk['game0'] + (when - clk['wall0']) * 1_000_000               # the game clock at that moment
                    iv = E1 - E0
                    # the margin for late frames changes only slowly: a sudden change would shift the picture in game time
                    # (a visible hitch at 60 fps, a frame shown one picture too long or short at the original rate)
                    want = pace.get('late'); want = PACE_MARGIN_US if want is None else max(-iv * 0.5, min(40_000, want + 2_000))
                    m = pace.get('m')
                    if m is None: m = want
                    elif want > m: m = min(want, m + 300)                  # (later frames: catch up within a few pictures)
                    else: m = max(want, m - 30)                            # (earlier: drift down, ~2 ms a second)
                    pace['m'] = m
                    g = now_e - iv - m                                   # shown one picture late (+ how late they get ready)
                    t = traw = (g - E0) / iv
                    if t < 0 and Em is not None and E0 > Em:             # (the newest came early: the pair before is right)
                        t = (g - E0) / (E0 - Em)                         # (-1..0: between the last two but one)
                    pace['n'] = pace.get('n', 0) + 1                                       # (for the log: how often the
                    if t < -1: pace['lo'] = pace.get('lo', 0) + 1                          #  newest pictures weren't ready
                    elif t > 1: pace['hi'] = pace.get('hi', 0) + 1                         #  or ran out)
                    t = -1.0 if t < -1 else 1.0 if t > 1 else t
                    if gl.get('snap'):                                        # (original frame rate: the whole game frame whose
                        t = 1.0 if traw >= 1.0 else -0.002 if traw >= 0.0 else -0.998   # moment has come: newest, one
                                                                              #  before, or the one before that)
                if perf['on']: perf['T'][seq] = (traw, t)
                return fb[0], fb[1] or 512, fb[2] if fb[2] < 4 else 3, t
            gl['paced_present'] = paced_present
            mach.ge.job_gate = job_gate
            gl['hold'] = (settings.get('present_timing', 'held') != 'direct' and 'Vulkan' in getattr(ctx, 'info', '')
                          and hasattr(lib, 'vk_swap_hold') and hasattr(lib, 'vk_present_held'))
            if gl['hold']: lib.vk_swap_hold.argtypes = [ct.c_uint32]; lib.vk_present_held.argtypes = [ct.c_uint32]
            gl['set_fps60'] = set_fps60
            if hasattr(lib, 'gl_present_t'): set_fps60(gl['fps60'])
            if hasattr(lib, 'gl_lighting'):                     # remaster lighting (F6 switches)
                def pct(k, d):
                    try: return max(0, min(100, int(settings.get(k, d)))) / 100.0
                    except ValueError: return d / 100.0
                lib.gl_lighting.argtypes = [ct.c_int, ct.c_float, ct.c_float, ct.c_float]
                try: ang = max(0.0, min(80.0, float(settings.get('sun_angle', '25'))))
                except ValueError: ang = 25.0
                gl['light_k'] = (pct('shadow_strength', 35), pct('sunlight', 15), ang, 2 if settings.get('shadows', 'characters') == 'everything' else 1)
                def set_lighting(on):
                    gl['lighting'] = on; k = gl['light_k']
                    mach.ge.call(lambda: lib.gl_lighting(k[3] if on else 0, k[0], k[1], k[2]))
                gl['set_lighting'] = set_lighting
                if hasattr(lib, 'gl_ao'):
                    ao_on = settings.get('ambient_occlusion', 'on') != 'off'
                    mach.ge.call(lambda: lib.gl_ao(1 if ao_on else 0))
                set_lighting(settings.get('lighting', 'remaster') == 'remaster')
                if hasattr(lib, 'gl_prim_cull'):                 # (what is wholly out of view is not worked out: skip_hidden = off to compare)
                    pc_on = settings.get('skip_hidden', 'on') != 'off'
                    mach.ge.call(lambda: lib.gl_prim_cull(1 if pc_on else 0))
            if gl['mode'] == 'copy': label.place(in_=frame, x=0, y=0, relwidth=1, relheight=1)
            print(f"Graphics card drawing: on - {ctx.info} - {480 * res}x{272 * res}, shown {'by copying' if gl['mode'] == 'copy' else 'directly'} (F3 switches)")
            print(f"Frame rate: {'60 fps (in-between pictures)' if gl['fps60'] else 'original (20/30 fps, evenly timed)'} (F4 switches)")
            if 'set_lighting' in gl:
                print(f"Lighting: {'remaster (shadows, sun shading, colour grade' + '' + ')' if gl['lighting'] else 'off (as on the PSP)'} (F6 switches)")
            q = gl.get('quality')
            if q is not None:
                print(f"Anti-aliasing: {str(q & 255) + 'x' if q & 255 > 1 else 'off'}   texture filtering: "
                      f"{'on' + (f', {q >> 16}x anisotropic' if q >> 16 else '') if settings.get('texture_filter') == 'on' else 'off'}")
        except Exception as e:
            print("Graphics card drawing: off -", e, "(using the old drawing)")
            try: lib.gl_off()
            except Exception: pass
            if frame is not None: frame.destroy()
            label.pack()
    held = set(); state = {'img': None, 'frames': 0, 'last_fb': -1, 'stopped': None, 't_fps': time.perf_counter(), 'fps': 0.0,
                           'wall0': time.perf_counter(), 'game0': mach.now()}

    pad = pad_reader(settings.get('right_stick', 'camera') != 'off', settings.get('aim_swap', 'on') != 'off')
    pad_state = {'b': 0, 'ana': None}
    def update_input():
        b = 0
        for k in held: b |= KEYS.get(k, 0)
        x = y = 0
        for k in held:
            if k in STICK: x += STICK[k][0]; y += STICK[k][1]
        b |= pad_state['b']
        # controller on menu screens (the mission and staff hub, the codec): the left stick moves like the D-pad there
        pad_menu = 0
        if menu_state['menu'] and pad_state['ana'] is not None:
            ax, ay = (pad_state['ana'] & 0xFF) - 0x80, (pad_state['ana'] >> 8) - 0x80
            if ax < -0x40: pad_menu |= 0x80
            elif ax > 0x40: pad_menu |= 0x20
            if ay < -0x40: pad_menu |= 0x10
            elif ay > 0x40: pad_menu |= 0x40
        # aiming in first person (R = middle mouse button held): the game walks with the D-pad and aims with the analog
        # stick then - so W A S D become the D-pad and the mouse becomes the stick
        aim = 'MOUSE2' in held and aim_state['on']
        # strafing: auto aim (L = right mouse button) + left Ctrl held - the game strafes with the D-pad then, so W A S D
        # become the D-pad
        strafe = is_strafe()
        # first-person aiming + Shift: W A S D aim instead (the game's analog stick, as on the PSP); the mouse still aims
        # when W A S D are let go. Shift is not Triangle then either
        key_aim = aim and ('Shift_L' in held or 'Shift_R' in held)
        if key_aim: b &= ~0x1000
        # the same swap while Circle (R key) is held - the weapon / item select - and on menu screens (the mission and
        # staff hub, the codec): the game moves through those with the D-pad
        swap = (aim and not key_aim) or strafe or ('r' in held and aim_state['on']) or menu_state['menu']
        if swap:
            b &= ~0xF0
            if x < 0: b |= 0x80
            elif x > 0: b |= 0x20
            if y < 0: b |= 0x10
            elif y > 0: b |= 0x40
            x = y = 0
        if menu_state['menu']: b |= pad_menu | (pad_state['b'] & 0xF0)   # (the controller's stick and D-pad still work there)
        util = mach.__dict__.get('util')
        if util is not None and util.active() is not None:               # a system dialog is open: the buttons are for it
            new = b & ~dlg_state['prev']; dlg_state['prev'] = b
            if new: util.ui_key(new)
            mach.live_buttons = 0; mach.live_analog = 0x8080; return
        dlg_state['prev'] = b
        # locker capture: Shift / R2 held = capture the enemy put into a locker (psp_lockers.py). While Snake drags a body
        # R2 is not the game's R then (R would switch to first-person view and stop the walk into the locker)
        rt = PAD_EXTRA['rt'] and pad_state['ana'] is not None
        mach.capture_held = lk_state['on'] and ('Shift_L' in held or 'Shift_R' in held or rt)
        if lk_state['on'] and (rt or 'Shift_L' in held or 'Shift_R' in held) and psp_lockers.dragging(mach):
            if rt and not PAD_EXTRA['rb'] and 'MOUSE2' not in held: b &= ~0x200
            if ('Shift_L' in held or 'Shift_R' in held) and not (pad_state['b'] & 0x1000) and 'i' not in held: b &= ~0x1000   # (Shift is Triangle otherwise)
        mach.live_buttons = b
        pad_still = pad_state['ana'] is None or (abs((pad_state['ana'] & 0xFF) - 0x80) < 24 and abs((pad_state['ana'] >> 8) - 0x80) < 24)
        if key_aim and (x or y):
            mach.live_analog = (max(0, min(255, 0x80 + x * 0x7F))) | (max(0, min(255, 0x80 + y * 0x7F)) << 8)
        elif aim and mouse['cap'] and pad_still:
            mach.live_analog = aim_state['ana']                            # (a controller plugged in but not used: still the mouse)
        elif (swap and pad_still) or pad_menu:
            mach.live_analog = 0x8080
        elif (x or y) or pad_state['ana'] is None:
            mach.live_analog = (max(0, min(255, 0x80 + x * 0x7F))) | (max(0, min(255, 0x80 + y * 0x7F)) << 8)
        else: mach.live_analog = pad_state['ana']
    dlg_state = {'prev': 0, 'ver': None, 'img': None}
    lk_state = {'on': settings.get('locker_capture', 'on') != 'off', 't': 0.0}
    aim_state = {'on': settings.get('aim_swap', 'on') != 'off', 'ana': 0x8080}
    menu_state = {'menu': False, 'n': 0}
    def is_strafe(): return 'MOUSE3' in held and 'Control_L' in held
    def poll_pad():
        if pad is None: return
        st = pad()
        if st is None: pad_state['b'] = 0; pad_state['ana'] = None; update_input(); return
        pad_state['b'], pad_state['ana'] = st; update_input()

    def keydown(ev):
        k = ev.keysym if ev.keysym in KEYS or len(ev.keysym) > 1 else ev.keysym.lower()
        if k == 'Escape': state['quit'] = True; root.destroy(); return
        util = mach.__dict__.get('util'); dl = util.active() if util is not None else None
        if dl is not None and dl.ui['kind'] == 'osk':                  # typing a name into the game's keyboard
            if ev.keysym == 'BackSpace': util.ui_key(0, '\b')
            elif ev.keysym in ('Return', 'KP_Enter'): util.ui_key(0, '\r')
            elif ev.char and ev.char.isprintable(): util.ui_key(0, ev.char)
            return
        if k == 'F5': state['req'].append('save'); return             # done by the game thread between steps
        if k == 'F8': gl['grab'] = True; state['req'].append('report'); return   # problem report (snapshot + screenshot + logs)
        if k == 'F9' and gl['on'] and hasattr(lib, 'gl_exact'):        # 60 fps in-between pictures: exact (new) / old way, to compare
            gl['exact'] = not gl.get('exact', True); on = gl['exact']; mach.ge.post(lambda: lib.gl_exact(1 if on else 0))
            state['msg'] = "in-between pictures: " + ("exact - from the game's cameras (F9 = old way)" if on else "old way (F9 = exact)"); print(state['msg'], flush=True); return
        if k == 'F3' and gl['on']:                                     # how the picture gets into the window
            gl['mode'] = 'window' if gl['mode'] == 'copy' else 'copy'
            if gl['mode'] == 'copy': label.place(in_=gl['frame'], x=0, y=0, relwidth=1, relheight=1)
            else: label.place_forget()
            state['msg'] = "picture shown " + ("by copying (F3 = directly)" if gl['mode'] == 'copy' else "directly (F3 = by copying)")
            print(state['msg']); return
        if k == 'F11':
            if gl['on']:
                fs = not root.attributes('-fullscreen')
                # full screen: the status line goes away, so the game's picture covers the whole screen. (With the
                # line, the picture was 20 pixels short of the screen and Windows had to put the two together for
                # every picture - its own timing, now and then a picture a refresh late. Without it the picture
                # can go to the screen directly.)
                try:
                    if fs: status.pack_forget()
                    elif not status.winfo_ismapped(): status.pack(fill='x', side='bottom', before=gl['frame'])
                except Exception: pass
                root.attributes('-fullscreen', fs); state['fs'] = fs
            else: scale[0] = 1 if scale[0] == 2 else 2
            return
        if k == 'F2': state['req'].append('sound'); return
        if k == 'F10': mouse_capture(not mouse['cap']); return 'break'
        if k == 'F7' and gl['on'] and perf.get('roll'):                 # (always recording) the last 12 s + 3 s more -> perf_log.txt
            if not stut.get('f7'):
                stut['f7'] = time.perf_counter(); state['msg'] = "perf_log: saving the last 12 seconds (+3 s more)..."; print(state['msg'], flush=True)
            return
        if k == 'F7' and gl['on'] and not perf['on']:                   # 10 s of exact timings -> perf_log.txt
            perf.update(P=[], C=[], G=[], J=[], T={}, t0=time.perf_counter(), tm=hasattr(lib, 'vk_timers'))
            if perf['tm']:
                lib.vk_tsc.restype = ct.c_uint64; perf['tsc0'] = lib.vk_tsc()
                b = (ct.c_uint64 * 8)(); lib.vk_timers(b); perf['tm0'] = tuple(b)
            mach.ge.jobtrace = []; perf['S'] = []; perf['on'] = True
            if hasattr(lib, 'vk_present_track') and 'Vulkan' in getattr(gl.get('ctx'), 'info', ''):
                def track():                                             # when each picture really reached the screen
                    lib.vk_present_track.argtypes = [ct.c_void_p]; b = (ct.c_uint64 * 3)()
                    while perf['on']:
                        r = lib.vk_present_track(b)
                        if r == 1: perf['S'].append((int(b[0]), int(b[1]), int(b[2])))
                        elif r < 0: perf['S'] = None; return
                        else: time.sleep(0.001)
                threading.Thread(target=track, daemon=True).start()
            state['msg'] = "perf_log: recording 10 seconds - keep playing (the part that stutters)"; print(state['msg'], flush=True); return
        if k == 'F6' and gl['on'] and 'set_lighting' in gl:             # remaster lighting on/off
            gl['set_lighting'](not gl['lighting']); state['msg'] = "lighting: remaster (F6 = off)" if gl['lighting'] else "lighting: off (F6 = remaster)"
            print(state['msg'], flush=True); return
        if k == 'F4' and gl['on'] and 'set_fps60' in gl:               # 60 fps on/off (done by the game thread between steps)
            state['req'].append('fps60'); return
        held.add(k); update_input(); return 'break'                       # ('break': Tab doesn't move the keyboard focus)
    def keyup(ev):
        k = ev.keysym if ev.keysym in KEYS or len(ev.keysym) > 1 else ev.keysym.lower()
        held.discard(k); update_input(); return 'break'
    root.bind('<KeyPress>', keydown); root.bind('<KeyRelease>', keyup)
    if settings.get('fullscreen', 'off') == 'on':                      # start in full screen (as if F11 was pressed once the window is up)
        def _start_fs():
            try:
                if gl['on'] and not root.attributes('-fullscreen'):
                    class _E: keysym = 'F11'; keycode = 0; char = ''
                    keydown(_E())
            except Exception as e: print("fullscreen at start:", e, flush=True)
        root.after(600, _start_fs)

    # ---- mouse: buttons = Square / L, movement = the camera (this game turns its camera with the D-pad, so moving the
    # mouse holds D-pad left / right (up / down) for as many game frames as the movement is worth)
    mouse = {'cap': False, 'dx': 0.0, 'dy': 0.0, 'on': settings.get('mouse_camera', 'on') != 'off'}
    try: msens = max(0.1, min(10.0, float(settings.get('mouse_sensitivity', '1.0'))))
    except ValueError: msens = 1.0
    try: aim_gain = max(0.1, min(10.0, float(settings.get('aim_sensitivity', '1.0'))))
    except ValueError: aim_gain = 1.0
    def mouse_center():
        return root.winfo_rootx() + root.winfo_width() // 2, root.winfo_rooty() + root.winfo_height() // 2
    def mouse_capture(on):
        on = bool(on) and mouse['on']
        if on == mouse['cap']: return
        mouse['cap'] = on; mouse['dx'] = mouse['dy'] = 0.0
        try:
            root.config(cursor='none' if on else '')
            for w in (gl.get('frame'), label):
                if w is not None: w.config(cursor='none' if on else '')
        except Exception: pass
        if on:
            cx, cy = mouse_center(); root.event_generate('<Motion>', warp=True, x=root.winfo_width() // 2, y=root.winfo_height() // 2)
        else:
            for k in ('MCAM_L', 'MCAM_R', 'MCAM_U', 'MCAM_D'): held.discard(k)
            update_input()
        state['msg'] = "mouse: camera (F10 = let go of the mouse)" if on else "mouse let go (click in the window to use it for the camera again)"
    def mouse_motion(ev):
        if not mouse['cap']: return
        cx, cy = mouse_center(); dx, dy = ev.x_root - cx, ev.y_root - cy
        if dx == 0 and dy == 0: return
        mouse['dx'] += dx; mouse['dy'] += dy
        root.event_generate('<Motion>', warp=True, x=root.winfo_width() // 2, y=root.winfo_height() // 2)
        # (react at once - not at the next 50 ms tick: the press starts as soon as the movement is worth one)
        if not ('MOUSE2' in held and aim_state['on']) and not ('r' in held and aim_state['on']) and not menu_state['menu'] and 'MOUSE3' not in held:
            for ax, neg, pos, step in (('dx', 'MCAM_L', 'MCAM_R', 30.0 / msens), ('dy', 'MCAM_U', 'MCAM_D', 60.0 / msens)):
                v = mouse[ax]
                if v >= step and pos not in held: held.discard(neg); held.add(pos); mouse[ax] = v - step; mouse['t' + ax] = time.perf_counter(); update_input()
                elif v <= -step and neg not in held: held.discard(pos); held.add(neg); mouse[ax] = v + step; mouse['t' + ax] = time.perf_counter(); update_input()
    def mouse_tick():                                                     # every 50 ms (a game frame): press or not
        if gl.get('on') and hasattr(lib, 'gl_world_verts') and aim_state['on']:   # menu screen? (little 3D drawing)
            wv = lib.gl_world_verts(); m = wv >= 0 and wv < 500
            # (menu screens - the mission map, the hub, the codec, comic panels - draw no 3D at all: 0. Play draws
            #  30 000 - 115 000, but close to a wall, locked on, it went under the old limit of 20 000 and the stick and
            #  W A S D turned into the camera)
            menu_state['n'] = menu_state['n'] + 1 if m != menu_state['menu'] else 0
            if menu_state['n'] >= 6:                                       # (0.3 s the same: no flicking on a fade)
                menu_state['menu'] = m; menu_state['n'] = 0; update_input()
                print("menu controls:", "on (W A S D = D-pad)" if m else "off", flush=True)
        if mouse['cap'] and 'MOUSE2' in held and aim_state['on']:        # aiming: the mouse is the analog stick
            for k in ('MCAM_L', 'MCAM_R', 'MCAM_U', 'MCAM_D'): held.discard(k)
            g = 4.0 * aim_gain                                            # (stick per pixel moved in this 50 ms; own setting -
                                                                          #  the camera's mouse_sensitivity doesn't change it)
            ax = int(max(0, min(255, 0x80 + mouse['dx'] * g))); ay = int(max(0, min(255, 0x80 + mouse['dy'] * g)))
            aim_state['ana'] = ax | (ay << 8); mouse['dx'] = mouse['dy'] = 0.0
            update_input(); root.after(50, mouse_tick); return
        if aim_state['ana'] != 0x8080: aim_state['ana'] = 0x8080; mouse['dx'] = mouse['dy'] = 0.0
        # (weapon select / menus / auto aim: the mouse leaves the D-pad alone - while auto aiming the game strafes with
        #  the D-pad, so the mouse camera would make Snake side-step; strafing is left Ctrl + W A S D only)
        if mouse['cap'] and (('r' in held and aim_state['on']) or menu_state['menu'] or 'MOUSE3' in held):
            for k in ('MCAM_L', 'MCAM_R', 'MCAM_U', 'MCAM_D'): held.discard(k)
            mouse['dx'] = mouse['dy'] = 0.0; update_input()
        elif mouse['cap']:
            for ax, neg, pos, step in (('dx', 'MCAM_L', 'MCAM_R', 30.0 / msens), ('dy', 'MCAM_U', 'MCAM_D', 60.0 / msens)):
                if time.perf_counter() - mouse.get('t' + ax, 0.0) < 0.05: continue   # (a press just started: a whole game frame long)
                v = mouse[ax]; held.discard(neg); held.discard(pos)
                # (at most one more step owed: the camera stops soon after the mouse does - six steps of backlog kept
                #  it turning for up to 0.3 s after the mouse stopped, which felt delayed)
                if v >= step: held.add(pos); mouse[ax] = min(v - step, step)
                elif v <= -step: held.add(neg); mouse[ax] = max(v + step, -step)
                else: mouse[ax] = v * 0.5                                   # (small leftovers fade: no slow drift)
            update_input()
        root.after(50, mouse_tick)
    def mouse_button(ev, down):
        name = 'MOUSE1' if ev.num == 1 else 'MOUSE2' if ev.num == 2 else 'MOUSE3' if ev.num == 3 else None
        if name is None: return
        if down and not mouse['cap'] and mouse['on'] and name == 'MOUSE1':
            mouse_capture(True); return                                   # (the click that takes the mouse: not Square)
        (held.add if down else held.discard)(name)
        if name == 'MOUSE3':                                              # (auto aim: a camera turn under way stops at once)
            for k in ('MCAM_L', 'MCAM_R', 'MCAM_U', 'MCAM_D'): held.discard(k)
            mouse['dx'] = mouse['dy'] = 0.0
        update_input()
    root.bind_all('<Motion>', mouse_motion)
    root.bind_all('<ButtonPress>', lambda ev: mouse_button(ev, True)); root.bind_all('<ButtonRelease>', lambda ev: mouse_button(ev, False))
    root.bind('<FocusOut>', lambda ev: root.after(50, lambda: mouse_capture(False) if root.focus_displayof() is None else None))   # (alt-tab: let go)
    root.after(50, mouse_tick)
    if mouse['on']: root.after(1500, lambda: mouse_capture(root.focus_displayof() is not None))

    rgb = bytearray(480 * 272 * 3)
    def show():
        if gl['on']:                                      # the graphics card makes the pictures
            p = gl.get('pic')
            if gl['mode'] != 'copy' or p is None or Image is None: return
            gl['pic'] = None; data, w, h = p
            state['img'] = ImageTk.PhotoImage(Image.frombytes('RGB', (w, h), data)); label.config(image=state['img'])
            return
        sh = getattr(mach, 'shown', None)                 # the picture as it was on screen at the last vblank
        if sh is None: return
        data, stride, fmt = sh
        if lib is not None and hasattr(lib, 'fb_to_rgb'):
            import ctypes as ct
            src = ct.create_string_buffer(data, max(len(data), 0x200000)); ob = (ct.c_uint8 * len(rgb)).from_buffer(rgb)
            lib.fb_to_rgb(src, ct.c_uint32(0x04000000), stride, fmt, ob); del ob
        else:
            from psp_ge import unpack_color
            bpp = 4 if fmt == 3 else 2
            for y in range(272):
                row = y * stride * bpp
                if fmt == 3:
                    src = data[row: row + 480 * 4]
                    rgb[y * 1440 + 0: (y + 1) * 1440: 3] = src[0::4]; rgb[y * 1440 + 1: (y + 1) * 1440: 3] = src[1::4]; rgb[y * 1440 + 2: (y + 1) * 1440: 3] = src[2::4]
                else:
                    for x in range(480):
                        c = unpack_color(int.from_bytes(data[row + x * 2: row + x * 2 + 2], 'little'), fmt)
                        rgb[(y * 480 + x) * 3: (y * 480 + x) * 3 + 3] = bytes(c[:3])
        if Image is not None:
            im = Image.frombytes('RGB', (480, 272), bytes(rgb))
            if dlg_state['img'] is not None:                              # a system dialog on top
                im = Image.alpha_composite(im.resize((960, 544)).convert('RGBA'), dlg_state['img']).convert('RGB').resize((480, 272), Image.LANCZOS)
            if scale[0] != 1: im = im.resize((480 * scale[0], 272 * scale[0]), Image.NEAREST)
            state['img'] = ImageTk.PhotoImage(im)
        else:
            ppm = b'P6 480 272 255\n' + bytes(rgb); img = tk.PhotoImage(data=ppm, format='PPM')
            state['img'] = img.zoom(scale[0], scale[0]) if scale[0] != 1 else img
        label.config(image=state['img'])

    # The game runs on its own thread, so drawing the window never holds it up (and the window keeps
    # showing frames while the game works). Most of the game's work happens in psp_core.dll, which lets both run at once.
    import threading, traceback
    state.update({'quit': False, 'req': [], 'msg': None, 'game_frames': 0})
    def handle_requests():
        while state['req']:
            r = state['req'].pop(0)
            if r == 'save':
                mach.save_state(os.path.join(FOLDER, "state_quick.pkl.gz")); state['msg'] = "snapshot saved: state_quick.pkl.gz"; state['snap_t'] = time.perf_counter()
            elif r == 'report':
                problem_report()
            elif r == 'sound':
                a = getattr(mach, 'aout', None)
                if a is None: continue
                if getattr(a, 'player', None) is not None: a.player.close(); a.player = None; state['msg'] = "sound off"
                else:
                    try:
                        import psp_sound; a.player = psp_sound.WaveOut(); state['msg'] = "sound on"
                    except Exception as e: state['msg'] = f"sound: {e}"
            elif r == 'fps60' and 'set_fps60' in gl:
                gl['set_fps60'](not gl['fps60'])
                state['msg'] = "60 fps (F4 = original frame rate)" if gl['fps60'] else "original frame rate, evenly timed (F4 = 60 fps)"
                print(state['msg'], flush=True)
    def game_thread():
        clk['wall0'] = wall0 = time.perf_counter(); clk['game0'] = game0 = mach.now()
        mach.idle_stop = settings.get('idle_wait', 'on') != 'off'      # (see psp_run.py run(): don't run ahead of real time over a wait)
        try:
            while not state['quit']:
                if state['req']: handle_requests()
                if time.perf_counter() - lk_state['t'] > 0.5:                     # locker capture hooks (psp_lockers.py)
                    lk_state['t'] = time.perf_counter()
                    try: psp_lockers.update(mach, lk_state['on'])
                    except Exception as ex: print('locker capture:', ex, flush=True); lk_state['on'] = False
                want = game0 + (time.perf_counter() - wall0) * 1_000_000          # where the game clock should be (real time)
                if mach.now() >= want: time.sleep(0.001); continue              # ahead of real time: wait a moment
                state['gcpu'] = time.thread_time()                                 # (for the speed report)
                t_run = time.perf_counter()
                try: mach.run(cpu.icount + 150_000)
                except psp_run.Stop as e:
                    if 'budget' not in e.reason: raise
                t_end = time.perf_counter()
                if perf['on']: perf['G'].append((t_run, t_end, time.thread_time() - state['gcpu']))
                t_run = t_end - t_run
                if t_run > 0.4:                                                   # the game stood still: say why (for Claude)
                    sl = mach.__dict__.get('slow_calls', [])
                    print(f"[pause] the game stood still for {t_run:.1f} s; slow system calls: {sl[-6:]}; "
                          f"drawing queue: {mach.ge._q.unfinished_tasks if mach.ge.__dict__.get('_q') else '-'}", flush=True)
                    lt = mach.__dict__.get('load_times')
                    if lt: print(f"        module loads (name, reading s, setting up s): {lt[-3:]}", flush=True); lt.clear()
                    sl.clear()
                if want - mach.now() > 500_000:                                   # can't keep up: don't try to catch up later
                    wall0 = time.perf_counter(); game0 = mach.now(); clk['wall0'] = wall0; clk['game0'] = game0
        except psp_run.Stop as e:
            state['stopped'] = f"The game stopped: {e.reason}  (pc={cpu.pc:#x}) - see the Python window"; print("STOPPED:", e.reason)
        except BaseException:
            traceback.print_exc(); print("\nThe game stopped because of the error above. Send play_log.txt to Claude.")
            state['stopped'] = "The game stopped because of an error - send play_log.txt to Claude"
    worker = threading.Thread(target=game_thread, daemon=True)

    def pacer_thread():
        """60 fps mode: one picture for every refresh of the screen, made shortly before that refresh and drawn for
        the exact moment the screen will show it (Windows tells us when the screen refreshes). So every refresh
        shows the game one refresh-time further on: even motion whatever the game thread's timing"""
        vb = VBlankClock()
        period = vb.period or 1.0 / screen_refresh(); gl['period'] = period
        cap = settings.get('fps_cap', 'off'); k = 1
        try:
            if cap not in ('off', '', '0'): k = max(1, round(1 / (period * float(cap))))   # a new picture every k refreshes
        except ValueError: pass
        print(f"60 fps pacing: a picture every {'' if k == 1 else f'{k} '}screen refresh{'' if k == 1 else 'es'} "
              f"({1 / period:.0f} Hz screen: {1 / (period * k):.0f} fps{', fps_cap = ' + cap if k > 1 else ''}), "
              f"{'timed to the screen' if vb.ok else 'timed by the clock'}", flush=True)
        # each picture is made just before the refresh it is for and drawn for that exact moment. Only sleeping in
        # between - never spinning, which would take time away from the game thread
        q = mach.ge.__dict__.get('_q'); lat = min(0.007, period * 0.75); v = time.perf_counter() + period
        # held pictures (Vulkan): drawn up to 12 ms early (the drawing thread may be busy with the game's own drawing for a
        # while), shown by this thread just before its refresh
        # (handed to the screen in the middle of the refresh before its own: never too late for Windows to take it in
        #  for that refresh, never so early that it shows a refresh too soon. present_margin = ms, to try other values)
        hold = gl.get('hold'); seq = 0
        try: margin = float(settings.get('present_margin', '')) / 1000.0; margin_fs = margin
        except ValueError: margin = period * 0.5; margin_fs = period * 0.5
        margin = max(0.0005, min(period * 0.9, margin)); margin_fs = max(0.0005, min(period * 0.9, margin_fs))
        # (full screen: the perf_log's "shown unevenly" is ~30% there against under 1% in a window - but handing pictures
        #  over earlier moved the measured moments with it and changed nothing: in full screen the driver reports a
        #  picture as shown when it is handed over, not when the screen shows it, so that figure isn't real there.
        #  Kept separate, at the same half refresh)
        margin_win = margin
        perf['period'] = period; perf['k'] = k; perf['margin'] = margin
        if hold: lat = max(lat, min(0.015, period * k * 0.9))
        perf['lat'] = lat
        print(f"Picture timing: {'held - drawn up to %.0f ms early, shown at its refresh, ahead of the game drawing' % (lat * 1000) if hold else 'direct'} (present_timing)", flush=True)
        # (Python hands over between threads more often: the pacer gets its turn on time. The drawing thread waits for
        #  its turn each time it comes back from the graphics code - in the busiest places (the hospital in an alert)
        #  its jobs spent about 2/3 of their time waiting like that, at 1 ms each turn. switch_interval = ms, to try)
        try: swi = float(settings.get('switch_interval', '') or 0.25) / 1000.0
        except ValueError: swi = 0.00025
        try: sys.setswitchinterval(max(0.00005, min(0.005, swi)))
        except Exception: pass
        while not state['quit']:
            d = v - lat - time.perf_counter()
            if d > 0: time.sleep(d)
            now = time.perf_counter()
            if vb.ok:
                w = vb.nearest(v)                                              # (the planned refresh, exactly)
                if w is not None: v = w
            if v < now: pace['wl'] = pace.get('wl', 0) + 1
            while v < now: v += period                                         # (woke up after it: the next refresh)
            seq += 1; posted = False; tpost = time.perf_counter(); res = None; tcall = dcall = 0.0
            if pace['on'] and 'paced_present' in gl:
                if q is not None and q.unfinished_tasks > 8: pace['skip'] = pace.get('skip', 0) + 1   # (drawing far behind)
                else: gl['paced_present'](v - lat, seq); posted = True; pace['next_post'] = v + period * (max(1, round(1 / (period * 60))) if gl.get('snap') else k) - lat   # (drawn for its moment, less how early it's drawn:
                                                                             #  the game frames it needs are there by then)
            if hold and posted:
                margin = margin_fs if state.get('fs') else margin_win; perf['margin'] = margin
                d = v - margin - time.perf_counter()
                if d > 0: time.sleep(d)
                tcall = time.perf_counter(); res = lib.vk_present_held(seq); dcall = time.perf_counter() - tcall
                if not res: pace['pl'] = pace.get('pl', 0) + 1     # (not drawn in time: shown when done)
            if perf['on']: perf['P'].append((seq, v, tpost, posted, tcall, dcall, res, q.qsize() if q is not None else 0))
            kk = k
            if gl.get('snap'):                                       # (original frame rate: a picture rate the game's 20 /
                kk = max(1, round(1 / (period * 60)))                #  30 fps divide evenly into - 60 - whatever fps_cap is)
            v += period * kk

    def dialog_overlay():
        """show the game's system dialog (if one is open) on top of the picture"""
        util = mach.__dict__.get('util'); dl = util.active() if util is not None else None
        ver = (util.ver if util is not None else 0, id(dl) if dl is not None else 0)
        if ver == dlg_state['ver']: return
        dlg_state['ver'] = ver
        img = None
        if dl is not None:
            try:
                import psp_dialogs; img = psp_dialogs.render(dl.ui)
            except Exception as e: print("dialog picture:", e, flush=True)
        dlg_state['img'] = img; state['last_fb'] = -2                     # (software drawing: show it now)
        if gl['on'] and hasattr(lib, 'gl_overlay'):
            if img is not None:
                data = img.tobytes(); w, h = img.size
                mach.ge.post(lambda: lib.gl_overlay(data, w, h))
            else: mach.ge.post(lambda: lib.gl_overlay(None, 0, 0))
            fb = getattr(mach, 'cur_fb', None)                        # show it even if the game isn't drawing new pictures
            if fb and fb[0] and getattr(mach, 'gl_present', None): mach.gl_present(fb[0], fb[1] or 512, fb[2] if fb[2] < 4 else 3)
    def problem_report():
        """F8: a snapshot, a screenshot and the logs in one zip for Claude (called on the game thread between steps)"""
        import zipfile, zlib, struct, datetime
        stamp = datetime.datetime.now().strftime('%Y-%m-%d_%H-%M-%S')
        rdir = os.path.join(FOLDER, 'problem_reports')
        try: os.makedirs(rdir, exist_ok=True)
        except OSError as e: print("problem report: can't make the folder:", e, flush=True); return
        st_path = os.path.join(rdir, '_state_' + stamp + '.pkl.gz')
        try: mach.save_state(st_path)
        except Exception as e: print("problem report: the snapshot failed:", e, flush=True); st_path = None
        lines = ["MGS Portable Ops PC port - problem report (F8) " + stamp,
                 "renderer: " + str(getattr(gl.get('ctx'), 'info', '') or ('software' if not gl.get('on') else '?')),
                 "60 fps: %s (in-between pictures: %s)   lighting: %s   window: %s   full screen: %s" % (gl.get('fps60'), 'exact' if gl.get('exact', True) else 'old way', gl.get('lighting'), tuple(gl.get('size', (0, 0))), bool(root.attributes('-fullscreen'))),
                 "framebuffer shown: %s" % (getattr(mach, 'cur_fb', None),),
                 "settings: " + ", ".join(f"{k}={v}" for k, v in sorted(settings.items()))]
        if gl.get('on') and hasattr(lib, 'gl_dbg_texlist'):         # the textures on the screen and their HD versions
            try:
                tb = (ct.c_uint32 * (7 * 1024))(); nt = mach.ge.call(lambda: lib.gl_dbg_texlist(tb, 1024))
                files = hd_state.get('files') or {}
                ST = {0xFFFFFFFF: 'no HD version in the packs', 0: 'HD version not asked for yet', 1: 'HD version loading', 2: 'HD version in use', 3: 'HD version could not be loaded'}
                lines.append(""); lines.append(f"TEXTURES IN THE LAST FRAMES ({nt}; HD loading queue {hd_state.get('waiting', 0)}, loaded so far {hd_state.get('loaded', 0)}):")
                for i in range(nt):
                    q = tb[7 * i:7 * i + 7]; key = (q[6] or q[0], q[1], q[2]) if q[5] != 0xFFFFFFFF else None
                    lines.append(f"  {q[0]:08x}{q[1]:08x}{q[2]:08x} {q[3]}x{q[4]}: {ST.get(q[5], q[5])}" + (f" ({os.path.relpath(files[key], FOLDER)})" if key in files else ""))
            except Exception as e: lines.append(f"(texture list: {e})")
        def png(raw, w, h):
            rows = b''.join(b'\x00' + raw[y * w * 3:(y + 1) * w * 3] for y in range(h))
            def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
            return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b'')
        def finish():
            t_end = time.perf_counter() + 1.5                       # (the screenshot: the next picture shown)
            while gl.get('grab') is True and time.perf_counter() < t_end: time.sleep(0.02)
            g = gl.get('grab'); gl['grab'] = None
            zpath = os.path.join(rdir, 'report_' + stamp + '.zip')
            try:
                try: sys.stdout.flush()
                except Exception: pass
                with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
                    z.writestr('report.txt', "\n".join(lines) + "\n")
                    if isinstance(g, tuple): z.writestr('screenshot.png', png(*g))
                    if st_path and os.path.exists(st_path): z.write(st_path, 'state_quick.pkl.gz', compress_type=zipfile.ZIP_STORED)
                    for name in ('play_log.txt', 'port_settings.txt'):
                        fp = os.path.join(FOLDER, name)
                        if os.path.exists(fp): z.write(fp, name)
                    fp = os.path.join(FOLDER, 'perf_log.txt')        # (an F7 report from the last half hour)
                    if os.path.exists(fp) and time.time() - os.path.getmtime(fp) < 1800: z.write(fp, 'perf_log.txt')
                if st_path:
                    try: os.remove(st_path)
                    except OSError: pass
                msg = "problem report saved: problem_reports\\report_" + stamp + ".zip - send it to Claude"
            except Exception:
                traceback.print_exc(); msg = "the problem report could not be written (error above)"
            print(msg, flush=True); state['msg'] = msg
        threading.Thread(target=finish, daemon=True).start()
        state['msg'] = "making a problem report..."
    def perf_stop():
        perf['on'] = False; perf['t1'] = time.perf_counter()
        jobs = mach.ge.__dict__.pop('jobtrace', None) or []
        if perf.get('tm'):
            perf['tsc1'] = lib.vk_tsc(); b = (ct.c_uint64 * 8)(); lib.vk_timers(b); perf['tm1'] = tuple(b)
        info = {'settings': dict(settings), 'lighting': gl.get('lighting'), 'hold': gl.get('hold'), 'size': tuple(gl.get('size', (0, 0))),
                'fullscreen': bool(root.attributes('-fullscreen')), 'renderer': getattr(gl.get('ctx'), 'info', '')}
        data = {k: perf.get(k) for k in ('P', 'C', 'G', 'J', 'T', 'S', 't0', 't1', 'tsc0', 'tsc1', 'tm0', 'tm1', 'period', 'k', 'margin', 'lat')}
        def write():
            try:
                import psp_perf
                txt = psp_perf.report(data, jobs, info)
                with open(os.path.join(FOLDER, 'perf_log.txt'), 'w', encoding='utf-8') as f: f.write(txt)
                print("perf_log.txt written - send it to Claude. In short:", flush=True); print(psp_perf.SHORT, flush=True)
            except Exception:
                traceback.print_exc(); print("perf_log.txt could not be written (error above)", flush=True)
        threading.Thread(target=write, daemon=True).start()
        state['msg'] = "perf_log.txt written (see the Python window)"
    # ---- stutter log: the exact timings are recorded all the time (the last ~20 s are kept). Every second a short
    # check; when the picture went choppy (pictures late, in-between pictures not made, a long hitch...) the 12 seconds
    # around it are written to stutter_logs\ by themselves - nobody has to press anything at the right moment.
    # F7 does the same at once for "what just happened" (the last 12 seconds + 3 more).
    stut = {'last': -1e9, 'pending': None, 'f7': None, 'hist': [], 'slow': [], 'gc': [], 'clk': [], 'from': None, 'base': []}
    PF_PARTS = ('in-between drawing', 'shadow pass', 'ambient occlusion', 'lighting place', 'new framebuffer', 'anti-aliasing resolve',
                'textures (decode/bind)', 'copying the screen back', 'matching frames', 'lost things', 'sending draws', 'new graphics card image',
                'graphics setups (pipelines)', 'handing work to the card', 'keeping last picture', 'draw commands (all of each)')
    PF_KIND = {1: 'picture', 2: 'new game frame', 3: 'game drawing', 4: 'HD texture'}
    def stutter_start():
        if not gl.get('on') or settings.get('stutter_log', 'on') == 'off' or perf['on'] or not hasattr(lib, 'vk_tsc'): return
        import gc
        lib.vk_tsc.restype = ct.c_uint64
        perf.update(P=[], C=[], G=[], J=[], T={}, S=[], t0=time.perf_counter(), tm=hasattr(lib, 'vk_timers'), roll=True)
        mach.ge.jobtrace = []; perf['on'] = True
        def clk_now():
            b = None
            if perf['tm']: b = (ct.c_uint64 * 8)(); lib.vk_timers(b); b = tuple(b)
            return (time.perf_counter(), lib.vk_tsc(), b)
        stut['clk_now'] = clk_now; stut['clk'].append(clk_now())
        def gc_cb(phase, info):                                          # (Python's memory clean-ups: each one stops the game)
            if phase == 'start': stut['gct'] = time.perf_counter()
            elif 'gct' in stut:
                d = time.perf_counter() - stut['gct']
                if d > 0.002: stut['gc'].append((stut['gct'], d, info.get('generation', -1)))
        gc.callbacks.append(gc_cb)
        if hasattr(lib, 'vk_present_track') and 'Vulkan' in getattr(gl.get('ctx'), 'info', ''):
            def track():                                                 # when each picture really reached the screen
                lib.vk_present_track.argtypes = [ct.c_void_p]; b = (ct.c_uint64 * 3)()
                while perf['on'] and not state['quit']:
                    r = lib.vk_present_track(b)
                    if r == 1: perf['S'].append((int(b[0]), int(b[1]), int(b[2])))
                    elif r < 0: perf['S'] = None; return
                    else: time.sleep(0.001)
            threading.Thread(target=track, daemon=True).start()
        else: perf['S'] = None
        print("Stutter log: on - choppy moments are written to stutter_logs\\ by themselves (F7 = save the last 12 seconds now)", flush=True)
    def tscf():
        c = stut['clk']
        if len(c) < 2 or c[-1][0] - c[0][0] < 0.5: return None
        return (c[-1][1] - c[0][1]) / (c[-1][0] - c[0][0])
    def perf_window(ta, tb):
        """the recorded timings between ta and tb, as psp_perf.report wants them"""
        P = [x for x in perf['P'] if ta <= x[2] <= tb]; seqs = {x[0] for x in P}
        T = {q: v for q, v in list(perf['T'].items()) if q in seqs}
        S = None if perf['S'] is None else [x for x in perf['S'] if x[0] in seqs]
        c0 = max((c for c in stut['clk'] if c[0] <= ta), key=lambda c: c[0], default=stut['clk'][0])
        c1 = stut['clk_now']()
        d = {'P': P, 'C': [x for x in perf['C'] if ta <= x[1] <= tb], 'G': [x for x in perf['G'] if ta <= x[0] <= tb],
             'J': [x for x in perf['J'] if x[0] in seqs], 'T': T, 'S': S, 't0': c0[0], 't1': c1[0], 'tsc0': c0[1], 'tsc1': c1[1],
             'tm0': c0[2], 'tm1': c1[2]}
        for k2 in ('period', 'k', 'margin', 'lat'): d[k2] = perf.get(k2)
        jobs = [j for j in (mach.ge.__dict__.get('jobtrace') or []) if j[3] >= c0[0] and j[2] <= tb]
        return d, jobs
    def write_window(path, ta, tb, head):
        d, jobs = perf_window(ta, tb)
        info = {'settings': dict(settings), 'lighting': gl.get('lighting'), 'hold': gl.get('hold'), 'size': tuple(gl.get('size', (0, 0))),
                'fullscreen': bool(root.attributes('-fullscreen')), 'renderer': getattr(gl.get('ctx'), 'info', '')}
        f = tscf(); t_ref = stut['clk'][-1]
        def tt(tsc): return t_ref[0] + (tsc - t_ref[1]) / f if f else 0.0
        extra = ["", "SLOW JOBS ON THE DRAWING THREAD (longer than 25 ms; the parts overlap - an in-between picture includes its shadow pass):"]
        for r in stut['slow']:
            if f and ta <= tt(r[1]) <= tb:
                parts = sorted(((r[3 + i] / f * 1000, PF_PARTS[i], r[3 + 16 + i]) for i in range(16) if r[3 + i]), key=lambda x: -x[0])
                extra.append("  at %+.2f s: %s %.1f ms - %s" % (tt(r[1]) - ta, PF_KIND.get(r[0], '?'), r[2] / f * 1000,
                             ", ".join(f"{n} {ms:.1f} ms ({c}x)" for ms, n, c in parts[:8] if ms >= 0.5) or 'nothing measured inside'))
        if len(extra) == 2: extra.append("  none")
        extra += ["", "SLOW DRAW COMMANDS (longer than 4 ms; the slowest 25):"]
        sps = sorted((r for r in stut.get('sprims', []) if f and ta <= tt(r[0]) <= tb), key=lambda r: -r[1])[:25]
        for r in sps:
            tx = r[4]
            extra.append("  at %+.2f s: %.1f ms - cmd %02x, %d vertices, into %06x%s%s, texture %s, software pixels %d, textures %.1f ms, copying back %.1f ms, sending %.1f ms"
                         % (tt(r[0]) - ta, r[1] / f * 1000, r[2] & 0xFF, r[2] >> 8, r[3], ' (also software)' if r[5] & 1 else '', ' (2D)' if r[5] & 2 else '',
                            ('%08x %dx%d' % (tx & 0xFFFFFFFF, 1 << ((tx >> 32) & 15), 1 << ((tx >> 40) & 15))) if tx else 'none', r[6], r[7] / f * 1000, r[8] / f * 1000, r[9] / f * 1000))
        if not sps: extra.append("  none")
        sm = [x for x in stut.get('sums', []) if ta - 1.0 <= x[0] <= tb + 1.0]
        if f and len(sm) >= 2:                                                  # all drawing jobs, by kind, with their parts
            (t0s, a), (t1s, b) = sm[0], sm[-1]; secs = max(0.5, t1s - t0s); W = 2 + 2 * 16
            extra += ["", f"WHERE THE DRAWING THREAD'S TIME WENT ({secs:.0f} s, ms per second; the parts overlap):"]
            for kd in (3, 1, 2, 4):
                o = kd * W; njob = b[o] - a[o]; tot = (b[o + 1] - a[o + 1]) / f * 1000 / secs
                if not njob: continue
                parts = sorted((((b[o + 2 + i] - a[o + 2 + i]) / f * 1000 / secs, PF_PARTS[i], b[o + 18 + i] - a[o + 18 + i]) for i in range(16)), key=lambda x: -x[0])
                extra.append(f"  {PF_KIND.get(kd, '?')}: {tot:.1f} ms/s in {njob} pieces - " +
                             (", ".join(f"{n} {ms:.1f} ({c}x)" for ms, n, c in parts if ms >= 0.2) or 'nothing measured inside'))
        extra.append(""); extra.append("PYTHON MEMORY CLEAN-UPS LONGER THAN 2 ms (they stop every Python thread):")
        g2 = [g for g in stut['gc'] if ta <= g[0] <= tb]
        extra += ["  at %+.2f s: %.1f ms (generation %d)" % (g[0] - ta, g[1] * 1000, g[2]) for g in g2] or ["  none"]
        def job():
            try:
                import psp_perf
                txt = psp_perf.report(d, jobs, info)
                with open(path, 'w', encoding='utf-8') as fo: fo.write("\n".join(head + extra) + "\n\n" + txt)
                print(os.path.relpath(path, FOLDER) + " written - send it to Claude. In short:", flush=True); print(psp_perf.SHORT, flush=True)
            except Exception:
                traceback.print_exc(); print("the stutter log could not be written (error above)", flush=True)
        threading.Thread(target=job, daemon=True).start()
    def stutter_second(now):
        """every second: what the last second looked like; a choppy one starts a stutter log"""
        f = tscf(); stut['clk'].append(stut['clk_now']())
        if len(stut['clk']) > 40: del stut['clk'][:len(stut['clk']) - 40]
        if hasattr(lib, 'gl_prof_take'):
            if stut['from'] is None:
                stut['from'] = ct.c_uint32(0); stut['pbuf'] = (ct.c_uint64 * (64 * 35))()
                lib.gl_prof_take.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_int]; lib.gl_prof_threshold.argtypes = [ct.c_uint64]
            if f and not stut.get('thr'): lib.gl_prof_threshold(int(0.025 * f)); stut['thr'] = True
            n = lib.gl_prof_take(ct.byref(stut['from']), stut['pbuf'], 64)
            if hasattr(lib, 'gl_prof_sums'):                                   # running totals of all drawing jobs (for the log)
                sb = (ct.c_uint64 * (5 * (2 + 2 * 16)))(); lib.gl_prof_sums(sb)
                stut.setdefault('sums', []).append((now, list(sb))); del stut['sums'][:-40]
            for i in range(n): stut['slow'].append(tuple(stut['pbuf'][i * 35:(i + 1) * 35]))
            if hasattr(lib, 'gl_slowprims') and f:
                if not stut.get('spthr'):
                    lib.gl_slowprim_threshold.argtypes = [ct.c_uint64]; lib.gl_slowprim_threshold(int(0.004 * f)); stut['spthr'] = True
                    stut['spbuf'] = (ct.c_uint64 * (64 * 10))(); lib.gl_slowprims.argtypes = [ct.c_void_p, ct.c_int]
                k = lib.gl_slowprims(stut['spbuf'], 64)
                sp = stut.setdefault('sprims', [])
                for i in range(k): sp.append(tuple(stut['spbuf'][i * 10:(i + 1) * 10]))
                del sp[:-400]
            if len(stut['slow']) > 300: del stut['slow'][:len(stut['slow']) - 300]
        ta = now - 1.0; period, k = perf.get('period') or 1 / 60, perf.get('k') or 1
        P = [x for x in perf['P'] if x[2] >= ta and x[3]]
        late = sum(1 for x in P if x[6] == 0) if gl.get('hold') else 0
        seqs = {x[0] for x in P}; uneven = meas = 0
        S = perf['S'] or []
        if f:
            sh = [x for x in S[-200:] if x[0] in seqs]
            for i in range(1, len(sh)):
                if sh[i][0] == sh[i - 1][0] + 1:
                    meas += 1; dt = (sh[i][2] - sh[i - 1][2]) / f
                    if abs(dt - period * k) > period * 0.5: uneven += 1
        pk = stut.get('pk', [0, 0, 0, 0])
        nb, tot = pk[1] + pk[2] + pk[3], sum(pk)
        C = [x for x in perf['C'] if x[1] >= ta]; gfps = len(C)
        Cg = [x for x in perf['C'] if x[1] >= ta - 1.0]
        gap = max((Cg[i][1] - Cg[i - 1][1] for i in range(1, len(Cg))), default=0.0)
        worst = 0.0; wk = ''
        if f:
            for r in stut['slow']:
                t = stut['clk'][-1][0] + (r[1] - stut['clk'][-1][1]) / f
                if t >= ta and r[2] / f > worst: worst = r[2] / f; wk = PF_KIND.get(r[0], '?')
        gcw = max((g[1] for g in stut['gc'] if g[0] >= ta), default=0.0)
        # how fast the computer was this second: the game's instructions per second of the game thread's own CPU time
        # (the same work getting slower = the processor itself slowed - power saving, another program, Windows), and
        # the drawing thread's CPU time per picture; and the window (full screen / not, in front / not)
        ic = getattr(getattr(mach, 'cpu', None), 'icount', 0); gc_s = sum(x[2] for x in perf['G'] if x[0] >= ta)
        dc = getattr(mach.ge, 'busy_cpu', 0.0); pic_cpu = (dc - stut.get('dc0', dc)) / max(1, len(P)) * 1000; stut['dc0'] = dc
        mips = (ic - stut.get('ic0', ic)) / gc_s / 1e6 if gc_s > 0.05 else 0.0; stut['ic0'] = ic
        try: fs = bool(root.attributes('-fullscreen')); front = root.focus_displayof() is not None
        except Exception: fs = front = None
        win = f"{'full screen' if fs else 'window'}{'' if front else ', not in front'}"
        gt = pace.get('gated', 0); gated = gt - stut.get('gt0', gt); stut['gt0'] = gt
        odd = [0] * 11
        if hasattr(lib, 'gl_present_odd'):                              # pictures shown black / from video memory
            ob = (ct.c_int * 11)(); lib.gl_present_odd(ob); odd = list(ob)
        oddtxt = (f", BLACK pictures {odd[0]}" if odd[0] else "") + (f", pictures from video memory {odd[1]}" if odd[1] else "") + \
                 (f" (blacked out {odd[2]})" if odd[2] else "") + \
                 (f" [last: black {odd[3]}, address {odd[4]:#x}, frames since drawn {odd[5]}, age {odd[6]}, older frame {odd[7]}, movie {odd[8]}, no new frames {odd[9]}, t {odd[10] / 1000:.3f}]" if odd[0] or odd[1] else "")
        if stut.get('win') is not None and win != stut['win']: stut['hist'].append(f"{time.strftime('%H:%M:%S')}  >>> the window changed: {stut['win']} -> {win}")
        stut['win'] = win
        frac = uneven / meas if meas >= 20 else 0.0
        stut['base'].append(frac); del stut['base'][:-30]
        base = sorted(stut['base'])[len(stut['base']) // 2]
        line = (f"{time.strftime('%H:%M:%S')}  pictures {len(P)}, late {late}, shown unevenly {uneven} of {meas}, in-between not blended {nb} of {tot}, "
                f"game frames {gfps}, longest gap between game frames {gap * 1000:.0f} ms, slowest drawing job {worst * 1000:.0f} ms{' (' + wk + ')' if wk else ''}"
                f"{f', Python clean-up {gcw * 1000:.0f} ms' if gcw else ''}, game {mips:.0f} M instructions per CPU second, drawing {pic_cpu:.1f} ms CPU per picture, new frames held back for a picture {gated}, {win}{oddtxt}")
        stut['hist'].append(line); del stut['hist'][:-40]
        why = []
        if len(P) >= 20 and late >= max(4, len(P) // 8): why.append(f"{late} of {len(P)} pictures not ready in time")
        if tot >= 20 and gfps >= 12 and nb * 3 >= tot: why.append(f"{nb} of {tot} in-between pictures not blended (looks like 20 fps)")
        if worst > 0.06: why.append(f"a {worst * 1000:.0f} ms hitch ({wk})")
        if gap > 0.15 and gfps >= 1 and now - state.get('snap_t', -1e9) > 2.5: why.append(f"the game made no new frame for {gap * 1000:.0f} ms")   # (not an F5 snapshot: that stops the game a moment)
        if gcw > 0.05: why.append(f"a {gcw * 1000:.0f} ms Python clean-up")
        if (odd[0] or odd[2]) and gfps >= 15: why.append(f"{odd[0] + odd[2]} black picture(s) while the game was running")
        if meas >= 20 and frac > 0.4 and frac > base + 0.25: why.append(f"{uneven} of {meas} pictures shown unevenly (usually {base * 100:.0f}%)")
        return why
    def stutter_tick(now):
        if not perf.get('roll'): return
        if now - stut.get('sec', 0.0) >= 1.0:
            stut['sec'] = now
            try: why = stutter_second(now)
            except Exception: traceback.print_exc(); why = []
            if why and stut['pending'] is None and stut['f7'] is None and now - stut['last'] > 45 and now - perf['t0'] > 15:
                stut['pending'] = (now, why); print("stutter noticed:", "; ".join(why), "- writing a stutter log in 4 s", flush=True)
            cut = now - 25                                               # (keep the last 25 s)
            for key, ti in (('P', 2), ('C', 1), ('G', 0), ('J', 1)):
                L = perf[key]; i = 0
                while i < len(L) and L[i][ti] < cut: i += 1
                if i: del L[:i]
            jt = mach.ge.__dict__.get('jobtrace')
            if jt is not None:
                i = 0
                while i < len(jt) and jt[i][3] < cut: i += 1
                if i: del jt[:i]
            if perf['P']:
                q0 = perf['P'][0][0]
                for q in [q for q in list(perf['T']) if q < q0]: perf['T'].pop(q, None)
                if perf['S']:
                    i = 0
                    while i < len(perf['S']) and perf['S'][i][0] < q0: i += 1
                    if i: del perf['S'][:i]
            del stut['gc'][:max(0, len(stut['gc']) - 200)]
        if stut['pending'] and now - stut['pending'][0] >= 4.0:
            t, why = stut['pending']; stut['pending'] = None; stut['last'] = now
            d = os.path.join(FOLDER, 'stutter_logs')
            try:
                os.makedirs(d, exist_ok=True)
                old = sorted(x for x in os.listdir(d) if x.startswith('stutter_') and x.endswith('.txt'))
                for x in old[:max(0, len(old) - 9)]: os.remove(os.path.join(d, x))   # (the newest 10 are kept)
            except OSError as e: print("stutter log: the folder:", e, flush=True); return
            path = os.path.join(d, 'stutter_' + time.strftime('%Y-%m-%d_%H-%M-%S') + '.txt')
            head = ["MGS Portable Ops PC port - stutter log (written by itself): 8 s before the moment it noticed, 4 s after",
                    "noticed: " + "; ".join(why), "", "EVERY SECOND BEFORE IT:"] + ["  " + x for x in stut['hist'][-20:]]
            write_window(path, t - 8.0, t + 4.0, head)
            state['msg'] = "stutter log written: stutter_logs\\" + os.path.basename(path)
        if stut['f7'] and now - stut['f7'] >= 3.0:
            t = stut['f7']; stut['f7'] = None
            head = ["MGS Portable Ops PC port - perf_log (F7): the 12 s before F7 was pressed, 3 s after", "", "EVERY SECOND BEFORE IT:"] + ["  " + x for x in stut['hist'][-20:]]
            write_window(os.path.join(FOLDER, 'perf_log.txt'), t - 12.0, t + 3.0, head)
            state['msg'] = "perf_log.txt written (see the Python window)"
    def tick():
        if state['quit']: return
        if perf['on'] and not perf.get('roll') and time.perf_counter() - perf['t0'] >= 10.0: perf_stop()
        if perf.get('roll'): stutter_tick(time.perf_counter())
        poll_pad()
        dialog_overlay()
        if gl['on']:                                                   # the picture fills the window as it is now
            fw, fh = gl['frame'].winfo_width(), gl['frame'].winfo_height()
            if fw > 1 and fh > 1: gl['size'][0], gl['size'][1] = fw, fh
        if state['stopped']: status.config(text=state['stopped']); return
        n = getattr(mach, 'shown_n', 0)
        if n != state['last_fb']:
            state['game_frames'] += (n - state['last_fb']) if state['last_fb'] >= 0 else 0
            t_show = time.perf_counter(); state['last_fb'] = n; show(); state['frames'] += 1
            state['show_t'] = state.get('show_t', 0.0) + time.perf_counter() - t_show
        now = time.perf_counter()
        if state['msg']: status.config(text=state['msg']); state['msg'] = None; state['t_fps'] = now + 1.5; state['frames'] = 0; state['game_frames'] = 0; state.pop('flips0', None)
        if gl.get('on') and hasattr(lib, 'gl_present_kinds') and now - state.get('t_pk', 0.0) >= 1.0:   # (each second)
            state['t_pk'] = now; pk1 = (ct.c_int * 4)(); lib.gl_present_kinds(pk1); stut['pk'] = list(pk1)
            acc = state.setdefault('pk10', [0, 0, 0, 0])
            for i in range(4): acc[i] += pk1[i]
        if now - state['t_fps'] >= 1.0:
            dt = now - state['t_fps']; flips, emu = getattr(mach, 'flips', 0), mach.now()
            if 'flips0' in state:
                fps = (flips - state['flips0']) / dt; speed = (emu - state['emu0']) / 1e6 / dt * 100
                shown = (gl.get('shown', 0) - state.get('shown_s0', 0)) / dt; state['shown_s0'] = gl.get('shown', 0)
                rate = f"{shown:.0f} fps (game {fps:.0f})" if gl.get('fps60') else f"{fps:.0f} fps"
                status.config(text=f"{rate}   game speed {speed:.0f}%   from: {source}   "
                                   f"{'fast core' if lib is not None else 'SLOW (no psp_core.dll)'}   F5 = snapshot, F8 = problem report, F10 = mouse, Esc = quit")
                if state.setdefault('nlog', 0) % 10 == 0:                         # every 10 s: how busy each part was
                    g, d, w = state.get('gcpu', 0.0), getattr(mach.ge, 'busy_cpu', 0.0), state.get('show_t', 0.0)
                    if 'g0' in state:
                        el = now - state['tl0']
                        extra = ""
                        if gl['on'] and hasattr(lib, 'gl_sw_count'): extra = f", pictures also drawn in software: {lib.gl_sw_count()}"
                        if gl['on']:
                            extra += f", pictures shown {(gl['shown'] - state.get('sh0', 0)) / el:.0f}/s, waiting for the screen {100 * (gl.get('swap_t', 0.0) - state.get('sw0', 0.0)) / el:.0f}%"
                            if gl.get('fps60') and hasattr(lib, 'gl_interp_stats'):
                                ist = (ct.c_int * 8)(); lib.gl_interp_stats(ist)
                                extra += (f" (last frame: {ist[0]} draws, {ist[2]} moved smoothly, {ist[6]} moved with the camera, "
                                          f"{ist[3]} jumped{', camera jump' if ist[7] else ''}{', too big' if ist[4] else ''})")
                            if hasattr(lib, 'vk_pipes_new') and 'save_pipes' in gl:          # new drawing setups (each one a
                                dirty = ct.c_int(0); npn = lib.vk_pipes_new(ct.byref(dirty))   #  possible hitch), kept on disk
                                if npn: extra += f", new graphics setups {npn}"
                                if dirty.value: gl['save_pipes']()
                            if hasattr(lib, 'gl_replace_stats') and settings.get('hd_textures', 'on') != 'off':
                                hs = (ct.c_int * 3)(); lib.gl_replace_stats(hs)
                                extra += f", HD textures in use {hs[0]} ({hs[1]} MB{f', {hs[2]} let go' if hs[2] else ''})"
                            if hasattr(lib, 'gl_present_kinds'):                              # in-between pictures that fell
                                pk = state.get('pk10', [0, 0, 0, 0]); state['pk10'] = [0, 0, 0, 0]   #  back to the plain game frame
                                if pk[1] + pk[2] + pk[3]: extra += f", in-between pictures not blended {pk[1] + pk[2] + pk[3]} of {sum(pk)} (camera cut {pk[1]}, no match {pk[2] + pk[3]})"
                            if hasattr(lib, 'gl_garbage_hidden'):                             # scene-change "TV snow" pictures
                                gh = lib.gl_garbage_hidden()                                   #  shown as black instead (total)
                                if gh: extra += f", scene-change snow pictures hidden {gh}"
                            if gl.get('slow'):
                                extra += f", slow pictures {gl['slow']} (slowest {gl.get('slowest', 0) * 1000:.0f} ms)"; gl['slow'] = 0; gl['slowest'] = 0.0
                            if pace.get('n'):
                                extra += (f"; pacing: {pace['n']} paced pictures, {pace.get('lo', 0)} too early, {pace.get('hi', 0)} ran out, "
                                          f"{pace.get('skip', 0)} skipped (drawing busy), {pace.get('wl', 0)} woke up late"
                                          + (f", {pace.get('pl', 0)} not drawn in time" if gl.get('hold') else "") +
                                          f" (refresh {1 / gl['period'] if gl.get('period') else 0:.1f} Hz)")
                                pace['n'] = pace['lo'] = pace['hi'] = pace['skip'] = pace['wl'] = pace['pl'] = 0
                        print(f"[speed] {fps:.0f} fps (the game's own pictures), game speed {speed:.0f}%   busy: game {100 * (g - state['g0']) / el:.0f}%, "
                              f"drawing {100 * (d - state['d0']) / el:.0f}%, window {100 * (w - state['w0']) / el:.0f}%{extra}", flush=True)
                    else: print(f"[speed] {fps:.0f} fps, game speed {speed:.0f}%", flush=True)
                    state['g0'], state['d0'], state['w0'], state['tl0'] = g, d, w, now; state['sh0'] = gl.get('shown', 0); state['sw0'] = gl.get('swap_t', 0.0)
                state['nlog'] += 1
            state['flips0'], state['emu0'] = flips, emu; state['frames'] = 0; state['game_frames'] = 0; state['t_fps'] = now
        root.after(5, tick)

    if gl.get('on') and settings.get('hd_textures', 'on') != 'off' and hasattr(lib, 'gl_replace_add'):
        try: start_hd_textures(mach, lib)
        except Exception as e: print("HD textures: off -", e, flush=True)
    if gl.get('on') and settings.get('hd_movies', 'on') != 'off' and hasattr(lib, 'gl_movie_hd'):
        try:
            import ctypes as ct, psp_hdmovie
            lib.gl_movie_hd.argtypes = [ct.c_uint32, ct.c_int, ct.c_int, ct.c_char_p]
            mach.movie_hook = psp_hdmovie.HDMovies(FOLDER, lib, mach.ge.post, lambda msg: print(msg, flush=True))
            have = [d for d in psp_hdmovie.FOLDERS if os.path.isdir(os.path.join(FOLDER, d)) and os.listdir(os.path.join(FOLDER, d))]
            print("HD movies: on -", ", ".join(have) if have else "no folder 'HD movies' yet (the originals are shown)", flush=True)
        except Exception as e: print("HD movies: off -", e, flush=True)
    worker.start()
    if gl.get('on') and 'paced_present' in gl: threading.Thread(target=pacer_thread, daemon=True).start()
    try: stutter_start()
    except Exception: traceback.print_exc()
    root.after(10, tick)
    root.mainloop()
    state['quit'] = True; worker.join(timeout=5)
    if 'save_pipes' in gl:
        try: gl['save_pipes'](wait=True)
        except Exception: pass
    if getattr(mach, 'aout', None) is not None:
        mach.aout.close()
        if getattr(mach.aout, 'player', None) is not None: mach.aout.player.close()

try:
    main()
except SystemExit: raise
except BaseException:
    import traceback; traceback.print_exc()
    print("\nThe game window closed because of the error above. Send play_log.txt to Claude.")
    sys.exit(1)
