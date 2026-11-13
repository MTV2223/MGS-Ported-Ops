#!/usr/bin/env python3
"""
Runs the decrypted MGS Portable Ops EBOOT in the Python interpreter with stand-in PSP system calls.
Usage: python psp_run.py path/to/decrypted_EBOOT.BIN [max_instructions]
It runs until something is missing, then prints what stopped it. That list is the work still to do.
"""
import time, sys, struct, os
try: sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
except NameError: pass            # when started from the Python window, add the folder yourself (see instructions)
from prx_loader import PrxModule
from psp_cpu import CPU, Stop, Yield, Limit, Returned, RAM_LO

BASE = 0x08804000
NAMES = {  # NID -> (library, function). Written from memory; only NIDs actually present in the file are ever used.
 0x446D8DE6:"sceKernelCreateThread",0xF475845D:"sceKernelStartThread",0xAA73C935:"sceKernelExitThread",0x809CE29B:"sceKernelExitDeleteThread",
 0xCEADEB47:"sceKernelDelayThread",0x9ACE131E:"sceKernelSleepThread",0x82826F70:"sceKernelSleepThreadCB",0xFCCFAD26:"sceKernelCancelWakeupThread",0xD6DA4BA1:"sceKernelCreateSema",0x4E3A1105:"sceKernelWaitSema",0x6D212BAC:"sceKernelWaitSemaCB",0x840E8133:"sceKernelWaitThreadEndCB",0x58B1F937:"sceKernelPollSema",0x36AA6E91:"sceImposeSetLanguageMode",0x24FD7BCF:"sceImposeGetLanguageMode",0xC6183D47:"sceUmdActivate",0x37FB5C42:"sceKernelGetGPI",0xEA748E31:"sceKernelChangeCurrentThreadAttr",
 0x3F53E640:"sceKernelSignalSema",0x28B6489C:"sceKernelDeleteSema",0x369ED59D:"sceKernelGetSystemTimeLow",0x82BC5777:"sceKernelGetSystemTimeWide",
 0x293B45B8:"sceKernelGetThreadId",0x55C20A00:"sceKernelCreateEventFlag",0x9FA03CD3:"sceKernelDeleteThread",0x278C0DF5:"sceKernelWaitThreadEnd",
 0xE81CAF8F:"sceKernelCreateCallback",0x71BC9871:"sceKernelChangeThreadPriority",0x17C1684E:"sceKernelReferThreadStatus",0x383F7BCC:"sceKernelTerminateDeleteThread",
 0x237DBD4F:"sceKernelAllocPartitionMemory",0xB6D61D02:"sceKernelFreePartitionMemory",0x9D9A5BA1:"sceKernelGetBlockHeadAddr",
 0xA291F107:"sceKernelMaxFreeMemSize",0xF919F628:"sceKernelTotalFreeMemSize",0x3FC9AE6A:"sceKernelDevkitVersion",
 0x092968F4:"sceKernelCpuSuspendIntr",0x5F10D406:"sceKernelCpuResumeIntr",
 0x109F50BC:"sceIoOpen",0x810C4BC3:"sceIoClose",0x6A638D83:"sceIoRead",0x27EB27B8:"sceIoLseek",0x42EC03AC:"sceIoWrite",
 0x977DE386:"sceKernelLoadModule",0x50F0C1EC:"sceKernelStartModule",0xD1FF982A:"sceKernelStopModule",0x2E0911AA:"sceKernelUnloadModule",
 0x27CC57F0:"sceKernelLibcTime",0x79D1C3FA:"sceKernelDcacheWritebackAll",0xB435DEC5:"sceKernelDcacheWritebackInvalidateAll",
 0x920F104A:"sceKernelIcacheInvalidateAll",0x3EE30821:"sceKernelDcacheWritebackRange",0x34B9FA9E:"sceKernelDcacheWritebackInvalidateRange",
 0x91E4F6A7:"sceKernelLibcClock",0xBFA98062:"sceKernelDcacheInvalidateRange",0xC2DF770E:"sceKernelIcacheInvalidateRange",
 0x7591C7DB:"sceKernelSetCompiledSdkVersion",0x68DA9E36:"sceKernelDelayThreadCB",0xB7F46618:"sceKernelLoadModuleByID",0x6B4A146C:"sceUmdGetDriveStat",0x4A9E5E29:"sceUmdWaitDriveStatWithTimer",0xC07BB470:"sceKernelCreateFpl",0xD979E9BF:"sceKernelAllocateFpl",0xE7C27D1B:"sceRtcGetCurrentClockLocalTime",0x3F7AD767:"sceRtcGetCurrentTick",0xC69BEBCE:"sceOpenPSIDGetOpenPSID",0xD8B73127:"sceKernelGetModuleIdByAddress",0xF0A26395:"sceKernelGetModuleId",0x9944F31F:"sceKernelSuspendThread",0xD59EAD2F:"sceKernelWakeupThread",0x9FA03CD3:"sceKernelDeleteThread",0x1FB15A32:"sceKernelSetEventFlag",0x402FCF22:"sceKernelWaitEventFlag",0xF77D77CB:"sceKernelSetCompilerVersion",0x05572A5F:"sceKernelExitGame",0x4AC57943:"sceKernelRegisterExitCallback",
 0x3E0271D3:"sceKernelVolatileMemLock",0xA14F40B2:"sceKernelVolatileMemTryLock",0xA569E425:"sceKernelVolatileMemUnlock",
 0x328C546A:"sceKernelWaitEventFlagCB",0x30FD48F0:"sceKernelPollEventFlag",0x812346E4:"sceKernelClearEventFlag",0xEF9E4C70:"sceKernelDeleteEventFlag",
 0xCA04A2B9:"sceKernelRegisterSubIntrHandler",0xD61E6961:"sceKernelReleaseSubIntrHandler",0xFB8E22EC:"sceKernelEnableSubIntr",0x8A389411:"sceKernelDisableSubIntr",
 0x616403BA:"sceKernelTerminateThread",0x3B183E26:"sceKernelGetThreadExitStatus",0x56C039B5:"sceKernelCreateVpl",0x89B3D48C:"sceKernelDeleteVpl",
 0xAF36D708:"sceKernelTryAllocateVpl",0xB736E9FF:"sceKernelFreeVpl",0xBED27435:"sceKernelAllocateVpl",0xEC0A693F:"sceKernelAllocateVplCB",
 0x71EC4271:"sceKernelLibcGettimeofday",0x27F98543:"sceRtcFormatRFC3339LocalTime",0x8EF08FCE:"sceUmdWaitDriveStat",
 0x04B7766E:"scePowerRegisterCallback",0x8F2DF740:"sceKernelStopUnloadSelfModuleWithStatus",0x13A5ABEF:"sceKernelPrintf",
 0x172D316E:"sceKernelStdin",0xA6BAB2E9:"sceKernelStdout",0xF78BA90A:"sceKernelStderr",
 0xF6414A71:"sceKernelFreeFpl",0xED1410E0:"sceKernelDeleteFpl",0xE7282CB6:"sceKernelTryAllocateFpl",0xE9A31DC3:"sceKernelAllocateFplCB",
 0xB29DDF9C:"sceIoDopen",0xE3EB004C:"sceIoDread",0xEB092469:"sceIoDclose",
 0x13F592BC:"sceAudioOutputPannedBlocking",0x136CAF51:"sceAudioOutputBlocking",
 0xEADB1BD7:"scePowerLock",0x3AEE7261:"scePowerUnlock",0x090CCB3F:"scePowerTick",
 0x1F4011E6:"sceCtrlSetSamplingMode",0x1F803938:"sceCtrlReadBufferPositive",
 0x0E20F177:"sceDisplaySetMode",0x682A619B:"sceMpegInit",0x2C34E053:"sceKernelReleaseWaitThread",0x800C44DF:"sceMpegAtracDecode",0x31BD0272:"sceMpegAvcCsc",0x611E9E11:"sceMpegQueryStreamSize",0x3A622550:"sceCtrlPeekBufferPositive",0xA780CF7E:"sceMpegMallocAvcEsBuf",0xCEB870B1:"sceMpegFreeAvcEsBuf",0xF8DCB679:"sceMpegQueryAtracEsSize",0x0E3C2E9D:"sceMpegAvcDecode",0x874624D6:"sceMpegFinish",0xC132E22F:"sceMpegQueryMemSize",0xD8C5F121:"sceMpegCreate",0x606A4649:"sceMpegDelete",0x42560F23:"sceMpegRegistStream",0x591A4AA2:"sceMpegUnRegistStream",0x0E3C2E9D:"sceMpegAvcDecode",0xD7A29F46:"sceMpegRingbufferQueryMemSize",0x37295ED8:"sceMpegRingbufferConstruct",0x13407F13:"sceMpegRingbufferDestruct",0xB240A59E:"sceMpegRingbufferPut",0xB5F6DC87:"sceMpegRingbufferAvailableSize",0xFE246728:"sceMpegGetAvcAu",0xE1CE83A7:"sceMpegGetAtracAu",0x21FF80E4:"sceMpegQueryStreamOffset",0xA11C7026:"sceMpegAvcDecodeMode",0x740FCCD1:"sceMpegAvcDecodeStop",0xF0EB1125:"sceMpegAvcDecodeFlush",0x167AFD9E:"sceMpegInitAu",0x289D82FE:"sceDisplaySetFrameBuf",0x210EAB3A:"sceDisplayGetAccumulatedHcount",0x9C6EAAD7:"sceDisplayGetVcount",0x46F186C3:"sceDisplayWaitVblankStartCB",0xEEDA2E54:"sceDisplayGetFrameBuf",
 0x1F6752AD:"sceGeEdramGetSize",0xE47E40E4:"sceGeEdramGetAddr",0xAB49E76A:"sceGeListEnQueue",0x1C0D95A6:"sceGeListEnQueueHead",
 0xE0D68148:"sceGeListUpdateStallAddr",0x03444EB4:"sceGeListSync",0xB287BD61:"sceGeDrawSync",0xB448EC0D:"sceGeBreak",0x4C06E472:"sceGeContinue",
 0xA4FC06A4:"sceGeSetCallback",0x05DB22CE:"sceGeUnsetCallback",
 0x89AA9906:"sceIoOpenAsync",0xFF5940B6:"sceIoCloseAsync",0xA0B5A7C2:"sceIoReadAsync",0x71B19E77:"sceIoLseekAsync",0xE95A012B:"sceIoIoctlAsync",
 0x63632449:"sceIoIoctl",0x3251EA56:"sceIoPollAsync",0x35DBD746:"sceIoWaitAsyncCB",0xE23EEC33:"sceIoWaitAsync",0xCB05F8D6:"sceIoGetAsyncStat",
 0x68963324:"sceIoLseek32",0x1B385D8F:"sceIoLseek32Async",
 0x54F5FB11:"sceIoDevctl",0x06A70004:"sceIoMkdir",0x1117C65F:"sceIoRmdir",0xF27A9C51:"sceIoRemove",0xACE946E8:"sceIoGetstat",
 0x779103A0:"sceIoRename",0x55F4717D:"sceIoChdir",
 0x0C622081:"sceWlanGetEtherAddr",0xD7763699:"sceWlanGetSwitchState",0x93440B11:"sceWlanDevIsPowerOn",0x011F03C1:"sceRtcGetAccumulativeTime",
}

INF = float('inf')
import ctypes as _ct
def _ct_addr(o): return _ct.addressof(o)
def psp_cpu_core():
    import psp_cpu; return psp_cpu.core()
def _gl_movie(on):                              # tells the drawing code a movie is playing (so its pictures are never hidden)
    try:
        lib = psp_cpu_core()
        if hasattr(lib, 'gl_movie'): lib.gl_movie(on)
    except Exception: pass
def _gl_vram_written(ge, addr):                 # a movie picture went straight into video memory (CPU): the drawing code puts
    if not (0x04000000 <= (addr & 0x1FFFFFFF) < 0x04200000): return   # it under what the game draws there next (subtitles)
    try:
        lib = psp_cpu_core()
        if not hasattr(lib, 'gl_vram_written'): return
        post = getattr(ge, 'post', None)
        if post is not None and getattr(ge, 'render', False): post(lambda: lib.gl_vram_written(addr & 0x1FFFFFFF))   # (in order with the game's drawing)
        else: lib.gl_vram_written(addr & 0x1FFFFFFF)
    except Exception: pass
# snapshots made by an older version than this are not used any more (the game's state inside them was set up wrongly)
PORT_VERSION = 2
TITLE_WAIT = 300            # frames the title screen is left alone before START is pressed

class SliceFile:
    """A window onto part of a host file: what a raw 'sce_lbn' open of a UMD sector range looks like."""
    def __init__(self, f, start, length): self.f, self.start, self.length, self.pos = f, start, length, 0
    def read(self, n):
        n = max(0, min(n, self.length - self.pos)); self.f.seek(self.start + self.pos); d = self.f.read(n); self.pos += len(d); return d
    def seek(self, off, whence=0):
        self.pos = off if whence == 0 else self.pos + off if whence == 1 else self.length + off
        self.pos = max(0, min(self.pos, self.length)); return self.pos
    def close(self): self.f.close()

class IsoDisc:
    """The game disc read straight from its .iso image (ISO 9660): every file, real sector numbers, nothing missing."""
    def __init__(self, path):
        self.path = path; self.files = {}; self.dirs = {'': []}; self._f = None
        f = self.f(); f.seek(16 * 2048); pvd = f.read(2048)
        if pvd[1:6] != b'CD001': raise ValueError(f"{path} is not an ISO 9660 disc image")
        self.size = struct.unpack_from('<I', pvd, 80)[0] * 2048
        root = pvd[156:156 + 34]; self._walk('', struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0], 0)
    def f(self):
        if self._f is None: self._f = open(self.path, 'rb')
        return self._f
    def __getstate__(self): d = dict(self.__dict__); d['_f'] = None; return d
    def _walk(self, key, lba, size, depth):
        if depth > 12: return
        f = self.f(); f.seek(lba * 2048); data = f.read(size); i = 0; names = self.dirs.setdefault(key, [])
        while i < len(data):
            ln = data[i]
            if ln == 0: i = (i // 2048 + 1) * 2048; continue                    # records never cross a sector
            rec = data[i:i + ln]; i += ln
            el, sz, fl, nl = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0], rec[25], rec[32]
            nm = rec[33:33 + nl]
            if nm in (b'\x00', b'\x01'): continue
            name = nm.decode('latin1').split(';')[0].rstrip('.')
            sub = (key + '/' + name).strip('/'); names.append(name)
            if fl & 2: self.files[sub.lower()] = (el, 0, True, name); self._walk(sub.lower(), el, sz, depth + 1)
            else: self.files[sub.lower()] = (el, sz, False, name)
    def lookup(self, p):
        """'disc0:/PSP_GAME/USRDIR/x' -> (lba, size, is_dir, name) or None"""
        q = p.split(':', 1)[-1].replace('\\', '/').strip('/').lower()
        if q == '': return (0, 0, True, '')
        return self.files.get(q)
    def listdir(self, p):
        q = p.split(':', 1)[-1].replace('\\', '/').strip('/').lower()
        return list(self.dirs.get(q, []))

