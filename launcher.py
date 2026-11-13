# Metal Gear Solid Ported Ops - LAUNCHER
# The first window you see: the FOXHOUND intro, then the menu on the grid:
#   CONTINUE MISSION  - the game, from your last F5 snapshot (or the game's own title screen if there is none)
#   NEW MISSION       - the game from power-on (title screen; your memory-stick saves load from there as usual)
#   DISC CHECK        - finds and checks your game disc image (.iso) and the files the port needs
#   SETTINGS          - every setting of port_settings.txt, with what it does; saved as you change them
#   EXIT
# The FOXHOUND art is locked to the game disc (pak\launcher_assets.pak): it shows only once the file check is all good.
# DISC CHECK also installs the HD packs (MGSPO-HD-*.pak, from the release page): upscaled movies and textures.
# Keyboard: arrows / Enter / Esc (F11 = full screen). Mouse: point and click, wheel scrolls. Controller: D-pad or left
# stick, A (Cross) = select, B (Circle) = back, LB / RB = previous / next settings page.
import os, sys, io, re, json, time, glob, struct, tempfile, threading, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "launcher_assets")
CONFIG = os.path.join(HERE, "launcher.ini")
SETTINGS_FILE = os.path.join(HERE, "port_settings.txt")
GAME_ID = "ULUS10202"
SNAPS = ["state_quick.pkl.gz", "state_menu.pkl.gz", "state_movie2.pkl.gz"]   # the order play.py looks for them

from PIL import Image, ImageDraw, ImageFont, ImageFilter
sys.path.insert(0, HERE)
import mgspo_pack as PK
PAK_DIR = os.path.join(HERE, "pak")                      # the locked packs: the launcher's art and the HD packs
ART_PAK = next((p for p in (os.path.join(PAK_DIR, "launcher_assets.pak"), os.path.join(HERE, "launcher_assets.pak")) if os.path.exists(p)),
               os.path.join(PAK_DIR, "launcher_assets.pak"))
# TheGreatBulzome's HD texture pack (environments, objects, weapons, characters): made by him, shared by him -
# the launcher only points to it and gives him the credit.
BULZOME_VIDEO = "https://www.youtube.com/watch?v=5MRC69kFOLM"
BULZOME_LINK = "https://rb.gy/jyajnt"

VW, VH = 1920, 1080                     # everything is laid out on a 1920x1080 page, then scaled to the window
PITCH, GX, GY = 31.75, 38.0, 31.0       # the grid of the intro's last picture: a line every 31.75 px, first at 38 / 31
CREAM = (232, 226, 208); DIM = (150, 145, 130); FAINT = (88, 85, 76); RED = (200, 30, 28); DRED = (110, 18, 16)
OKC = (232, 226, 208); BAD = (226, 52, 44); WARN = (222, 170, 60)
SPRITE_AT = (527, 350)                  # where the logo sits in the intro's 1920x1080 picture (top-left of the sprite)


def gx(k): return GX + PITCH * k          # grid line k (x)
def gy(k): return GY + PITCH * k          # grid line k (y)


# ------------------------------------------------------------------------------------------------ fonts and drawing
_FONT_FILES = [("bahnschrift.ttf", True), ("BAHNSCHRIFT.TTF", True), ("arialbd.ttf", False), ("Arial Bold.ttf", False),
               ("DejaVuSansCondensed-Bold.ttf", False), ("/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf", False)]
_fonts = {}

def font(px, weight="Bold"):
    px = max(6, int(round(px)))
    key = (px, weight)
    if key in _fonts: return _fonts[key]
    f = None
    for name, variable in _FONT_FILES:
        try:
            f = ImageFont.truetype(name, px)
            if variable:
                try: f.set_variation_by_name(weight)
                except Exception: pass
            break
        except Exception: f = None
    if f is None: f = ImageFont.load_default()
    _fonts[key] = f
    return f


class Painter:
    """draws on a picture of the window's size, in the 1920x1080 page's units (s = window pixels per page unit)"""
    def __init__(self, img, s, ox, oy):
        self.img, self.s, self.ox, self.oy = img, s, ox, oy
        self.d = ImageDraw.Draw(img, "RGBA")
    def p(self, x, y): return (self.ox + x * self.s, self.oy + y * self.s)
    def r(self, x0, y0, x1, y1): return [self.ox + x0 * self.s, self.oy + y0 * self.s, self.ox + x1 * self.s - 1, self.oy + y1 * self.s - 1]
    def box(self, x0, y0, x1, y1, fill=None, line=None, w=2):
        self.d.rectangle(self.r(x0, y0, x1, y1), fill=fill, outline=line, width=max(1, int(round(w * self.s))) if line else 0)
    def text(self, x, y, t, px, col=CREAM, anchor="lm", weight="Bold"):
        self.d.text(self.p(x, y), t, font=font(px * self.s, weight), fill=col, anchor=anchor)
    def tlen(self, t, px, weight="Bold"): return font(px * self.s, weight).getlength(t) / self.s
    def poly(self, pts, fill): self.d.polygon([self.p(x, y) for x, y in pts], fill=fill)
    def line(self, pts, col, w): self.d.line([self.p(x, y) for x, y in pts], fill=col, width=max(1, int(round(w * self.s))), joint="curve")
    # little drawn signs (the font may not have them)
    def tri(self, x, y, size, left, col):
        h = size / 2
        self.poly([(x + h, y - h), (x - h, y), (x + h, y + h)] if left else [(x - h, y - h), (x + h, y), (x - h, y + h)], col)
    def tick(self, x, y, size, col): self.line([(x - size * .5, y), (x - size * .12, y + size * .38), (x + size * .55, y - size * .42)], col, size * .18)
    def cross(self, x, y, size, col):
        h = size * .42; w = size * .17
        self.line([(x - h, y - h), (x + h, y + h)], col, w); self.line([(x - h, y + h), (x + h, y - h)], col, w)
    def arrow(self, x, y, size, col):                                 # the logo's double arrow
        L = size * 1.25; g = size * .16; w = size * .12
        self.line([(x, y - g), (x + L * .8, y - g)], col, w); self.line([(x, y + g), (x + L * .8, y + g)], col, w)
        self.line([(x + L * .58, y - size * .42), (x + L, y), (x + L * .58, y + size * .42)], col, w)
    def wrap(self, t, px, width, weight="Regular"):
        out = []
        for para in t.split("\n"):
            words = para.split(); cur = ""
            for wd in words:
                nxt = (cur + " " + wd).strip()
                if self.tlen(nxt, px, weight) <= width or not cur: cur = nxt
                else: out.append(cur); cur = wd
            out.append(cur)
        return out


