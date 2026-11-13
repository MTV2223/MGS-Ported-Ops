"""Reads the MPEG program stream inside a PSP movie (.pmf) sector by sector and lists the video/audio
access units (AUs) found - enough for the game's movie code to run, without decoding any picture yet."""
def _pts(b, p):
    return (((b[p] >> 1) & 7) << 30) | (b[p+1] << 22) | ((b[p+2] >> 1) << 15) | (b[p+3] << 7) | (b[p+4] >> 1)

def parse_sector(buf):
    """-> list of (kind 'v'/'a', pts or None, payload size, payload offset in the sector)"""
    out = []; pos = 0; n = len(buf)
    while pos + 6 <= n:
        if buf[pos] == 0 and buf[pos+1] == 0 and buf[pos+2] == 1:
            sid = buf[pos+3]
            if sid == 0xBA:
                pos += 14 + (buf[pos+13] & 7) if (buf[pos+4] >> 6) == 1 else 12; continue
            plen = (buf[pos+4] << 8) | buf[pos+5]
            if sid == 0xB9: break
            if sid == 0xBB or sid == 0xBE or sid == 0xBF: pos += 6 + plen; continue
            if 0xE0 <= sid <= 0xEF or sid == 0xBD or 0xC0 <= sid <= 0xDF:
                end = pos + 6 + plen if plen else n
                end = min(end, n); pts = None; size = 0
                if pos + 9 < n and (buf[pos+6] >> 6) == 2:
                    flags = buf[pos+7]; hlen = buf[pos+8]
                    if flags & 0x80 and pos + 14 <= n: pts = _pts(buf, pos + 9)
                    size = max(0, end - (pos + 9 + hlen))
                else: size = max(0, end - pos - 6)
                out.append(('v' if 0xE0 <= sid <= 0xEF else 'a', pts, size, end - size)); pos = end; continue
        pos += 1
    return out