class DiscSlice:
    """A run of 2048-byte sectors on the made-up disc: your host files laid end to end by sector number, zeros in the gaps."""
    def __init__(self, mach, lbn, nsec):
        self.m, self.base, self.length, self.pos = mach, lbn * 2048, nsec * 2048, 0
    def read(self, n):
        import bisect
        n = max(0, min(n, self.length - self.pos))
        iso = getattr(self.m, 'iso', None)
        if iso is not None:                                                 # real disc image: just read it
            f = iso.f(); f.seek(self.base + self.pos); d = f.read(n); d += bytes(n - len(d)); self.pos += n; return d
        tbl = self.m.lbn_table; starts = [e[0] for e in tbl]; out = bytearray()
        while n > 0:
            a = self.base + self.pos; sec = a // 2048
            k = bisect.bisect_right(starts, sec) - 1
            if k >= 0 and sec < tbl[k][0] + tbl[k][1]:
                st, ns, fp = tbl[k]; off = a - st * 2048; take = min(n, (st + ns) * 2048 - a)
                f = self.m.hostfiles.get(fp)
                if f is None:
                    try: f = self.m.hostfiles.setdefault(fp, open(fp, 'rb'))
                    except OSError: f = None; self.m.missing.append(fp)
                if f is not None: f.seek(off); d = f.read(take); d += bytes(take - len(d))
                else: d = bytes(take)
            else:
                nxt = tbl[k + 1][0] * 2048 if k + 1 < len(tbl) else a + n
                take = min(n, max(1, nxt - a)); d = bytes(take)
            out += d; self.pos += take; n -= take
        return bytes(out)
    def seek(self, off, whence=0):
        self.pos = off if whence == 0 else self.pos + off if whence == 1 else self.length + off
        self.pos = max(0, min(self.pos, self.length)); return self.pos
    def close(self): pass

class UmdBlock(DiscSlice):
    """umd0: / umd1: - the whole disc as a block device: positions and read sizes count 2048-byte sectors"""
    sector_mode = True
    def __init__(self, mach):
        DiscSlice.__init__(self, mach, 0, max(mach.next_lbn, 0x100000))
        if getattr(mach, 'iso', None) is not None: self.length = mach.iso.size

class Thread:
    def __init__(self, uid, name, entry, prio, stack, stack_size):
        self.uid, self.name, self.entry, self.prio = uid, name, entry, prio
        self.stack, self.stack_size = stack, stack_size
        self.state = 'dormant'; self.wake = INF; self.wait = None
        self.ctx = None

