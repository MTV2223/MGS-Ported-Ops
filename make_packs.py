# Metal Gear Solid Ported Ops - PACK MAKER (for whoever puts the port on GitHub)
# Makes the locked .pak files from the art and HD files in this folder, with the key from your disc image:
# Everything goes into the folder "pak":
#   pak\launcher_assets.pak          the launcher's intro and logo (from the folder launcher_assets) - goes in the code
#   pak\MGSPO-HD-Movies-N.pak        upscaled movies (from "HD movies" or SD_Dump\Movies(HD))
#   pak\MGSPO-HD-Textures-N.pak      upscaled textures (from "HD textures", except the folders in SKIP below), with
#                                    the pictures their textures.ini points to outside the folder (e.g. the cutscene
#                                    drawings in SD_Dump\Cutscene drawings(HD)) taken along and the list pointed at them
# Each HD pack stays under 1.9 GB (GitHub release files can be at most 2 GB): upload them to a GitHub release; players
# put them in their pak folder and the launcher installs them (DISC CHECK > INSTALL HD PACKS).
# A pack that is already made and still has the same files is kept as it is (only what changed is made again).
import os, sys, json, time, tempfile

HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import mgspo_pack as P

LIMIT = int(1.9 * 2**30)
OUT = os.path.join(HERE, "pak")
TMP = tempfile.mkdtemp(prefix="mgspo_packs_")
# texture folders NOT to pack: packs made by other people (share them only with their permission) - names inside
# "HD textures". ULUS10202 is TheGreatBulzome's PPSSPP pack.
SKIP = {"ulus10202"}
SKIP_FILES = {"textures.ini.original"}


def find_iso():
    try: iso = json.load(open(os.path.join(HERE, "launcher.ini"), encoding="utf-8")).get("iso")
    except Exception: iso = None
    if iso and os.path.exists(iso): return iso
    for n in sorted(os.listdir(HERE)):
        if n.lower().endswith(".iso"): return os.path.join(HERE, n)
    return None


def files_under(top, rel_top):
    out = []
    for root, ds, fs in os.walk(top):
        ds.sort()
        for f in sorted(fs):
            if f.lower() in SKIP_FILES or f.endswith((".part", ".installing")): continue
            p = os.path.join(root, f)
            out.append((rel_top + "/" + os.path.relpath(p, top).replace("\\", "/"), p))
    return out


def with_outside_pictures(folder, rel_top, tmpdir):
    """the files of one texture folder; if its textures.ini points outside the folder ("../"), those pictures come
    along into a subfolder and a changed textures.ini (pointing at them) is packed in place of the original"""
    fs = files_under(folder, rel_top)
    ini = os.path.join(folder, "textures.ini")
    if not os.path.exists(ini): return fs
    lines = open(ini, encoding="utf-8-sig", errors="replace").read().split("\n"); extra = {}; changed = False
    for i, line in enumerate(lines):
        body = line.split("#", 1)[0]
        if "=" not in body: continue
        k, v = body.split("=", 1); v = v.strip()
        if not v.startswith(".."): continue
        src = os.path.normpath(os.path.join(folder, v.replace("/", os.sep)))
        if not os.path.isfile(src): continue
        new = os.path.basename(os.path.dirname(src)) + "/" + os.path.basename(src)
        extra[new] = src; lines[i] = k.rstrip() + " = " + new; changed = True
    if not changed: return fs
    out = os.path.join(tmpdir, rel_top.replace("/", "_") + "_textures.ini")
    open(out, "w", encoding="utf-8", newline="").write("\n".join(lines))
    fs = [(r, out if os.path.normcase(p) == os.path.normcase(ini) else p) for r, p in fs]
    print(f"  {rel_top}: {len(extra)} pictures from outside the folder taken along")
    return fs + [(rel_top + "/" + r, p) for r, p in sorted(extra.items())]


