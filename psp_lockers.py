"""Locker capture (a port option, port_settings.txt: locker_capture = on / off).

Hold Shift (keyboard) or R2 (controller) while Snake walks into an open locker dragging a knocked-out or sleeping
enemy: when the body is inside, the enemy is captured - exactly as if he had been loaded onto the truck. The game's
own "captured" script runs for him (the same one the truck runs), so he goes onto your roster and the game shows
CAPTURE COMPLETE! as usual. Without Shift / R2 held, lockers work as they always did.

How: three of the game's own routines (in the stage code) get a small hook -
  the body being dragged (each game frame, to know that Snake is dragging someone),
  the body starting to go into a locker (Shift / R2 held now? remember him),
  the body ending up inside the locker (then: the soldier's own "captured" script, like the truck's).
The routines are found by their place in the stage code and checked word by word first; if anything differs (another
version of the game, another stage module) nothing is hooked and lockers simply work as before.
"""

# place in the stage module "mgp_stage" and the first 24 instructions (jumps / calls left out: those depend on where the
# module was loaded)
_F = {
    'drag': (0xba3d8, (0x27bdffe0, 0xafb3000c, 0x00a09821, 0x24050004, 0xafb00000, 0x00c08021, 0xafb10004, 0x00808821, 0xafbf0010, 0x00000000, 0xafb20008, 0x3a030002, 0x00402821, 0x2c630001, 0x2e020001, 0x00431025, 0x10400007, 0x24120001, 0x26240020, 0x8c8300b8, 0x3c020002, 0x34424000, 0x00621825, 0xac8300b8)),
    'dump': (0xbabf0, (0x27bdffe0, 0xafb3000c, 0x00a09821, 0x24050004, 0xafb00000, 0x00c08021, 0xafb10004, 0x00808821, 0xafbf0010, 0x00000000, 0xafb20008, 0x3a030002, 0x00402821, 0x2c630001, 0x2e020001, 0x00431025, 0x10400007, 0x24120001, 0x26240020, 0x8c8300b8, 0x3c020002, 0x34424000, 0x00621825, 0xac8300b8)),
    'inlk': (0xbafc8, (0x27bdfff0, 0xafb00000, 0x24050004, 0x00c08021, 0xafb20008, 0x00e09021, 0xafb10004, 0xafbf000c, 0x00000000, 0x00808821, 0x3a030002, 0x2c630001, 0x00404021, 0x00402821, 0x2e020001, 0x3c060002, 0x00431025, 0x02202021, 0x26270020, 0x34c64000, 0x10400004, 0x2a030002, 0x8ce200b8, 0x00461025)),
    'capt': (0x3c320, (0x27bdffd0, 0xafbf0020, 0x00803021, 0x03a02821, 0x8c821438, 0x14400004, 0x00402021, 0x8fbf0020, 0x03e00008, 0x27bd0030, 0x8cc71850, 0x8cc305cc, 0x8cc618a0, 0x24020004, 0xa7a20000, 0x27a20010, 0xafa30010, 0xafa60014, 0xafa70018, 0xafa20004, 0x00000000, 0xafa0001c, 0x00000000, 0x8fbf0020)),
    # what the truck does with the body first: he is taken off the map (fades away, no longer there)
    'gone': (0x664dc, (0x27bdfff0, 0xafbf0008, 0x3c058000, 0xafb10004, 0x00808821, 0xafb00000, 0x00000000, 0x8c901bac, 0x00001021, 0x00001821, 0xae221888, 0xae23188c, 0x3c050001, 0x8e240010, 0x00000000, 0xae2018b8, 0x00000000, 0x02002021)),
}


def _matches(cpu, addr, sig):
    try:
        for i, want in enumerate(sig):
            w = cpu.r32(addr + 4 * i)
            if (w >> 26) in (2, 3): w = 0
            if w != want: return False
        return True
    except Exception:
        return False


def _find_base(m):
    for mod in list(getattr(m, 'modules', {}).values()):
        if mod is None or getattr(mod, 'name', '') != 'mgp_stage': continue
        base = mod.lo
        if all(_matches(m.cpu, base + off, sig) for off, sig in _F.values()): return base
    return None


def update(m, on=True):
    """call now and then from the game thread (between steps): (re)places the hooks when the stage code changes"""
    st = m.__dict__.setdefault('_lk', {'base': None, 'flag': set(), 'done': set()})
    base = _find_base(m) if on else None
    hooks = m.__dict__.get('_hooks') or {}
    if base == st['base'] and (base is None or all(base + off in hooks for k, (off, _) in _F.items() if k not in ('capt', 'gone'))): return
    if st['base'] is not None:
        for k, (off, _) in _F.items():
            if k not in ('capt', 'gone'): m.hook_clear(st['base'] + off)
    st['base'] = base; st['flag'] = set(); st['done'] = set()
    if base is None: return
    def on_drag(cpu): m._lk_drag_t = m.now()
    def on_dump(cpu):
        if cpu.r[6] == 0 and getattr(m, 'capture_held', False): st['flag'].add(cpu.r[4])
    def on_inlk(cpu):
        s = cpu.r[4]
        if cpu.r[6] == 0:                                               # he arrives inside the locker
            if s in st['flag'] or getattr(m, 'capture_held', False): st['pend'] = s
            st['flag'].discard(s); return
        if st.get('pend') != s: return                                 # (the frame after: the game has settled him in)
        st['pend'] = None
        key = (s, cpu.r32(s))
        if key in st['done']: return
        st['done'].add(key)
        m.call_nested(base + _F['gone'][0], s, 0)                      # as the truck does: the body leaves the map ...
        m.call_nested(base + _F['capt'][0], s, 0)                      # ... and the soldier's own "captured" script runs
        print("locker capture: enemy at %#x captured" % s, flush=True)
    m.hook_set(base + _F['drag'][0], on_drag)
    m.hook_set(base + _F['dump'][0], on_dump)
    m.hook_set(base + _F['inlk'][0], on_inlk)


def dragging(m, within_us=300000):
    """Snake is dragging a body (seen in the last 0.3 s of game time)"""
    return st_ok(m) and m.now() - getattr(m, '_lk_drag_t', -1e18) < within_us


def st_ok(m):
    st = m.__dict__.get('_lk')
    return st is not None and st['base'] is not None
