"""Decodes the H.264 video found inside PSP movies (.pmf). Uses PyAV (pip install av) if present,
otherwise an 'ffmpeg' program on the PATH. Frames come out as 480x272 RGB (3 bytes per pixel)."""
import shutil, subprocess, threading, collections
W, H = 480, 272

class H264Decoder:
    def __init__(self):
        self.mode = None; self.frames = collections.deque(); self.count = 0; self.err = None
        try:
            import av
            self.av = av; self.cc = av.CodecContext.create('h264', 'r'); self.mode = 'PyAV'
        except Exception as e:
            self.err = f"PyAV not available ({e.__class__.__name__})"
            exe = shutil.which('ffmpeg')
            if exe:
                self.p = subprocess.Popen([exe, '-loglevel', 'quiet', '-f', 'h264', '-i', 'pipe:0', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}', 'pipe:1'],
                                          stdin=subprocess.PIPE, stdout=subprocess.PIPE)
                threading.Thread(target=self._reader, daemon=True).start(); self.mode = 'ffmpeg program'

    def _reader(self):
        n = W * H * 3
        while True:
            b = self.p.stdout.read(n)
            if not b or len(b) < n: break
            self.frames.append(b); self.count += 1

    def feed(self, data):
        if self.mode == 'PyAV':
            self.pending = getattr(self, 'pending', collections.deque()); self.pending.append(bytes(data))   # decoded only when asked for
        elif self.mode == 'ffmpeg program':
            try: self.p.stdin.write(bytes(data)); self.p.stdin.flush()
            except Exception as e: self.err = f"ffmpeg pipe error: {e}"

    def _decode(self, data):
        if self.mode == 'PyAV':
            try:
                for pk in self.cc.parse(bytes(data)):
                    for fr in self.cc.decode(pk):
                        r = fr.reformat(width=W, height=H, format='rgb24'); pl = r.planes[0]; ls = pl.line_size; b = bytes(pl)
                        if ls != W * 3: b = b''.join(b[y * ls:y * ls + W * 3] for y in range(H))
                        self.frames.append(b); self.count += 1
            except Exception as e: self.err = f"decode error: {e}"

    def next_frame(self):
        pend = getattr(self, 'pending', None)
        while not self.frames and pend: self._decode(pend.popleft())
        if not self.frames and self.mode == 'ffmpeg program':          # the ffmpeg program decodes in the background: give it a moment
            import time
            t = time.time()
            while not self.frames and time.time() - t < 1.0: time.sleep(0.005)
        return self.frames.popleft() if self.frames else None

def to_pixels(rgb, x, y, w, h, stride, fmt, out):
    """write a w*h piece of the frame into 'out' (bytearray) in PSP pixel format fmt (0=565 1=5551 2=4444 3=8888)"""
    for row in range(h):
        sy = min(y + row, H - 1); src = rgb[(sy * W + x) * 3:(sy * W + x + w) * 3]
        if fmt == 3:
            line = bytearray(w * 4); line[0::4] = src[0::3]; line[1::4] = src[1::3]; line[2::4] = src[2::3]; line[3::4] = b'\xff' * w
        else:
            line = bytearray(w * 2)
            for i in range(w):
                r, g, b = src[i*3], src[i*3+1], src[i*3+2]
                v = ((r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11)) if fmt == 0 else ((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000) if fmt == 1 else ((r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8) | 0xF000)
                line[i*2] = v & 255; line[i*2+1] = v >> 8
        bp = 4 if fmt == 3 else 2
        out_off = row * stride * bp; out[out_off:out_off + len(line)] = line
