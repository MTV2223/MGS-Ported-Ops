"""
Stand-in for the PSP system font library (sceLibFont / libfont.prx).
The real PSP draws text with fonts stored in its firmware. Here the letters come from a font on the PC,
drawn with Pillow (pip install pillow). Without Pillow, a plain built-in block font is used so text
at least has the right size and spacing.
"""
import os

FONT_FILES = [r"C:\Windows\Fonts\arial.ttf", r"C:\Windows\Fonts\segoeui.ttf", r"C:\Windows\Fonts\tahoma.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
              "/System/Library/Fonts/Helvetica.ttc"]
PX = 14                 # default letter height in pixels (the PSP's latin system font is about this size)

NIDS = {0x67F17ED7: 'NewLib', 0x574B6FBC: 'DoneLib', 0x099EF33C: 'FindOptimumFont', 0x681E61A7: 'FindFont',
        0xA834319D: 'Open', 0x57FCB733: 'OpenUserFile', 0xBB8E7FE6: 'OpenUserMemory', 0x3AEA8CB6: 'Close',
        0x0DA7535E: 'GetFontInfo', 0x5333322D: 'GetFontInfoByIndexNumber', 0xDCC80C2F: 'GetCharInfo',
        0xCA1E6945: 'GetCharGlyphImage_Clip', 0x980F4895: 'GetCharGlyphImage', 0x5C3E4A9E: 'GetCharImageRect',
        0x27F6E642: 'GetNumFontList', 0xBC75D85B: 'GetFontList', 0x472694CD: 'PointToPixelH', 0x3C4B7E82: 'PointToPixelV',
        0x2F67356A: 'CalcMemorySize', 0x48293280: 'SetResolution', 0x02D7F94B: 'Flush', 0xEE232411: 'SetAltCharacterCode',
        0x74B21701: 'PixelToPointH', 0xF8F0752E: 'PixelToPointV', 0xAA3DE7B5: 'GetShadowInfo', 0x568BE516: 'GetShadowImageRect',
        0x5DCF6858: 'GetShadowGlyphImage', 0x48B06520: 'GetShadowGlyphImage_Clip'}

