"""HD movies: upscaled versions of the game's movies (.mp4, same name as the .pmf - e.g. MPO_OP.mp4), shown in place
of the PSP's 480x272 pictures. They are looked for in "HD movies" next to play.bat, then in SD_Dump\\Movies(HD).

The game decodes its own movie as always (so its timing, sound and skipping are untouched). Each time it writes
picture number N into video memory, picture N of the HD file is handed to the graphics card for that spot; while
the game shows that framebuffer, the HD picture is shown instead. The HD file is decoded ahead on a thread of its
own (PyAV if installed, else an ffmpeg program on the PATH)."""
import os, re, shutil, subprocess, threading, time

FOLDERS = ("HD movies", os.path.join("SD_Dump", "Movies(HD)"))


def find(base, name):
    if not name: return None
    for d in FOLDERS:
        dd = os.path.join(base, d)
        if not os.path.isdir(dd): continue
        for f in os.listdir(dd):
            stem, ext = os.path.splitext(f)
            if stem.lower() == name.lower() and ext.lower() in ('.mp4', '.mkv', '.mov', '.avi', '.webm'):
                return os.path.join(dd, f)
    return None


def _frames_pyav(path):
    import av
    with av.open(path) as c:
        st = c.streams.video[0]; st.thread_type = 'AUTO'
        for fr in c.decode(st):
            r = fr.reformat(format='rgb24'); pl = r.planes[0]; w, h, ls = r.width, r.height, pl.line_size
            b = bytes(pl)
            if ls != w * 3: b = b''.join(b[y * ls:y * ls + w * 3] for y in range(h))
            yield w, h, b


_SIZES = {}


def _frames_ffmpeg(path):
    exe = shutil.which('ffmpeg')
    if not exe: raise RuntimeError("no PyAV and no ffmpeg program")
    flags = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
    if path not in _SIZES:
        info = subprocess.run([exe, '-hide_banner', '-i', path], capture_output=True, text=True, errors='replace', creationflags=flags).stderr
        m = re.search(r'Video:.*?(\d{2,5})x(\d{2,5})', info)
        if not m: raise RuntimeError("can't read the size of " + path)
        _SIZES[path] = (int(m.group(1)), int(m.group(2)))
    w, h = _SIZES[path]; n = w * h * 3
    p = subprocess.Popen([exe, '-loglevel', 'quiet', '-i', path, '-an', '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1'],
                         stdout=subprocess.PIPE, creationflags=flags)
    try:
        while True:
            b = p.stdout.read(n)
            if not b or len(b) < n: break
            yield w, h, b
    finally:
        p.kill()


def frames(path):
    try:
        import av  # noqa: F401
        return _frames_pyav(path)
    except ImportError:
        return _frames_ffmpeg(path)


class Source:
    """one HD movie file, decoded ahead: get(i) -> (w, h, rgb bytes) of picture i, or None"""
    AHEAD = 10
    HEAD = 16          # the first pictures are kept: a looping movie starts again from them while the file is reopened

    def __init__(self, path):
        self.path = path; self.cv = threading.Condition(); self.buf = {}; self.want = 0; self.pos = 0; self.total = None
        self.restart = False; self.stop = False; self.err = None; self.head = {}
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        while not self.stop:
            pos = 0; ended = False
            try:
                for w, h, b in frames(self.path):
                    with self.cv:
                        while not self.stop and not self.restart and pos >= self.want + self.AHEAD: self.cv.wait(0.2)
                        if self.stop or self.restart: break
                        if pos >= self.want: self.buf[pos] = (w, h, b)
                        if pos < self.HEAD and self.total is None: self.head[pos] = (w, h, b)
                        for k in [k for k in self.buf if k < self.want]: del self.buf[k]
                        pos += 1; self.pos = pos; self.cv.notify_all()
                else: ended = True
            except Exception as e:
                with self.cv: self.err = str(e); self.total = 0; self.cv.notify_all()
                return
            with self.cv:
                if ended:
                    if self.total is None: self.total = pos
                    self.cv.notify_all()
                    while not self.stop and not self.restart: self.cv.wait(0.2)   # (at the end: until asked again)
                self.restart = False; self.buf.clear(); self.pos = 0

    def _ask(self, i):
        if self.total: i %= self.total                         # (a looping movie: from the start again)
        if i not in self.buf and i < self.pos and not self.restart: self.restart = True   # (already passed: decode again)
        self.want = i; self.cv.notify_all(); return i

    def get(self, i, timeout=0.06):
        end = time.perf_counter() + timeout
        with self.cv:
            if self.total == 0: return None
            i = self._ask(i)
            if i in self.head and i not in self.buf: return self.head[i]
            while i not in self.buf and not self.stop:
                left = end - time.perf_counter()
                if left <= 0 or self.total == 0: return None
                self.cv.wait(left)
                if self.total and i >= self.total: i = self._ask(i)
            return self.buf.get(i)

    def close(self):
        with self.cv: self.stop = True; self.cv.notify_all()


class HDMovies:
    """the hook psp_run calls (mach.movie_hook); post(fn) runs fn on the drawing thread"""
    def __init__(self, base, lib, post, log=print):
        self.base, self.lib, self.post, self.log = base, lib, post, log; self.src = None; self.name = None
        self.shown = self.missed = 0

    def __call__(self, what, name, *a):
        lib = self.lib
        if what == 'start':
            if self.src: self.src.close()
            self.src = None; self.name = name; self.shown = self.missed = 0
            path = find(self.base, name)
            if path:
                self.src = Source(path); self.log(f"HD movie: {name} - {os.path.relpath(path, self.base)}")
            self.post(lambda: lib.gl_movie_hd_clear())
        elif what == 'frame':
            i, dest = a; got = None
            if self.src is not None and name == self.name: got = self.src.get(i)
            if got: self.shown += 1
            elif self.src is not None:
                self.missed += 1
                if self.missed in (1, 30): self.log(f"HD movie: picture {i} not ready - the original shown" + (f" ({self.src.err})" if self.src.err else ""))
            if got:
                w, h, b = got
                self.post(lambda: lib.gl_movie_hd(dest, w, h, b))
            elif self.src is not None:
                self.post(lambda: lib.gl_movie_hd(dest, 0, 0, None))
        elif what == 'end':
            if self.src:
                self.log(f"HD movie: {self.name} done - {self.shown} HD pictures shown, {self.missed} originals")
                self.src.close()
            self.src = None
            self.post(lambda: lib.gl_movie_hd_clear())
