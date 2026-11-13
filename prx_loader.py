#!/usr/bin/env python3
"""
PSP PRX / relocatable-ELF loader (decrypted modules only).
Loads segments into a flat memory image at a chosen base address, applies the
PSP relocation tables (R_MIPS_32 / 26 / HI16 / LO16) and builds the import-stub map.

Usage:  python prx_loader.py module.prx [base_hex]     (default base 0x08804000)
Library use:
    from prx_loader import PrxModule
    m = PrxModule.load("EBOOT.BIN", 0x08804000)
    m.read32(addr), m.stubs[addr] -> (library, nid)
"""
import struct, sys

R_MIPS_NONE, R_MIPS_16, R_MIPS_32, R_MIPS_26, R_MIPS_HI16, R_MIPS_LO16, R_MIPS_GPREL16 = 0, 1, 2, 4, 5, 6, 7
SHT_PRXRELOC = 0x700000A0

def s16(x): return x - 0x10000 if x & 0x8000 else x

class PrxModule:
    def __init__(self):
        self.segs = []      # (index, vaddr_rel, filesz, memsz)
        self.seg_base = []  # absolute base address of each loadable segment
        self.mem = None; self.lo = self.hi = 0
        self.stubs = {}; self.imports = []; self.name = ""; self.stats = {}; self.exports = {}; self.entry = 0

    # ---------------------------------------------------------------- memory
    def _off(self, a):
        o = a - self.lo
        if not 0 <= o <= len(self.mem) - 4: raise ValueError(f"address {a:#x} outside module")
        return o
    def read32(self, a): return struct.unpack_from('<I', self.mem, self._off(a))[0]
    def write32(self, a, v): struct.pack_into('<I', self.mem, self._off(a), v & 0xFFFFFFFF)

    # ---------------------------------------------------------------- loading
    @classmethod
    def load(cls, path, base=0x08804000):
        """path: a file name, or the module's bytes themselves (no temporary file needed)"""
        d = bytes(path) if isinstance(path, (bytes, bytearray, memoryview)) else open(path, 'rb').read()
        if d[:4] != b'\x7fELF': raise ValueError("not an ELF (still encrypted?)")
        m = cls()
        etype = struct.unpack('<H', d[16:18])[0]
        phoff = struct.unpack('<I', d[28:32])[0]; phn = struct.unpack('<H', d[44:46])[0]
        loads = []
        for i in range(phn):
            t, off, va, pa, fs, ms, fl, al = struct.unpack('<IIIIIIII', d[phoff+i*32:phoff+(i+1)*32])
            if t == 1: loads.append((i, off, va, fs, ms))
        if etype == 0xFFA0:           # relocatable PRX: segment addresses are module-relative
            seg_abs = {i: base + va for i, off, va, fs, ms in loads}
        else:                          # fixed-address ELF
            seg_abs = {i: va for i, off, va, fs, ms in loads}
        m.lo = min(seg_abs.values()); m.hi = max(seg_abs[i] + ms for i, off, va, fs, ms in loads)
        m.mem = bytearray(m.hi - m.lo)
        for i, off, va, fs, ms in loads:
            m.mem[seg_abs[i]-m.lo: seg_abs[i]-m.lo+fs] = d[off:off+fs]
            m.segs.append((i, va, fs, ms)); m.seg_base.append(seg_abs[i])
        m._sections(d)
        if etype == 0xFFA0: m._relocate(d)
        m._imports(d)
        m._exports()
        m.entry = struct.unpack('<I', d[24:28])[0]
        return m

    def _sections(self, d):
        shoff = struct.unpack('<I', d[32:36])[0]; shes, shn, shstr = struct.unpack('<HHH', d[46:52])
        S = [struct.unpack('<IIIIIIIIII', d[shoff+i*shes:shoff+(i+1)*shes]) for i in range(shn)]
        nm = d[S[shstr][4]:S[shstr][4]+S[shstr][5]]
        self.sec = {nm[s[0]:nm.index(b'\0', s[0])].decode(): s for s in S}
        self.S = S
        mi = self.sec.get('.rodata.sceModuleInfo')
        if mi: self.name = d[mi[4]+4:mi[4]+32].split(b'\0')[0].decode()

    # ------------------------------------------------------------- relocation
    def _relocate(self, d):
        stats = {k: 0 for k in ('32', '26', 'hi', 'lo', 'skipped')}
        pending = []                       # HI16 entries waiting for their LO16
        for s in self.S:
            if s[1] != SHT_PRXRELOC: continue
            for k in range(s[5] // 8):
                r_off, r_info = struct.unpack_from('<II', d, s[4] + k*8)
                typ, ofs_seg, addr_seg = r_info & 0xFF, (r_info >> 8) & 0xFF, (r_info >> 16) & 0xFF
                try:
                    where = self.seg_base[ofs_seg] + r_off
                    add = self.seg_base[addr_seg]
                    w = self.read32(where)
                except (IndexError, ValueError):
                    stats['skipped'] += 1; continue
                if typ == R_MIPS_32:
                    self.write32(where, w + add); stats['32'] += 1
                elif typ == R_MIPS_26:
                    tgt = (((w & 0x03FFFFFF) << 2) + add) >> 2
                    self.write32(where, (w & 0xFC000000) | (tgt & 0x03FFFFFF)); stats['26'] += 1
                elif typ == R_MIPS_HI16:
                    pending.append((where, add)); stats['hi'] += 1
                elif typ == R_MIPS_LO16:
                    full_lo = s16(w & 0xFFFF)
                    for hwhere, hadd in pending:           # fix every HI16 that pairs with this LO16
                        hw = self.read32(hwhere)
                        full = ((hw & 0xFFFF) << 16) + full_lo + hadd
                        hi = ((full - s16(full & 0xFFFF)) >> 16) & 0xFFFF
                        self.write32(hwhere, (hw & 0xFFFF0000) | hi)
                    self.write32(where, (w & 0xFFFF0000) | ((full_lo + add) & 0xFFFF))
                    stats['lo'] += 1
                    pending = []
                else:
                    stats['skipped'] += 1
        self.stats = stats

    # ---------------------------------------------------------------- exports
    def _exports(self):
        se = self.sec.get('.lib.ent')
        if not se or not self.seg_base: return
        base0 = self.seg_base[0]
        o = base0 + se[3]
        for i in range(se[5] // 16):
            a = o + 16*i
            try:
                nameptr = self.read32(a); w = self.read32(a+4); cnt = self.read32(a+8); tbl = self.read32(a+12)
                fcount = (cnt >> 16) & 0xFFFF; vcount = (w >> 24)  # layout: u16 ver, u16 attr | u8 size, u8 vcount, u16 fcount
            except ValueError: continue
            fcount = self.read32(a+8) >> 16 if False else (self.read32(a+4) >> 16) & 0xFFFF
            # entry: name(4) ver/attr(4) size/vcount/fcount(4) table(4)
            info = self.read32(a+8); fcount = (info >> 16) & 0xFFFF; vcount = (info >> 8) & 0xFF
            lib = self.cstr(nameptr) if nameptr else None
            if lib is None or not (0 < fcount < 2000): continue
            nid_tbl = tbl
            for j in range(fcount):
                try: self.exports[(lib, self.read32(nid_tbl + 4*j))] = self.read32(nid_tbl + 4*(fcount + vcount + j))
                except ValueError: break

    def cstr(self, a):
        o = self._off(a); e = self.mem.index(b'\0', o); return self.mem[o:e].decode('latin1')

    # ---------------------------------------------------------------- imports
    def _imports(self, d):
        st = self.sec.get('.lib.stub')
        if not st: return
        delta = st[4] - st[3]
        for i in range(st[5] // 20):
            o = st[4] + i*20
            nameptr, ver, flags, size, vcnt, fcnt, nidptr, stubptr = struct.unpack('<IHHBBHII', d[o:o+20])
            lib = d[nameptr+delta:d.index(b'\0', nameptr+delta)].decode('latin1')
            nids = struct.unpack('<%dI' % fcnt, d[nidptr+delta:nidptr+delta+4*fcnt])
            sbase = (self.seg_base[0] + stubptr) if self.segs and self.name else stubptr
            for j, nid in enumerate(nids):
                self.stubs[sbase + 8*j] = (lib, nid)
            self.imports.append((lib, fcnt))

if __name__ == '__main__':
    path = sys.argv[1]; base = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x08804000
    m = PrxModule.load(path, base)
    print(f'module "{m.name}"  image {m.lo:#x}-{m.hi:#x} ({(m.hi-m.lo)/1024:.0f} KB)')
    print('segments:', [(hex(a), hex(ms)) for a, (i, va, fs, ms) in zip(m.seg_base, m.segs)])
    print('relocations applied:', m.stats)
    print('import stubs:', len(m.stubs))
