"""
Pictures of the PSP's system dialogs (message boxes, the save / load slot list, the keyboard) for the play window.
psp_util.Utility describes the dialog the game opened (dialog.ui); render(ui) turns that into an RGBA picture the
size of the game picture (960 x 544), transparent around the dialog, that the window shows on top of the game.
"""
from PIL import Image, ImageDraw, ImageFont

W, H = 960, 544
_fonts = {}


def font(size, bold=False):
    key = (size, bold)
    if key in _fonts: return _fonts[key]
    names = (["segoeuib.ttf", "arialbd.ttf", "DejaVuSans-Bold.ttf"] if bold else []) + ["segoeui.ttf", "arial.ttf", "DejaVuSans.ttf"]
    f = None
    for n in names:
        try: f = ImageFont.truetype(n, size); break
        except OSError: pass
    if f is None:
        try: f = ImageFont.load_default(size=size)
        except TypeError: f = ImageFont.load_default()
    _fonts[key] = f
    return f


def wrap(draw, text, f, width):
    lines = []
    for para in text.replace('\r', '').split('\n'):
        cur = ''
        for word in para.split(' '):
            t = (cur + ' ' + word).strip()
            if draw.textlength(t, font=f) <= width or not cur: cur = t
            else: lines.append(cur); cur = word
        lines.append(cur)
    return lines


GREEN = (150, 200, 140, 255); WHITE = (235, 240, 230, 255); GREY = (150, 160, 150, 255); SEL = (60, 95, 60, 235)


def panel(d, x0, y0, x1, y1):
    d.rounded_rectangle((x0, y0, x1, y1), radius=14, fill=(14, 20, 14, 235), outline=GREEN, width=2)


def buttons(d, labels, sel, cx, y, f):
    wds = [d.textlength(l, font=f) + 60 for l in labels]; x = cx - (sum(wds) + 30 * (len(labels) - 1)) / 2
    for i, (l, w) in enumerate(zip(labels, wds)):
        if i == sel: d.rounded_rectangle((x, y, x + w, y + 46), radius=8, fill=SEL, outline=GREEN, width=2)
        d.text((x + w / 2, y + 23), l, font=f, fill=WHITE if i == sel else GREY, anchor='mm'); x += w + 30


def render(ui):
    im = Image.new('RGBA', (W, H), (0, 0, 0, 120)); d = ImageDraw.Draw(im)
    k = ui['kind']; fb = font(26); fs = font(21); ft = font(30, True)
    if k == 'msg':
        lines = wrap(d, ui['text'] or '', fb, 700)[:9]
        h = 60 + 36 * len(lines) + 90; y0 = (H - h) // 2
        panel(d, 110, y0, W - 110, y0 + h)
        for i, l in enumerate(lines): d.text((W // 2, y0 + 40 + 36 * i), l, font=fb, fill=WHITE, anchor='mt')
        buttons(d, ui['buttons'], ui['sel'], W // 2, y0 + h - 70, fb)
    elif k == 'list':
        panel(d, 70, 40, W - 70, H - 40)
        d.text((100, 62), ui['title'], font=ft, fill=GREEN)
        items = ui['items']; sel = ui['sel']; rows = 5; top = max(0, min(sel - rows // 2, len(items) - rows))
        for j, i in enumerate(range(top, min(top + rows, len(items)))):
            nm, title, detail, exists, when = items[i]; y = 116 + j * 70
            if i == sel: d.rounded_rectangle((90, y - 4, W - 90, y + 62), radius=8, fill=SEL, outline=GREEN, width=2)
            d.text((110, y + 2), title if exists else 'New saved data', font=fb, fill=WHITE if exists else GREY)
            if exists:
                sub = (detail.split('\n')[0] if detail else '')[:70]
                d.text((110, y + 34), sub, font=fs, fill=GREY)
                d.text((W - 110, y + 6), when, font=fs, fill=GREY, anchor='ra')
        if top > 0: d.text((W // 2, 104), '▲', font=fs, fill=GREEN, anchor='mm')
        if top + rows < len(items): d.text((W // 2, 116 + rows * 70 - 4), '▼', font=fs, fill=GREEN, anchor='mm')
        d.text((W // 2, H - 66), 'Circle = choose      Cross = back', font=fs, fill=GREY, anchor='mm')
        if ui.get('confirm') is not None:
            q = 'Overwrite this saved data?' if ui['mode'] == 'save' else 'Delete this saved data?'
            panel(d, 220, 190, W - 220, 360)
            d.text((W // 2, 228), q, font=fb, fill=WHITE, anchor='mt')
            buttons(d, ['Yes', 'No'], ui['confirm'], W // 2, 290, fb)
    elif k == 'osk':
        panel(d, 110, 150, W - 110, 400)
        d.text((W // 2, 185), ui['desc'], font=fb, fill=GREEN, anchor='mt')
        d.rounded_rectangle((150, 240, W - 150, 300), radius=8, fill=(0, 0, 0, 255), outline=GREY, width=2)
        d.text((170, 270), ui['text'] + '_', font=fb, fill=WHITE, anchor='lm')
        d.text((W // 2, 350), 'Type on the keyboard     Enter or Circle = done     Cross = cancel', font=fs, fill=GREY, anchor='mm')
    return im