class Font:
    def __init__(self, px):
        self.px = px; self.cache = {}; self.pil = None
        try:
            from PIL import ImageFont
            for f in FONT_FILES:
                if os.path.exists(f): self.pil = ImageFont.truetype(f, px); break
            if self.pil is None: self.pil = ImageFont.load_default()
        except Exception: self.pil = None
        if self.pil is not None:
            try: a, d = self.pil.getmetrics(); self.asc, self.desc = a, d
            except Exception: self.asc, self.desc = px * 4 // 5, px // 5
        else: self.asc, self.desc = px * 4 // 5, px // 5

    def __reduce__(self): return (Font, (self.px,))           # save states: rebuild from the size (no Pillow objects inside)

    def glyph(self, code):
        """-> (left, top, width, height, advance, rows of 0..255)"""
        g = self.cache.get(code)
        if g: return g
        ch = chr(code) if code < 0x110000 else '?'
        if self.pil is not None:
            from PIL import Image, ImageDraw
            try:
                x0, y0, x1, y1 = self.pil.getbbox(ch); adv = int(round(self.pil.getlength(ch)))
            except Exception: x0, y0, x1, y1, adv = 0, 0, self.px // 2, self.px, self.px // 2
            w, h = max(x1 - x0, 0), max(y1 - y0, 0)
            rows = []
            if w and h:
                im = Image.new('L', (w, h), 0); ImageDraw.Draw(im).text((-x0, -y0), ch, fill=255, font=self.pil)
                px = im.tobytes(); rows = [px[r * w:(r + 1) * w] for r in range(h)]
            g = (x0, self.asc - y0, w, h, adv, rows)
        else:
            w = max(self.px // 2 - 1, 1); h = self.asc if ch.strip() else 0
            rows = [bytes([255 if (x in (0, w - 1) or y in (0, h - 1)) else 0 for x in range(w)]) for y in range(h)]
            g = (0, self.asc, w if h else 0, h, self.px // 2 + 1, rows)
        self.cache[code] = g; return g

class FontLib:
    def __init__(self, mach):
        self.m = mach; self.fonts = {}; self.next = 1; self.calls = {}
    def cpu(self): return self.m.cpu
    def f32(self, a):
        import struct; return struct.unpack('<f', struct.pack('<I', self.cpu().r32(a)))[0]
    def wf(self, a, v):
        import struct; self.cpu().w32(a, struct.unpack('<I', struct.pack('<f', float(v)))[0])

    def call(self, nid, r):
        name = NIDS.get(nid)
        if name is None: return False
        self.calls[name] = self.calls.get(name, 0) + 1
        cpu = self.cpu(); a0, a1, a2, a3, t0, t1, t2 = r[4], r[5], r[6], r[7], r[8], r[9], r[10]
        def err(p, v=0):
            if p: cpu.w32(p, v)
        if name == 'NewLib': err(a1); r[2] = 0x5000
        elif name in ('DoneLib', 'Close', 'Flush', 'SetAltCharacterCode', 'SetResolution'): r[2] = 0
        elif name in ('FindOptimumFont', 'FindFont'):
            px = PX
            if a1:
                h = self.f32(a1); hres = self.f32(a1 + 8) or 128.0
                self.m.log.append(f"[font] style h={h} v={self.f32(a1 + 4)} hres={hres} family={cpu.r16(a1 + 20)} lang={cpu.r16(a1 + 26)}")
                # the PSP latin font is 16 points high at 128 dpi and its letters are about 14 pixels tall
                if 2 < h < 100: px = max(8, min(48, int(round(h * 14 / 16.0))))
            err(a2); idx = self.next; self.next += 1; self.fonts[('style', idx)] = px; r[2] = idx
        elif name in ('Open', 'OpenUserFile', 'OpenUserMemory'):
            px = self.fonts.get(('style', a1), PX); h = 0x100 + len([k for k in self.fonts if isinstance(k, int)])
            self.fonts[h] = Font(px); err(a3 if name == 'Open' else t0); r[2] = h
        elif name == 'CalcMemorySize': err(a1); r[2] = 0x10000
        elif name == 'GetNumFontList': err(a1); r[2] = 1
        elif name == 'GetFontList': r[2] = 0
        elif name in ('PointToPixelH', 'PointToPixelV', 'PixelToPointH', 'PixelToPointV'):
            import struct
            v = struct.unpack('<f', struct.pack('<I', cpu.f[12] & 0xFFFFFFFF))[0]
            v = v * 128.0 / 72.0 if name.startswith('Point') else v * 72.0 / 128.0
            err(a1); cpu.f[0] = struct.unpack('<I', struct.pack('<f', v))[0]; r[2] = 0
        elif name in ('GetFontInfo', 'GetFontInfoByIndexNumber'):
            f = self.font(a0); p = a1 if name == 'GetFontInfo' else a1
            if p:
                for i in range(0, 264, 4): cpu.w32(p + i, 0)
                ints = (f.px, f.asc + f.desc, f.asc, -f.desc, 0, f.asc, 0, f.asc, f.px, f.asc + f.desc)
                for i, v in enumerate(ints): cpu.w32(p + 4 * i, (v * 64) & 0xFFFFFFFF); self.wf(p + 40 + 4 * i, v)
                cpu.w16(p + 80, f.px); cpu.w16(p + 82, f.asc + f.desc); cpu.w32(p + 84, 0x100); cpu.w32(p + 88, 0)
                self.wf(p + 92, f.px); self.wf(p + 96, f.px); self.wf(p + 100, 128); self.wf(p + 104, 128); self.wf(p + 108, 0)
                cpu.w16(p + 112, 1); cpu.w16(p + 114, 1); cpu.w16(p + 116, 1); cpu.w16(p + 118, 2)
                cpu.w8(p + 260, 4)
            r[2] = 0
        elif name == 'GetCharInfo':
            f = self.font(a0); left, top, w, h, adv, rows = f.glyph(a1); p = a2
            if p:
                vals = (w, h, left, top, w * 64, h * 64, f.asc * 64, -f.desc * 64, left * 64, top * 64, 0, 0, adv * 64, (f.asc + f.desc) * 64)
                for i, v in enumerate(vals): cpu.w32(p + 4 * i, v & 0xFFFFFFFF)
                cpu.w32(p + 56, 0)
            r[2] = 0
        elif name == 'GetCharImageRect':
            f = self.font(a0); left, top, w, h, adv, rows = f.glyph(a1)
            if a2: cpu.w16(a2, w); cpu.w16(a2 + 2, h)
            r[2] = 0
        elif name in ('GetCharGlyphImage', 'GetCharGlyphImage_Clip'):
            clip = (a3, t0, t1, t2) if name.endswith('Clip') else None
            self.draw(self.font(a0), a1, a2, clip); r[2] = 0
        else: r[2] = 0
        return True

    def font(self, h):
        f = self.fonts.get(h)
        if f is None: f = self.fonts[h] = Font(PX)
        return f

    def draw(self, f, code, img, clip):
        cpu = self.cpu()
        if not img: return
        fmt = cpu.r32(img); x = (cpu.r32(img + 4) ^ 0x80000000) - 0x80000000; y = (cpu.r32(img + 8) ^ 0x80000000) - 0x80000000
        bw, bh, bpl = cpu.r16(img + 12), cpu.r16(img + 14), cpu.r16(img + 16); buf = cpu.r32(img + 20)
        x >>= 6; y >>= 6
        left, top, w, h, adv, rows = f.glyph(code)
        cx0, cy0, cx1, cy1 = 0, 0, bw, bh
        if clip: cx0, cy0 = max(clip[0], 0), max(clip[1], 0); cx1, cy1 = min(clip[0] + clip[2], bw), min(clip[1] + clip[3], bh)
        for yy in range(h):
            py = y + yy
            if py < cy0 or py >= cy1: continue
            row = rows[yy]
            for xx in range(w):
                v = row[xx]
                if not v: continue
                px = x + xx
                if px < cx0 or px >= cx1: continue
                if fmt in (0, 1):                                     # 4 bits per pixel
                    a = buf + py * bpl + (px >> 1); old = cpu.r8(a); n = v >> 4
                    lo_first = (fmt == 0)
                    if (px & 1) == (0 if lo_first else 1): cur = old & 15; cpu.w8(a, (old & 0xF0) | max(cur, n))
                    else: cur = old >> 4; cpu.w8(a, (old & 0x0F) | (max(cur, n) << 4))
                elif fmt == 2:
                    a = buf + py * bpl + px; cpu.w8(a, max(cpu.r8(a), v))
                elif fmt == 3:
                    a = buf + py * bpl + px * 3
                    for k in range(3): cpu.w8(a + k, max(cpu.r8(a + k), v))
                else:
                    a = buf + py * bpl + px * 4; cpu.w32(a, (v << 24) | 0xFFFFFF if v > (cpu.r32(a) >> 24) else cpu.r32(a))