def grid_background(w, h, s, ox, oy):
    """the grid of the clip's last picture, drawn fresh at any size (with the clip's soft dark edges)"""
    img = Image.new("RGB", (w, h), (0, 0, 0)); d = ImageDraw.Draw(img)
    lw = max(1, int(round(2.6 * s))); col = (54, 54, 50)
    k0 = int((-ox / s - GX) // PITCH) - 1; k1 = int(((w - ox) / s - GX) // PITCH) + 2
    for k in range(k0, k1):
        x = ox + gx(k) * s; d.line([(x, 0), (x, h)], fill=col, width=lw)
    k0 = int((-oy / s - GY) // PITCH) - 1; k1 = int(((h - oy) / s - GY) // PITCH) + 2
    for k in range(k0, k1):
        y = oy + gy(k) * s; d.line([(0, y), (w, y)], fill=col, width=lw)
    if s > 0.6: img = img.filter(ImageFilter.GaussianBlur(0.45 * s))
    # vignette: darker towards the corners
    v = Image.new("L", (64, 36))
    v.putdata([int(255 * max(0.0, min(1.0, 1.18 - 0.62 * (((x - 31.5) / 32) ** 2 + ((y - 17.5) / 18) ** 2)))) for y in range(36) for x in range(64)])
    v = v.resize((w, h), Image.BICUBIC)
    return Image.composite(img, Image.new("RGB", (w, h)), v)


# ------------------------------------------------------------------------------------------------ the settings
def settings_text():
    """the settings list with their explanations, as play.py writes them (so this always matches the port)"""
    try:
        src = open(os.path.join(HERE, "play.py"), encoding="utf-8", errors="replace").read()
        m = re.search(r'SETTINGS_TEXT\s*=\s*"""(.*?)"""', src, re.S)
        if m: return m.group(1).replace("\\\\", "\\")
    except OSError: pass
    return ""


def parse_defs(text):
    defs = {}; last = None
    for line in text.splitlines():
        if not line.strip() or line.lstrip().startswith('#') and not line.startswith(' '): continue
        body, _, com = line.partition('#')
        if '=' in body:
            k, v = body.split('=', 1); last = k.strip().lower()
            defs[last] = {'default': v.strip(), 'desc': com.strip(), 'line': line}
        elif last and line.startswith(' ') and com: defs[last]['desc'] += " " + com.strip()
    for d in defs.values(): d['desc'] = re.sub(r"\s+", " ", d['desc'])
    return defs


def _nums(a, b, step, fmt="{:g}"):
    out = []; x = a
    while x <= b + 1e-9: out.append((fmt.format(round(x, 4)), fmt.format(round(x, 4)))); x += step
    return out

ONOFF = [("on", "ON"), ("off", "OFF")]
CATEGORIES = [
    ("GRAPHICS", [
        ("renderer", "RENDERER", [("vulkan", "VULKAN"), ("opengl", "OPENGL"), ("software", "SOFTWARE (OLD)")]),
        ("resolution", "RESOLUTION", [("1", "480 x 272  (PSP)"), ("2", "960 x 544"), ("3", "1440 x 816"), ("4", "1920 x 1088"), ("6", "2880 x 1632")]),
        ("anti_aliasing", "ANTI-ALIASING", [("off", "OFF"), ("2", "2x"), ("4", "4x"), ("8", "8x")]),
        ("texture_filter", "TEXTURE FILTER", [("on", "SMOOTH"), ("off", "SHARP (PSP)")]),
        ("frame_rate", "FRAME RATE", [("60", "60 FPS"), ("original", "ORIGINAL (20 / 30)")]),
        ("fps_cap", "FPS CAP", [("off", "OFF"), ("30", "30"), ("60", "60"), ("80", "80"), ("120", "120"), ("144", "144"), ("165", "165")]),
        ("present_timing", "PRESENT TIMING", [("held", "HELD"), ("direct", "DIRECT")]),
        ("window", "WINDOW SIZE", [("960x544", "960 x 544"), ("1280x720", "1280 x 720"), ("1600x900", "1600 x 900"), ("1920x1080", "1920 x 1080"), ("2560x1440", "2560 x 1440")]),
        ("fullscreen", "START IN FULL SCREEN", ONOFF),
        ("display", "DISPLAY", [("window", "GRAPHICS CARD"), ("copy", "COPY")]),
        ("software_drawing", "SOFTWARE DRAWING", [("auto", "AUTO"), ("always", "ALWAYS")]),
    ]),
    ("LIGHTING", [
        ("lighting", "LIGHTING", [("remaster", "REMASTER"), ("off", "PSP ORIGINAL")]),
        ("shadows", "SHADOWS", [("characters", "CHARACTERS"), ("everything", "EVERYTHING")]),
        ("ambient_occlusion", "AMBIENT OCCLUSION", ONOFF),
        ("shadow_strength", "SHADOW STRENGTH", _nums(0, 100, 5)),
        ("sunlight", "SUNLIGHT", _nums(0, 100, 5)),
        ("sun_angle", "SUN ANGLE", _nums(0, 80, 5)),
    ]),
    ("HD", [
        ("hd_textures", "HD TEXTURES", ONOFF),
        ("hd_near_match", "HD NEAR MATCH", ONOFF),
        ("hd_movies", "HD MOVIES", ONOFF),
        ("hd_texture_memory", "HD TEXTURE MEMORY (MB)", [(x, x) for x in ("512", "1024", "1536", "2048", "3072", "4096", "6144")]),
        ("text_hd", "SHARP HD TEXT", ONOFF),
    ]),
    ("CONTROLS", [
        ("mouse_camera", "MOUSE CAMERA", ONOFF),
        ("mouse_sensitivity", "MOUSE SENSITIVITY", _nums(0.1, 3.0, 0.1)),
        ("aim_swap", "PC-STYLE AIMING", ONOFF),
        ("aim_sensitivity", "AIM SENSITIVITY", _nums(0.1, 3.0, 0.1)),
        ("right_stick", "RIGHT STICK", [("camera", "CAMERA"), ("off", "OFF")]),
    ]),
    ("GAMEPLAY", [
        ("locker_capture", "LOCKER CAPTURE", ONOFF),
    ]),
    ("SYSTEM", [
        ("stutter_log", "STUTTER LOG", ONOFF),
    ]),
]


class SettingsFile:
    """port_settings.txt: read it, and change one value at a time - the rest of the file (comments, your own notes)
    stays exactly as it is"""
    def __init__(self, path, defs):
        self.path, self.defs = path, defs
        if not os.path.exists(path):
            try: open(path, "w", encoding="utf-8").write(settings_text() or SETTINGS_HEAD + "\n".join(d['line'] for d in defs.values()) + "\n")
            except OSError: pass
        self.load()
    def load(self):
        self.vals = {k: d['default'] for k, d in self.defs.items()}
        try:
            for line in open(self.path, encoding="utf-8", errors="replace"):
                body = line.split('#', 1)[0]
                if '=' in body: k, v = body.split('=', 1); self.vals[k.strip().lower()] = v.strip()
        except OSError: pass
    def get(self, k): return self.vals.get(k, self.defs.get(k, {}).get('default', ''))
    def set(self, k, v):
        self.vals[k] = v
        try: lines = open(self.path, encoding="utf-8", errors="replace").read().splitlines()
        except OSError: lines = []
        for i, line in enumerate(lines):
            body, sep, com = line.partition('#')
            if '=' in body and body.split('=', 1)[0].strip().lower() == k:
                new = f"{k} = {v}"
                if sep: new = new.ljust(max(len(body.rstrip()) + 1, len(new) + 1, 25)) + "#" + com
                lines[i] = new; break
        else:
            d = self.defs.get(k)
            if d:
                body, sep, com = d['line'].partition('#'); new = f"{k} = {v}"
                lines.append(new.ljust(max(25, len(new) + 1)) + ("#" + com if sep else ""))
            else: lines.append(f"{k} = {v}")
        try:
            with open(self.path, "w", encoding="utf-8") as f: f.write("\n".join(lines) + "\n")
            return True
        except OSError: return False

SETTINGS_HEAD = "# MGS Portable Ops PC port - settings. Change a value, save this file, start play.bat again.\n"


# ------------------------------------------------------------------------------------------------ the disc
Iso = PK.Iso


def read_sfo(data):
    out = {}
    try:
        magic, ver, kt, dt, n = struct.unpack_from("<4sIIII", data, 0)
        if magic != b"\x00PSF": return out
        for i in range(n):
            ko, fmt, ln, mx, do = struct.unpack_from("<HHIII", data, 20 + 16 * i)
            k = data[kt + ko:data.index(b"\x00", kt + ko)].decode("ascii", "replace"); v = data[dt + do:dt + do + ln]
            out[k] = int.from_bytes(v[:4], "little") if fmt == 0x0404 else v.rstrip(b"\x00").decode("utf-8", "replace")
    except Exception: pass
    return out


def find_isos():
    try: return [os.path.join(HERE, n) for n in sorted(os.listdir(HERE)) if n.lower().endswith(".iso")]
    except OSError: return []


def eboot_ok(path):
    try:
        with open(path, "rb") as f: return f.read(4) == b"\x7fELF" and os.path.getsize(path) > 1_000_000
    except OSError: return False


EXTRA_PAK_DIRS = []      # folders with HD packs that IMPORT FOLDER found elsewhere (installed from there, not copied)


def hd_packs():
    found = {}
    for d in [PAK_DIR, os.path.join(HERE, "packs"), HERE] + EXTRA_PAK_DIRS:            # (pak\ first; a copy elsewhere with the same name is the same pack)
        for p in glob.glob(os.path.join(d, "MGSPO-HD-*.pak")): found.setdefault(os.path.basename(p).lower(), p)
    return sorted(found.values())


def find_bulzome():
    """the folder of TheGreatBulzome's pack, if it is installed (its textures.ini names him)"""
    for top in ("HD textures", "HD Textures", "TEXTURES", "textures"):
        t = os.path.join(HERE, top)
        try: names = os.listdir(t)
        except OSError: continue
        for n in sorted(names, key=lambda x: x.upper() != "ULUS10202"):
            ini = os.path.join(t, n, "textures.ini")
            try:
                with open(ini, encoding="utf-8-sig", errors="replace") as f: head = f.read(2000)
            except OSError: continue
            if "bulzome" in head.lower(): return os.path.join(t, n)
    return None


def patch_bulzome(folder):
    """the two small additions the port needs in his textures.ini (his original is kept as textures.ini.original):
    his see-through pictures were saved with their colours darkened by their see-through-ness (the port undoes that
    as they load), and one more font letter the game also uses. -> True if the file is (now) right"""
    ini = os.path.join(folder, "textures.ini")
    try:
        raw = open(ini, "rb").read(); txt = raw.decode("utf-8-sig", errors="replace")
    except OSError: return False
    nl = "\r\n" if "\r\n" in txt else "\n"; lines = txt.split(nl); changed = False
    if not any(l.split("#", 1)[0].strip().lower().startswith("premultiplied") for l in lines):
        at = next((i for i, l in enumerate(lines) if l.strip().lower().startswith("hash")), None)
        if at is None: at = next((i for i, l in enumerate(lines) if l.strip().lower() == "[options]"), len(lines) - 1)
        lines[at + 1:at + 1] = ["# (added for the PC port: this pack's see-through pictures were saved with their colours darkened by their see-through-ness; the port undoes it as they load)",
                                "premultiplied = true"]; changed = True
    extra = "099d3d00a01ffe647c19c66f = fonts/09da9480a01ffe6492eeb1d4.png"
    if extra.split("=")[0].strip() not in txt and os.path.exists(os.path.join(folder, "fonts", "09da9480a01ffe6492eeb1d4.png")):
        at = next((i for i, l in enumerate(lines) if l.startswith("09db6080a01ffe6492eeb1d4")), None)
        if at is None: lines.append(extra)
        else: lines.insert(at + 1, extra)
        changed = True
    if not changed: return True
    try:
        bak = ini + ".original"
        if not os.path.exists(bak): open(bak, "wb").write(raw)
        out = nl.join(lines).encode("utf-8")
        open(ini, "wb").write((b"\xef\xbb\xbf" if raw.startswith(b"\xef\xbb\xbf") else b"") + out)
        return True
    except OSError: return False


def pack_installed(pak):
    """every file of an (unlocked) pack is in place"""
    for it in pak.items:
        p = os.path.join(HERE, *it["path"].split("/"))
        try:
            if os.path.getsize(p) != it["size"]: return False
        except OSError: return False
    return True


def check_everything(iso_path):
    """-> rows (label, state 'ok' / 'bad' / 'warn' / 'info', text), ready (bool), verdict text, disc key (or None),
    HD packs not installed yet"""
    rows = []; ready = True; why = []; key = None; todo = []
    def row(label, st, text):
        nonlocal ready
        rows.append((label, st, text))
        if st == "bad": ready = False
    if not iso_path or not os.path.exists(iso_path):
        row("DISC IMAGE", "bad", "No disc image chosen - choose BROWSE and pick your game's .iso"); why.append("choose your .iso")
    else:
        row("DISC IMAGE", "ok", os.path.basename(iso_path))
        try:
            iso = Iso(iso_path)
            row("IMAGE TYPE", "ok", "ISO 9660 disc image, %.2f GB" % (os.path.getsize(iso_path) / 2**30))
            sfo = read_sfo(iso.read("PSP_GAME/PARAM.SFO") or b"")
            did = sfo.get("DISC_ID", "")
            if did == GAME_ID: row("GAME ID", "ok", f"{did}  (Metal Gear Solid: Portable Ops, USA)")
            else: row("GAME ID", "bad", f"{did or 'unknown'} - this port is made for {GAME_ID} (the USA disc)"); why.append("wrong disc")
            row("TITLE", "ok" if sfo.get("TITLE") else "warn", (sfo.get("TITLE") or "not found").replace("\n", " ") + (f"   version {sfo['DISC_VERSION']}" if sfo.get("DISC_VERSION") else ""))
            n = iso.count_files()
            has_boot = iso.find("PSP_GAME/SYSDIR/EBOOT.BIN") is not None
            row("DISC FILES", "ok" if has_boot and n > 50 else "bad", f"{n} files" + ("" if has_boot else " - the game's program (EBOOT.BIN) is missing"))
            iso.close()
            if did == GAME_ID and has_boot:
                key = PK.disc_key(iso_path)
                if os.path.exists(ART_PAK):
                    try: pk = PK.Pak(ART_PAK); good = pk.opens_with(key); pk.close()
                    except Exception: good = False
                    if good: row("DISC SIGNATURE", "ok", "matches the USA disc")
                    else: row("DISC SIGNATURE", "warn", "this copy of the disc differs from the known one - the art stays locked")
        except Exception as e:
            row("IMAGE TYPE", "bad", f"can't be read: {e}"); why.append("the image can't be read")
    eb = os.path.join(HERE, "EBOOT.BIN.dec")
    if eboot_ok(eb): row("GAME CODE", "ok", "EBOOT.BIN.dec - ready (%.1f MB)" % (os.path.getsize(eb) / 2**20))
    else:
        tool = os.path.exists(os.path.join(HERE, "pspdecrypt.exe"))
        row("GAME CODE", "bad", "EBOOT.BIN.dec missing - " + ("choose BUILD (uses pspdecrypt.exe)" if tool else
            "put pspdecrypt.exe in this folder, then choose BUILD")); why.append("game code missing")
    dll = os.path.join(HERE, "psp_core.dll")
    row("FAST CORE", "ok" if os.path.exists(dll) else "warn", "psp_core.dll found" if os.path.exists(dll) else "psp_core.dll missing - the game runs, but very slowly (build_core.bat makes it)")
    ms = os.path.join(HERE, "memstick")
    try: nsav = len([n for n in os.listdir(os.path.join(ms, "PSP", "SAVEDATA")) if n.upper().startswith(GAME_ID)])
    except OSError: nsav = 0
    row("SAVE DATA", "ok", f"{nsav} game save folder(s) on the memory stick" if nsav else "none yet (made when you save in the game)")
    paks = hd_packs()
    if not paks: row("HD PACKS", "info", "none yet (optional) - put the MGSPO-HD-*.pak files from the release page in the folder pak")
    else:
        done = 0; locked = 0; gb = 0.0
        for p in paks:
            try:
                pk = PK.Pak(p)
                if pk.opens_with(key):
                    pk.unlock(key)
                    if pack_installed(pk): done += 1
                    else: todo.append(p); gb += pk.total() / 2**30
                else: locked += 1
                pk.close()
            except Exception: locked += 1
        if locked and not key: row("HD PACKS", "info", f"{len(paks)} found - they open once your disc is verified")
        elif locked: row("HD PACKS", "warn", f"{locked} of {len(paks)} don't open with this disc (another copy, or a broken download)")
        elif todo: row("HD PACKS", "info", f"{done} of {len(paks)} installed - choose INSTALL HD PACKS ({gb:.1f} GB)")
        else: row("HD PACKS", "ok", f"all {len(paks)} installed - our upscaled movies, cutscene drawings, menus and HUD")
    bz = find_bulzome()
    if bz:
        fixed = patch_bulzome(bz)
        row("BULZOME PACK", "ok" if fixed else "warn", ("TheGreatBulzome's HD texture pack is installed - thank you, Bulzome!" if fixed else
            "TheGreatBulzome's pack is installed, but its textures.ini couldn't be updated for the port"))
    else:
        row("BULZOME PACK", "info", "TheGreatBulzome's HD textures (optional) - GET BULZOME'S PACK, unzip it into HD textures\\ULUS10202")
    verdict = "READY FOR THE MISSION" if ready else "NOT READY: " + ", ".join(why).upper()
    return rows, ready, verdict, key, todo


def make_game_code(iso_path):
    """take EBOOT.BIN off the disc image and decrypt it with pspdecrypt.exe -> EBOOT.BIN.dec. -> (ok, message)"""
    tool = os.path.join(HERE, "pspdecrypt.exe")
    if not os.path.exists(tool): return False, "pspdecrypt.exe is not in this folder"
    try:
        iso = Iso(iso_path); data = iso.read("PSP_GAME/SYSDIR/EBOOT.BIN"); iso.close()
    except Exception as e: return False, f"the disc image can't be read: {e}"
    if not data: return False, "EBOOT.BIN is not on the disc image"
    src = os.path.join(HERE, "EBOOT.BIN")
    try: open(src, "wb").write(data)
    except OSError as e: return False, f"can't write EBOOT.BIN: {e}"
    before = set(os.listdir(HERE))
    try:
        r = subprocess.run([tool, "EBOOT.BIN"], cwd=HERE, capture_output=True, text=True, timeout=120,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        out = (r.stdout + r.stderr).strip()
    except Exception as e: return False, f"pspdecrypt didn't run: {e}"
    dst = os.path.join(HERE, "EBOOT.BIN.dec")
    if not eboot_ok(dst):                                    # (another name? take the new decrypted file)
        for n in set(os.listdir(HERE)) - before:
            p = os.path.join(HERE, n)
            if os.path.isfile(p) and eboot_ok(p): os.replace(p, dst); break
    if eboot_ok(dst): return True, "EBOOT.BIN.dec made"
    return False, "pspdecrypt didn't make it: " + (out.splitlines()[-1] if out else "no message")


# ------------------------------------------------------------------------------------------------ controller
def controller_reader():
    """-> read() giving a set of names ('up','down','left','right','ok','back','lb','rb') held now, or None"""
    import ctypes as ct
    xi = None
    for n in ("xinput1_4", "xinput1_3", "xinput9_1_0"):
        try: xi = ct.WinDLL(n); break
        except Exception: pass
    class GP(ct.Structure): _fields_ = [("b", ct.c_ushort), ("lt", ct.c_ubyte), ("rt", ct.c_ubyte), ("lx", ct.c_short), ("ly", ct.c_short), ("rx", ct.c_short), ("ry", ct.c_short)]
    class ST(ct.Structure): _fields_ = [("pk", ct.c_uint), ("g", GP)]
    sdl = None; cur = {"c": None, "scan": 0.0}
    p = os.path.join(HERE, "SDL2.dll")
    if os.path.exists(p):
        try:
            sdl = ct.CDLL(p)
            if sdl.SDL_Init(0x2000) != 0: sdl = None
            else:
                sdl.SDL_GameControllerOpen.restype = ct.c_void_p; sdl.SDL_GameControllerOpen.argtypes = [ct.c_int]
                sdl.SDL_GameControllerGetButton.argtypes = [ct.c_void_p, ct.c_int]; sdl.SDL_GameControllerGetButton.restype = ct.c_uint8
                sdl.SDL_GameControllerGetAxis.argtypes = [ct.c_void_p, ct.c_int]; sdl.SDL_GameControllerGetAxis.restype = ct.c_int16
                sdl.SDL_GameControllerGetAttached.argtypes = [ct.c_void_p]
        except Exception: sdl = None
    if xi is None and sdl is None: return None
    def from_x(b, lx, ly):
        h = set()
        if b & 1 or ly > 16000: h.add("up")
        if b & 2 or ly < -16000: h.add("down")
        if b & 4 or lx < -16000: h.add("left")
        if b & 8 or lx > 16000: h.add("right")
        if b & 0x1000 or b & 0x10: h.add("ok")
        if b & 0x2000: h.add("back")
        if b & 0x100: h.add("lb")
        if b & 0x200: h.add("rb")
        if b & 0x8000: h.add("y")
        return h
    def read():
        if xi is not None:
            for i in range(4):
                st = ST()
                if xi.XInputGetState(i, ct.byref(st)) == 0: return from_x(st.g.b, st.g.lx, st.g.ly)
        if sdl is None: return None
        sdl.SDL_GameControllerUpdate()
        c = cur["c"]
        if c is not None and not sdl.SDL_GameControllerGetAttached(c): c = cur["c"] = None
        if c is None:
            if time.time() - cur["scan"] < 1.0: return None
            cur["scan"] = time.time()
            for i in range(sdl.SDL_NumJoysticks()):
                if sdl.SDL_IsGameController(i): c = cur["c"] = sdl.SDL_GameControllerOpen(i); break
            if c is None: return None
        B = lambda k: sdl.SDL_GameControllerGetButton(c, k)
        b = (B(11) and 1) | (B(12) and 2) | (B(13) and 4) | (B(14) and 8) | (B(0) and 0x1000) | (B(6) and 0x10) | (B(1) and 0x2000) | (B(9) and 0x100) | (B(10) and 0x200) | (B(3) and 0x8000)
        return from_x(b, sdl.SDL_GameControllerGetAxis(c, 0), -max(-32767, sdl.SDL_GameControllerGetAxis(c, 1)))
    return read


# ------------------------------------------------------------------------------------------------ sounds
SND = {}          # the game's own menu sounds (WAV files in the temp folder), once the file check is good
PCM = {}          # ... the same as samples, for the mixer
SND_ON = [True]   # the MENU SOUND box (menu sounds and music)
MUSIC = {}        # the hub music: its decoder and loop points
MIX = [None]      # the mixer, once there is something to play


class Mixer:
    """the launcher's sound: the hub music, looping where the game loops it, with the menu sounds on top. Windows'
    simple sound player (winsound) plays one sound at a time - this mixes them and feeds the speakers (winmm)."""
    BLK = 1024                                                     # stereo frames per block (23 ms)
    def __init__(self):
        import ctypes as ct, array
        self.ct, self.array = ct, array; self.w = ct.WinDLL("winmm")
        class WFX(ct.Structure):
            _fields_ = [("tag", ct.c_ushort), ("ch", ct.c_ushort), ("rate", ct.c_uint), ("bps", ct.c_uint), ("align", ct.c_ushort), ("bits", ct.c_ushort), ("cb", ct.c_ushort)]
        class HDR(ct.Structure):
            _fields_ = [("data", ct.c_void_p), ("len", ct.c_uint), ("rec", ct.c_uint), ("user", ct.c_size_t), ("flags", ct.c_uint),
                        ("loops", ct.c_uint), ("next", ct.c_void_p), ("res", ct.c_size_t)]
        self.HDR = HDR; self.h = ct.c_void_p(); fmt = WFX(1, 2, 44100, 176400, 4, 16, 0)
        if self.w.waveOutOpen(ct.byref(self.h), ct.c_uint(0xFFFFFFFF), ct.byref(fmt), 0, 0, 0) != 0: raise OSError("no sound device")
        self.busy = []; self.sfx = []; self.music = None; self.mvol = 0.0; self.mtarget = 0.0; self.alive = True
        self.lock = threading.Lock(); threading.Thread(target=self._run, daemon=True).start()
    def play(self, mono):
        with self.lock: self.sfx.append([mono, 0])
    def start_music(self, dec, offset, loop_st, loop_en, level=0.7):
        with self.lock: self.music = {"dec": dec, "pos": offset, "st": loop_st, "en": loop_en}; self.mvol = 0.0; self.mtarget = level
    def fade_music(self): self.mtarget = 0.0
    def _block(self):
        n = self.BLK; out = self.array.array("h", bytes(n * 4)); on = SND_ON[0]
        m = self.music
        if m and on and (self.mvol > 0.001 or self.mtarget > 0):
            d = m["dec"]; got = bytearray(); pos = m["pos"]
            while len(got) < n * 4:
                end = min(m["en"], pos + n - len(got) // 4)
                if d.need < (end + 44100 * 4) * 4: d.need = (end + 44100 * 4) * 4
                with d.lock: piece = bytes(d.pcm[pos * 4:end * 4])
                if len(piece) < (end - pos) * 4: break                     # (not decoded that far yet)
                got += piece; pos = end
                if pos >= m["en"]: pos = m["st"]                            # the game's loop point
            if len(got) == n * 4:
                m["pos"] = pos; a = self.array.array("h"); a.frombytes(bytes(got)); v0 = self.mvol
                v1 = v0 + max(-0.02, min(0.004, self.mtarget - v0))         # (fades in over ~4 s, out over ~1 s)
                self.mvol = v1
                for i in range(n * 2): out[i] = int(a[i] * (v0 + (v1 - v0) * i / (n * 2)))
        with self.lock:
            live = []
            for e in self.sfx:
                if not on: continue
                s_, p = e; k = min(n, len(s_) - p)
                for i in range(k):
                    v = s_[p + i]; j = 2 * i
                    l = out[j] + v; r = out[j + 1] + v
                    out[j] = 32767 if l > 32767 else -32768 if l < -32768 else l
                    out[j + 1] = 32767 if r > 32767 else -32768 if r < -32768 else r
                e[1] = p + k
                if e[1] < len(s_): live.append(e)
            self.sfx = live
        return out.tobytes()
    def _run(self):
        ct = self.ct
        while self.alive:
            keep = []
            for hdr, buf in self.busy:
                if hdr.flags & 1: self.w.waveOutUnprepareHeader(self.h, ct.byref(hdr), ct.sizeof(hdr))
                else: keep.append((hdr, buf))
            self.busy = keep
            while len(self.busy) < 4 and self.alive:
                blk = self._block(); buf = ct.create_string_buffer(blk, len(blk)); hdr = self.HDR()
                hdr.data = ct.cast(buf, ct.c_void_p); hdr.len = len(blk)
                self.w.waveOutPrepareHeader(self.h, ct.byref(hdr), ct.sizeof(hdr))
                self.w.waveOutWrite(self.h, ct.byref(hdr), ct.sizeof(hdr)); self.busy.append((hdr, buf))
            time.sleep(0.005)
    def close(self):
        self.alive = False
        try: time.sleep(0.02); self.w.waveOutReset(self.h); self.w.waveOutClose(self.h)
        except Exception: pass


def load_game_sounds(iso_path):
    """the game's Mother Base menu sounds and music, read from the disc image (sounds: also as WAV files, for when
    the mixer can't start)"""
    got = PK.game_sounds_pcm(iso_path)
    if "back" not in got and "move" in got: got["back"] = got["move"]
    PCM.update(got)
    for name, a in got.items():
        p = os.path.join(tempfile.gettempdir(), f"mgspo_ui_{name}.wav")
        try:
            with open(p, "wb") as f: f.write(PK.wav_bytes(a.tobytes(), 44100))
            SND[name] = p
        except OSError: pass
    try:
        at3 = PK.game_music(iso_path)
        if at3:
            import psp_atrac                                          # (the game's own music decoder: PyAV or ffmpeg)
            info = psp_atrac.parse_riff(at3)
            if info and info.get("loops"):
                st, en = info["loops"][0]
                MUSIC.update(dec=psp_atrac.WholeDecode(at3), offset=info["offset"] if info.get("total") else 0, st=st, en=en)
    except Exception: pass
    if MIX[0] is None and (PCM or MUSIC):
        try: MIX[0] = Mixer()
        except Exception: MIX[0] = None


def play(name):
    if not SND_ON[0]: return
    if MIX[0] is not None and name in PCM: MIX[0].play(PCM[name])
    else: play_sound(SND.get(name))


def start_music():
    if MIX[0] is not None and MUSIC and MIX[0].music is None:
        MIX[0].start_music(MUSIC["dec"], MUSIC["offset"], MUSIC["st"], MUSIC["en"])


def play_sound(path):
    if not path: return
    try:
        import winsound
        winsound.PlaySound(path, winsound.SND_FILENAME | winsound.SND_ASYNC | winsound.SND_NODEFAULT)
    except Exception: pass


# ------------------------------------------------------------------------------------------------ the screens
TEX_TOPS = ("hd textures", "textures")


def _walk_dirs(top, depth):
    """top and its folders, down to depth levels"""
    out = [top]; lvl = [top]
    for _ in range(depth):
        nxt = []
        for d in lvl:
            try: nxt += [os.path.join(d, n) for n in sorted(os.listdir(d)) if os.path.isdir(os.path.join(d, n)) and not n.startswith(".")]
            except OSError: pass
        out += nxt; lvl = nxt
    return out


def scan_import(folder):
    """what a chosen folder holds: the disc image, HD texture packs, the locked HD packs, the game code, pspdecrypt.
    -> dict(iso, tex=[(source folder, place in the port's folder)], paks=[folders], files=[(source, place)])"""
    found = {"iso": None, "tex": [], "paks": [], "files": []}
    here_tex = {os.path.normcase(os.path.abspath(os.path.join(HERE, t))) for t in ("HD textures", "TEXTURES")}
    dirs = _walk_dirs(folder, 2); isos = []
    for d in dirs:
        try: names = os.listdir(d)
        except OSError: continue
        low = {n.lower(): n for n in names}
        isos += [os.path.join(d, n) for n in names if n.lower().endswith(".iso")]
        if any(n.lower().startswith("mgspo-hd-") and n.lower().endswith(".pak") for n in names): found["paks"].append(d)
        if "launcher_assets.pak" in low and not os.path.exists(os.path.join(PAK_DIR, "launcher_assets.pak")):
            found["files"].append((os.path.join(d, low["launcher_assets.pak"]), os.path.join(PAK_DIR, "launcher_assets.pak")))
        for want in ("eboot.bin.dec", "pspdecrypt.exe"):
            if want in low and not os.path.exists(os.path.join(HERE, low[want])):
                found["files"].append((os.path.join(d, low[want]), os.path.join(HERE, low[want])))
    # texture packs: an "HD textures" folder (all of it), or pack folders themselves (each has a textures.ini)
    taken = []
    def under(p): return any(os.path.normcase(p).startswith(os.path.normcase(t) + os.sep) or os.path.normcase(p) == os.path.normcase(t) for t in taken)
    for d in dirs:
        if under(d) or os.path.normcase(os.path.abspath(d)) in here_tex: continue
        if os.path.basename(d).lower() in TEX_TOPS:
            found["tex"].append((d, os.path.join(HERE, "HD textures"))); taken.append(d)
    for d in dirs:
        nd = os.path.normcase(os.path.abspath(d))
        if under(d) or any(nd == t or nd.startswith(t + os.sep) for t in here_tex): continue   # (not the port's own HD textures)
        if os.path.isfile(os.path.join(d, "textures.ini")):
            found["tex"].append((d, os.path.join(HERE, "HD textures", os.path.basename(d.rstrip("\\/")) or "pack"))); taken.append(d)
    # the disc image: the one that is this game, if there are several
    for p in isos:
        try:
            iso = Iso(p); sfo = read_sfo(iso.read("PSP_GAME/PARAM.SFO") or b""); iso.close()
            if sfo.get("DISC_ID") == GAME_ID: found["iso"] = p; break
        except Exception: pass
    if found["iso"] is None and isos: found["iso"] = isos[0]
    return found


def copy_tree(src, dst, progress=None, done0=0):
    """copies a folder into another (merging; files already there with the same size are left). -> bytes copied"""
    import shutil
    done = done0
    for root, ds, fs in os.walk(src):
        ds.sort(); rel = os.path.relpath(root, src); out = os.path.join(dst, rel) if rel != "." else dst
        os.makedirs(out, exist_ok=True)
        for f in sorted(fs):
            a, b = os.path.join(root, f), os.path.join(out, f)
            try:
                n = os.path.getsize(a)
                if not (os.path.exists(b) and os.path.getsize(b) == n): shutil.copyfile(a, b)
                done += n
                if progress: progress(done)
            except OSError: pass
    return done - done0


def tree_size(src):
    n = 0
    for root, ds, fs in os.walk(src):
        for f in fs:
            try: n += os.path.getsize(os.path.join(root, f))
            except OSError: pass
    return n


class Launcher:
    def __init__(self):
        self.defs = parse_defs(settings_text())
        self.settings = SettingsFile(SETTINGS_FILE, self.defs)
        self.cfg = {}
        try: self.cfg = json.load(open(CONFIG, encoding="utf-8"))
        except Exception: pass
        EXTRA_PAK_DIRS[:] = [d for d in self.cfg.get("pak_dirs", []) if os.path.isdir(d)]
        iso = self.cfg.get("iso")
        if not iso or not os.path.exists(iso):
            l = find_isos(); iso = l[0] if l else None
        self.iso = iso
        self.disc = None                       # (rows, ready, verdict) of the last check
        self.busy = None                       # text while something works in the background
        self.page = "menu"; self.sel = {"menu": 0, "cat": 0, "row": 0, "disc": 0}; self.focus = "cat"
        self.hits = []; self.hover = None; self.msg = None
        self.sprite = None; self.art = None; self.art_new = False   # (the FOXHOUND art: only once the file check is good)
        self.recheck()

    # -- background work
    def recheck(self, then=None):
        def work():
            self.busy = "CHECKING..."
            try:
                r = check_everything(self.iso)
                if self.art is None and r[3] and r[1]:                    # (the art and the game's sounds: only once
                    self.unlock_art(r[3])                                 #  every check is good)
                    if not SND: load_game_sounds(self.iso)
                self.disc = r
            finally: self.busy = None; self.dirty = True
            if then: then()
        self.dirty = True
        threading.Thread(target=work, daemon=True).start()

    def unlock_art(self, key):
        """open launcher_assets.pak with the disc's key: the logo, the intro's pictures and its sound"""
        try:
            pk = PK.Pak(ART_PAK)
            if not pk.opens_with(key): pk.close(); return
            items = pk.unlock(key); got = {it["path"]: pk.read(it) for it in items}; pk.close()
        except Exception: return
        frames = [got[k] for k in sorted(k for k in got if k.lower().startswith("intro/") and k.lower().endswith(".jpg"))]
        wav = None
        if "intro.wav" in got:
            try:
                wav = os.path.join(tempfile.gettempdir(), "mgspo_intro.wav")
                with open(wav, "wb") as f: f.write(got["intro.wav"])
            except OSError: wav = None
        try: logo = Image.open(io.BytesIO(got["logo.png"])).convert("RGBA")
        except Exception: return
        self.art = {"frames": frames, "wav": wav}; self.sprite = logo; self.art_new = True

    def import_folder(self, folder):
        """IMPORT FOLDER: everything the port can use in one folder - the disc image (used where it is), HD texture
        packs (copied into "HD textures"), the locked HD packs (installed from where they are), the game code"""
        import shutil
        def work():
            self.busy = "LOOKING THROUGH THE FOLDER..."; self.dirty = True; got = []; f = None
            try:
                f = scan_import(folder)
                if f["iso"]:
                    self.iso = os.path.normpath(f["iso"]); self.cfg["iso"] = self.iso; got.append("DISC IMAGE")
                for src, dst in f["files"]:
                    try: os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copyfile(src, dst); got.append(os.path.basename(dst).upper())
                    except OSError: pass
                if f["paks"]:
                    dirs = [d for d in self.cfg.get("pak_dirs", []) if os.path.isdir(d)]
                    for d in f["paks"]:
                        if d not in dirs: dirs.append(d)
                    self.cfg["pak_dirs"] = dirs; EXTRA_PAK_DIRS[:] = dirs
                    got.append("HD PACKS")
                if f["tex"]:
                    tot = sum(tree_size(s_) for s_, _ in f["tex"]) or 1; done = 0
                    for s_, d_ in f["tex"]:
                        def prog(n):
                            self.busy = f"COPYING HD TEXTURES  {100 * n / tot:.0f}%"; self.dirty = True
                        done += copy_tree(s_, d_, prog, done)
                    got.append("HD TEXTURES")
                self.save_cfg()
                self.msg = ("FOUND: " + ", ".join(got)) if got else "NOTHING THE PORT USES WAS FOUND IN THAT FOLDER"
            except Exception as e:
                self.msg = f"IMPORT STOPPED: {e}".upper()[:110]
            self.busy = None
            def after():                                              # (the HD packs: installed straight away, if the disc is right)
                if self.disc and self.disc[3] and self.disc[4] and not self.busy: self.install_packs()
            self.recheck(after if f and f.get("paks") else None)
        threading.Thread(target=work, daemon=True).start()

    def install_packs(self, done_cb=None):
        def work():
            key = self.disc[3] if self.disc else None; todo = list(self.disc[4]) if self.disc else []
            try:
                tot = 0; paks = []
                for p in todo:
                    pk = PK.Pak(p); pk.unlock(key); paks.append(pk); tot += pk.total()
                done = 0
                for i, pk in enumerate(paks):
                    def prog(d, name, i=i):
                        self.busy = f"INSTALLING HD PACKS  {100 * d / max(1, tot):.0f}%   ({i + 1} / {len(paks)})"; self.dirty = True
                    done += pk.extract(HERE, prog, done); pk.close()
                self.msg = "HD PACKS INSTALLED - YOU CAN DELETE THE .PAK FILES NOW IF YOU LIKE"
            except Exception as e:
                self.msg = f"INSTALL STOPPED: {e}".upper()[:110]
            self.busy = None; self.recheck()
        self.busy = "INSTALLING HD PACKS..."; self.dirty = True
        threading.Thread(target=work, daemon=True).start()

    def save_cfg(self):
        try: json.dump(self.cfg, open(CONFIG, "w", encoding="utf-8"), indent=1)
        except OSError: pass

    # -- what is on each page
    def menu_items(self):
        snap = next((n for n in SNAPS if os.path.exists(os.path.join(HERE, n))), None)
        det = ""
        if snap:
            t = time.localtime(os.path.getmtime(os.path.join(HERE, snap)))
            det = ("QUICK SAVE  " if snap == SNAPS[0] else "SNAPSHOT  ") + time.strftime("%d %b  %H:%M", t).upper()
        return [("CONTINUE MISSION", det or "TITLE SCREEN", "continue"), ("NEW MISSION", "FROM POWER-ON", "new"),
                ("DISC CHECK", "", "disc"), ("SETTINGS", "", "settings"), ("EXIT", "", "exit")]

    def disc_buttons(self):
        b = [("BROWSE", "browse"), ("IMPORT FOLDER", "import")]
        if self.disc and any(r[0] == "GAME CODE" and r[1] == "bad" for r in self.disc[0]): b.append(("BUILD", "make"))
        if self.disc and self.disc[3] and self.disc[4]: b.append(("INSTALL HD PACKS", "install"))
        if self.disc and any(r[0] == "BULZOME PACK" and r[1] == "info" for r in self.disc[0]): b.append(("GET BULZOME'S PACK", "bulzome"))
        b += [("CHECK AGAIN", "recheck"), ("BACK", "back")]
        return b

    # -- drawing
    def place_logo(self, img, s, ox, oy, scale, x, y, alpha=1.0):
        if self.sprite is None: return
        w, h = self.sprite.size
        sw, sh = max(1, int(w * scale * s)), max(1, int(h * scale * s))
        sp = self.sprite.resize((sw, sh), Image.LANCZOS)
        if alpha < 1: sp.putalpha(sp.getchannel("A").point(lambda a: int(a * alpha)))
        img.paste(sp, (int(ox + x * s), int(oy + y * s)), sp)

    def logo_pos(self, page):
        w, h = self.sprite.size if self.sprite else (824, 374)
        if page == "menu": sc = 0.62; return sc, (VW - w * sc) / 2, 88
        sc = 0.34; return sc, 62, 34

    def draw_page(self, img, s, ox, oy, page=None, alpha_ui=1.0):
        page = page or self.page
        self.hits = []
        P = Painter(img, s, ox, oy)
        if self.sprite is not None:
            sc, lx, ly = self.logo_pos(page); self.place_logo(img, s, ox, oy, sc, lx, ly)
        elif page == "menu":                                            # (no art until the disc is verified: plain title)
            P.text(VW / 2, 168, "METAL GEAR SOLID", 36, DIM, "mm"); P.text(VW / 2, 236, "PORTED OPS", 104, CREAM, "mm")
        else:
            P.text(gx(1), gy(1.6), "METAL GEAR SOLID", 18, DIM, "lm"); P.text(gx(1), gy(2.6), "PORTED OPS", 40, CREAM, "lm")
        if page == "menu": self.draw_menu(P)
        elif page == "settings": self.draw_settings(P)
        elif page == "disc": self.draw_disc(P)
        self.draw_footer(P, page)

    def hit(self, P, x0, y0, x1, y1, act): self.hits.append((P.ox + x0 * P.s, P.oy + y0 * P.s, P.ox + x1 * P.s, P.oy + y1 * P.s, act))

    def item_box(self, P, x0, y0, x1, y1, label, detail, on, px=34, arrow=True):
        if on:
            P.box(x0, y0, x1, y1, fill=RED + (255,))
            P.box(x0 + 5, y0 + 5, x1 - 5, y1 - 5, line=(255, 120, 100, 120), w=1.5)
            if arrow: P.arrow(x0 - 64, (y0 + y1) / 2, 34, CREAM)
        else:
            P.box(x0, y0, x1, y1, fill=(0, 0, 0, 225), line=FAINT + (255,), w=2)
        P.text(x0 + 28, (y0 + y1) / 2 + 1, label, px, CREAM)
        if detail: P.text(x1 - 24, (y0 + y1) / 2 + 2, detail, px * .52, CREAM if on else DIM, "rm", "SemiBold")

    def draw_menu(self, P):
        items = self.menu_items(); x0, x1 = gx(19), gx(39); y = gy(11)
        for i, (label, det, act) in enumerate(items):
            on = self.sel["menu"] == i
            self.item_box(P, x0, y, x1, y + PITCH * 2, label, det, on)
            self.hit(P, x0, y, x1, y + PITCH * 2, ("menu", i)); y += PITCH * 2.5
        # disc state, under the menu
        y += PITCH * 0.6
        if self.busy: P.text(VW / 2, y + 14, "CHECKING THE DISC...", 22, DIM, "mm")
        elif self.disc:
            ok = self.disc[1]; t = (GAME_ID + "  DISC VERIFIED") if ok else self.disc[2]
            w = P.tlen(t, 22); cx = VW / 2 - w / 2
            (P.tick if ok else P.cross)(cx - 22, y + 14, 22, OKC if ok else BAD)
            P.text(cx, y + 14, t, 22, CREAM if ok else BAD)
        if self.msg: P.text(VW / 2, y + 52, self.msg, 22, WARN, "mm")
        if self.disc and any(r[0] == "BULZOME PACK" and r[1] == "ok" for r in self.disc[0]):
            P.text(VW / 2, gy(29.6), "HD TEXTURE PACK BY THEGREATBULZOME", 18, DIM, "mm", "SemiBold")
        # the MENU SOUND box (M / controller Y)
        bx, by = gx(1), gy(29.6); on = SND_ON[0]
        P.box(bx, by - 13, bx + 26, by + 13, fill=(0, 0, 0, 230), line=(CREAM if on else DIM) + (255,), w=2)
        if on: P.tick(bx + 13, by, 18, CREAM)
        P.text(bx + 40, by + 1, "MENU SOUND", 20, CREAM if on else DIM, "lm", "SemiBold")
        P.text(bx + 40 + P.tlen("MENU SOUND", 20, "SemiBold") + 14, by + 1, "(M)", 16, FAINT, "lm", "SemiBold")
        self.hit(P, bx - 8, by - 20, bx + 220, by + 20, ("snd", 0))

    def header(self, P, title, sub):
        P.text(gx(12), gy(2) + 6, title, 64, CREAM, "lm")
        tw = P.tlen(title, 64)
        P.box(gx(12), gy(3) + 8, gx(12) + tw, gy(3) + 14, fill=RED + (255,))
        P.text(gx(12), gy(4) + 6, sub, 22, DIM, "lm", "SemiBold")

    def draw_settings(self, P):
        self.header(P, "SETTINGS", "Saved to port_settings.txt as you change them. They are used the next time the game starts.")
        cx0, cx1, top = gx(1), gx(10), gy(6)
        for i, (name, rows) in enumerate(CATEGORIES):
            y = top + i * PITCH * 2
            on = self.sel["cat"] == i
            if on and self.focus == "cat": self.item_box(P, cx0, y, cx1, y + PITCH * 1.75, name, "", True, 28, arrow=False)
            elif on:
                P.box(cx0, y, cx1, y + PITCH * 1.75, fill=DRED + (255,)); P.text(cx0 + 28, y + PITCH * .875 + 1, name, 28, CREAM)
            else: self.item_box(P, cx0, y, cx1, y + PITCH * 1.75, name, "", False, 28)
            self.hit(P, cx0, y, cx1, y + PITCH * 1.75, ("cat", i))
        rows = CATEGORIES[self.sel["cat"]][1]
        rx0, rx1 = gx(11), gx(59); rh = PITCH * 1.5
        P.box(rx0, top, rx1, top + rh * len(rows) + 20, fill=(0, 0, 0, 225), line=FAINT + (255,), w=2)
        for j, (key, label, choices) in enumerate(rows):
            y = top + 10 + j * rh; on = self.focus == "rows" and self.sel["row"] == j
            if on: P.box(rx0 + 8, y, rx1 - 8, y + rh - 4, fill=RED + (255,))
            P.text(rx0 + 30, y + rh / 2 - 1, label, 26, CREAM)
            v = self.settings.get(key); lab = next((d for c, d in choices if c == v.lower()), v.upper() or "-")
            vx0, vx1 = rx1 - 470, rx1 - 30; cy = y + rh / 2 - 2
            P.tri(vx0 + 14, cy, 18, True, CREAM if on else DIM); P.tri(vx1 - 14, cy, 18, False, CREAM if on else DIM)
            dflt = self.defs.get(key, {}).get('default', '').lower()
            P.text((vx0 + vx1) / 2, cy + 1, lab, 26, CREAM, "mm")
            if v.lower() != dflt and dflt: P.text(vx0 - 22, cy + 1, "CHANGED", 15, CREAM if on else (190, 90, 70), "rm", "SemiBold")
            self.hit(P, rx0, y, vx0 + 40, y + rh, ("row", j)); self.hit(P, vx0 - 10, y, vx0 + 50, y + rh, ("dec", j))
            self.hit(P, vx0 + 50, y, vx1 - 50, y + rh, ("inc", j)); self.hit(P, vx1 - 50, y, vx1 + 10, y + rh, ("inc", j))
        # what the chosen setting does
        dy = gy(26.5); key = rows[min(self.sel["row"], len(rows) - 1)][0]
        P.box(gx(1), dy, rx1, gy(30), fill=(0, 0, 0, 235), line=FAINT + (255,), w=2)
        P.box(gx(1), dy, gx(1) + 8, gy(30), fill=RED + (255,))
        desc = self.defs.get(key, {}).get('desc', '') or "(no description)"
        desc = desc[0].upper() + desc[1:]
        lines = P.wrap(desc, 22, rx1 - gx(1) - 70, "Regular")[:3]
        for k, l in enumerate(lines): P.text(gx(1) + 34, dy + 26 + k * 30, l, 22, CREAM, "lm", "Regular")

    def draw_disc(self, P):
        self.header(P, "DISC CHECK", "The port reads the game straight from your own copy of the disc (.iso).")
        x0, x1, top = gx(1), gx(59), gy(6); rh = PITCH * 1.35
        rows = self.disc[0] if self.disc else []
        P.box(x0, top, x1, top + rh * max(len(rows), 1) + 20, fill=(0, 0, 0, 225), line=FAINT + (255,), w=2)
        if self.busy and not rows: P.text(x0 + 30, top + 10 + rh / 2, self.busy, 26, DIM)
        for j, (label, st, text) in enumerate(rows):
            y = top + 10 + j * rh; cy = y + rh / 2 - 2
            P.text(x0 + 30, cy, label, 24, DIM)
            if st == "ok": P.tick(x0 + 330, cy, 26, OKC)
            elif st == "bad": P.cross(x0 + 330, cy, 26, BAD)
            elif st == "info": P.line([(x0 + 320, cy), (x0 + 340, cy)], DIM, 3)
            else: P.text(x0 + 330, cy, "!", 30, WARN, "mm")
            if P.tlen(text, 24, "SemiBold") > x1 - x0 - 410:
                while len(text) > 8 and P.tlen(text + "...", 24, "SemiBold") > x1 - x0 - 410: text = text[:-1]
                text += "..."
            P.text(x0 + 370, cy, text, 24, BAD if st == "bad" else CREAM, "lm", "SemiBold")
        # verdict
        vy = gy(22.5)
        ready = bool(self.disc and self.disc[1])
        P.box(x0, vy, x1, vy + PITCH * 2.5, fill=(0, 0, 0, 235), line=(CREAM if ready else BAD) + (255,), w=2)
        vt = self.busy or (self.disc[2] if self.disc else "")
        P.text(VW / 2, vy + PITCH * 1.25 + 1, vt, 34, CREAM if ready or self.busy else BAD, "mm")
        # buttons
        bs = self.disc_buttons(); nb = len(bs); gap = PITCH * (1 if nb <= 4 else 0.5)
        bw = min(PITCH * 9, (gx(59) - gx(1) - (nb - 1) * gap) / nb); tot = nb * bw + (nb - 1) * gap
        bx = (gx(1) + gx(59)) / 2 - tot / 2; by = gy(26)
        self.sel["disc"] = min(self.sel["disc"], len(bs) - 1)
        for i, (lab, act) in enumerate(bs):
            on = self.sel["disc"] == i
            self.item_box(P, bx, by, bx + bw, by + PITCH * 1.75, lab, "", on, 26 if nb <= 4 else 22 if nb <= 5 else 19, arrow=False)
            self.hit(P, bx, by, bx + bw, by + PITCH * 1.75, ("dbtn", i)); bx += bw + gap
        if self.msg: P.text(VW / 2, gy(28.8), self.msg, 22, WARN, "mm")

    def draw_footer(self, P, page):
        y0 = gy(31)
        P.box(0, y0, VW, VH, fill=(0, 0, 0, 230)); P.box(0, y0, VW, y0 + 2, fill=FAINT + (255,))
        cy = (y0 + VH) / 2
        P.text(gx(1), cy, "METAL GEAR SOLID  PORTED OPS", 20, DIM, "lm", "SemiBold")
        hint = {"menu": "ARROWS / D-PAD  MOVE      ENTER / A  SELECT      M / Y  MENU SOUND      F11  FULL SCREEN",
                "settings": "UP / DOWN  CHOOSE      LEFT / RIGHT  CHANGE      LB / RB  PAGE      ESC / B  BACK",
                "disc": "LEFT / RIGHT  CHOOSE      ENTER / A  SELECT      ESC / B  BACK"}[page]
        P.text(gx(59), cy, hint, 20, DIM, "rm", "SemiBold")


# ------------------------------------------------------------------------------------------------ the window
def main():
    import tkinter as tk
    from PIL import ImageTk
    L = Launcher(); L.dirty = True
    SND_ON[0] = L.cfg.get("menu_sound", True) is not False
    root = tk.Tk(); root.title("Metal Gear Solid Ported Ops"); root.configure(bg="black")
    def set_icon():
        if L.sprite is None: return
        try: st["icon"] = ImageTk.PhotoImage(L.sprite.crop((0, 0, 290, L.sprite.size[1])).resize((64, 83))); root.iconphoto(True, st["icon"])
        except Exception: pass
    sw, sh = root.winfo_screenwidth(), root.winfo_screenheight()
    w0, h0 = (1600, 900) if sw >= 1920 and sh >= 1080 else (1280, 720)
    root.geometry(f"{w0}x{h0}+{(sw - w0) // 2}+{max(0, (sh - h0) // 2 - 30)}"); root.minsize(640, 360)
    cv = tk.Canvas(root, bg="black", highlightthickness=0); cv.pack(fill="both", expand=True)
    if L.settings.get("fullscreen").lower() == "on": root.attributes("-fullscreen", True)
    item = cv.create_image(0, 0, anchor="nw"); st = {"photo": None, "bg": None, "bgkey": None, "fs": L.settings.get("fullscreen").lower() == "on", "anim": None,
                                                     "pad": set(), "rep": 0.0, "last": None, "frames": None, "t0": None}

    def geom():
        w, h = max(1, cv.winfo_width()), max(1, cv.winfo_height())
        s = min(w / VW, h / VH); return w, h, s, (w - VW * s) / 2, (h - VH * s) / 2

    def bg():
        w, h, s, ox, oy = geom()
        if st["bgkey"] != (w, h): st["bg"] = grid_background(w, h, s, ox, oy); st["bgkey"] = (w, h)
        return st["bg"]

    def show(img):
        st["photo"] = ImageTk.PhotoImage(img); cv.itemconfigure(item, image=st["photo"])

    def render():
        w, h, s, ox, oy = geom(); img = bg().copy()
        L.draw_page(img, s, ox, oy); return img

    def redraw():
        if st["anim"]: return
        show(render()); L.dirty = False

    # ---- the intro: the clip, then the grid comes in and the logo moves up to make room for the menu
    def intro_frames(): return (L.art or {}).get("frames") or []

    def start():
        """wait for the disc check: verified = the intro and the FOXHOUND menu, otherwise the plain menu"""
        if cv.winfo_width() < 50 or L.disc is None:
            if cv.winfo_width() >= 50 and not st.get("waitshown"):
                w, h, s, ox, oy = geom(); img = Image.new("RGB", (w, h)); P = Painter(img, s, ox, oy)
                P.text(VW / 2, VH / 2, "CHECKING THE DISC...", 26, FAINT, "mm"); show(img); st["waitshown"] = True
            root.after(30, start); return
        st["started"] = True; L.art_new = False
        if L.sprite is not None: set_icon(); start_intro()
        else: start_reveal(skip=True)

    def start_intro():
        if cv.winfo_width() < 50: root.after(30, start_intro); return     # (the window isn't on screen yet)
        fr = intro_frames()
        if not fr: return start_reveal(skip=True)
        st["frames"] = fr; st["anim"] = "intro"; st["t0"] = time.perf_counter()
        play_sound((L.art or {}).get("wav"))
        st["cache"] = {}; tick_intro()

    def tick_intro():
        if st["anim"] != "intro": return
        fr = st["frames"]; i = int((time.perf_counter() - st["t0"]) * 30)
        if i >= len(fr) - 1: return start_reveal()
        w, h, s, ox, oy = geom()
        im = Image.open(io.BytesIO(fr[i])).convert("RGB").resize((max(1, int(VW * s)), max(1, int(VH * s))), Image.BILINEAR)
        full = Image.new("RGB", (w, h)); full.paste(im, (int(ox), int(oy))); st["lastframe"] = full; show(full)
        root.after(12, tick_intro)

    def start_reveal(skip=False):
        st["anim"] = "reveal"; st["t0"] = time.perf_counter(); st["skipintro"] = skip
        w, h, s, ox, oy = geom()
        a = bg().copy(); L.place_logo(a, s, ox, oy, 1.0, *SPRITE_AT) if L.sprite is not None else None; st["revealA"] = a
        ui = bg().copy(); L.draw_page(ui, s, ox, oy, "menu"); st["menuimg"] = ui
        tick_reveal()

    def tick_reveal():
        if st["anim"] != "reveal": return
        t = time.perf_counter() - st["t0"]; w, h, s, ox, oy = geom()
        T1, T2, T3 = (0.0, 0.0, 0.75) if st["skipintro"] else (0.35, 0.85, 1.6)
        if t < T1:                                                   # the grid and the white circle come in
            base = st.get("lastframe") or Image.new("RGB", (w, h))
            show(Image.blend(base, st["revealA"], t / T1))
        elif t < T2: show(st["revealA"])
        elif t < T3:                                                 # the logo moves up, the menu comes in
            u = (t - T2) / (T3 - T2); e = u * u * (3 - 2 * u)
            sc1, x1, y1 = L.logo_pos("menu")
            sc = 1 + (sc1 - 1) * e; x = SPRITE_AT[0] + (x1 - SPRITE_AT[0]) * e; y = SPRITE_AT[1] + (y1 - SPRITE_AT[1]) * e
            img = bg().copy(); L.place_logo(img, s, ox, oy, sc, x, y)
            if L.sprite is None: img = Image.new("RGB", (w, h))              # (plain menu: it simply fades in)
            if u > 0.55 or L.sprite is None:
                k = (u - 0.55) / 0.45 if L.sprite is not None else u
                mask = Image.new("L", (w, h), int(255 * k))
                ui = st["menuimg"].copy(); L.place_logo(ui, s, ox, oy, sc, x, y)
                img = Image.composite(ui, img, mask)
            show(img)
        else:
            st["anim"] = None; redraw(); start_music(); return
        root.after(10, tick_reveal)

    def skip():
        if st["anim"] in ("intro", "reveal"):
            st["anim"] = None
            try:
                import winsound; winsound.PlaySound(None, 0)
            except Exception: pass
            redraw(); start_music(); return True
        return False

    # ---- page changes, with a short fade
    def go(page):
        w, h, s, ox, oy = geom()
        a = bg().copy(); L.draw_page(a, s, ox, oy)                   # (the page as it is now)
        L.page = page; L.msg = None
        b = render(); st["anim"] = "fade"; t0 = time.perf_counter()
        def step():
            u = (time.perf_counter() - t0) / 0.16
            if u >= 1: st["anim"] = None; redraw(); return
            show(Image.blend(a, b, u)); root.after(10, step)
        step()

    # ---- what the keys / buttons do
    def activate(act=None):
        if L.page == "menu":
            items = L.menu_items(); a = act or items[L.sel["menu"]][2]
            if a in ("continue", "new"):
                if L.busy: L.msg = "ONE MOMENT - THE DISC IS BEING CHECKED"; redraw(); return
                if not (L.disc and L.disc[1]):
                    play("back"); go("disc"); L.msg = "THE GAME CAN'T START YET - SEE BELOW"; redraw(); return
                play("ok"); launch(fresh=(a == "new")); return
            if a == "disc": play("ok"); L.recheck(); go("disc"); return
            if a == "settings": play("ok"); L.settings.load(); L.focus = "cat"; go("settings"); return
            if a == "exit": quit_all(); return
        elif L.page == "disc":
            bs = L.disc_buttons(); a = act or bs[L.sel["disc"]][1]
            play("ok")
            if a == "back": go("menu")
            elif a == "recheck": L.recheck()
            elif a == "bulzome":
                import webbrowser
                try: webbrowser.open(BULZOME_VIDEO)
                except Exception: pass
                L.msg = "HIS VIDEO IS OPEN IN YOUR BROWSER - THE DOWNLOAD LINK IS UNDER IT (" + BULZOME_LINK.split("//")[1].upper() + ")"
            elif a == "install":
                if L.busy: return
                L.install_packs()
            elif a == "import":
                if L.busy: return
                from tkinter import filedialog
                p = filedialog.askdirectory(parent=root, mustexist=True, initialdir=os.path.dirname(L.iso) if L.iso else HERE,
                                            title="Choose a folder with your disc image (.iso), HD textures and/or the .pak files")
                if p: L.import_folder(os.path.normpath(p))
            elif a == "browse":
                from tkinter import filedialog
                p = filedialog.askopenfilename(parent=root, title="Choose your Metal Gear Solid: Portable Ops disc image",
                                               initialdir=os.path.dirname(L.iso) if L.iso else HERE,
                                               filetypes=[("Disc image", "*.iso"), ("All files", "*.*")])
                if p: L.iso = os.path.normpath(p); L.cfg["iso"] = L.iso; L.save_cfg(); L.recheck()
            elif a == "make":
                if not L.iso: L.msg = "CHOOSE THE DISC IMAGE FIRST"; redraw(); return
                def work():
                    L.busy = "BUILDING..."; L.dirty = True
                    ok, m = make_game_code(L.iso); L.msg = m.upper()
                    L.busy = None; L.recheck()
                threading.Thread(target=work, daemon=True).start()
        elif L.page == "settings":
            if L.focus == "cat": L.focus = "rows"; L.sel["row"] = 0; play("ok")
            else: change(+1)
        redraw()

    def change(d, j=None):
        rows = CATEGORIES[L.sel["cat"]][1]
        if j is not None: L.sel["row"] = j; L.focus = "rows"
        key, label, choices = rows[L.sel["row"]]
        vals = [c for c, _ in choices]; cur = L.settings.get(key).lower()
        if cur in vals: i = (vals.index(cur) + d) % len(vals)
        else: i = 0 if d > 0 else len(vals) - 1
        if not L.settings.set(key, vals[i]): L.msg = "port_settings.txt could not be saved"
        play("move"); redraw()

    def back():
        if L.page == "menu": return
        play("back")
        if L.page == "settings" and L.focus == "rows": L.focus = "cat"; redraw(); return
        go("menu")

    def move(dx, dy):
        if L.page == "menu":
            if dy: L.sel["menu"] = (L.sel["menu"] + dy) % len(L.menu_items())
        elif L.page == "disc":
            n = len(L.disc_buttons())
            if dx or dy: L.sel["disc"] = (L.sel["disc"] + (dx or dy)) % n
        elif L.page == "settings":
            if L.focus == "cat":
                if dy: L.sel["cat"] = (L.sel["cat"] + dy) % len(CATEGORIES); L.sel["row"] = 0
                if dx > 0: L.focus = "rows"; L.sel["row"] = 0
            else:
                n = len(CATEGORIES[L.sel["cat"]][1])
                if dy: L.sel["row"] = (L.sel["row"] + dy) % n
                if dx: change(dx); return
        play("move"); redraw()

    def toggle_sound():
        SND_ON[0] = not SND_ON[0]; L.cfg["menu_sound"] = SND_ON[0]; L.save_cfg()
        play("move"); redraw()

    def page_tab(d):
        if L.page != "settings": return
        L.sel["cat"] = (L.sel["cat"] + d) % len(CATEGORIES); L.sel["row"] = 0
        play("move"); redraw()

    def launch(fresh=False):
        """start the game (play.py) and close the launcher"""
        L.cfg["iso"] = L.iso; L.save_cfg()
        env = dict(os.environ); env["MGSPO_ISO"] = L.iso or ""
        if fresh: env["MGSPO_FRESH"] = "1"
        py = sys.executable
        if os.path.basename(py).lower() == "pythonw.exe":
            alt = os.path.join(os.path.dirname(py), "python.exe")
            if os.path.exists(alt): py = alt
        try:
            subprocess.Popen([py, os.path.join(HERE, "play.py")], cwd=HERE, env=env,
                             creationflags=getattr(subprocess, "CREATE_NEW_CONSOLE", 0))
        except Exception as e:
            L.msg = f"the game could not start: {e}"; redraw(); return
        if MIX[0] is not None: MIX[0].fade_music()
        root.after(700, quit_all)

    def quit_all():
        if MIX[0] is not None: MIX[0].close()
        root.destroy()

    # ---- input
    def key(e):
        k = e.keysym
        if k == "F11":
            st["fs"] = not st["fs"]; root.attributes("-fullscreen", st["fs"]); return
        if skip(): return
        if k in ("Up", "w", "W"): move(0, -1)
        elif k in ("Down", "s", "S"): move(0, 1)
        elif k in ("Left", "a", "A"): move(-1, 0)
        elif k in ("Right", "d", "D"): move(1, 0)
        elif k in ("Return", "KP_Enter", "space"): activate()
        elif k in ("Escape", "BackSpace"):
            if st["fs"] and L.page == "menu": st["fs"] = False; root.attributes("-fullscreen", False)
            else: back()
        elif k == "Prior": page_tab(-1)
        elif k == "Next": page_tab(1)
        elif k in ("m", "M") and L.page == "menu": toggle_sound()

    def mouse_at(x, y):
        for x0, y0, x1, y1, act in reversed(L.hits):
            if x0 <= x < x1 and y0 <= y < y1: return act
        return None

    def motion(e):
        if st["anim"]: return
        a = mouse_at(e.x, e.y)
        if a == L.hover: return
        L.hover = a; changed = False
        if a:
            kind, i = a
            if kind == "menu" and L.sel["menu"] != i: L.sel["menu"] = i; changed = True
            elif kind == "dbtn" and L.sel["disc"] != i: L.sel["disc"] = i; changed = True
            elif kind == "cat" and L.focus == "cat" and L.sel["cat"] != i: L.sel["cat"] = i; L.sel["row"] = 0; changed = True
            elif kind in ("row", "dec", "inc") and L.focus == "rows" and L.sel["row"] != i: L.sel["row"] = i; changed = True
        if changed: play("move"); redraw()

    def click(e):
        if skip(): return
        a = mouse_at(e.x, e.y)
        if not a: return
        kind, i = a
        if kind == "snd": toggle_sound()
        elif kind == "menu": L.sel["menu"] = i; activate()
        elif kind == "dbtn": L.sel["disc"] = i; activate()
        elif kind == "cat": L.sel["cat"] = i; L.sel["row"] = 0; L.focus = "rows"; play("ok"); redraw()
        elif kind == "row": L.sel["row"] = i; L.focus = "rows"; change(+1)
        elif kind == "inc": change(+1, i)
        elif kind == "dec": change(-1, i)

    def right_click(e):
        if not skip(): back()

    def wheel(e):
        if L.page == "settings" and L.focus == "rows" and e.delta:
            a = mouse_at(e.x, e.y)
            if a and a[0] in ("row", "dec", "inc"): change(1 if e.delta > 0 else -1, a[1])

    root.bind("<Key>", key); cv.bind("<Motion>", motion); cv.bind("<Button-1>", click); cv.bind("<Button-3>", right_click)
    root.bind("<MouseWheel>", wheel)
    resize_job = {"id": None}
    def on_resize(e):
        if resize_job["id"]: root.after_cancel(resize_job["id"])
        resize_job["id"] = root.after(60, lambda: (st.__setitem__("bgkey", None), redraw()))
    cv.bind("<Configure>", on_resize)

    pad = controller_reader()
    def poll():
        if pad:
            try: held = pad() or set()
            except Exception: held = set()
            now = time.time(); newp = held - st["pad"]
            for n in newp:
                if n in ("ok", "back") and skip(): break
                if n == "ok": activate()
                elif n == "back": back()
                elif n == "lb": page_tab(-1)
                elif n == "rb": page_tab(1)
                elif n == "y" and L.page == "menu": toggle_sound()
                elif n in ("up", "down", "left", "right"):
                    if st["anim"] in ("intro", "reveal"): skip(); break
                    move({"left": -1, "right": 1}.get(n, 0), {"up": -1, "down": 1}.get(n, 0)); st["rep"] = now + 0.38; st["last"] = n
            dirs = [n for n in ("up", "down", "left", "right") if n in held]
            if st["last"] in dirs and not newp and now >= st["rep"] and not st["anim"]:
                n = st["last"]; move({"left": -1, "right": 1}.get(n, 0), {"up": -1, "down": 1}.get(n, 0)); st["rep"] = now + 0.09
            st["pad"] = held
        if L.art_new and st.get("started") and not st["anim"] and not L.busy:   # (just verified: the art comes in)
            L.art_new = False; set_icon(); L.page = "menu"; L.msg = None; start_intro()
        if L.dirty and not st["anim"]: redraw()
        root.after(25, poll)

    root.protocol("WM_DELETE_WINDOW", quit_all)
    root.after(80, start); root.after(200, poll)
    root.mainloop()


if __name__ == "__main__":
    try: main()
    except SystemExit: raise
    except BaseException:
        import traceback; traceback.print_exc()
        input("\nThe launcher closed because of the error above. Press Enter to close this window.")
