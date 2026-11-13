# perf_log.txt: 10 seconds of exact timings (F7 in play.py), turned into a report of where the time went and why
# pictures came late. The raw tables at the end are for Claude.
SHORT = ""


def _pct(v, p):
    if not v: return 0.0
    v = sorted(v); return v[min(len(v) - 1, int(len(v) * p / 100))]


def _ms(x): return f"{x * 1000:.1f}"


def report(d, jobs, info):
    global SHORT
    L = []; w = L.append
    t0, t1 = d['t0'], d['t1']; dur = t1 - t0
    period, k, margin, lat = d.get('period') or 1 / 60, d.get('k') or 1, d.get('margin') or 0.002, d.get('lat') or 0.007
    tscf = None
    if d.get('tsc0') and d.get('tsc1') and t1 > t0: tscf = (d['tsc1'] - d['tsc0']) / dur
    def tms(c): return c / tscf * 1000 if tscf else 0.0
    st = info.get('settings', {})
    w("MGS Portable Ops PC port - perf_log (F7): %.1f s recorded" % dur)
    w("renderer: %s   window %sx%s %s   lighting: %s   picture timing: %s" % (info.get('renderer', '?'), info['size'][0], info['size'][1],
      '(fullscreen)' if info.get('fullscreen') else '(window)', 'remaster' if info.get('lighting') else 'off', 'held' if info.get('hold') else 'direct'))
    w("settings: " + ", ".join(f"{a}={st.get(a)}" for a in ('resolution', 'anti_aliasing', 'texture_filter', 'hd_textures', 'fps_cap', 'shadows', 'ambient_occlusion')))
    w("screen: %.1f Hz, a picture every %d refreshes, drawn %.1f ms early, shown %.1f ms before its refresh" % (1 / period, k, lat * 1000, margin * 1000))
    w("")
    # ---- pictures
    P = d['P']; J = {j[0]: j for j in d['J'] if j[0] is not None}; T = d.get('T') or {}
    posted = [p for p in P if p[3]]; late = []; rows = []
    jobs = sorted(jobs, key=lambda j: j[2])
    def overlap(a, b):
        out = {}; idle = b - a
        for kind, tp, s, e, c in jobs:
            if e <= a or s >= b: continue
            o = min(e, b) - max(s, a); out[kind] = out.get(kind, 0.0) + o; idle -= o
        return out, max(0.0, idle)
    causes = {}
    for p in posted:
        seq, v, tpost, _, tcall, dcall, res, qs = p
        j = J.get(seq)
        if j is None: continue
        _, js, jsw, je, tm = j
        lateness = je - (v - margin)
        waits, idle = overlap(tpost, js)
        tmm = [tms(x) for x in tm] if tm else [0.0] * 8
        r = dict(seq=seq, v=v, late=lateness, wait=js - tpost, waits=waits, idle=idle, draw=jsw - js, swap=je - jsw, tm=tmm, res=res, qs=qs,
                 t=T.get(seq))
        rows.append(r)
        if res == 0 or lateness > 0:
            late.append(r)
            parts = dict(("waiting behind " + k2, v2) for k2, v2 in waits.items())
            parts['drawing thread idle (waiting its turn: Python / Windows)'] = idle
            parts['drawing this picture'] = r['draw']
            sw_other = r['swap'] - (tmm[0] + tmm[1] + tmm[2] + tmm[3] + tmm[4]) / 1000
            parts['waiting for the graphics card (previous pictures)'] = tmm[0] / 1000
            parts['waiting for the lock (picture being shown)'] = tmm[1] / 1000
            parts['getting a window image'] = tmm[2] / 1000
            parts['sending to the graphics card'] = (tmm[3] + tmm[4]) / 1000 + max(0.0, sw_other)
            top = max(parts, key=parts.get); causes[top] = causes.get(top, 0) + 1
    n = len(posted)
    w("PICTURES: %d made, %d not ready at their refresh (%.0f%%)" % (n, len(late), 100 * len(late) / max(1, n)))
    if rows:
        w("  time from 'make it' to ready (ms): median %s, 95%% %s, worst %s   (it has %.1f ms)" % (
          _ms(_pct([r['wait'] + r['draw'] + r['swap'] for r in rows], 50)), _ms(_pct([r['wait'] + r['draw'] + r['swap'] for r in rows], 95)),
          _ms(max(r['wait'] + r['draw'] + r['swap'] for r in rows)), (lat - margin) * 1000))
        w("    waiting for the drawing thread: median %s, 95%% %s, worst %s" % (_ms(_pct([r['wait'] for r in rows], 50)), _ms(_pct([r['wait'] for r in rows], 95)), _ms(max(r['wait'] for r in rows))))
        w("    drawing the picture:            median %s, 95%% %s, worst %s" % (_ms(_pct([r['draw'] for r in rows], 50)), _ms(_pct([r['draw'] for r in rows], 95)), _ms(max(r['draw'] for r in rows))))
        w("    handing it to the card:         median %s, 95%% %s, worst %s" % (_ms(_pct([r['swap'] for r in rows], 50)), _ms(_pct([r['swap'] for r in rows], 95)), _ms(max(r['swap'] for r in rows))))
    if causes:
        w("  main reason for each late picture:")
        for c, nn in sorted(causes.items(), key=lambda x: -x[1]): w("    %4d  %s" % (nn, c))
    w("")
    # ---- interpolation
    tr = [x[0] for x in T.values() if x and x[0] is not None]
    if tr:
        a = sum(1 for x in tr if x < -1); b = sum(1 for x in tr if -1 <= x < 0); c = sum(1 for x in tr if 0 <= x <= 1); e = sum(1 for x in tr if x > 1)
        w("IN-BETWEEN PICTURES (%d): normal %d, used the pair before (a frame came early) %d, too early even for that %d, ran out (no newer frame yet) %d"
          % (len(tr), c, b, a, e))
    C = d['C']
    if len(C) > 2:
        lt = [x[3] / 1000 for x in C]; iv = [(C[i][2] - C[i - 1][2]) * 1000 for i in range(1, len(C))]
        gv = [(C[i][0] - C[i - 1][0]) / 1000 for i in range(1, len(C))]
        w("GAME FRAMES: %d; ready this late after being made (ms): 10%% %.1f, median %.1f, 90%% %.1f, worst %.1f" % (len(C), _pct(lt, 10), _pct(lt, 50), _pct(lt, 90), max(lt)))
        w("  time between them: game clock median %.1f ms; real time median %.1f, 5%% %.1f, 95%% %.1f, worst %.1f" % (_pct(gv, 50), _pct(iv, 50), _pct(iv, 5), _pct(iv, 95), max(iv)))
        w("  game frame made -> on the drawing thread done (ms): median %s, 95%% %s" % (_ms(_pct([x[2] - x[1] for x in C], 50)), _ms(_pct([x[2] - x[1] for x in C], 95))))
    w("")
    # ---- drawing thread
    w("DRAWING THREAD (%.1f s): busy %.0f%%" % (dur, 100 * sum(min(e, t1) - max(s, t0) for _, _, s, e, _ in jobs if e > t0 and s < t1) / dur))
    kinds = {}
    for kind, tp, s, e, c in jobs: kinds.setdefault(kind, []).append((e - s, c, s - tp))
    w("  %-16s %6s %9s %9s %9s %9s %10s %10s" % ('job', 'count', 'total ms', 'mean', '95%', 'worst', 'not CPU ms', 'queued 95%'))
    for kind, v in sorted(kinds.items(), key=lambda x: -sum(y[0] for y in x[1])):
        ws = [y[0] for y in v]
        w("  %-16s %6d %9.1f %9.2f %9.2f %9.2f %10.1f %10.2f" % (kind, len(v), sum(ws) * 1000, sum(ws) / len(v) * 1000, _pct(ws, 95) * 1000, max(ws) * 1000,
          max(0.0, sum(y[0] - y[1] for y in v)) * 1000, _pct([y[2] for y in v], 95) * 1000))
    w("  (not CPU ms: rough - Windows counts processor time in steps of about 16 ms, so only large values mean anything)")
    long = [j for j in jobs if j[0] != 'picture' and j[3] - j[2] > lat - margin]
    w("  jobs longer than a picture's whole time (%.1f ms): %d" % ((lat - margin) * 1000, len(long)))
    w("")
    # ---- game thread
    G = d['G']
    if G:
        ws = [e - s for s, e, c in G]; nc = max(0.0, sum((e - s) - c for s, e, c in G))
        w("GAME THREAD: %d steps, %.0f%% of the time; step ms median %.2f, 95%% %.2f, worst %.2f; not on the CPU while running %.0f ms (waiting for Python/Windows)"
          % (len(G), 100 * sum(ws) / dur, _pct(ws, 50) * 1000, _pct(ws, 95) * 1000, max(ws) * 1000, nc * 1000))
        gaps = [G[i][0] - G[i - 1][1] for i in range(1, len(G))]
        w("  gaps between steps: 95%% %.2f ms, worst %.2f ms" % (_pct(gaps, 95) * 1000, max(gaps) * 1000 if gaps else 0))
    if d.get('tm0') and d.get('tm1') and tscf:
        dt = [tms(d['tm1'][i] - d['tm0'][i]) for i in range(8)]
        w("GRAPHICS DRIVER (ms in %.0f s): waiting for the card %.1f, lock (drawing) %.1f, window image %.1f, sending %.1f, showing %.1f, pacer show %.1f, new pipelines %.1f, lock (pacer) %.1f"
          % (dur, *dt))
    w("")
    # ---- when pictures really reached the screen (Vulkan, present_wait)
    S = d.get('S')
    if S is None: w("ON THE SCREEN: not measurable here (OpenGL, or the driver has no present_wait)")
    elif tscf and S:
        vby = {p[0]: p[1] for p in P}
        sh = [(seq, t0 + (b - d['tsc0']) / tscf, t0 + (c - d['tsc0']) / tscf) for seq, b, c in S]
        sh = [x for x in sh if x[0] in vby]
        ints = [sh[i][2] - sh[i - 1][2] for i in range(1, len(sh)) if sh[i][0] == sh[i - 1][0] + 1]
        want = (round(_pct(ints, 50) / period) or k) * period if ints else period * k   # (original frame rate runs at 60 whatever the cap)
        off = [x for x in ints if abs(x - want) > period * 0.5]
        rel = [x[2] - vby[x[0]] for x in sh]
        w("ON THE SCREEN (%d pictures measured): time between pictures should be %.1f ms; %d of %d were not (%.1f%%)" % (len(sh), want * 1000, len(off), len(ints), 100 * len(off) / max(1, len(ints))))
        if ints:
            from collections import Counter
            c = Counter(round(x / period) for x in ints)
            w("  time between pictures in screen refreshes: " + ", ".join(f"{kk} refreshes: {vv}" for kk, vv in sorted(c.items())))
        if rel: w("  shown vs planned refresh (ms): 5%% %.1f, median %.1f, 95%% %.1f" % (_pct(rel, 5) * 1000, _pct(rel, 50) * 1000, _pct(rel, 95) * 1000))
        if (info or {}).get('fullscreen'): w("  (full screen: the driver tells when a picture is handed over, not when the screen shows it - these figures aren't reliable there)")
        SHOWN = (len(off), len(ints))
    w("")
    w("WORST PICTURES (ms; 'late' = after the moment it had to be ready):")
    for r in sorted(rows, key=lambda r: -r['late'])[:15]:
        wt = ", ".join(f"{k2} {v2 * 1000:.1f}" for k2, v2 in sorted(r['waits'].items(), key=lambda x: -x[1]))
        w("  #%d late %+.1f: waited %.1f (%s; idle %.1f), drew %.1f, handed over %.1f [card %.1f, lock %.1f, image %.1f, send %.1f, show %.1f, pipes %.1f]%s"
          % (r['seq'], r['late'] * 1000, r['wait'] * 1000, wt or '-', r['idle'] * 1000, r['draw'] * 1000, r['swap'] * 1000,
             r['tm'][0], r['tm'][1], r['tm'][2], r['tm'][3], r['tm'][4], r['tm'][6], '' if r['res'] else ' (shown late)'))
    # ---- short verdict
    sh = []
    sh.append("  pictures not ready in time: %d of %d" % (len(late), n))
    if causes: sh.append("  main reason: " + max(causes, key=causes.get))
    if tr: sh.append("  in-between pictures that ran out of game frames: %d of %d" % (sum(1 for x in tr if x > 1), len(tr)))
    if 'SHOWN' in dir(): sh.append("  pictures not shown evenly by the screen: %d of %d" % SHOWN)
    SHORT = "\n".join(sh)
    w(""); w("IN SHORT:"); L.extend(sh)
    # ---- raw (for Claude)
    w(""); w("RAW PICTURES: seq, refresh ms, posted ms, start ms, drew ms, swap ms, done-vs-deadline ms, shown on time, t raw, t used, queue, timers ms")
    for r in rows:
        j = J[r['seq']]
        w("%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%s,%s,%s,%d,%s" % (r['seq'], (r['v'] - t0) * 1000, (j[1] - r['wait'] - t0) * 1000, (j[1] - t0) * 1000, r['draw'] * 1000,
          r['swap'] * 1000, r['late'] * 1000, r['res'], '%.3f' % r['t'][0] if r['t'] and r['t'][0] is not None else '', '%.3f' % r['t'][1] if r['t'] else '', r['qs'],
          "/".join("%.2f" % x for x in r['tm'])))
    w(""); w("RAW DRAWING JOBS: kind, posted ms, start ms, end ms, cpu ms")
    for kind, tp, s, e, c in jobs:
        if e < t0 or s > t1: continue
        w("%s,%.2f,%.2f,%.2f,%.2f" % (kind, (tp - t0) * 1000, (s - t0) * 1000, (e - t0) * 1000, c * 1000))
    w(""); w("RAW GAME FRAMES: game us, posted ms, done ms, late us")
    for E, tp, tj, lt in C: w("%d,%.2f,%.2f,%d" % (E, (tp - t0) * 1000, (tj - t0) * 1000, lt))
    w(""); w("RAW GAME STEPS (only > 2 ms): start ms, ms, cpu ms")
    for s, e, c in G:
        if e - s > 0.002: w("%.2f,%.2f,%.2f" % ((s - t0) * 1000, (e - s) * 1000, c * 1000))
    return "\n".join(L) + "\n"
