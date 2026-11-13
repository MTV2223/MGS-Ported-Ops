"""Keeps people's personal details out of the logs and problem reports (F8) they share.

What a shared report could otherwise give away is where things are on their PC: folder paths hold the Windows user name
(C:\\Users\\<name>\\...), and the port folder's or disc image's location can hold more. The game itself gets nothing
personal: the port gives it a made-up network address, a fixed console ID and the console name "SNAKE".

  text(s)            a log line / file with the paths and names replaced by placeholders (<port folder>, <user>, ...)
  state_file(a, b)   a snapshot (.pkl.gz) copied with every such path masked. The mask keeps each text's length, so the
                     snapshot still loads exactly as before - only the masked paths are gone (the port sets those again
                     when a snapshot is loaded).
"""
import os, re, gzip

_PATHS = []          # (path, placeholder), longest first
_NAMES = []          # (word, placeholder)


def _add_path(p, label):
    try: p = os.path.abspath(p)
    except Exception: return
    if not p or len(p) < 4 or p in (os.sep, "C:\\"): return
    for v in {p, p.replace("\\", "/"), p.replace("/", "\\")}:
        if all(v.lower() != q.lower() for q, _ in _PATHS): _PATHS.append((v, label))
    _PATHS.sort(key=lambda t: -len(t[0]))


def set_paths(port_folder=None, iso=None):
    """the places to hide (called once the port knows them; the home folder and names are found by themselves)"""
    if port_folder: _add_path(port_folder, "<port folder>")
    if iso:
        _add_path(os.path.dirname(os.path.abspath(iso)), "<disc image folder>")


def _init():
    home = os.path.expanduser("~")
    if home and home != "~": _add_path(home, "<home>")
    for env, label in (("USERNAME", "<user>"), ("USER", "<user>"), ("COMPUTERNAME", "<pc>"), ("USERDOMAIN", "<pc>")):
        v = os.environ.get(env, "")
        if len(v) >= 3 and all(v.lower() != w.lower() for w, _ in _NAMES): _NAMES.append((v, label))
    try:
        import getpass; v = getpass.getuser()
        if len(v) >= 3 and all(v.lower() != w.lower() for w, _ in _NAMES): _NAMES.append((v, "<user>"))
    except Exception: pass
    if home and home != "~":
        v = os.path.basename(home.rstrip("\\/"))
        if len(v) >= 3 and all(v.lower() != w.lower() for w, _ in _NAMES): _NAMES.append((v, "<user>"))


_init()

# any user folder, also of other accounts / drives:  X:\Users\<name>\   /home/<name>/   /Users/<name>/
_USERDIR = re.compile(r"(?i)((?:[a-z]:)?[\\/](?:users|documents and settings|home)[\\/])([^\\/:*?\"<>|\r\n]+)")


def text(s):
    if not s: return s
    for p, label in _PATHS:
        if p.lower() in s.lower(): s = re.sub(re.escape(p), lambda m: label, s, flags=re.I)
    s = _USERDIR.sub(lambda m: m.group(1) + ("<user>" if m.group(2) not in ("<user>", "Public", "Default", "Shared") else m.group(2)), s)
    for w, label in _NAMES:
        if label == "<pc>":                                   # (a computer name: anywhere it appears on its own)
            s = re.sub(r"(?i)(?<![A-Za-z0-9])" + re.escape(w) + r"(?![A-Za-z0-9])", label, s)
        else:                                                 # (a user name can be an ordinary word: only inside paths)
            s = re.sub(r"(?i)(?<=[\\/])" + re.escape(w) + r"(?=[\\/]|$|\s)", label, s)
    return s


def _mask(n):
    """n bytes of placeholder"""
    return (b"<private>" + b"_" * n)[:n] if n >= 9 else b"x" * n


_USERDIR_B = re.compile(rb"(?i)((?:[a-z]:)?[\\/](?:users|documents and settings|home)[\\/])([^\\/:*?\"<>|\r\n\x00-\x1f\x80-\xff]{1,64})")


def state_bytes(raw):
    """the pickled snapshot with the paths masked (same lengths everywhere: it stays a valid snapshot)"""
    b = bytearray(raw)
    for p, _ in _PATHS:
        for enc in ("utf-8", "utf-16-le"):
            try: pb = p.encode(enc)
            except Exception: continue
            for m in re.finditer(re.escape(pb), bytes(b), flags=re.I): b[m.start():m.end()] = _mask(len(pb))
    for m in _USERDIR_B.finditer(bytes(b)):
        nm = m.group(2)
        if nm.lower() not in (b"public", b"default", b"shared") and not nm.startswith(b"<private>"):
            b[m.start(2):m.end(2)] = b"x" * len(nm)
    for w, _ in _NAMES:                                      # (a name on its own: only where it sits in a path)
        wb = w.encode("utf-8", "ignore")
        for m in re.finditer(rb"(?i)(?<=[\\/])" + re.escape(wb) + rb"(?=[\\/])", bytes(b)): b[m.start():m.end()] = b"x" * len(wb)
    return bytes(b)


def state_file(src, dst, level=6):
    with gzip.open(src, "rb") as f: raw = f.read()
    with gzip.open(dst, "wb", compresslevel=level) as g: g.write(state_bytes(raw))


def leftovers(data):
    """(test aid) is any of the hidden paths or names still in this text / bytes?"""
    if isinstance(data, str): data = data.encode("utf-8", "ignore")
    found = []
    for p, _ in _PATHS:
        if p.encode("utf-8").lower() in data.lower(): found.append(p)
    for w, _ in _NAMES:
        if re.search(rb"(?i)[\\/]" + re.escape(w.encode("utf-8", "ignore")) + rb"[\\/]", data): found.append(w)
    return found
