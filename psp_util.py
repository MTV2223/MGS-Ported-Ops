"""
PSP system dialogs and save data for the port (sceUtility):
  - message boxes (MsgDialog), the save / load slot lists and the on-screen keyboard (Osk). In the play window
    (Utility.interactive = True) they are shown to the player and answered with the pad / keyboard (see .ui and
    ui_key()); otherwise (tests) they are answered automatically: YES/OK, the first slot, OSK_NAME.
  - save data (Savedata): saved into the folder  memstick/PSP/SAVEDATA/<game><slot>/  next to the EBOOT
  - system settings (GetSystemParamInt/String): English, X = confirm, nickname
Each dialog goes  INIT(1) -> VISIBLE(2) -> QUIT(3) -> (game calls ShutdownStart) -> FINISHED(4) -> NONE(0),
which is what games wait for.
"""
import os, json, time

OSK_NAME = "PLAYER"            # the name typed into the system keyboard (change it if you like)
NONE, INIT, VISIBLE, QUIT, FINISHED = 0, 1, 2, 3, 4

NIDS = {
    0x2AD8E239: 'MsgDialogInitStart', 0x67AF3428: 'MsgDialogShutdownStart', 0x95FC253B: 'MsgDialogUpdate',
    0x9A1C91D7: 'MsgDialogGetStatus', 0x4928BD96: 'MsgDialogAbort',
    0x50C4CD57: 'SavedataInitStart', 0x9790B33C: 'SavedataShutdownStart', 0xD4B95FFB: 'SavedataUpdate',
    0x8874DBE0: 'SavedataGetStatus',
    0xF6269B82: 'OskInitStart', 0x3DFAEBA9: 'OskShutdownStart', 0x4B85C861: 'OskUpdate', 0xF3F76017: 'OskGetStatus',
    0xA5DA2406: 'GetSystemParamInt', 0x34B78343: 'GetSystemParamString',
    0xC629AF26: 'LoadAvModule', 0xF7D8D092: 'UnloadAvModule', 0x1579A159: 'LoadNetModule', 0x64D50C56: 'UnloadNetModule',
    0x2A2B3DE0: 'LoadModule', 0xE49BFE92: 'UnloadModule',
}

def what_mode_is_all_delete(mode): return mode == 7


class Dialog:
    def __init__(self, kind): self.kind = kind; self.status = NONE; self.param = 0; self.visible_calls = 0; self.ui = None; self.choice = None

# PSP buttons (as in mach.live_buttons)
B_UP, B_RIGHT, B_DOWN, B_LEFT, B_CROSS, B_CIRCLE, B_START = 0x10, 0x20, 0x40, 0x80, 0x4000, 0x2000, 0x8
B_OK, B_BACK = B_CIRCLE, B_CROSS      # like the game's own menus: Circle chooses, Cross goes back