def same_pack(path, key, fs):
    """an already made pack with exactly these files (names and sizes)"""
    try:
        pk = P.Pak(path)
        try:
            if not pk.opens_with(key): return False
            have = [(i["path"], i["size"]) for i in pk.unlock(key)]
        finally: pk.close()
        return have == [(r.replace("\\", "/"), os.path.getsize(p)) for r, p in fs]
    except Exception: return False


def split(files):
    parts = [[]]; size = 0
    for rel, p in files:
        s = os.path.getsize(p)
        if parts[-1] and size + s > LIMIT: parts.append([]); size = 0
        parts[-1].append((rel, p)); size += s
    return [x for x in parts if x]


def main():
    iso = find_iso()
    if not iso: print("No disc image found: put your .iso in this folder (or choose it in the launcher first)."); return 1
    key = P.disc_key(iso)
    if not key: print("The disc image", iso, "can't be read."); return 1
    print("Disc image:", iso)
    jobs = []
    la = os.path.join(HERE, "launcher_assets")
    if os.path.isdir(la):
        fs = [(rel.split("/", 1)[1], p) for rel, p in files_under(la, "x") if not os.path.basename(p).startswith("ui_")]
        jobs.append((os.path.join(OUT, "launcher_assets.pak"), fs))
    mv = next((os.path.join(HERE, d) for d in ("HD movies", os.path.join("SD_Dump", "Movies(HD)")) if os.path.isdir(os.path.join(HERE, d))), None)
    if mv:
        fs = [(rel, p) for rel, p in files_under(mv, "HD movies") if p.lower().endswith(".mp4")]
        for i, part in enumerate(split(fs), 1): jobs.append((os.path.join(OUT, f"MGSPO-HD-Movies-{i}.pak"), part))
    tx = os.path.join(HERE, "HD textures")
    if os.path.isdir(tx):
        fs = []
        for name in sorted(os.listdir(tx)):
            p = os.path.join(tx, name)
            if name.lower() in SKIP: print("Not packed (someone else's pack):", "HD textures\\" + name); continue
            if os.path.isdir(p): fs += with_outside_pictures(p, "HD textures/" + name, TMP)
            elif name.lower() not in SKIP_FILES: fs.append(("HD textures/" + name, p))
        for i, part in enumerate(split(fs), 1): jobs.append((os.path.join(OUT, f"MGSPO-HD-Textures-{i}.pak"), part))
    os.makedirs(OUT, exist_ok=True)
    for out, fs in jobs:
        old = [p for p in (out, os.path.join(HERE, "packs", os.path.basename(out)), os.path.join(HERE, os.path.basename(out))) if os.path.exists(p)]
        keep = next((p for p in old if same_pack(p, key, fs)), None)
        if keep:
            if keep != out: os.replace(keep, out)
            print(f"\n{os.path.relpath(out, HERE)}: unchanged - kept"); continue
        tot = sum(os.path.getsize(p) for _, p in fs); t0 = time.time(); last = [0.0]
        print(f"\n{os.path.relpath(out, HERE)}: {len(fs)} files, {tot / 2**20:.0f} MB")
        def prog(done, total, name):
            if time.time() - last[0] > 0.5: last[0] = time.time(); print(f"\r  {100 * done / max(1, total):5.1f}%  {name[-60:]:60}", end="", flush=True)
        P.write_pak(out, key, fs, prog)
        print(f"\r  done in {time.time() - t0:.0f} s{' ' * 70}")
    for p in os.listdir(OUT):                                             # (packs left over from a bigger earlier run)
        if p.startswith("MGSPO-HD-") and os.path.join(OUT, p) not in [o for o, _ in jobs]: print("Note: pak\\" + p, "is not part of this set any more - delete it")
    print("\nAll done. Upload the MGSPO-HD-*.pak files in the folder 'pak' to a GitHub release (not into the code).")
    print("pak\\launcher_assets.pak goes into the code (it is small).")
    return 0


if __name__ == "__main__":
    r = main()
    input("\nPress Enter to close this window.")
    sys.exit(r)