class Machine:
    def __init__(self, cpu, gamedir, fallback):
        self.cpu = cpu; self.gamedir = gamedir; self.fallback = fallback
        self.heap = 0; self.heap_end = 0x09FF0000; self.free_list = []; self.sizes = {}; self.fpls = {}; self.log = []; self.calls = {}
        self.lenient = False; self.uid = 0x2000; self.unknown = {}; self.blocks = {}
        self.threads = {}; self.cur = None; self.sema = {}; self.evf = {}; self.zstreams = {}; self.tmpfiles = []; self.hostfiles = {}; self.lbn_by_path = {}; self.lbn_table = []; self.next_lbn = 0x1000; self.strlog = []; self.openlog = []; self.dirs = {}; self.subintr = {}; self.next_vblank = 0; self.vblanks = 0; self.clock = 0
        self.exports = {}; self.modules = {}; self.missing = []; self.gp = 0
        self.fds = {}; self.fdpath = {}; self.nextfd = 3; self.loaded = []
        self.aio = {}; self.log_base = 0; self.want_save = False; self.state_path = None; self.resumed = False; self.m2_frames = 0

    # -------- helpers
    idle_stop = False
    def now(self): return self.clock + self.cpu.icount // 200
    def aio_due(self):
        d = self.__dict__.get('_aio_due')
        if d is None: d = self._aio_due = {}
        return d
    def alloc(self, size, align=64):
        size = (max(size, 1) + 15) & ~15
        for k, (fa, fs) in enumerate(self.free_list):          # first fit among freed blocks
            st = (fa + align - 1) & ~(align - 1)
            if st + size <= fa + fs:
                del self.free_list[k]
                if st > fa: self.free_list.append((fa, st - fa))
                if st + size < fa + fs: self.free_list.append((st + size, fa + fs - st - size))
                self.free_list.sort(); self.sizes[st] = size; return st
        self.heap = (self.heap + align - 1) & ~(align - 1); a = self.heap; self.heap += size
        if self.heap > self.heap_end:
            raise Stop(f"out of emulated memory (asked for {size:#x} bytes; used up to {self.heap:#x}, limit {self.heap_end:#x}, "
                       f"{len(self.free_list)} freed pieces, biggest {max([x[1] for x in self.free_list] or [0]):#x})")
        self.sizes[a] = size; return a
    def free(self, a):
        size = self.sizes.pop(a, None)
        if size is None: return
        self.free_list.append((a, size)); self.free_list.sort(); merged = []
        for fa, fs in self.free_list:
            if merged and merged[-1][0] + merged[-1][1] == fa: merged[-1] = (merged[-1][0], merged[-1][1] + fs)
            else: merged.append((fa, fs))
        if merged and merged[-1][0] + merged[-1][1] == self.heap: self.heap = merged.pop()[0]
        self.free_list = merged
    def newuid(self): self.uid += 1; return self.uid
    def note(self, msg):
        """a game event worth knowing when something goes wrong (play window: also printed into play_log.txt)"""
        self.log.append(f"[event] {msg}")
        if getattr(self, 'live', False): print(f"[game] {msg}", flush=True)
    def mspath(self, p):
        """ms0:/... (the Memory Stick) -> the memstick folder next to the EBOOT"""
        dev, _, rest = p.partition(':')
        if dev.lower() not in ('ms0', 'fatms0', 'ms'): return None
        root = os.path.join(self.outdir, 'memstick'); os.makedirs(os.path.join(root, 'PSP', 'SAVEDATA'), exist_ok=True)
        return os.path.join(root, *[x for x in rest.replace('\\', '/').split('/') if x])
    def hostpath(self, p):
        ms = self.mspath(p)
        if ms is not None: return ms if os.path.isfile(ms) else None
        q = p.split(':', 1)[-1].lstrip('/\\')
        for root in (self.gamedir, self.fallback):
            if root:
                for cand in (os.path.join(root, q), os.path.join(root, os.path.basename(q))):
                    if os.path.isfile(cand): return cand
        return None

    def lbn_of(self, fp, size):
        """Give every host file a stable made-up start sector (2048-byte sectors) so raw sce_lbn opens can find it again."""
        if fp not in self.lbn_by_path:
            start = self.next_lbn; ns = max(1, (size + 2047) // 2048); self.next_lbn += ns + 16
            self.lbn_by_path[fp] = start; self.lbn_table.append((start, ns, fp))
        return self.lbn_by_path[fp]
    def open_lbn(self, path):
        import re
        m = re.search(r"sce_lbn0x([0-9a-fA-F]+)_size0x([0-9a-fA-F]+)", path)
        if not m: return None
        # the size in the name is in BYTES (it used to be taken as a count of 2048-byte sectors: every stage module
        # load then read ~2 GB - the whole disc image and more - which was the long pause at each area change)
        ds = DiscSlice(self, int(m.group(1), 16), 0); ds.length = int(m.group(2), 16); return ds, None
    # ---- MGS:PO texture cache guard. The game keeps (next free VRAM address, cache size) for its texture cache at
    # 0x8a34498 / 0x8a3449c. In one play-through both words got wiped to 0 during the first stage load and the game
    # crashed later when it copied a texture to address 0. Until the cause is found: put the values back (they follow
    # from the bytes still free at 0x8a344a0) and write down who wiped them.
    TEX_PTR, TEX_BASE, TEX_TOTAL = 0x8a34498, 0x040CC000, 0xD4000
    def texcache_check(self):
        c = self.cpu
        try:                                                    # 1. put the values back if they were wiped
            ptr, free = c.r32(self.TEX_PTR), c.r32(self.TEX_PTR + 8)
            if ptr == 0 and 0 < free <= self.TEX_TOTAL and c.r32(self.TEX_PTR - 4):
                c.w32(self.TEX_PTR, self.TEX_BASE + self.TEX_TOTAL - free); c.w32(self.TEX_PTR + 4, self.TEX_TOTAL)
                self.texcache_note(f"texture cache pointer was 0: repaired to {self.TEX_BASE + self.TEX_TOTAL - free:#x}")
        except Exception as ex:
            self.texcache_note(f"repair failed: {ex!r}")
        if self.__dict__.get('_watch_broken'): return
        try:                                                    # 2. find out who wipes them (C core write watch)
            lib = psp_cpu_core()
            if lib is None or not hasattr(lib, 'core_watch'): return
            if getattr(lib, '_watch_types', False) is False:
                lib.core_watch.argtypes = [_ct.c_void_p, _ct.c_uint32, _ct.c_uint32]; lib.core_watch.restype = None
                lib.core_hits.argtypes = [_ct.c_void_p, _ct.c_void_p, _ct.c_void_p, _ct.c_void_p]; lib.core_hits.restype = _ct.c_uint32
                lib._watch_types = True
            if getattr(c, '_cst', None) is None: c._native_setup()
            st = _ct_addr(c._cst)
            if getattr(c, '_watch_for', None) != st:
                lib.core_watch(st, self.TEX_PTR, self.TEX_PTR + 8); c._watch_for = st
            P = (_ct.c_uint32 * 16)(); V = (_ct.c_uint32 * 16)(); A = (_ct.c_uint32 * 16)()
            n = lib.core_hits(st, _ct_addr(P), _ct_addr(V), _ct_addr(A))
            for i in range(n):
                if V[i] == 0: self.texcache_note(f"game code at {P[i]:#x} wrote 0 to {A[i]:#x} (thread {self.cur.name if self.cur else '-'})")
        except Exception as ex:
            self._watch_broken = True; self.texcache_note(f"write watch off: {ex!r}")
    def texcache_note(self, msg):
        notes = self.__dict__.setdefault('tex_notes', [])
        if len(notes) < 40:
            notes.append(msg); print("[texture cache]", msg)
            if len(notes) == 1 or 'wrote 0' in msg: print("[texture cache] last system calls:", " | ".join(self.log[-12:]))

    def zlog(self):
        """what each open zlib stream was fed so far (so a snapshot can rebuild it)"""
        d = self.__dict__.get('_zlog')
        if d is None: d = self._zlog = {}
        return d
    def zlib_call(self, cpu, nid):
        """zlibdec.prx exports the standard zlib calls: inflateInit_, inflate, inflateEnd (32-bit z_stream layout)."""
        import zlib
        r = cpu.r; strm = r[4]
        if nid == 0x18CB51AB:                           # inflateInit_(strm, version, size)
            self.zstreams[strm] = zlib.decompressobj(15); self.zlog()[strm] = []
            size = r[6] if 0 < r[6] <= 0x100 else 56             # this game's zlib has a 44-byte z_stream: don't write past it
            for off in (8, 20, 24, 44, 48):
                if off + 4 <= size: cpu.w32(strm + off, 0)
            cpu.w32(strm + 28, 1); r[2] = 0             # state pointer just has to be non-zero
        elif nid == 0x461C7724:                         # inflateEnd(strm)
            self.zstreams.pop(strm, None); self.zlog().pop(strm, None); cpu.w32(strm + 28, 0); r[2] = 0
        else:                                           # inflate(strm, flush)
            d = self.zstreams.get(strm)
            if d is None and strm in self.zlog():           # snapshot was saved in the middle: rebuild the stream
                d = self.zstreams[strm] = zlib.decompressobj(15)
                for chunk, mx in self.zlog()[strm]: d.decompress(chunk, mx)
            if d is None: r[2] = 0xFFFFFFFE; return     # Z_STREAM_ERROR
            nin, ain, nout, aout = cpu.r32(strm), cpu.r32(strm + 4), cpu.r32(strm + 12), cpu.r32(strm + 16)
            # feed the input in small pieces: the game often hands over a whole big file but asks for a few KB at a
            # time, and copying all of the input on every call made stage loading crawl
            out = b''; used = 0
            try:
                while aout and len(out) < aout and not d.eof:
                    n = min(ain - used, 65536)
                    if n > 0: b, o = cpu._loc(nin + used, n); data = bytes(b[o:o + n])
                    else: data = b''
                    piece = d.decompress(data, aout - len(out))
                    took = len(data) - len(d.unconsumed_tail) - (len(d.unused_data) if d.eof else 0)
                    out += piece; used += took
                    zl = self.zlog().get(strm)
                    if zl is not None: zl.append((data[:took], aout - len(out) + len(piece)))
                    if not piece and not took: break
                    if d.unconsumed_tail: break                                   # output space is full
            except zlib.error: r[2] = 0xFFFFFFFD; return   # Z_DATA_ERROR
            if out: cpu.load_bytes(nout, out)
            cpu.w32(strm, nin + used); cpu.w32(strm + 4, ain - used); cpu.w32(strm + 8, cpu.r32(strm + 8) + used)
            cpu.w32(strm + 12, nout + len(out)); cpu.w32(strm + 16, aout - len(out)); cpu.w32(strm + 20, cpu.r32(strm + 20) + len(out))
            r[2] = 1 if d.eof else (0 if (used or out) else 0xFFFFFFFB)    # Z_STREAM_END / Z_OK / Z_BUF_ERROR
    def hostdir(self, p):
        ms = self.mspath(p)
        if ms is not None: return ms if os.path.isdir(ms) else None
        q = p.split(':', 1)[-1].strip('/\\')
        for root in (self.gamedir, self.fallback):
            if root:
                cand = os.path.join(root, q)
                if os.path.isdir(cand): return cand
        return None

    # -------- thread plumbing
    def save(self, t):
        c = self.cpu; t.ctx = (list(c.r), c.hi, c.lo, c.pc, list(c.f), c.cc)
    def restore(self, t):
        c = self.cpu; c.r, c.hi, c.lo, c.pc, c.f, c.cc = list(t.ctx[0]), t.ctx[1], t.ctx[2], t.ctx[3], list(t.ctx[4]), t.ctx[5]
    def mpg_release(self):
        mp = self.mpg; rb = mp.get('rb')
        if not rb: return
        while mp['nf'] < mp['end'] and mp['pend'].get(mp['nf'], 0) <= 0:
            mp['pend'].pop(mp['nf'], None); mp['nf'] += 1
            self.cpu.w32(rb + 4, self.cpu.r32(rb + 4) + 1); self.cpu.w32(rb + 12, self.cpu.r32(rb + 12) + 1)

    def start_thread(self, t, arglen, argp):
        c = self.cpu; r = [0]*32
        top = t.stack + t.stack_size - 0x100
        if arglen and argp:
            n = (arglen + 15) & ~15; top -= n; c.load_bytes(top, bytes(c.r8(argp + i) for i in range(arglen))); argp = top
        r[4], r[5] = arglen, argp; r[28] = self.gp; r[26] = top; r[29] = top - 0x40; r[31] = c.halt
        t.ctx = (r, 0, 0, t.entry, [0]*32, False); t.state = 'ready'
    @staticmethod
    def evf_ok(p, bits, mode): return (p & bits) != 0 if mode & 1 else (p & bits) == bits
    @staticmethod
    def evf_clear(p, bits, mode): return 0 if mode & 0x10 else (p & ~bits & 0xFFFFFFFF if mode & 0x20 else p)
    def block(self, state, wake=INF, wait=None, ret=0):
        t = self.cur; t.state = state; t.wake = wake; t.wait = wait
        self.cpu.r[2] = ret; self.cpu.pc = self.cpu.r[31]; raise Yield()

    # -------- the system-call stand-ins
    def hle(self, cpu, lib, nid):
        """system call, then the PSP's rule: if it woke or started a thread with a higher priority, that one runs now"""
        b, o = cpu._loc(0x8a34498, 8); before = bytes(b[o:o + 8]); t_call = time.perf_counter()
        try: res = self._hle(cpu, lib, nid)
        finally:
            dt = time.perf_counter() - t_call
            if dt > 0.05:                                                 # slow system calls, for the pause report in play.py
                sl = self.__dict__.setdefault('slow_calls', []); sl.append((round(dt, 3), NAMES.get(nid, f"{lib}.{nid:08X}"), self.cur.name if self.cur else '-'))
                del sl[:-20]
            after = bytes(b[o:o + 8])
            if after != before and after == bytes(8):
                self.texcache_note(f"system call {lib}.{NAMES.get(nid, f'{nid:08X}')} wiped it (thread {self.cur.name if self.cur else '-'}, "
                                   f"args {cpu.r[4]:#x} {cpu.r[5]:#x} {cpu.r[6]:#x} {cpu.r[7]:#x})")
        if res is None and getattr(self, '_nest', 0) == 0 and self.cur is not None and self.cur.state == 'running':
            p = self.cur.prio
            for t in self.threads.values():
                if t.state == 'ready' and t.prio < p:
                    self.cur.state = 'ready'; cpu.pc = cpu.r[31]; raise Yield()
        return res

    # -------- port hooks: a bit of Python where the game reaches the first instruction of one of its functions. That
    # instruction must be 'addiu sp, sp, -n' (a function's first): the hook runs, does that instruction itself, and the
    # function goes on as usual. Used by psp_lockers.py. (Not kept in save states: they are put back by whoever set them.)
    def hook_set(self, addr, fn):
        w = self.cpu.r32(addr)
        if (w >> 16) != 0x27BD: return False
        self.__dict__.setdefault('_hooks', {})[addr] = (fn, w); self.cpu.stubs[addr] = ('PORTHOOK', addr); return True

    def hook_clear(self, addr):
        hk = self.__dict__.get('_hooks') or {}
        hk.pop(addr, None)
        if self.cpu.stubs.get(addr, (None,))[0] == 'PORTHOOK': del self.cpu.stubs[addr]

    def _hook_run(self, cpu, addr):
        fn, w = (self.__dict__.get('_hooks') or {}).get(addr, (None, None))
        if fn is None or cpu.r32(addr) != w:                              # (gone, or other code there now: run it as it is)
            self.hook_clear(addr); return addr
        try: fn(cpu)
        except Exception as ex:
            print("port hook at %#x failed (%r) - switched off" % (addr, ex), flush=True); self.hook_clear(addr)
        imm = w & 0xFFFF
        cpu.r[29] = (cpu.r[29] + (imm - 0x10000 if imm & 0x8000 else imm)) & 0xFFFFFFFF
        return addr + 4

    def _hle(self, cpu, lib, nid):
        if lib == 'PORTHOOK': return self._hook_run(cpu, nid)
        if lib == 'zlibdec' and nid in (0x18CB51AB, 0x216D1BF1, 0x461C7724):   # the game's zlib: use the PC's own, it is far faster
            self.calls[f"zlibdec.{nid:08X}"] = self.calls.get(f"zlibdec.{nid:08X}", 0) + 1
            self.zlib_call(cpu, nid); return
        if (lib, nid) in self.exports:                                        # provided by a loaded game module
            rr = cpu.r
            try:
                st = cpu.cstr(rr[4], 80) if 0x08000000 <= rr[4] < 0x0A000000 else ''
                if not (st and st.isprintable()): st = ''
            except Exception: st = ''
            if st: self.strlog.append((lib, nid, st))
            self.log.append(f"[{self.cur.name if self.cur else '-'}] >> {lib}.{nid:08X}({rr[4]:#x}, {rr[5]:#x}, {rr[6]:#x}, {rr[7]:#x})  ra={rr[31]:#x}")
            return self.exports[(lib, nid)]
        name = NAMES.get(nid); key = f"{lib}.{name or f'{nid:08X}'}"
        self.calls[key] = self.calls.get(key, 0) + 1
        r = cpu.r; a0, a1, a2, a3, t0 = r[4], r[5], r[6], r[7], r[8]
        self.log.append(f"[{self.cur.name if self.cur else '-'}] {key}({a0:#x}, {a1:#x}, {a2:#x}, {a3:#x}, t0={t0:#x})  ra={r[31]:#x}")
        if lib == 'sceSasCore':
            if not hasattr(self, 'snd'): import psp_sound; self.snd = psp_sound.Sas(self)
            if self.snd.call(nid, r): return
            if self.sas(cpu, nid): return
        if lib == 'sceAtrac3plus':                         # music, voices: whole .at3 files decoded on the PC
            if getattr(self, 'atrac', None) is None: import psp_atrac; self.atrac = psp_atrac.AtracLib(self)
            if self.atrac.call(nid, r): return
        if lib == 'sceAudio' and nid in (0x5EC81C55, 0xCB2E439E, 0x13F592BC, 0x136CAF51, 0x8C1009B2, 0xE2D56B2D, 0x6FC46853, 0xB7E1D8E7):
            if getattr(self, 'aout', None) is None:
                import psp_sound; self.aout = psp_sound.AudioOut(self, os.path.join(self.outdir or '.', 'sound_out.wav'))
            sa0 = a0 - (1 << 32) if a0 & 0x80000000 else a0
            if nid == 0x5EC81C55: r[2] = self.aout.reserve(sa0, a1, a2); return
            if nid == 0xCB2E439E: self.aout.setlen(a0, a1); r[2] = 0; return
            if nid == 0x6FC46853: self.aout.release(a0); r[2] = 0; return
            if nid == 0xB7E1D8E7: r[2] = 0; return
            n = self.aout.output(a0, a1, a2, a3) if nid in (0x13F592BC, 0xE2D56B2D) else self.aout.output(a0, a1, a1, a2)
            r[2] = n
            if nid in (0x13F592BC, 0x136CAF51):
                # Like the PSP's sound chip: a block is taken as soon as the one before it has finished playing (the
                # game mixes the next one meanwhile). Waiting a whole block from the moment of the call (as before) added
                # the mixing time to every block: the game made only ~98% of the sound it should, the speakers ran dry
                # every few seconds - crackling.
                due = getattr(self, 'aout_due', None)
                if due is None: due = self.aout_due = {}
                now = self.now(); start = due.get(a0, 0)
                if start < now or start > now + 200000: start = now          # (fell behind, or a loaded save state: start afresh)
                due[a0] = start + n * 1_000_000 / 44100
                self.block('sleep', wake=int(start) + (1000 if start == now else 0), ret=n)
            return
        if lib == 'sceUtility':
            if not hasattr(self, 'util'): import psp_util; self.util = psp_util.Utility(self)
            self.util.interactive = bool(self.__dict__.get('dialogs_shown'))   # play window: the player answers them
            if self.util.call(nid, r): return
        if lib == 'sceLibFont':
            if not hasattr(self, 'font'): import psp_font; self.font = psp_font.FontLib(self)
            if self.font.call(nid, r): return
        if name is None:
            if not self.lenient: raise Stop(f"unknown import {lib} NID {nid:08X}", cpu.pc)
            self.unknown.setdefault(key, (a0, a1, a2, a3)); r[2] = 0; return
        if name in ("sceKernelDcacheWritebackAll", "sceKernelDcacheWritebackInvalidateAll", "sceKernelIcacheInvalidateAll",
                    "sceKernelDcacheWritebackRange", "sceKernelDcacheWritebackInvalidateRange", "sceKernelDcacheInvalidateRange",
                    "sceKernelIcacheInvalidateRange", "sceKernelSetCompiledSdkVersion", "sceKernelSetCompilerVersion", "scePowerTick",
                    "scePowerLock", "scePowerUnlock", "sceKernelRegisterExitCallback", "sceCtrlSetSamplingMode", "sceKernelChangeThreadPriority"):
            r[2] = 0
        elif name in ("sceKernelGetModuleIdByAddress", "sceKernelGetModuleId"): r[2] = 0x500
        elif name == "sceKernelCpuSuspendIntr": r[2] = 1
        elif name == "sceKernelCpuResumeIntr": r[2] = 0
        elif name == "sceKernelDevkitVersion": r[2] = 0x06060110
        elif name == "sceKernelGetSystemTimeLow": r[2] = self.now() & 0xFFFFFFFF
        elif name == "sceKernelGetSystemTimeWide": t = self.now(); r[2] = t & 0xFFFFFFFF; r[3] = t >> 32
        elif name in ("sceKernelMaxFreeMemSize", "sceKernelTotalFreeMemSize"): r[2] = (self.heap_end - self.heap) if name == "sceKernelTotalFreeMemSize" and False else max([self.heap_end - self.heap] + [x[1] for x in self.free_list]) if name == "sceKernelMaxFreeMemSize" else (self.heap_end - self.heap) + sum(x[1] for x in self.free_list)
        elif name == "sceKernelAllocPartitionMemory":
            uid = self.newuid(); self.blocks[uid] = self.alloc(a3 if a3 else 64); r[2] = uid
        elif name == "sceKernelGetBlockHeadAddr": r[2] = self.blocks.get(a0, 0)
        elif name == "sceKernelFreePartitionMemory":
            b = self.blocks.pop(a0, None)
            if isinstance(b, int): self.free(b)
            r[2] = 0
        elif name in ("sceKernelVolatileMemLock", "sceKernelVolatileMemTryLock"):
            if a1: cpu.w32(a1, 0x08400000)
            if a2: cpu.w32(a2, 0x00400000)
            r[2] = 0
        elif name == "sceKernelVolatileMemUnlock": r[2] = 0
        elif name in ("sceAudioOutputPannedBlocking", "sceAudioOutputBlocking"):
            self.block('sleep', wake=self.now() + 10000)       # one audio block (~10 ms) so other threads get the CPU
        # ---- event flags: [pattern, [(thread id, bits, mode, out pointer)]]
        elif name == "sceKernelCreateEventFlag":
            uid = self.newuid(); self.evf[uid] = [a2, []]; r[2] = uid
        elif name == "sceKernelDeleteEventFlag": self.evf.pop(a0, None); r[2] = 0
        elif name == "sceKernelClearEventFlag":
            e = self.evf.get(a0)
            if e is None: r[2] = 0x80020199
            else: e[0] &= a1; r[2] = 0
        elif name == "sceKernelSetEventFlag":
            e = self.evf.get(a0)
            if e is None: r[2] = 0x80020199
            else:
                e[0] |= a1; r[2] = 0; still = []
                for tid, bits, mode, outp in e[1]:
                    if self.evf_ok(e[0], bits, mode):
                        if outp: cpu.w32(outp, e[0])
                        e[0] = self.evf_clear(e[0], bits, mode); self.threads[tid].state = 'ready'
                    else: still.append((tid, bits, mode, outp))
                e[1] = still
        elif name in ("sceKernelWaitEventFlag", "sceKernelWaitEventFlagCB", "sceKernelPollEventFlag"):
            e = self.evf.get(a0)
            if e is None: r[2] = 0x80020199
            elif self.evf_ok(e[0], a1, a2):
                if a3: cpu.w32(a3, e[0])
                e[0] = self.evf_clear(e[0], a1, a2); r[2] = 0
            elif name == "sceKernelPollEventFlag": r[2] = 0x80020201
            else: e[1].append((self.cur.uid, a1, a2, a3)); self.block('waitevf', wait=a0)
        elif name == "sceKernelRegisterSubIntrHandler":
            self.subintr[(a0, a1)] = (a2, a3); r[2] = 0
        elif name == "sceKernelReleaseSubIntrHandler": self.subintr.pop((a0, a1), None); r[2] = 0
        elif name in ("sceKernelEnableSubIntr", "sceKernelDisableSubIntr"): r[2] = 0
        elif name == "sceKernelReleaseWaitThread":
            t = self.threads.get(a0)
            if t and t.state in ('sleep', 'waitevf', 'waitsema', 'waitthread'):
                for e in self.evf.values(): e[1] = [w for w in e[1] if w[0] != t.uid]
                for sm in self.sema.values(): sm[2] = [w for w in sm[2] if w[0] != t.uid]
                if t.ctx: t.ctx[0][2] = 0x800201AF
                t.state = 'ready'; r[2] = 0
            else: r[2] = 0x80020198 if not t else 0x800201A4
        elif name == "sceKernelGetThreadId": r[2] = self.cur.uid
        elif name == "sceGeEdramGetSize": r[2] = 0x200000
        elif name == "sceGeEdramGetAddr": r[2] = 0x04000000
        elif name == "sceGeSetCallback":
            ge = self.ge; cid = len(ge.callbacks) + 1
            ge.callbacks[cid] = (cpu.r32(a0), cpu.r32(a0 + 4), cpu.r32(a0 + 8), cpu.r32(a0 + 12)); r[2] = cid
        elif name == "sceGeUnsetCallback": self.ge.callbacks.pop(a0, None); r[2] = 0
        elif name in ("sceGeListEnQueue", "sceGeListEnQueueHead"):
            self.ge_lists = getattr(self, 'ge_lists', 0) + 1; r[2] = self.ge.enqueue(a0, a1, a2)
        elif name == "sceGeListUpdateStallAddr": self.ge.update_stall(a0, a1); r[2] = 0
        elif name in ("sceGeListSync", "sceGeDrawSync"): self.ge.sync(); self.ge.deliver(); r[2] = 0
        elif name.startswith("sceMpeg"):
            self.mpeglog = getattr(self, 'mpeglog', []); self.mpegring = getattr(self, 'mpegring', [])
            self.mpegring.append(f"[{self.cur.name}] {name}({a0:#x},{a1:#x},{a2:#x},{a3:#x}) t0={t0:#x}"); del self.mpegring[:-80]
            if name == "sceMpegRingbufferConstruct" or name == "sceMpegRingbufferPut": self.mpg['rbs'] = self.mpg.get('rbs', set()) | {a0}
            if len(self.mpeglog) < 120 and not (self.mpeglog and self.mpeglog[-1].split('(')[0] == name and name in ('sceMpegRingbufferAvailableSize', 'sceMpegGetAvcAu', 'sceMpegGetAtracAu')): self.mpeglog.append(f"{name}({a0:#x},{a1:#x},{a2:#x},{a3:#x})")
            if name == "sceMpegInit":
                self.mpeg_inits = getattr(self, 'mpeg_inits', 0) + 1
                if self.mpeg_inits == 2:
                    self.m2_mark = self.log_base + len(self.log) - 1
                    if self.state_path and not self.resumed: self.want_save = True
            if name == "sceMpegDelete": _gl_movie(0); self.note(f"movie ends ({self.mpg.get('shown', 0)} pictures shown so far)"); self.movie_event('end', None)
            if name in ("sceMpegInit", "sceMpegFinish", "sceMpegDelete", "sceMpegUnRegistStream", "sceMpegRingbufferDestruct", "sceMpegAvcDecodeStop", "sceMpegAvcDecodeFlush", "sceMpegInitAu"): r[2] = 0
            elif name == "sceMpegQueryMemSize": r[2] = 0x10000
            elif name == "sceMpegRingbufferQueryMemSize": r[2] = a0 * 2152
            elif name == "sceMpegCreate": cpu.w32(a0, a1 + 0x30); r[2] = 0
            elif name == "sceMpegRingbufferConstruct":
                for i, v in enumerate((a1, 0, 0, a1, 2048, a2, r[8], r[9], a2 + a1 * 2048, 0, 0)): cpu.w32(a0 + i * 4, v)
                mp = self.mpg; old = mp.get('dec')                  # new movie: forget everything left over from the last one
                if old and hasattr(old, 'close'):
                    try: old.close()
                    except Exception: pass
                mp.update({'v': [], 'a': [], 'pend': {}, 'nf': 0, 'end': 0, 'lastpts': {'v': 0, 'a': 0}, 'dec': None, 'frame': None, 'rb': a0, 'adec': None, 'vcur': None, 'vtail': b''})
                mp['movies'] = mp.get('movies', 0) + 1; _gl_movie(1); mp['name'] = mp.get('file'); mp['hdn'] = 0
                self.note(f"movie {mp['movies']} starts ({mp['name'] or '?'})"); self.movie_event('start', mp['name'])
                r[2] = 0
            elif name == "sceMpegRegistStream": r[2] = 0x300 + a1
            elif name == "sceMpegQueryStreamOffset":
                be = lambda o: int.from_bytes(bytes(cpu.r8(a1 + o + i) for i in range(4)), 'big')
                cpu.w32(a2, be(8)); r[2] = 0
            elif name == "sceMpegQueryStreamSize":
                be = lambda o: int.from_bytes(bytes(cpu.r8(a0 + o + i) for i in range(4)), 'big')
                cpu.w32(a1, be(12)); r[2] = 0
            elif name == "sceMpegMallocAvcEsBuf": r[2] = 1
            elif name == "sceMpegQueryAtracEsSize": cpu.w32(a1, 2112); cpu.w32(a2, 8192); r[2] = 0
            elif name == "sceMpegRingbufferAvailableSize": r[2] = cpu.r32(a0 + 12) if a0 else 0
            elif name == "sceMpegRingbufferPut":
                import psp_mpeg
                rb = a0; npk = cpu.r32(rb); wr = cpu.r32(rb + 8); free = cpu.r32(rb + 12); cb, cba, data = cpu.r32(rb + 24), cpu.r32(rb + 28), cpu.r32(rb + 20)
                n = min(a1, a2, free, npk - wr % npk) if npk else 0
                if n <= 0 or not cb: r[2] = 0
                else:
                    got = self.call_nested(cb, data + (wr % npk) * 2048, n, cba)
                    self.mpegring[-1] += f"  [game's read callback returned {got:#x}]"
                    if got > 0x7FFFFFFF or got > n: got = 0
                    mp = self.mpg
                    # What the game takes out (sceMpegGetAvcAu / GetAtracAu) are whole pictures and single sound frames, like on
                    # the PSP. (They used to be single 2 KB pieces of the stream: a picture is many pieces, so the ring buffer
                    # filled up with picture data and the sound behind it couldn't come in - the choppy movie sound.)
                    # A ring buffer piece is given back once everything in it has been taken (mpg_release).
                    for k in range(got):
                        idx = wr + k; base = data + (idx % npk) * 2048; b, o = cpu._loc(base, 2048)
                        sec = bytes(b[o:o + 2048]); aus = psp_mpeg.parse_sector(sec); mp['pend'][idx] = 0
                        for kind, pts, size, poff in aus:
                            if kind == 'v' and size and mp['dec'] is not False:
                                if mp['dec'] is None:
                                    import psp_video; mp['dec'] = False if os.environ.get('PSP_NOVIDEO') else psp_video.H264Decoder()
                                    if not mp['dec'] or mp['dec'].mode is None: mp['dec'] = False
                                if mp['dec']: mp['dec'].feed(sec[poff:poff + size])
                            if kind == 'v':
                                # each picture starts with an "access unit delimiter" (00 00 01 09) in the H.264 data
                                pay = sec[poff:poff + size]; tail = mp.get('vtail', b''); buf = tail + pay; starts = 0; q0 = 0
                                while True:
                                    q0 = buf.find(b'\x00\x00\x01\x09', q0)
                                    if q0 < 0: break
                                    if q0 + 4 > len(tail): starts += 1                # (not one already counted)
                                    q0 += 4
                                mp['vtail'] = buf[-3:]
                                cur = mp.get('vcur')
                                if cur is not None and not cur[3] and idx not in cur[0]:   # the rest of the picture before
                                    cur[0].append(idx); mp['pend'][idx] += 1; cur[2] += size
                                for j in range(starts if (starts or cur is not None) else 1):
                                    p_ = pts if (pts is not None and j == 0) else mp['lastpts']['v'] + 3003
                                    mp['lastpts']['v'] = p_; cur = [[idx], p_, size, False]; mp['vcur'] = cur
                                    mp['v'].append(cur); mp['pend'][idx] += 1
                            elif kind == 'a' and size > 4:                   # movie sound: one entry per ATRAC3+ frame
                                if mp.get('adec') is None:
                                    import psp_atrac; mp['adec'] = psp_atrac.Atrac3pStream()
                                nfr = mp['adec'].feed(sec[poff + 4:poff + size]) or 0
                                if pts is None: pts = mp['lastpts']['a']
                                for j in range(nfr):
                                    mp['a'].append((idx, pts + j * 4180, size)); mp['pend'][idx] += 1
                                mp['lastpts']['a'] = pts + nfr * 4180
                    cpu.w32(rb + 8, wr + got); cpu.w32(rb + 12, free - got); mp['rb'] = rb; mp['end'] = wr + got; r[2] = got
                    self.mpg_release()
            elif name in ("sceMpegGetAvcAu", "sceMpegGetAtracAu"):
                mp = self.mpg; q = mp['v'] if name == "sceMpegGetAvcAu" else mp['a']
                if not q: r[2] = 0x80618001
                else:
                    e = q.pop(0); idx, pts, size = e[0], e[1], e[2]
                    if isinstance(idx, list):                           # a picture: all its pieces
                        e[3] = True
                        for i2 in idx: mp['pend'][i2] = mp['pend'].get(i2, 1) - 1
                    else: mp['pend'][idx] = mp['pend'].get(idx, 1) - 1
                    mp['pops'][name] = mp['pops'].get(name, 0) + 1
                    for i, v in enumerate((pts >> 32, pts & 0xFFFFFFFF, pts >> 32, pts & 0xFFFFFFFF, 0, size)): cpu.w32(a2 + i * 4, v)
                    if a3: cpu.w32(a3, 1 if name == "sceMpegGetAvcAu" else 0)
                    r[2] = 0; self.mpg_release()
            elif name == "sceMpegAvcDecode":
                # (mpeg, au, frameWidth, &bufferAddress, &gotFrame in t0): the picture goes INTO the game's buffer
                mp = self.mpg; d = mp['dec']; f = d.next_frame() if d else None; got = 0
                dest = cpu.r32(a3) if a3 else 0
                if f is not None and dest:
                    import psp_video
                    mp['frame'] = f; mp['shown'] += 1; fw = a2 if 0 < a2 <= 1024 else 512; bpp = 4 if mp['fmt'] == 3 else 2
                    out = bytearray(fw * bpp * 272); psp_video.to_pixels(f, 0, 0, 480, 272, fw, mp['fmt'], out)
                    try: cpu.load_bytes(dest, bytes(out)); got = 1
                    except Exception as ex: mp['err'] = f"decode target {dest:#x}: {ex}"
                    mp.setdefault('targets', set()).add(dest)
                    self.movie_event('frame', mp.get('name'), mp.get('hdn', 0), dest); mp['hdn'] = mp.get('hdn', 0) + 1
                    _gl_vram_written(self.ge, dest)
                if d is False and dest: got = 1          # no video decoder on this PC: pretend a (blank) picture came out so the game moves on
                if t0: cpu.w32(t0, got)
                r[2] = 0
            elif name == "sceMpegAvcDecodeMode":
                if a1: self.mpg['fmt'] = cpu.r32(a1 + 4) & 3
                r[2] = 0
            elif name == "sceMpegAvcCsc":
                mp = self.mpg
                if mp['frame'] is not None and t0:
                    import psp_video
                    x, y, w, h = [cpu.r32(a2 + 4 * i) for i in range(4)] if a2 else (0, 0, 480, 272)
                    w = w if 0 < w <= 480 else 480; h = h if 0 < h <= 272 else 272
                    bpp = 4 if mp['fmt'] == 3 else 2; out = bytearray(a3 * bpp * h if a3 else 512 * bpp * h)
                    psp_video.to_pixels(mp['frame'], max(x, 0), max(y, 0), w, h, a3 or 512, mp['fmt'], out)
                    cpu.load_bytes(t0, bytes(out))
                    if (x, y, w, h) == (0, 0, 480, 272): self.movie_event('frame', mp.get('name'), mp.get('hdn', 1) - 1, t0)
                    _gl_vram_written(self.ge, t0)
                r[2] = 0
            elif name == "sceMpegAtracDecode":
                ad = self.mpg.get('adec')
                if a2: cpu.load_bytes(a2, ad.next_pcm() if ad is not None and not os.environ.get('PSP_NOMOVIESOUND') else bytes(8192))
                r[2] = 0
            else: r[2] = 0
            self.mpegring[-1] += f" -> {r[2]:#x}  ra={r[31]:#x}"
            if name == "sceMpegInit" and self.want_save: cpu.pc = r[31]; raise Yield()     # stop here so the scheduler can save the state cleanly
        elif name in ("sceCtrlReadBufferPositive", "sceCtrlPeekBufferPositive"):
            fr = self.now() // 16667; btn = 0x8 if (fr % 40) < 6 else 0     # a tap of START every ~0.7 s, so intro movies can be skipped
            if getattr(self, 'm2_mark', None) is not None:                 # title screen: wait ~5 s, then one press of START
                if getattr(self, 'title_frame', None) is None: self.title_frame = fr
                k = fr - self.title_frame; btn = 0x8 if TITLE_WAIT <= k < TITLE_WAIT + 6 else 0
                if k >= TITLE_WAIT and not getattr(self, 'menu_saved', False) and getattr(self, 'menu_state_path', None):
                    self.menu_saved = True; self.state_path = self.menu_state_path; self.want_save = True; self.menu_icount = self.cpu.icount
                if k >= TITLE_WAIT and getattr(self, 'menu_fb', None) is None:
                    self.menu_fb = getattr(self, 'fb_calls', 0); self.m2_mark = self.log_base + len(self.log)    # the call log now starts at the START press
            if os.environ.get("PSP_NOPAD"): btn = 0
            ana = 0x8080
            if getattr(self, 'live', False):                                 # play.py: the keyboard / controller of the PC
                btn = self.live_buttons; ana = self.live_analog
            self.pad_reads = getattr(self, 'pad_reads', 0) + 1; k = self.pad_reads - getattr(self, 'pad_base', 0)
            for (at, what, arg) in getattr(self, 'pad_script', []):          # buttons.txt: your own button presses
                if what == 'SAVE':
                    if k == at and self.cur and not getattr(self, '_saved_' + arg, False):
                        setattr(self, '_saved_' + arg, True); self.state_path = os.path.join(self.outdir, arg); self.want_save = True
                elif at <= k < at + arg: btn |= what
            for i in range(max(a1, 1)):
                b = a0 + i * 16; cpu.w32(b, self.now() & 0xFFFFFFFF); cpu.w32(b + 4, btn); cpu.w32(b + 8, ana); cpu.w32(b + 12, 0)
            r[2] = max(a1, 1)
        elif name == "sceDisplayGetFrameBuf":                 # (topaddr*, bufferwidth*, pixelformat*, sync)
            fb = getattr(self, 'cur_fb', None) or (0, 0, 0)
            if a0: cpu.w32(a0, fb[0] & 0xFFFFFFFF)
            if a1: cpu.w32(a1, fb[1])
            if a2: cpu.w32(a2, fb[2])
            r[2] = 0
        elif name == "sceDisplayGetAccumulatedHcount": r[2] = (self.now() * 286 // 16667) & 0x7FFFFFFF
        elif name == "sceDisplayGetVcount": r[2] = (self.now() // 16667) & 0x7FFFFFFF
        elif name == "sceDisplaySetFrameBuf":
            self.fb_calls = getattr(self, 'fb_calls', 0) + 1; self.cur_fb = (a0, a1, a2); r[2] = 0
            if a0 != getattr(self, '_flip_a0', None):
                self._flip_a0 = a0; self.flips = getattr(self, 'flips', 0) + 1   # new picture (for the fps counter)
                gc_ = self.__dict__.get('gl_commit')
                if gc_ is not None:                                # 60 fps mode: this picture is complete; note the game's pace
                    vb = self.vblanks; self._ip_iv = max(1, min(4, vb - self.__dict__.get('_ip_vb', vb - 3))); self._ip_vb = vb; self._ip_k = 0
                    gc_()
            vt = dict(getattr(self.ge, 'vtypes_used', {})); prev = getattr(self, '_vt_prev', {}); self._vt_prev = vt
            fl = self.__dict__.setdefault('flip_log', []); fl.append((self.fb_calls, a0, a2, {f"{k[0]:#x}/{k[1]}": v - prev.get(k, 0) for k, v in vt.items() if v != prev.get(k, 0)},
                                                              getattr(self.ge, 'fb_addr', 0), getattr(self.ge, 'fb_fmt', 0))); del fl[:-40]
            n = self.fb_calls
            if not os.environ.get('PSP_NOGE'):        # drawing pixels in Python is slow: only draw the frames just before a saved picture
                self.ge.render = bool(os.environ.get('PSP_RENDER_ALL')) or (n + 1) % 30 >= 15 or (n + 1) % 30 == 0 or (n + 1) in (1, 5, 20, 60, 150, 400)
                if getattr(self, 'render_mode', None) is not None: self.ge.render = self.render_mode
            if getattr(self, 'live', False): pass                                # play window: no picture files
            elif a0 and (n in (1, 5, 20, 60, 150, 400) or (n > 400 and n % 300 == 0 and len(self.saved_frames) < 16)):
                try: self.ge.dump_frame(os.path.join(self.outdir, f"frame_{n:04d}.png"), a0, a1 or 512, a2 if a2 < 4 else 3); self.saved_frames.append(n)
                except Exception as ex: self.saved_frames.append(f"{n} failed: {ex}")
            elif a0 and getattr(self, 'menu_fb', None) is not None and n % 30 == 0 and getattr(self, 'menu_frames', 0) < 24:
                try: self.ge.dump_frame(os.path.join(self.outdir, f"frame_menu_{n:04d}.png"), a0, a1 or 512, a2 if a2 < 4 else 3); self.saved_frames.append(f"menu_{n}"); self.menu_frames = getattr(self, 'menu_frames', 0) + 1
                except Exception as ex: self.saved_frames.append(f"menu_{n} failed: {ex}")
            elif a0 and getattr(self, 'm2_mark', None) is not None and getattr(self, 'menu_fb', None) is None and n % 60 == 0 and self.m2_frames < 12:
                try: self.ge.dump_frame(os.path.join(self.outdir, f"frame_m2_{n:04d}.png"), a0, a1 or 512, a2 if a2 < 4 else 3); self.saved_frames.append(f"m2_{n}"); self.m2_frames += 1
                except Exception as ex: self.saved_frames.append(f"m2_{n} failed: {ex}")
        # ---- threads
        elif name == "sceKernelCreateThread":
            uid = self.newuid(); stack = self.alloc(max(a3, 0x1000), 0x100)
            self.threads[uid] = Thread(uid, cpu.cstr(a0), a1, a2, stack, max(a3, 0x1000)); r[2] = uid
        elif name == "sceKernelStartThread":
            t = self.threads.get(a0)
            if not t: r[2] = 0x80020198
            else: self.start_thread(t, a1, a2); r[2] = 0
        elif name in ("sceKernelExitThread", "sceKernelExitDeleteThread"):
            self.cur.exit_status = a0; self.cur.state = 'dead'; raise Yield()
        elif name in ("sceKernelDelayThread", "sceKernelDelayThreadCB"): self.block('sleep', wake=self.now() + max(a0, 1))
        elif name in ("sceKernelSleepThread", "sceKernelSleepThreadCB"):
            t = self.cur                                  # a wake-up that came first is kept and used up here (PSP rule)
            if getattr(t, 'wakeups', 0) > 0: t.wakeups -= 1; r[2] = 0
            else: self.block('sleep')
        elif name == "sceKernelWakeupThread":
            t = self.threads.get(a0)
            if t and t.state == 'sleep' and t.wake == INF: t.state = 'ready'      # asleep (not just waiting a set time)
            elif t: t.wakeups = getattr(t, 'wakeups', 0) + 1
            r[2] = 0 if t else 0x80020198
        elif name == "sceKernelCancelWakeupThread":
            t = self.threads.get(a0) if a0 else self.cur
            r[2] = getattr(t, 'wakeups', 0) if t else 0x80020198
            if t: t.wakeups = 0
        elif name == "sceDisplayWaitVblankStartCB":
            self.texcache_check()
            fb = getattr(self, 'cur_fb', None)
            if fb and fb[0] and getattr(self, 'live', False):           # what the screen shows now (the game may reuse the buffer right after)
                bpp = 4 if (fb[2] if fb[2] < 4 else 3) == 3 else 2; n = (fb[1] or 512) * 272 * bpp
                gp = getattr(self, 'gl_present', None)
                if self.__dict__.get('gl_present_t') is not None: pass   # 60 fps mode: shown at every vblank instead (run())
                elif gp is not None:                                   # graphics-card drawing: show each new picture once
                    if getattr(self, 'flips', 0) != getattr(self, '_gl_shown', -1):
                        self._gl_shown = getattr(self, 'flips', 0); gp(fb[0], fb[1] or 512, fb[2] if fb[2] < 4 else 3)
                    self.shown_n = getattr(self, 'shown_n', 0) + 1
                else:
                    try: b, o = cpu._loc(fb[0], n); self.shown = (bytes(b[o:o + n]), fb[1] or 512, fb[2] if fb[2] < 4 else 3); self.shown_n = getattr(self, 'shown_n', 0) + 1
                    except Exception: pass
            nt = (self.now() // 16667 + 1) * 16667; self.block('sleep', wake=nt)
        elif name == "sceImposeGetLanguageMode":
            if a0: cpu.w32(a0, 1)                      # English
            if a1: cpu.w32(a1, 0)
            r[2] = 0
        elif name == "sceImposeSetLanguageMode": r[2] = 0
        elif name in ("sceUmdActivate", "sceKernelGetGPI", "sceKernelChangeCurrentThreadAttr"): r[2] = 0
        elif name == "sceKernelPollSema":
            sm = self.sema.get(a0)
            if sm is None: r[2] = 0x80020199
            elif sm[0] >= a1: sm[0] -= a1; r[2] = 0
            else: r[2] = 0x800201AD
        elif name in ("sceKernelWaitThreadEnd", "sceKernelWaitThreadEndCB"):
            t = self.threads.get(a0)
            if t and t.state != 'dead': self.block('waitthread', wait=a0)
            r[2] = 0
        # ---- semaphores
        elif name == "sceKernelCreateSema":
            uid = self.newuid(); self.sema[uid] = [a2, a3, []]; r[2] = uid
        elif name == "sceKernelDeleteSema": self.sema.pop(a0, None); r[2] = 0
        elif name in ("sceKernelWaitSema", "sceKernelWaitSemaCB"):
            sm = self.sema.get(a0)
            if sm is None: r[2] = 0x80020199
            elif sm[0] >= a1: sm[0] -= a1; r[2] = 0
            else: sm[2].append((self.cur.uid, a1)); self.block('waitsema', wait=a0)
        elif name == "sceKernelSignalSema":
            sm = self.sema.get(a0)
            if sm is None: r[2] = 0x80020199
            else:
                sm[0] += a1; still = []
                for tid, need in sm[2]:
                    if self.threads.get(tid) is None or self.threads[tid].state != 'waitsema': continue   # (stopped meanwhile)
                    if sm[0] >= need: sm[0] -= need; self.threads[tid].state = 'ready'
                    else: still.append((tid, need))
                sm[2] = still; r[2] = 0
        # ---- modules / files
        elif name == "sceKernelLoadModule":
            path = cpu.cstr(a0); hp = self.hostpath(path)
            e = self.iso.lookup(path) if getattr(self, 'iso', None) is not None else None
            src = hp
            if e is not None and not e[2]:                              # module inside the disc image: read it into memory
                f = self.iso.f(); f.seek(e[0] * 2048); src = f.read(e[1])
            if not src: self.missing.append(path); r[2] = 0x80010002
            else: r[2] = self.load_module(src, path)
        elif name == "sceKernelStartModule":
            mod = self.modules.get(a0)
            if mod is not None and mod.entry not in (0, 0xFFFFFFFF):
                self.call_nested(mod.seg_base[0] + mod.entry, a1, a2)
            if a3: cpu.w32(a3, 0)
            r[2] = 0
        elif name in ("sceIoOpen", "sceIoOpenAsync"):
            path = cpu.cstr(a0); r[2] = self.io_open(path, a1)
            if r[2] < 0x80000000: self.note_movie_file(path)
            disc = path.lower().startswith(('disc0:', 'umd')) and not os.environ.get('PSP_NODISCDELAY')
            if name == "sceIoOpenAsync" and r[2] < 0x80000000:
                self.aio[r[2]] = r[2]
                if disc and not os.environ.get('PSP_FASTREAD'): self.aio_due()[r[2]] = self.now() + 1000
            elif disc and self.cur is not None and getattr(self, '_nest', 0) == 0:
                # a real disc takes a moment to open a file. The game counts on that: its sound loader thread must
                # pause here so the main thread can switch music slots first (without it the intro froze).
                self.block('sleep', wake=self.now() + 1000, ret=r[2])
        elif name == "sceIoCloseAsync":
            f = self.fds.pop(a0, None)
            if f: f.close()
            self.aio[a0] = 0 if f else 0x80010009; r[2] = 0 if f else 0x80010009
        elif name in ("sceIoLseekAsync", "sceIoLseek32", "sceIoLseek32Async"):
            f = self.fds.get(a0)
            if name == "sceIoLseekAsync": off, wh = (a3 << 32 | a2), t0; off = off - (1 << 64) if off >> 63 else off
            else: off, wh = a1, a2; off = off - (1 << 32) if off >> 31 else off
            if not f: r[2] = 0x80010009
            else:
                if getattr(f, 'sector_mode', False): pos = f.seek(off * 2048, wh) // 2048
                else: pos = f.seek(off, wh)
                if name == "sceIoLseek32": r[2] = pos & 0xFFFFFFFF
                else: self.aio[a0] = pos; r[2] = 0
        elif name == "sceIoReadAsync":
            f = self.fds.get(a0)
            if not f: r[2] = 0x80010009
            else:
                sm = getattr(f, 'sector_mode', False); data = f.read(a2 * 2048 if sm else a2)
                if data: cpu.load_bytes(a1, data)
                self.aio[a0] = len(data) // 2048 if sm else len(data); r[2] = 0
                if not os.environ.get('PSP_FASTREAD'):
                    # a disc read takes a moment. The game counts on that: while its file system is busy, the cutscene
                    # sound loader waits, so the main thread hands over the sound slots first (without it the cutscene
                    # voices were registered one section late and never played). ~16 MB/s: faster than a real disc.
                    self.aio_due()[a0] = self.now() + 200 + len(data) // 16
        elif name in ("sceIoIoctl", "sceIoIoctlAsync"):
            res = self.io_ioctl(a0, a1, a2, a3, r[8], r[9])
            self.ioctls = getattr(self, 'ioctls', {}); self.ioctls[f"{a1:#x}"] = self.ioctls.get(f"{a1:#x}", 0) + 1
            if name == "sceIoIoctl": r[2] = res
            elif res >= 0x80000000: r[2] = res
            else: self.aio[a0] = res; r[2] = 0
        elif name in ("sceIoWaitAsync", "sceIoWaitAsyncCB", "sceIoPollAsync", "sceIoGetAsyncStat"):
            resp = a2 if name == "sceIoGetAsyncStat" else a1
            poll = name == "sceIoPollAsync" or (name == "sceIoGetAsyncStat" and a1 == 1)
            due = self.aio_due().get(a0, 0); now = self.now()
            if a0 in self.aio and due > now and poll: r[2] = 1                  # still busy
            elif a0 in self.aio:
                v = self.aio.pop(a0); self.aio_due().pop(a0, None)
                if v >= 0x80000000 and v < 1 << 32: v |= 0xFFFFFFFF00000000       # 32-bit error code -> negative 64-bit result
                if resp: cpu.w32(resp, v & 0xFFFFFFFF); cpu.w32(resp + 4, (v >> 32) & 0xFFFFFFFF)
                r[2] = 0
                if due > now and getattr(self, '_nest', 0) == 0: self.block('sleep', wake=due, ret=0)
            else: r[2] = 0          # nothing pending: answer 'done' (what the old lenient answer did)
        elif name == "sceIoWrite":
            if a0 in (1, 2):
                try: txt = bytes(cpu.r8(a1 + i) for i in range(min(a2, 512))).decode('latin1')
                except Exception: txt = ''
                self.stdout = getattr(self, 'stdout', []); self.stdout.append(txt); del self.stdout[:-200]; r[2] = a2
            else:
                f = self.fds.get(a0)
                if not f or not hasattr(f, 'write'): r[2] = 0x80010009
                else:
                    b, o = cpu._loc(a1, max(a2, 1)); f.write(bytes(b[o:o + a2])); r[2] = a2
        elif name == "sceIoDevctl":
            dev = cpu.cstr(a0, 32); out, outlen = r[8], r[9]
            self.devctls = getattr(self, 'devctls', {}); self.devctls[f"{dev} {a1:#x}"] = self.devctls.get(f"{dev} {a1:#x}", 0) + 1
            if a1 in (0x02025806, 0x02425823):                         # Memory Stick inserted?
                if out: cpu.w32(out, 1)
            elif a1 == 0x02025801:                                    # Memory Stick state: ready
                if out: cpu.w32(out, 4)
            elif a1 == 0x02425824:                                    # write protected? no
                if out: cpu.w32(out, 0)
            elif a1 == 0x02425818 and a2:                             # free space
                st = cpu.r32(a2)
                if st:
                    for i, v in enumerate((0x10000, 0x8000, 0x10000, 0x200, 0x40)): cpu.w32(st + 4 * i, v)   # 1 GB, half free
            r[2] = 0
        elif name in ("sceIoMkdir", "sceIoRmdir", "sceIoRemove", "sceIoRename", "sceIoChdir"):
            ms = self.mspath(cpu.cstr(a0))
            try:
                if ms is None: r[2] = 0 if name == "sceIoChdir" else 0x80010013
                elif name == "sceIoMkdir": os.makedirs(ms, exist_ok=True); r[2] = 0
                elif name == "sceIoRmdir": os.rmdir(ms); r[2] = 0
                elif name == "sceIoRemove": os.remove(ms); r[2] = 0
                elif name == "sceIoRename": os.replace(ms, self.mspath(cpu.cstr(a1)) or ms); r[2] = 0
                else: r[2] = 0
            except OSError: r[2] = 0x80010002
        elif name == "sceIoGetstat" and getattr(self, 'iso', None) is not None and self.mspath(cpu.cstr(a0)) is None and self.iso.lookup(cpu.cstr(a0)):
            lba, size, isd, _ = self.iso.lookup(cpu.cstr(a0))
            for i in range(0, 0x58, 4): cpu.w32(a1 + i, 0)
            cpu.w32(a1, 0x1000 | 0o777 if isd else 0x2000 | 0o777); cpu.w32(a1 + 4, 0x10 if isd else 0x20)
            cpu.w32(a1 + 8, size); cpu.w32(a1 + 0x40, lba)
            for o in (16, 32, 48): cpu.w16(a1 + o, 2006); cpu.w16(a1 + o + 2, 11); cpu.w16(a1 + o + 4, 1)
            r[2] = 0
        elif name == "sceIoGetstat":
            pth = cpu.cstr(a0); ms = self.mspath(pth)
            fp = ms if ms is not None else (self.hostpath(pth) or self.hostdir(pth))
            if not fp or not os.path.exists(fp): r[2] = 0x80010002
            else:
                isd = os.path.isdir(fp); size = 0 if isd else os.path.getsize(fp)
                for i in range(0, 0x58, 4): cpu.w32(a1 + i, 0)
                cpu.w32(a1, 0x1000 | 0o777 if isd else 0x2000 | 0o777); cpu.w32(a1 + 4, 0x10 if isd else 0x20)
                cpu.w32(a1 + 8, size & 0xFFFFFFFF); cpu.w32(a1 + 12, size >> 32)
                for o in (16, 32, 48): cpu.w16(a1 + o, 2006); cpu.w16(a1 + o + 2, 11); cpu.w16(a1 + o + 4, 1)
                r[2] = 0
        elif name == "sceIoDopen" and getattr(self, 'iso', None) is not None and (self.iso.lookup(cpu.cstr(a0)) or (0, 0, False))[2]:
            pth = cpu.cstr(a0); h = self.newuid(); self.dirs[h] = ('iso:' + pth, iter(self.iso.listdir(pth))); r[2] = h
        elif name == "sceIoDopen":
            d = self.hostdir(cpu.cstr(a0))
            if not d: r[2] = 0x80010002
            else:
                h = self.newuid(); self.dirs[h] = (d, iter(sorted(os.listdir(d)))); r[2] = h
        elif name == "sceIoDread":
            e = self.dirs.get(a0)
            nm = next(e[1], None) if e else None
            if nm is None: r[2] = 0
            elif isinstance(e[0], str) and e[0].startswith('iso:'):
                lba, size, isd, _ = self.iso.lookup(e[0][4:].rstrip('/') + '/' + nm)
                for i in range(0, 0x160, 4): cpu.w32(a1 + i, 0)
                cpu.w32(a1, 0x1000 | 0o777 if isd else 0x2000 | 0o777); cpu.w32(a1 + 4, 0x10 if isd else 0x20)
                cpu.w32(a1 + 8, size)
                if not isd: cpu.w32(a1 + 0x40, lba)                          # st_private[0] = real start sector
                for i, ch in enumerate(nm.encode('latin1', 'replace')[:255]): cpu.w8(a1 + 0x58 + i, ch)
                r[2] = 1
            else:
                fp = os.path.join(e[0], nm); isd = os.path.isdir(fp); size = 0 if isd else os.path.getsize(fp)
                for i in range(0, 0x160, 4): cpu.w32(a1 + i, 0)
                cpu.w32(a1, 0x1000 | 0o777 if isd else 0x2000 | 0o777); cpu.w32(a1 + 4, 0x10 if isd else 0x20)
                cpu.w32(a1 + 8, size & 0xFFFFFFFF); cpu.w32(a1 + 12, size >> 32)
                if not isd: cpu.w32(a1 + 0x40, self.lbn_of(fp, size))      # st_private[0] = start sector on the 'disc'
                for i, ch in enumerate(nm.encode('latin1', 'replace')[:255]): cpu.w8(a1 + 0x58 + i, ch)
                r[2] = 1
        elif name == "sceIoDclose": self.dirs.pop(a0, None); r[2] = 0
        elif name == "sceKernelLoadModuleByID":
            hp = self.fdpath.get(a0)
            if hp and hp[0] is None:                      # module sits on raw disc sectors: read it into memory
                # (it used to go through a temporary .prx file, and on Windows the virus scanner held each new one for
                #  seconds - the long pause when the game changes area)
                t_rd = time.perf_counter(); f = self.fds.get(a0); data = f.read(f.length - f.pos)
                self.__dict__['_mod_read_s'] = time.perf_counter() - t_rd; hp = (data, hp[1])
            try: r[2] = self.load_module(hp[0], hp[1]) if hp else 0x80010009
            except (ValueError, OSError) as e:
                self.loaded.append(f"FAILED to load module from {hp[1] if hp else a0}: {e}"); r[2] = 0x80020148
        elif name == "sceIoClose":
            f = self.fds.pop(a0, None)
            if f: f.close()
            r[2] = 0
        elif name == "sceIoRead":
            f = self.fds.get(a0)
            if not f: r[2] = 0x80010009
            elif getattr(f, 'sector_mode', False): data = f.read(a2 * 2048); cpu.load_bytes(a1, data); r[2] = len(data) // 2048
            else: data = f.read(a2); cpu.load_bytes(a1, data); r[2] = len(data)
        elif name == "sceIoLseek":
            f = self.fds.get(a0)
            off = (a3 << 32 | a2) if a3 < 0x80000000 else -(((~(a3 << 32 | a2)) & 0xFFFFFFFFFFFFFFFF) + 1)
            if f and getattr(f, 'sector_mode', False): pos = f.seek(off * 2048, t0) // 2048; r[2] = pos & 0xFFFFFFFF; r[3] = pos >> 32
            elif f: pos = f.seek(off, t0); r[2] = pos & 0xFFFFFFFF; r[3] = pos >> 32
            else: r[2] = 0x80010009
        # ---- calls the later parts of the game (stage modules, menus, sound) may make
        elif name == "sceKernelTerminateThread":
            t = self.threads.get(a0)
            if not t: r[2] = 0x80020198
            elif t is self.cur: r[2] = 0x800201A2                    # (a thread can't terminate itself)
            else: t.state = 'dormant'; t.wake = INF; t.exit_status = 0x800201AC; r[2] = 0
        elif name == "sceKernelGetThreadExitStatus":
            t = self.threads.get(a0)
            if not t: r[2] = 0x80020198
            elif t.state in ('dead', 'dormant'): r[2] = getattr(t, 'exit_status', 0) & 0xFFFFFFFF
            else: r[2] = 0x800201A4                                  # still running
        elif name == "sceKernelCreateVpl":
            uid = self.newuid(); self.__dict__.setdefault('vpls', {})[uid] = [a3, set()]; r[2] = uid
        elif name in ("sceKernelTryAllocateVpl", "sceKernelAllocateVpl", "sceKernelAllocateVplCB"):
            v = self.__dict__.setdefault('vpls', {}).get(a0)
            if v is None: r[2] = 0x800201D0
            else:
                used = sum(self.sizes.get(x, 0) for x in v[1])
                if used + a1 > v[0]: r[2] = 0x800201D1               # pool full
                else: a = self.alloc(max(a1, 1), 8); v[1].add(a); cpu.w32(a2, a); r[2] = 0
        elif name == "sceKernelFreeVpl":
            v = self.__dict__.setdefault('vpls', {}).get(a0)
            if v is not None and a1 in v[1]: v[1].discard(a1); self.free(a1)
            r[2] = 0
        elif name == "sceKernelDeleteVpl":
            v = self.__dict__.setdefault('vpls', {}).pop(a0, None)
            if v is not None:
                for x in v[1]: self.free(x)
            r[2] = 0
        elif name == "sceKernelLibcGettimeofday":
            us = 1_160_000_000 * 1_000_000 + self.now()
            if a0: cpu.w32(a0, (us // 1_000_000) & 0xFFFFFFFF); cpu.w32(a0 + 4, us % 1_000_000)
            if a1: cpu.w32(a1, 0); cpu.w32(a1 + 4, 0)
            r[2] = 0
        elif name == "sceRtcFormatRFC3339LocalTime":
            import datetime
            tick = cpu.r32(a1) | cpu.r32(a1 + 4) << 32 if a1 else 0
            try: t = datetime.datetime(1, 1, 1) + datetime.timedelta(microseconds=tick)
            except OverflowError: t = datetime.datetime(2008, 1, 1)
            cpu.load_bytes(a0, t.strftime('%Y-%m-%dT%H:%M:%S+00:00').encode() + b'\0'); r[2] = 0
        elif name in ("sceUmdWaitDriveStat", "scePowerRegisterCallback"): r[2] = 0
        elif name == "sceKernelPrintf": self.log.append(f"[printf] {cpu.cstr(a0)!r}"); r[2] = 0
        elif name in ("sceKernelStdin", "sceKernelStdout", "sceKernelStderr"): r[2] = {"sceKernelStdin": 0, "sceKernelStdout": 1}.get(name, 2)
        elif name == "sceKernelStopUnloadSelfModuleWithStatus":
            self.cur.exit_status = a1; self.cur.state = 'dead'; raise Yield()
        elif name == "sceKernelCreateFpl":
            uid = self.newuid(); blk, n = a3, max(t0, 1); self.fpls[uid] = [self.alloc(blk * n, 0x100), blk, n, set()]; r[2] = uid
        elif name in ("sceKernelAllocateFpl", "sceKernelAllocateFplCB", "sceKernelTryAllocateFpl"):
            b = self.fpls.get(a0)
            if not b: r[2] = 0x800201D0
            else:
                free_i = next((i for i in range(b[2]) if i not in b[3]), None)
                if free_i is None: r[2] = 0x800201D1                # no free block
                else: b[3].add(free_i); cpu.w32(a1, b[0] + free_i * b[1]); r[2] = 0
        elif name == "sceKernelFreeFpl":
            b = self.fpls.get(a0)
            if b and b[1]: b[3].discard((a1 - b[0]) // b[1])
            r[2] = 0
        elif name == "sceKernelDeleteFpl":
            b = self.fpls.pop(a0, None)
            if b: self.free(b[0])
            r[2] = 0
        elif name in ("sceKernelDeleteThread", "sceKernelTerminateDeleteThread"):
            t = self.threads.get(a0)
            if t and t is not self.cur and t.state in ('dormant', 'dead', 'sleep', 'ready', 'waitsema', 'waitevf'):
                t.state = 'dead'; self.free(t.stack)
            r[2] = 0
        elif name == "sceKernelStopModule": r[2] = 0
        elif name == "sceKernelUnloadModule":
            m = self.modules.pop(a0, None)
            if m is not None:
                for a in [a for a in self.cpu.stubs if m.lo <= a < m.hi]: del self.cpu.stubs[a]
                for k in [k for k, v in self.exports.items() if m.lo <= v < m.hi]: del self.exports[k]
                self.free(m.alloc_base)
            r[2] = 0
        elif name == "sceRtcGetCurrentClockLocalTime":
            for i, v in enumerate((2008, 1, 1, 12, 0, 0)): cpu.w16(a0 + 2 * i, v)
            cpu.w32(a0 + 12, 0); r[2] = 0
        elif name == "sceRtcGetCurrentTick": cpu.w32(a0, self.now() & 0xFFFFFFFF); cpu.w32(a0 + 4, self.now() >> 32); r[2] = 0
        elif name == "sceWlanGetEtherAddr": cpu.load_bytes(a0, bytes([0x00, 0x1D, 0xD9, 0x4D, 0x47, 0x53])); r[2] = 0   # a made-up but valid-looking MAC address
        elif name in ("sceWlanGetSwitchState", "sceWlanDevIsPowerOn"): r[2] = 0
        elif name == "sceRtcGetAccumulativeTime":
            t = 62135596800 * 1_000_000 + 1_160_000_000 * 1_000_000 + self.now(); r[2] = t & 0xFFFFFFFF; r[3] = t >> 32
        elif name == "sceOpenPSIDGetOpenPSID": cpu.load_bytes(a0, bytes(range(1, 17))); r[2] = 0
        elif name == "sceUmdGetDriveStat": r[2] = 0x32          # present | inited | ready
        elif name == "sceUmdWaitDriveStatWithTimer": r[2] = 0
        elif name == "sceKernelExitGame": raise Stop("game called sceKernelExitGame", cpu.pc)
        elif self.lenient:
            self.unknown.setdefault(key, (a0, a1, a2, a3))
            r[2] = self.newuid() if name.startswith("sceKernelCreate") else 0
        else:
            raise Stop(f"needs a stand-in for {lib}.{name}", cpu.pc)

    def sas(self, cpu, nid):
        """Sound chip voices (sceSasCore). No sound is made yet, but each voice's on/off state and length are tracked,
        so the game can tell when a sound effect has finished playing (it waits for that)."""
        r = cpu.r; a1, a2, a3, t0 = r[5], r[6], r[7], r[8]
        V = self.__dict__.setdefault('voices', {})
        def vo(n): return V.setdefault(n & 31, {'on': False, 'start': 0, 'dur': 0.0, 'pitch': 0x1000, 'addr': 0, 'size': 0, 'loop': 0})
        def ended(v):
            if not v['on']: return True
            if v['dur'] is None: return False                       # looping sound: plays until keyed off
            return self.now() >= v['start'] + v['dur']
        def length(v):                                              # VAG ADPCM: 16-byte blocks of 28 samples, flag byte marks end/loop
            if v['loop']: return None
            n = 0; a = v['addr']; lim = max(16, v['size'])
            try:
                while n * 16 < lim and n < 200000:
                    fl = cpu.r8(a + n * 16 + 1); n += 1
                    if fl & 1: 
                        if fl & 2: return None
                        break
            except Exception: pass
            rate = 44100 * max(v['pitch'], 1) / 0x1000
            return int(n * 28 / rate * 1_000_000)
        if nid == 0x99944089: v = vo(a1); v.update(addr=a2, size=a3, loop=t0); r[2] = 0                    # SetVoice
        elif nid == 0xE1CD9561: v = vo(a1); v.update(addr=a2, size=a3 * 2, loop=t0, pcm=True); r[2] = 0     # SetVoicePCM
        elif nid == 0xAD84D37F: vo(a1)['pitch'] = a2; r[2] = 0                                            # SetPitch
        elif nid == 0x76F01ACA:                                                                           # KeyOn
            v = vo(a1); v['on'] = True; v['start'] = self.now()
            v['dur'] = (None if v['loop'] else int(v['size'] // 2 / (44100 * max(v['pitch'], 1) / 0x1000) * 1_000_000)) if v.get('pcm') else length(v)
            r[2] = 0
        elif nid == 0xA0CF2FA4: vo(a1)['on'] = False; r[2] = 0                                           # KeyOff
        elif nid == 0x68A46B95:                                                                           # GetEndFlag: bit set = voice silent
            m = 0
            for i in range(32):
                if ended(vo(i)): m |= 1 << i
            r[2] = m
        elif nid == 0x74AE582A: r[2] = 0 if ended(vo(a1)) else 0x40000000                                # GetEnvelopeHeight
        elif nid == 0x07F58C24:                                                                           # GetAllEnvelopeHeights
            for i in range(32): cpu.w32(a1 + 4 * i, 0 if ended(vo(i)) else 0x40000000)
            r[2] = 0
        elif nid == 0x2C8E6AB3: r[2] = 0                                                                  # GetPauseFlag
        else: return False
        return True

    def note_movie_file(self, path):
        """remember the name of a movie (.pmf) the game opens - by name, or by its place on the disc (sce_lbn...)"""
        import re
        name = None; low = path.lower()
        if low.endswith('.pmf'): name = os.path.splitext(os.path.basename(path.replace('\\', '/')))[0]
        else:
            mm = re.search(r'sce_lbn(0x[0-9a-f]+|\d+)', low)
            if mm:
                lbn = int(mm.group(1), 0); iso = getattr(self, 'iso', None)
                if iso is not None:                                       # (the real disc: its own sector numbers)
                    for k, e in iso.files.items():
                        if e[0] == lbn and k.endswith('.pmf'): name = os.path.splitext(e[3])[0]; break
                for st, ns, fp in ([] if name else self.lbn_table):
                    if st == lbn and fp.lower().endswith('.pmf'): name = os.path.splitext(os.path.basename(fp.replace('\\', '/')))[0]; break
        if name and isinstance(getattr(self, 'mpg', None), dict): self.mpg['file'] = name

    def movie_event(self, what, name, *args):
        """play.py's HD movies: 'start' (name), 'frame' (name, picture number, where it was written), 'end'"""
        hook = getattr(self, 'movie_hook', None)
        if hook is not None:
            try: hook(what, name, *args)
            except Exception as ex: self.note(f"HD movie: {ex}")

    def io_open(self, path, flags):
        if path.lower().rstrip('/').split(':')[0] in ('umd', 'umd0', 'umd1', 'umd00', 'umd01') and path.rstrip('/').endswith(':'):
            fd = self.nextfd; self.nextfd += 1; self.fds[fd] = UmdBlock(self); self.fdpath[fd] = (None, path)
            self.openlog.append((path, flags, fd)); return fd
        ms = self.mspath(path)
        if ms is not None and (flags & 2):                           # writing to the Memory Stick
            if (flags & 0x800) and os.path.exists(ms): return 0x80010011
            if not os.path.exists(ms) and not (flags & 0x200): self.openlog.append((path, flags, 0x80010002)); return 0x80010002
            os.makedirs(os.path.dirname(ms), exist_ok=True)
            mode = 'r+b' if os.path.exists(ms) and not (flags & 0x400) else 'w+b'
            f = open(ms, mode)
            if flags & 0x100: f.seek(0, 2)
            fd = self.nextfd; self.nextfd += 1; self.fds[fd] = f; self.fdpath[fd] = (ms, path); self.openlog.append((path, flags, fd)); return fd
        iso = getattr(self, 'iso', None)
        if iso is not None and 'sce_lbn' not in path:
            e = iso.lookup(path)
            if e is not None and not e[2]:
                ds = DiscSlice(self, e[0], 0); ds.length = e[1]
                fd = self.nextfd; self.nextfd += 1; self.fds[fd] = ds; self.fdpath[fd] = (None, path)
                self.openlog.append((path, flags, fd)); return fd
        hp = self.hostpath(path)
        lb = self.open_lbn(path) if not hp and 'sce_lbn' in path else None
        if lb:
            fd = self.nextfd; self.nextfd += 1; self.fds[fd] = lb[0]; self.fdpath[fd] = (None, path)
            self.openlog.append((path, flags, fd)); return fd
        if not hp: self.missing.append(path); res = 0x80010002
        else: fd = self.nextfd; self.nextfd += 1; self.fds[fd] = open(hp, 'rb'); self.fdpath[fd] = (hp, path); res = fd
        self.openlog.append((path, flags, res)); return res

    def io_ioctl(self, fd, cmd, inp, inlen, outp, outlen):
        """The UMD file ioctls games use (same meaning as in PPSSPP): seek, tell, start sector, size, read."""
        f = self.fds.get(fd); cpu = self.cpu
        if not f: return 0x80010009
        tell = f.pos if hasattr(f, 'pos') else f.tell()
        if cmd == 0x01010005 and inp:                                   # seek (16-byte struct: u64 offset, u32 ?, u32 whence)
            off = cpu.r32(inp) | cpu.r32(inp + 4) << 32; off = off - (1 << 64) if off >> 63 else off
            f.seek(off, cpu.r32(inp + 12) if inlen >= 16 else 0); return 0
        if cmd == 0x01020004 and outp: cpu.w32(outp, tell); return 0      # current position
        if cmd in (0x01020006, 0x01020007):
            if isinstance(f, DiscSlice): start, size = f.base // 2048, f.length
            else:
                size = os.fstat(f.fileno()).st_size if hasattr(f, 'fileno') else getattr(f, 'length', 0)
                hp = self.fdpath.get(fd, (None,))[0]; start = self.lbn_of(hp, size) if hp else 0
            if outp and cmd == 0x01020006: cpu.w32(outp, start)
            if outp and cmd == 0x01020007: cpu.w32(outp, size & 0xFFFFFFFF); cpu.w32(outp + 4, size >> 32)
            return 0
        if cmd == 0x01030008 and inp and outp:                          # read
            data = f.read(cpu.r32(inp))
            if data: cpu.load_bytes(outp, data)
            return len(data)
        return 0

    # -------- save state (so a test can start right before the second movie instead of from power-on)
    def save_state(self, path):
        import pickle, gzip
        self.ge.sync(); self.ge.deliver()                             # the drawing thread must be idle
        gp = self.__dict__.pop('gl_present', None)                     # (a function: not saved)
        gfx = {k: self.__dict__.pop(k) for k in ('gl_present_t', 'gl_commit', 'movie_hook') if k in self.__dict__}
        cpu = self.cpu; keep = (cpu.hle, cpu._vfpu, self.fds, self.hostfiles, self.zstreams, self.dirs, self.mpg.get('dec'), self.log, self.log_base)
        hooks = self.__dict__.pop('_hooks', None); lk = self.__dict__.pop('_lk', None)     # (port hooks: not saved)
        hstubs = {k: v for k, v in cpu.stubs.items() if isinstance(v, tuple) and v and v[0] == 'PORTHOOK'}
        for k in hstubs: del cpu.stubs[k]
        fds = {}
        for fd, f in self.fds.items():
            if isinstance(f, DiscSlice): fds[fd] = f
            elif isinstance(f, SliceFile): fds[fd] = ('slice', f.f.name, f.start, f.length, f.pos)
            else: fds[fd] = ('file', f.name, f.tell())
        dirs = {k: (d, list(it)) for k, (d, it) in self.dirs.items()}
        try:
            cpu.hle = None; del cpu._vfpu
            self.fds, self.hostfiles, self.zstreams, self.dirs = fds, {}, {}, dirs
            self.mpg['dec'] = None; self.log_base += max(0, len(self.log) - 3000); self.log = self.log[-3000:]
            tmp = path + ".tmp"
            with gzip.open(tmp, 'wb', compresslevel=1) as g: pickle.dump(self, g, protocol=pickle.HIGHEST_PROTOCOL)
            os.replace(tmp, path); self.state_note = f"saved state at {cpu.icount:,} instructions -> {path}"
        except Exception as ex:
            self.state_note = f"could not save state: {ex!r}"
        finally:
            cpu.hle, cpu._vfpu, self.fds, self.hostfiles, self.zstreams, self.dirs, self.mpg['dec'], self.log, self.log_base = keep
            self.dirs = {k: (d, iter(l)) for k, (d, l) in dirs.items()}
            if gp is not None: self.gl_present = gp
            if hooks is not None: self._hooks = hooks
            if lk is not None: self._lk = lk
            cpu.stubs.update(hstubs)
            self.__dict__.update(gfx)
        print("\n" + self.state_note)

    @staticmethod
    def load_state(path, new_root=None):
        import pickle, gzip, psp_vfpu
        me = sys.modules[__name__]
        class _U(pickle.Unpickler):        # the state may have been saved by psp_run started directly or through run_port.py
            def find_class(self, mod, nm): return getattr(me, nm) if mod in ('__main__', 'psp_run') else super().find_class(mod, nm)
        with gzip.open(path, 'rb') as g: mach = _U(g).load()
        cpu = mach.cpu; cpu._vfpu = psp_vfpu; cpu.hle = mach.hle
        if new_root and mach.gamedir and os.path.normpath(mach.gamedir) != os.path.normpath(new_root):
            mach.relocate(mach.gamedir, new_root)                   # snapshot made on another computer / folder
        import io
        def _open(p):
            try: return open(p, 'rb')
            except OSError: print("   (snapshot refers to a file not on this computer:", p, ")"); return io.BytesIO(b'')
        for fd, f in list(mach.fds.items()):
            if isinstance(f, tuple):
                if f[0] == 'file': h = _open(f[1]); h.seek(f[2]); mach.fds[fd] = h
                else: s = SliceFile(_open(f[1]), f[2], f[3]); s.pos = f[4]; mach.fds[fd] = s
        mach.dirs = {k: (d, iter(l)) for k, (d, l) in mach.dirs.items()}
        import re as _re
        for fd, f in mach.fds.items():                              # older snapshots: raw-sector files had a far too big size
            mm = _re.search(r"sce_lbn0x([0-9a-fA-F]+)_size0x([0-9a-fA-F]+)", (mach.fdpath.get(fd) or (None, ''))[1] or '')
            if mm and type(f) is DiscSlice: f.length = int(mm.group(2), 16); f.pos = min(f.pos, f.length)
        mach.tmpfiles = []                                          # (no temporary module files are made any more)
        mach.resumed = True; mach.want_save = False
        if not hasattr(mach.ge, 'trace'): import collections; mach.ge.trace = collections.deque(maxlen=20000)   # snapshot from an older version
        return mach

    def relocate(self, old, new):
        """point every host path inside a snapshot from folder 'old' to folder 'new'"""
        o = old.replace('\\', '/').rstrip('/')
        def fix(p):
            if not isinstance(p, str): return p
            q = p.replace('\\', '/')
            if q == o or q.startswith(o + '/'): return os.path.join(new, *[x for x in q[len(o):].split('/') if x])
            return p
        self.gamedir = fix(self.gamedir); self.fallback = fix(self.fallback)
        self.lbn_table = [(a, b, fix(c)) for a, b, c in self.lbn_table]
        self.lbn_by_path = {fix(k): v for k, v in self.lbn_by_path.items()}
        self.fdpath = {k: (fix(v[0]), v[1]) for k, v in self.fdpath.items()}
        self.fds = {k: ((f[0], fix(f[1])) + tuple(f[2:]) if isinstance(f, tuple) else f) for k, f in self.fds.items()}
        self.dirs = {k: (fix(d), l) for k, (d, l) in self.dirs.items()}
        self.hostfiles = {}; self.outdir = new
        iso = getattr(self, 'iso', None)
        if iso is not None and not os.path.exists(iso.path):
            p = find_iso(new)
            if p: iso.path = p

    def load_module(self, hostfile, path):
        """hostfile: a file on this computer, or the module's bytes"""
        t0 = time.perf_counter()
        if not isinstance(hostfile, (bytes, bytearray)):
            with open(hostfile, 'rb') as fh: data = fh.read()
            shown = os.path.basename(hostfile)
        else: data = bytes(hostfile); shown = path
        t1 = time.perf_counter()
        try: res = self._load_module(data, shown)
        finally:
            t2 = time.perf_counter(); rd = self.__dict__.pop('_mod_read_s', 0) + (t1 - t0)
            lt = self.__dict__.setdefault('load_times', []); lt.append((shown, round(rd, 3), round(t2 - t1, 3))); del lt[:-10]
        return res

    def _load_module(self, data, shown):
        hostfile = data
        try: m0 = PrxModule.load(hostfile, 0x09000000)
        except ValueError:
            # wrapped Sony system library (libfont, libmp3, libsha*, ...): a PC port replaces these, so just note it
            uid = self.newuid(); self.modules[uid] = None
            self.loaded.append(f"{shown}  -> wrapped system library, handled by a built-in stand-in")
            return uid
        base = self.alloc((m0.hi - m0.lo + 0xFFF) & ~0xFFF, 0x1000)
        m = PrxModule.load(hostfile, base)
        self.cpu.load_bytes(m.lo, bytes(m.mem))
        for a, v in m.stubs.items(): self.cpu.stubs[a] = v
        self.exports.update(m.exports)
        m.alloc_base = base
        uid = self.newuid(); self.modules[uid] = m; self.loaded.append(f"{m.name} @ {base:#x}  ({shown})"); self.note(f"loaded module {m.name} from {shown}")
        return uid

    def call_nested(self, addr, a0, a1, a2=0):
        if getattr(self, '_nest', 0) > 2: return 0
        self._nest = getattr(self, '_nest', 0) + 1
        try: return self._call_nested(addr, a0, a1, a2)
        finally: self._nest -= 1

    def _call_nested(self, addr, a0, a1, a2=0):
        c = self.cpu; saved = (list(c.r), c.hi, c.lo, c.pc, list(c.f), c.cc, list(c.v), list(c.vc), c.fcr31)
        c.r[4], c.r[5], c.r[6], c.r[31] = a0, a1, a2, c.halt; c.pc = addr
        try: c.run(2_000_000)
        except Returned: pass
        ret = c.r[2]
        # put everything back IN PLACE: the system call that called us still holds a reference to c.r and writes its
        # return value into it afterwards (replacing the list here used to throw that return value away)
        c.r[:] = saved[0]; c.f[:] = saved[4]; c.v[:] = saved[6]; c.vc[:] = saved[7]
        c.hi, c.lo, c.pc, c.cc, c.fcr31 = saved[1], saved[2], saved[3], saved[5], saved[8]
        return ret

    # -------- scheduler
    def run(self, total):
        cpu = self.cpu; budget = total
        while True:
            now = self.now()
            for t in self.threads.values():
                if t.state == 'sleep' and t.wake <= now: t.state = 'ready'
                if t.state == 'waitthread' and self.threads[t.wait].state in ('dead', 'dormant'): t.state = 'ready'
            ready = [t for t in self.threads.values() if t.state == 'ready']
            if not ready and (self.ge.busy() or self.ge.pending()):     # everyone waits: let the drawing finish first
                self.ge.sync(); self.ge.deliver(); continue
            if not ready:
                sleepers = [t.wake for t in self.threads.values() if t.state == 'sleep' and t.wake < INF]
                if not sleepers: raise Stop("every thread is blocked or finished", cpu.pc)
                self.clock += int(min(sleepers) - now) + 1
                # (everyone sleeps: the game clock jumps to the next wake-up. play.py asks to get control back here: it
                #  waits until that moment has come in real time. Without it the game went on right away - up to 50 ms
                #  ahead of real time - and handed over its new picture that much too early for the 60 fps pacing.)
                if self.idle_stop: raise Stop("budget: idle until the next wake-up", cpu.pc)
                continue
            t = min(ready, key=lambda x: x.prio)
            if self.cur is not t and self.cur and self.cur.state == 'running': self.cur.state = 'ready'
            self.cur = t; self.restore(t); t.state = 'running'
            if now >= self.next_vblank:                  # 60 Hz "vblank": run the game's registered vblank handlers
                self.next_vblank = now + 16667; self.vblanks += 1
                for (intno, subid), (h, arg) in list(self.subintr.items()):
                    if intno == 0x1e: self.call_nested(h, subid, arg)
                gpt = self.__dict__.get('gl_present_t')
                if gpt is not None:                      # 60 fps mode: a picture at every vblank, part of the way to the newest
                    fb = getattr(self, 'cur_fb', None)
                    if fb and fb[0]:
                        self._ip_k = self.__dict__.get('_ip_k', 0) + 1
                        gpt(fb[0], fb[1] or 512, fb[2] if fb[2] < 4 else 3, min(1.0, self._ip_k / self.__dict__.get('_ip_iv', 1)))
            if self.ge.pending(): self.ge.deliver()                      # "drawing done" callbacks, like an interrupt
            try:
                cpu.run(50_000)
            except Limit: t.state = 'ready'; self.save(t)
            except Yield:
                self.save(t)
                if t.state == 'running': t.state = 'ready'
            except Returned: t.state = 'dead'
            if getattr(self, 'save_at', None) and cpu.icount >= self.save_at and self.state_path: self.save_at = None; self.want_save = True
            if self.want_save: self.want_save = False; self.save_state(self.state_path)
            if len(self.log) > 600000: del self.log[:200000]; self.log_base += 200000
            budget -= 1
            if cpu.icount > total: raise Stop("total instruction budget used", cpu.pc)

def _movie2_files(mach, cpu):
    """Everything the game did after the 2nd movie started (sound threads left out), and disassembly of the code that made the calls."""
    import re
    from psp_dis import dis
    mark = getattr(mach, 'm2_mark', None)
    if mark is None: print("movie 2 never started: no psp_movie2_log.txt"); return
    lines = [l for l in mach.log[max(0, mark - mach.log_base):] if not l.startswith(('[snd_thread', '[soundbuf'))]
    sel = lines if len(lines) <= 6000 else lines[:4000] + [f"... {len(lines) - 6000} lines left out ..."] + lines[-2000:]
    out = mach.outdir
    with open(os.path.join(out, "psp_movie2_log.txt"), "w", encoding="utf-8", errors="replace") as f: f.write("\n".join(sel))
    ras = {}
    for l in lines:
        mm = re.match(r"^\[([^\]]+)\].*ra=(0x[0-9a-f]+)", l)
        if mm and mm.group(1) in ('Main', 'ringbuf', 'decavc', 'decatr'): ras.setdefault(int(mm.group(2), 16), mm.group(1))
    spots = {ra: f"return point of a call made by thread '{th}'" for ra, th in ras.items()}
    for t in mach.threads.values():
        if t.name in ('ringbuf', 'decavc', 'decatr', 'Main'): spots.setdefault(t.entry, f"start of thread '{t.name}'")
        if t.ctx: spots.setdefault(t.ctx[3], f"where thread '{t.name}' is now")
    for rb in mach.mpg.get('rbs', []):
        try: spots.setdefault(cpu.r32(rb + 24), "ringbuffer read callback")
        except Exception: pass
    rng = sorted((a - 0x100, a + 0x60, a) for a in spots if a)
    merged = []
    for lo, hi, a in rng:
        if merged and lo <= merged[-1][1]: merged[-1][1] = max(merged[-1][1], hi); merged[-1][2].append(a)
        else: merged.append([lo, hi, [a]])
    rev = {v: f"{l}.{n:08X}" for (l, n), v in mach.exports.items()}
    total = 0
    with open(os.path.join(out, "psp_disasm.txt"), "w", encoding="utf-8") as f:
        for lo, hi, marks in merged:
            if total > 12000: f.write("\n(more left out)\n"); break
            f.write(f"\n===== {lo:#x} - {hi:#x} =====\n")
            for a in range(lo & ~3, hi, 4):
                try: w = cpu.r32(a)
                except Exception: break
                txt = dis(w, a); note = ''
                if txt.startswith('jal ') or txt.startswith('j '):
                    tg = int(txt.split()[1], 16)
                    if tg in cpu.stubs: ln, nid = cpu.stubs[tg]; note = f"   <{ln}.{NAMES.get(nid, f'{nid:08X}')}>"
                    elif tg in rev: note = f"   <{rev[tg]}>"
                if a in spots: f.write(f"   ; ---- {spots[a]}\n")
                f.write(f"{a:#010x}: {w:08x}  {txt}{note}\n"); total += 1
    print(f"movie-2 log: {len(lines)} calls -> psp_movie2_log.txt; disassembly of {len(spots)} spots -> psp_disasm.txt")

class _Tee:
    def __init__(self, a, b): self.a, self.b = a, b
    def write(self, x): self.a.write(x); self.b.write(x)
    def flush(self): self.a.flush(); self.b.flush()

def main():
    try:
        rep = os.path.join(os.path.dirname(os.path.abspath(__file__)), "psp_run_report.txt")
        _f = open(rep, "w", encoding="utf-8", errors="replace"); _old = sys.stdout; sys.stdout = _Tee(_old, _f)
        try: return _main()
        finally: sys.stdout = _old; _f.close(); print("\n(the same report was saved to", rep, ")")
    except OSError:
        return _main()

def _main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    path = args[0]; limit = int(args[1]) if len(args) > 1 else 30_000_000
    gamedir = next((a.split('=', 1)[1] for a in sys.argv if a.startswith('--game=')), None)
    resume = next((a.split('=', 1)[1] for a in sys.argv if a.startswith('--resume=')), None)
    savep = next((a.split('=', 1)[1] for a in sys.argv if a.startswith('--save=')), None)
    if resume:
        mach = Machine.load_state(resume); cpu = mach.cpu; limit += cpu.icount
        mach.outdir = os.path.dirname(os.path.abspath(path))        # pictures go next to the EBOOT on this computer
        mach.menu_state_path = next((a.split('=', 1)[1] for a in sys.argv if a.startswith('--save-menu=')), None)
        print(f"resumed from saved state {resume} at {cpu.icount:,} instructions; running {limit - cpu.icount:,} more")
        return _finish(mach, cpu, limit)
    mach, cpu = power_on(path, gamedir, find_iso(gamedir) if gamedir else None); mach.state_path = savep
    mach.save_at = next((int(a.split('=', 1)[1]) for a in sys.argv if a.startswith('--save-at=')), None)
    mach.menu_state_path = next((a.split('=', 1)[1] for a in sys.argv if a.startswith('--save-menu=')), None)
    mach.fallback = next((a.split('=', 1)[1] for a in sys.argv if a.startswith('--files=')), mach.fallback if not gamedir else None)
    mach.lenient = '--lenient' in sys.argv
    return _finish(mach, cpu, limit)

def find_iso(folder):
    """the game's .iso image in this folder, if there is one"""
    try: names = sorted(os.listdir(folder))
    except OSError: return None
    for n in names:
        if n.lower().endswith('.iso'): return os.path.join(folder, n)
    return None

def power_on(path, gamedir, iso=None):
    """load the EBOOT and set up a fresh PSP, ready to run from the start (from the .iso image if given)"""
    m = PrxModule.load(path, BASE)
    cpu = CPU(); mach = Machine(cpu, gamedir, os.path.dirname(os.path.abspath(path)))
    mach.iso = IsoDisc(iso) if iso else None
    mach.port_version = PORT_VERSION
    if mach.iso: print("Reading the game from the disc image", iso, f"({len(mach.iso.files)} files)")
    mach.lenient = True
    mach.mpg = {'v': [], 'a': [], 'pend': {}, 'nf': 0, 'end': 0, 'lastpts': {'v': 0, 'a': 0}, 'pops': {}, 'dec': None, 'frame': None, 'fmt': 3, 'shown': 0}
    import psp_ge; mach.ge = psp_ge.GE(cpu, mach); mach.ge.render = not os.environ.get('PSP_NOGE'); mach.outdir = os.path.dirname(os.path.abspath(path)); mach.saved_frames = []
    cpu.load_bytes(m.lo, bytes(m.mem)); cpu.stubs = dict(m.stubs); cpu.hle = mach.hle
    mi = m.sec['.rodata.sceModuleInfo']; mach.gp = m.read32(m.seg_base[0] + mi[3] + 0x20)
    mach.heap = (m.hi + 0xFFF) & ~0xFFF
    stack = mach.alloc(0x40000, 0x100)
    main_t = Thread(0x1000, "main", BASE + m.entry, 0x20, stack, 0x40000); mach.threads[0x1000] = main_t
    mach.start_thread(main_t, 0, 0)
    named = sum(1 for (l, n) in m.stubs.values() if n in NAMES)
    print(f'module "{m.name}"  entry {BASE + m.entry:#x}  gp {mach.gp:#x}  imports {len(m.stubs)} ({named} named by my table)')
    return mach, cpu

BUTTONS = {'SELECT': 0x1, 'START': 0x8, 'UP': 0x10, 'RIGHT': 0x20, 'DOWN': 0x40, 'LEFT': 0x80, 'L': 0x100, 'R': 0x200,
           'TRIANGLE': 0x1000, 'CIRCLE': 0x2000, 'CROSS': 0x4000, 'X': 0x4000, 'SQUARE': 0x8000}

def load_buttons(mach):
    """buttons.txt next to the EBOOT, one press per line:  <game frame>  <BUTTON[+BUTTON]>  [frames to hold, default 4]
       or  <game frame>  SAVE  <snapshot name>.   Game frames are counted from the start of this run."""
    path = os.path.join(mach.outdir, 'buttons.txt'); mach.pad_script = []; mach.pad_base = getattr(mach, 'pad_reads', 0)
    if not os.path.exists(path): return
    for ln in open(path, encoding='utf-8', errors='replace'):
        p = ln.split('#')[0].split()
        if len(p) < 2 or not p[0].isdigit(): continue
        if p[1].upper() == 'SAVE': mach.pad_script.append((int(p[0]), 'SAVE', p[2] if len(p) > 2 else f'state_frame{p[0]}.pkl.gz')); continue
        mask = 0
        for b in p[1].upper().split('+'): mask |= BUTTONS.get(b, 0)
        mach.pad_script.append((int(p[0]), mask, int(p[2]) if len(p) > 2 and p[2].isdigit() else 4))
    print("button presses from buttons.txt:", [(a, w if w == 'SAVE' else hex(w), h) for a, w, h in mach.pad_script])

def _finish(mach, cpu, limit):
    load_buttons(mach)
    try: mach.run(limit)
    except Stop as e:
        print(f"\nSTOPPED after {cpu.icount:,} instructions at pc={e.pc if e.pc is not None else cpu.pc:#x}: {e.reason}")
        if mach.cur: print(f"in thread '{mach.cur.name}'")
    print("\nthreads:", {t.name: t.state for t in mach.threads.values()})
    if getattr(mach, 'aout', None) is not None:
        mach.aout.close(); print(f"sound: {mach.aout.frames / 44100:.1f} seconds written to sound_out.wav")
    if getattr(mach, 'font', None) is not None: print("system font calls:", mach.font.calls)
    if mach.lbn_table:
        big = sorted(mach.lbn_table, key=lambda e: -e[1])[:8]
        print(f"sector table: {len(mach.lbn_table)} files, covers 0x1000-{mach.next_lbn:#x}")
        for st, ns, fp in big: print(f"   biggest: sectors {st:#x}-{st+ns:#x} ({ns} sectors)  {fp}")
        print("   files named slot/codec:", [(hex(st), ns, fp) for st, ns, fp in mach.lbn_table if 'slot' in fp.lower() or 'codec' in fp.lower()])
    if mach.strlog: print("string args to game-module calls:", mach.strlog[:40])
    if mach.openlog: print("files opened:", mach.openlog)
    print("thread positions (pc / ra):", {t.name: (hex(t.ctx[3]), hex(t.ctx[0][31])) for t in mach.threads.values() if t.ctx})
    _d = getattr(mach, "mpg", {}).get("dec")
    print("\nvideo decoder:", (f"{_d.mode}, frames decoded {_d.count}, shown to game {mach.mpg['shown']}, last problem: {_d.err}" if _d else ("not started yet for this movie" if _d is None else "NOT AVAILABLE (install PyAV: see instructions)")))
    print("\nmovie data: AUs popped", getattr(mach, "mpg", {}).get("pops"), "queued video/audio:", len(getattr(mach, "mpg", {}).get("v", [])), len(getattr(mach, "mpg", {}).get("a", [])))
    _mp = getattr(mach, "mpg", {}); print("movie pictures written to:", [hex(x) for x in sorted(_mp.get('targets', []))], "pixel format:", _mp.get('fmt'), _mp.get('err', ''))
    for _k, _t in enumerate(sorted(_mp.get('targets', []))[:4]):
        try: mach.ge.dump_frame(os.path.join(mach.outdir, f"movie_buffer_{_k}.png"), _t, 512, _mp.get('fmt', 3))
        except Exception as _e: print("   movie buffer dump failed:", _e)
    print("\nlast 80 sceMpeg calls:", *getattr(mach, "mpegring", []), sep="\n   ")
    for _rb in sorted(getattr(mach, "mpg", {}).get("rbs", [])):
        try: print(f"ringbuffer {_rb:#x}: packets/read/written/free/pktsize/data/callback/cbarg =", [hex(cpu.r32(_rb + i * 4)) for i in range(9)])
        except Exception as _e: print("ringbuffer dump failed", _e)
    print("\nsceMpeg calls (first 120):", getattr(mach, "mpeglog", []))
    print("\n" + mach.ge.report()); print("   frames saved:", mach.saved_frames, "(frame_XXXX.png next to the EBOOT; display calls:", getattr(mach, 'fb_calls', 0), ")")
    if getattr(mach, 'cur_fb', None) and mach.cur_fb[0]:
        try: mach.ge.dump_frame(os.path.join(mach.outdir, "frame_last.png"), mach.cur_fb[0], mach.cur_fb[1] or 512, mach.cur_fb[2] if mach.cur_fb[2] < 4 else 3)
        except Exception as ex: print("   last frame failed:", ex)
    if os.environ.get("PSP_KEEP"): globals()["LAST"] = mach
    if mach.loaded: print("modules loaded:", *mach.loaded, sep="\n   ")
    if mach.missing: print("files the game asked for that I don't have:", *sorted(set(mach.missing)), sep="\n   ")
    print("\nlast system calls:"); [print("  ", x) for x in mach.log[-14:]]
    print("\ncall counts:", dict(sorted(mach.calls.items(), key=lambda kv: -kv[1])[:14]))
    if mach.unknown:
        print(f"\nimports I answered with 'success, 0' (lenient mode): {len(mach.unknown)}")
        for k, v in list(mach.unknown.items())[:40]: print("  ", k, [hex(x) for x in v])
    print("\nlast screen flips (display call, shown buffer, format, what was drawn since the previous flip, where the GPU was drawing, its format):")
    for e in getattr(mach, 'flip_log', [])[-25:]: print(f"   #{e[0]} show {e[1]:#x} fmt {e[2]}  drawn {e[3] or 'nothing'}  gpu target {e[4]:#x} fmt {e[5]}")
    try:
        mach.ge.write_trace(os.path.join(mach.outdir, "psp_ge_trace.txt")); print("last 20000 graphics commands -> psp_ge_trace.txt")
        for nm, ad, fm in (("vram_0x4000000_16bit", 0x4000000, 1), ("vram_0x4044000_16bit", 0x4044000, 1), ("vram_0x4088000_16bit", 0x4088000, 1),
                           ("vram_0x4000000_32bit", 0x4000000, 3), ("vram_0x4088000_32bit", 0x4088000, 3), ("gpu_target", getattr(mach.ge, 'fb_addr', 0x4000000), getattr(mach.ge, 'fb_fmt', 1))):
            mach.ge.dump_frame(os.path.join(mach.outdir, nm + ".png"), ad, 512, fm)
        print("video memory pictures -> vram_*.png and gpu_target.png")
    except Exception as ex: print("graphics dump failed:", repr(ex))
    if getattr(mach, 'state_note', None): print("\nsave state:", mach.state_note)
    if getattr(mach, 'menu_fb', None) is not None: print(f"START pressed on the title screen at display call {mach.menu_fb} ({getattr(mach, 'menu_icount', 0):,} instructions)")
    else: print("START was not pressed yet (title screen not reached or not long enough)")
    print("game frames this run (controller reads):", getattr(mach, 'pad_reads', 0) - getattr(mach, 'pad_base', 0))
    u = getattr(mach, 'util', None)
    if u and (u.messages or u.saves):
        print("\nsystem message boxes the game showed (answered YES/OK automatically):", *u.messages[-8:], sep="\n   ")
        print("save data operations:", u.saves[-12:])
    if getattr(mach, 'devctls', None): print("Memory Stick checks:", mach.devctls)
    print("UMD ioctls used:", getattr(mach, 'ioctls', {}))
    try: _movie2_files(mach, cpu)
    except Exception as ex: print("movie-2 log/disassembly failed:", repr(ex))
    print("registers: a0-a3 =", [f"{cpu.r[i]:#x}" for i in range(4, 8)], " ra =", f"{cpu.r[31]:#x}", " sp =", f"{cpu.r[29]:#x}")

if __name__ == '__main__': main()