class Utility:
    def __init__(self, mach):
        self.m = mach; self.d = {k: Dialog(k) for k in ('msg', 'save', 'osk')}; self.messages = []; self.saves = []
        self.interactive = False; self.ver = 0

    def __setstate__(self, st):                       # (snapshots from before the dialogs were shown)
        self.__dict__.update(st); self.__dict__.setdefault('interactive', False); self.__dict__.setdefault('ver', 0)
        for d in self.d.values(): d.__dict__.setdefault('ui', None); d.__dict__.setdefault('choice', None)

    # ------------------------------------------------------------------ shown to the player (play window)
    def active(self):
        """the dialog waiting for the player, or None (read by the window thread)"""
        for d in self.d.values():
            if d.ui is not None and d.choice is None and d.status in (INIT, VISIBLE): return d
        return None

    def ui_key(self, pressed, char=None):
        """buttons newly pressed (PSP button bits) / a typed character, from the window thread. Only notes the
        answer: the game thread applies it at the next GetStatus (it may write the game's memory)."""
        d = self.active()
        if d is None: return False
        ui = d.ui; k = ui['kind']; n = len(ui.get('items') or ui.get('buttons') or [1])
        if k == 'osk':
            if char == '\b': ui['text'] = ui['text'][:-1]
            elif char == '\r' or pressed & (B_OK | B_START): d.choice = 'ok'
            elif pressed & B_BACK: d.choice = 'cancel'
            elif char and len(char) == 1 and char.isprintable() and len(ui['text']) < ui['max']: ui['text'] += char
        elif ui.get('confirm') is not None:                               # "overwrite?" yes / no
            if pressed & (B_LEFT | B_RIGHT | B_UP | B_DOWN): ui['confirm'] ^= 1
            elif pressed & B_OK:
                if ui['confirm'] == 0: d.choice = ui['sel']
                else: ui['confirm'] = None
            elif pressed & B_BACK: ui['confirm'] = None
        else:
            step = (pressed & (B_DOWN | B_RIGHT) and 1) or (pressed & (B_UP | B_LEFT) and -1) or 0
            if step: ui['sel'] = (ui['sel'] + step) % n
            elif pressed & B_OK:
                if k == 'list' and ui['mode'] == 'save' and ui['items'][ui['sel']][3]: ui['confirm'] = 1   # overwriting: ask (No first)
                elif k == 'list' and ui['mode'] == 'delete': ui['confirm'] = 1
                else: d.choice = ui['sel']
            elif pressed & B_BACK: d.choice = 'cancel'
        self.ver += 1
        return True

    # ------------------------------------------------------------------ helpers
    def root(self):
        r = os.path.join(self.m.outdir, 'memstick'); os.makedirs(os.path.join(r, 'PSP', 'SAVEDATA'), exist_ok=True); return r
    def cpu(self): return self.m.cpu
    def rstr(self, a, n): return self.cpu().cstr(a, n) if a else ''
    def rbytes(self, a, n):
        if not a or n <= 0: return b''
        b, o = self.cpu()._loc(a, n); return bytes(b[o:o + n])

    def status(self, dlg, autoq=1):
        """GetStatus: report and step the state machine"""
        s = dlg.status
        if s == INIT: dlg.status = VISIBLE
        elif s == VISIBLE:
            dlg.visible_calls += 1
            if dlg.ui is not None:                                     # shown: wait for the player's answer
                if dlg.choice is not None: self.finish(dlg); dlg.status = QUIT; self.ver += 1
            elif dlg.visible_calls >= autoq: dlg.status = QUIT
        elif s == FINISHED: dlg.status = NONE
        return s

    # ------------------------------------------------------------------ entry point
    def call(self, nid, r):
        name = NIDS.get(nid)
        if name is None: return False
        a0, a1 = r[4], r[5]; cpu = self.cpu()
        if name == 'GetSystemParamInt':
            val = {1: 0, 2: 1, 3: 0, 4: 0, 5: 0, 6: -300, 7: 0, 8: 1, 9: 1}.get(a0, 0)   # 8 = English, 9 = X confirms
            cpu.w32(a1, val & 0xFFFFFFFF); r[2] = 0
        elif name == 'GetSystemParamString':
            s = b'SNAKE'[:max(r[6] - 1, 0)] if a0 == 1 else b''
            cpu.load_bytes(a1, s + b'\0'); r[2] = 0
        elif name in ('LoadAvModule', 'UnloadAvModule', 'LoadNetModule', 'UnloadNetModule', 'LoadModule', 'UnloadModule'): r[2] = 0
        # ---- message box
        elif name == 'MsgDialogInitStart':
            d = self.d['msg']; d.param = a0; d.status = INIT; d.visible_calls = 0
            mode = cpu.r32(a0 + 0x34); err = cpu.r32(a0 + 0x38); opts = cpu.r32(a0 + 0x23C)
            text = self.rstr(a0 + 0x3C, 512).encode('latin1', 'replace').decode('utf-8', 'replace') if mode == 1 else f"(system error message {err:#x})"
            self.messages.append(text.strip()); r[2] = 0
            self.note(f"[dialog] MESSAGE: {text.strip()!r}  options={opts:#x}")
            d.choice = None; d.ui = None
            if self.interactive:
                yesno = bool(opts & 0x10) and mode == 1
                d.ui = {'kind': 'msg', 'text': text.strip(), 'buttons': ['Yes', 'No'] if yesno else ['OK'],
                        'sel': 1 if (yesno and opts & 0x100) else 0}; self.ver += 1
        elif name == 'MsgDialogGetStatus': r[2] = self.status(self.d['msg'])
        elif name == 'MsgDialogUpdate': r[2] = 0
        elif name in ('MsgDialogShutdownStart', 'MsgDialogAbort'):
            d = self.d['msg']
            if d.param and d.ui is None:                       # answered automatically: YES / OK
                cpu.w32(d.param + 0x1C, 0)                        # base.result: OK
                if cpu.r32(d.param) >= 0x244: cpu.w32(d.param + 0x240, 1)   # buttonPressed: YES / OK
            d.status = FINISHED; d.ui = None; self.ver += 1; r[2] = 0
        # ---- on-screen keyboard
        elif name == 'OskInitStart' and self.interactive:
            d = self.d['osk']; d.param = a0; d.status = INIT; d.visible_calls = 0; d.choice = None
            n = max(cpu.r32(a0 + 0x30), 1); data = cpu.r32(a0 + 0x34)
            desc = self.u16(cpu.r32(data + 28), 64); init = self.u16(cpu.r32(data + 32), 64)
            cap = cpu.r32(data + 36); lim = cpu.r32(data + 48)
            mx = min(x for x in (lim or 99, (cap - 1) if cap else 99, 40))
            d.ui = {'kind': 'osk', 'desc': desc or 'Enter text', 'text': (init or OSK_NAME)[:mx], 'max': max(mx, 1)}
            self.note(f"[dialog] KEYBOARD '{desc}' shown")
            self.ver += 1; r[2] = 0
        elif name == 'OskInitStart':
            d = self.d['osk']; d.param = a0; d.status = INIT; d.visible_calls = 0
            n = max(cpu.r32(a0 + 0x30), 1); data = cpu.r32(a0 + 0x34)
            for i in range(n):
                f = data + i * 52; out = cpu.r32(f + 40); cap = cpu.r32(f + 36); lim = cpu.r32(f + 48)
                desc = self.u16(cpu.r32(f + 28), 64)
                n = min(x for x in (len(OSK_NAME), lim or 99, (cap - 1) if cap else 99))
                txt = OSK_NAME[:max(n, 0)]
                if out: cpu.load_bytes(out, txt.encode('utf-16-le') + b'\0\0')
                cpu.w32(f + 44, 2)                                    # result: text changed
                self.note(f"[dialog] KEYBOARD '{desc}': typed {txt!r}")
            r[2] = 0
        elif name == 'OskGetStatus': r[2] = self.status(self.d['osk'])
        elif name == 'OskUpdate': r[2] = 0
        elif name == 'OskShutdownStart':
            d = self.d['osk']
            if d.param and d.ui is None: cpu.w32(d.param + 0x1C, 0)
            d.status = FINISHED; d.ui = None; self.ver += 1; r[2] = 0
        # ---- save data
        elif name == 'SavedataInitStart':
            d = self.d['save']; d.param = a0; d.status = INIT; d.visible_calls = 0; d.choice = None; d.ui = None
            ui = self.save_list(a0) if self.interactive else None
            if ui is not None: d.ui = ui; self.ver += 1; self.note(f"[savedata] {ui['title']} list shown: {[it[0] for it in ui['items']]}")
            else: res = self.savedata(a0); cpu.w32(a0 + 0x1C, res & 0xFFFFFFFF); self.note(f"[savedata] result {res & 0xFFFFFFFF:#x}")
            r[2] = 0
        elif name == 'SavedataGetStatus': r[2] = self.status(self.d['save'])
        elif name == 'SavedataUpdate': r[2] = 0
        elif name == 'SavedataShutdownStart': d = self.d['save']; d.status = FINISHED; d.ui = None; self.ver += 1; r[2] = 0
        return True

    def finish(self, d):
        """apply the player's answer (game thread)"""
        cpu = self.cpu(); p = d.param; ui = d.ui
        if ui['kind'] == 'msg':
            if p:
                cpu.w32(p + 0x1C, 0)
                if cpu.r32(p) >= 0x244: cpu.w32(p + 0x240, 3 if d.choice == 'cancel' else 1 if d.choice == 0 else 2)   # 1 yes 2 no 3 back
            self.note(f"[dialog] answered: {d.choice}")
        elif ui['kind'] == 'osk':
            n = max(cpu.r32(p + 0x30), 1); data = cpu.r32(p + 0x34)
            for i in range(n):
                f = data + i * 52; out = cpu.r32(f + 40)
                if d.choice == 'ok' and out: cpu.load_bytes(out, ui['text'].encode('utf-16-le') + b'\0\0')
                cpu.w32(f + 44, 2 if d.choice == 'ok' else 0)
            cpu.w32(p + 0x1C, 0 if d.choice == 'ok' else 1)
            self.note(f"[dialog] KEYBOARD: {ui['text']!r} ({d.choice})")
        elif ui['kind'] == 'list':
            if d.choice == 'cancel':
                cpu.w32(p + 0x1C, 1); self.note("[savedata] the player cancelled"); return   # result: cancelled
            name = ui['items'][d.choice][0]
            cpu.load_bytes(p + 0x4C, name.encode()[:19] + b'\0')         # the chosen slot, like the PSP writes it back
            res = self.savedata(p, name); cpu.w32(p + 0x1C, res & 0xFFFFFFFF); self.note(f"[savedata] result {res & 0xFFFFFFFF:#x}")

    def save_list(self, p):
        """the slot list for list-load / list-save / list-delete, or None if this mode shows no list"""
        cpu = self.cpu(); mode = cpu.r32(p + 0x30)
        if mode == 7: return self.all_saves_list()
        if mode not in (4, 5, 6): return None
        game = self.rstr(p + 0x3C, 13); lst = cpu.r32(p + 0x60); names = []
        if lst:
            for i in range(64):
                s = self.rstr(lst + 20 * i, 20)
                if not s: break
                names.append(s)
        if not names: return None
        items = []
        for nm in names:
            base = os.path.join(self.root(), 'PSP', 'SAVEDATA', game + nm); info = {}
            fn = self.rstr(p + 0x64, 13); exists = os.path.isdir(base) and (not fn or os.path.isfile(os.path.join(base, fn)))
            if exists:
                try: info = json.load(open(os.path.join(base, 'PARAM.json')))
                except (OSError, ValueError): info = {}
                when = time.strftime('%d %b %Y  %H:%M', time.localtime(os.path.getmtime(base)))
            else: when = ''
            if mode in (4, 6) and not exists: continue                     # loading / deleting: only what exists
            items.append((nm, (info.get('savedataTitle') or info.get('title') or ('Saved game' if exists else 'New save')).strip(),
                          (info.get('detail') or '').strip(), exists, when))
        if not items: return None                                         # nothing saved yet: answered as before
        title = {4: 'Load', 5: 'Save', 6: 'Delete'}[mode]
        sel = 0
        if mode == 5:                                                     # saving: start on the newest save
            ts = [(os.path.getmtime(os.path.join(self.root(), 'PSP', 'SAVEDATA', game + it[0])), i) for i, it in enumerate(items) if it[3]]
            if ts: sel = max(ts)[1]
        return {'kind': 'list', 'mode': title.lower(), 'title': title, 'items': items, 'sel': sel, 'confirm': None}

    def note(self, msg):
        if hasattr(self.m, 'note'): self.m.note(msg)
        else: self.m.log.append(msg)

    def all_saves_list(self):
        """delete-from-list over every save on the memory stick (the game uses it to make room)"""
        sd = os.path.join(self.root(), 'PSP', 'SAVEDATA'); items = []
        for dn in sorted(os.listdir(sd)):
            base = os.path.join(sd, dn)
            if not os.path.isdir(base): continue
            try: info = json.load(open(os.path.join(base, 'PARAM.json')))
            except (OSError, ValueError): info = {}
            when = time.strftime('%d %b %Y  %H:%M', time.localtime(os.path.getmtime(base)))
            title = (info.get('savedataTitle') or info.get('title') or dn).strip()
            items.append((dn, title, (info.get('detail') or '').strip(), True, when))
        if not items: return None
        return {'kind': 'list', 'mode': 'delete', 'title': 'Delete', 'items': items, 'sel': 0, 'confirm': None, 'all': True}

    def u16(self, a, n):
        if not a: return ''
        out = []
        for i in range(n):
            c = self.cpu().r16(a + 2 * i)
            if not c: break
            out.append(chr(c))
        return ''.join(out)

    # ------------------------------------------------------------------ save data
    def savedata(self, p, chosen=None):
        cpu = self.cpu(); mode = cpu.r32(p + 0x30)
        game = self.rstr(p + 0x3C, 13); save = self.rstr(p + 0x4C, 20); fname = self.rstr(p + 0x64, 13)
        names = []
        lst = cpu.r32(p + 0x60)
        if lst:
            for i in range(64):
                s = self.rstr(lst + 20 * i, 20)
                if not s: break
                names.append(s)
        if mode in (4, 5, 6) and names and not save.strip('<>'): save = names[0]
        if save in ('<>', ''): save = names[0] if names else save
        if chosen is not None: save = chosen
        if what_mode_is_all_delete(mode):
            if chosen is None: self.note("[savedata] delete-from-list without the player: cancelled"); return 1
            game = ''                                                       # (the list holds whole folder names)
        base = os.path.join(self.root(), 'PSP', 'SAVEDATA', game + save)
        buf, bufsize, size = cpu.r32(p + 0x74), cpu.r32(p + 0x78), cpu.r32(p + 0x7C)
        what = {0: 'autoload', 1: 'autosave', 2: 'load', 3: 'save', 4: 'listload', 5: 'listsave', 6: 'listdelete', 7: 'listalldelete',
                8: 'sizes', 9: 'autodelete', 10: 'delete', 11: 'list', 12: 'files', 13: 'makedatasecure', 14: 'makedata',
                15: 'readsecure', 16: 'readdata', 17: 'writesecure', 18: 'writedata', 19: 'erasesecure', 20: 'erase',
                21: 'deletedata', 22: 'getsize'}.get(mode, f'mode{mode}')
        self.saves.append(f"{what} {game}{save}/{fname}")
        self.note(f"[savedata] {what}: {game}{save}/{fname} buf={buf:#x} size={size:#x}")
        if what in ('autosave', 'save', 'listsave', 'makedata', 'makedatasecure', 'writedata', 'writesecure'):
            os.makedirs(base, exist_ok=True)
            if fname and buf: open(os.path.join(base, fname), 'wb').write(self.rbytes(buf, size))
            sfo = {k: self.rstr(p + 0x80 + o, n) for k, o, n in (('title', 0, 0x80), ('savedataTitle', 0x80, 0x80), ('detail', 0x100, 0x400))}
            json.dump(sfo, open(os.path.join(base, 'PARAM.json'), 'w'), indent=1)
            for nm, off in (('ICON0.PNG', 0x584), ('ICON1.PMF', 0x594), ('PIC1.PNG', 0x5A4), ('SND0.AT3', 0x5B4)):
                ib, isz = cpu.r32(p + off), cpu.r32(p + off + 8)
                if ib and isz: open(os.path.join(base, nm), 'wb').write(self.rbytes(ib, isz))
            return 0
        if what in ('autoload', 'load', 'listload', 'readdata', 'readsecure'):
            fp = os.path.join(base, fname) if fname else None
            if not fp or not os.path.isfile(fp): return 0x80110307 if what not in ('readdata', 'readsecure') else 0x80110339
            data = open(fp, 'rb').read()[:bufsize]
            if buf: cpu.load_bytes(buf, data)
            cpu.w32(p + 0x7C, len(data)); return 0
        if what in ('delete', 'listdelete', 'autodelete', 'listalldelete', 'deletedata', 'erase', 'erasesecure'):
            # Only ever inside ONE save folder (memstick/PSP/SAVEDATA/<game><save>): with no names given it used to point
            # at the SAVEDATA folder itself.
            sd = os.path.join(self.root(), 'PSP', 'SAVEDATA')
            if (what != 'listalldelete' and not game) or not save or os.path.normpath(os.path.dirname(base)) != os.path.normpath(sd):
                return 0x80110307 if what not in ('deletedata', 'erase', 'erasesecure') else 0x80110339
            if not os.path.isdir(base): return 0x80110307 if what not in ('deletedata', 'erase', 'erasesecure') else 0x80110339
            if what in ('deletedata', 'erase', 'erasesecure'):                    # one file of the save
                fp = os.path.join(base, fname) if fname else None
                if not fp or not os.path.isfile(fp): return 0x80110339
                os.remove(fp); return 0
            for f in os.listdir(base):                                             # the whole save
                fp = os.path.join(base, f)
                if os.path.isfile(fp): os.remove(fp)
            try: os.rmdir(base)
            except OSError: pass
            return 0
        if what == 'sizes':
            free = cpu.r32(p + 0x5D0); used = cpu.r32(p + 0x5D4); need = cpu.r32(p + 0x5D8)
            if free:
                cpu.w32(free, 0x8000); cpu.w32(free + 4, 0x8000); cpu.w32(free + 8, 1024 * 1024); cpu.load_bytes(free + 12, b'1 GB\0\0\0\0')
            res = 0
            if used:
                g, s = self.rstr(used, 13), self.rstr(used + 16, 20)
                d2 = os.path.join(self.root(), 'PSP', 'SAVEDATA', g + s)
                if os.path.isdir(d2):
                    kb = max(1, sum(os.path.getsize(os.path.join(d2, f)) for f in os.listdir(d2)) // 1024)
                    for o, v in ((36, (kb + 31) // 32), (40, kb), (52, kb)): cpu.w32(used + o, v)
                    cpu.load_bytes(used + 44, f"{kb}KB".encode()[:7] + b'\0'); cpu.load_bytes(used + 56, f"{kb}KB".encode()[:7] + b'\0')
                else: res = 0x801103C7
            if need:
                kb = 256
                for o, v in ((0, (kb + 31) // 32), (4, kb), (16, kb)): cpu.w32(need + o, v)
                cpu.load_bytes(need + 8, b'256KB\0\0\0'); cpu.load_bytes(need + 20, b'256KB\0\0\0')
            return res
        if what == 'list':
            idl = cpu.r32(p + 0x5F4)
            if not idl: return 0
            maxc, ents = cpu.r32(idl), cpu.r32(idl + 8); sd = os.path.join(self.root(), 'PSP', 'SAVEDATA'); n = 0
            for dn in sorted(os.listdir(sd)):
                if not dn.startswith(game) or n >= maxc: continue
                e = ents + n * 0x48; cpu.w32(e, 0x11FF)
                for k in range(4, 0x34, 4): cpu.w32(e + k, 0)
                cpu.load_bytes(e + 0x34, dn[len(game):].encode()[:19] + b'\0'); n += 1
            cpu.w32(idl + 4, n); return 0
        if what == 'getsize': return 0
        return 0
