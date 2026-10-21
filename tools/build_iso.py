#!/usr/bin/env python3
"""REV R5: build_iso.py <disc dir> <boot block> <out.iso> - the data track's ISO 9660 image, laid out as the Makefile's
`genisoimage -sysid "SEGA SEGACD" -volid "SCDENG" -generic-boot <boot block> -full-iso9660-filenames` lays it out
(checked against R4K's SCDENG.iso: the same bytes but the dates). For machines without genisoimage, and for discs with
new files.

Layout: sectors 0-15 the boot block (32 KB); 16 the primary volume descriptor; 17 the terminator; 18 empty; 19 the
L path table (20 empty); 21 the M path table (22 empty); 23 on the root directory (records never cross a sector);
then every file, sorted by its ISO name, each from a new sector; then 150 empty sectors (genisoimage's padding).
Only a root directory (the disc has no folders)."""
import os, struct, sys, time

SECTOR = 2048


def both16(v):
    return struct.pack('<H', v) + struct.pack('>H', v)


def both32(v):
    return struct.pack('<I', v) + struct.pack('>I', v)


def _tz(g):
    return (g.tm_gmtoff // 900) & 0xFF  # (quarter hours from GMT, as genisoimage: local time)


def dir_date(t):
    g = time.localtime(t)
    return bytes([g.tm_year - 1900, g.tm_mon, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec, _tz(g)])


def vol_date(t):
    g = time.localtime(t)
    return time.strftime('%Y%m%d%H%M%S', g).encode() + b'00' + bytes([_tz(g)])


def record(name, extent, size, flags, date):
    n = len(name)
    ln = 33 + n + (1 if n % 2 == 0 else 0)
    r = bytes([ln, 0]) + both32(extent) + both32(size) + date + bytes([flags, 0, 0]) + both16(1) + bytes([n]) + name
    return r + b'\0' * (ln - len(r))


def build(disc, boot, out, now=None):
    now = time.time() if now is None else now
    files = []
    for f in os.listdir(disc):
        p = os.path.join(disc, f)
        if os.path.isfile(p):
            files.append((f.upper() + ';1', p))
    files.sort(key=lambda x: x[0].encode())
    dd = dir_date(now)
    # the root directory's size: its records, none across a sector boundary
    recs = [34, 34] + [33 + len(n) + (1 if len(n) % 2 == 0 else 0) for n, _ in files]
    used, sectors = 0, 1
    for ln in recs:
        if used + ln > SECTOR:
            sectors += 1
            used = 0
        used += ln
    root_ext, root_len = 23, sectors * SECTOR
    ext = root_ext + sectors
    placed = []
    for name, p in files:
        size = os.path.getsize(p)
        placed.append((name, p, ext, size))
        ext += (size + SECTOR - 1) // SECTOR
    total = ext + 150
    img = bytearray(total * SECTOR)
    b = open(boot, 'rb').read()
    assert len(b) <= 16 * SECTOR
    img[0:len(b)] = b
    # the root directory
    root = bytearray(root_len)
    pos = 0
    entries = [record(b'\0', root_ext, root_len, 2, dd), record(b'\1', root_ext, root_len, 2, dd)]
    entries += [record(n.encode(), e, s, 0, dir_date(os.path.getmtime(p))) for n, p, e, s in placed]
    for r in entries:
        if pos // SECTOR != (pos + len(r) - 1) // SECTOR:
            pos = (pos // SECTOR + 1) * SECTOR
        root[pos:pos + len(r)] = r
        pos += len(r)
    img[root_ext * SECTOR:root_ext * SECTOR + root_len] = root
    # the primary volume descriptor
    pvd = bytearray(SECTOR)
    pvd[0:8] = b'\x01CD001\x01\x00'
    pvd[8:40] = b'SEGA SEGACD'.ljust(32)
    pvd[40:72] = b'SCDENG'.ljust(32)
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SECTOR)
    pvd[132:140] = both32(10)
    pvd[140:144] = struct.pack('<I', 19)
    pvd[148:152] = struct.pack('>I', 21)
    pvd[156:190] = record(b'\0', root_ext, root_len, 2, dd)
    pvd[190:318] = b' ' * 128
    pvd[318:446] = b' ' * 128
    pvd[446:574] = b' ' * 128
    pvd[574:702] = b'GENISOIMAGE ISO 9660/HFS FILESYSTEM CREATOR (C) 1993 E.YOUNGDALE (C) 1997-2006 J.PEARSON/J.SCHILLING (C) 2006-2007 CDRKIT TEAM'.ljust(128)
    pvd[702:813] = b' ' * 111
    vd = vol_date(now)
    pvd[813:830] = vd
    pvd[830:847] = vd
    pvd[847:864] = b'0' * 16 + b'\0'
    pvd[864:881] = vd
    pvd[881] = 1
    pvd[883:1395] = b' ' * 512  # (the application use area: spaces, as genisoimage)
    img[16 * SECTOR:17 * SECTOR] = pvd
    img[17 * SECTOR:17 * SECTOR + 8] = b'\xffCD001\x01\x00'
    # the path tables: one entry, the root
    img[19 * SECTOR:19 * SECTOR + 10] = bytes([1, 0]) + struct.pack('<I', root_ext) + struct.pack('<H', 1) + b'\0\0'
    img[21 * SECTOR:21 * SECTOR + 10] = bytes([1, 0]) + struct.pack('>I', root_ext) + struct.pack('>H', 1) + b'\0\0'
    for name, p, e, s in placed:
        img[e * SECTOR:e * SECTOR + s] = open(p, 'rb').read()
    open(out, 'wb').write(img)
    return total, placed


if __name__ == '__main__':
    total, placed = build(sys.argv[1], sys.argv[2], sys.argv[3])
    print(f'{sys.argv[3]}: {len(placed)} files, {total} sectors')
