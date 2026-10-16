#!/usr/bin/env python3
# PORT32X D8: the data part of MDPLANE.BIN for the zone disc - only the format-3 header (md/mdplane_data.h):
# no cells (each act's tiles come from the cart), black CRAM. Usage: make_md_header.py <out.dat>
import sys, struct
cells_off, cram_off = 72, 72
total = cram_off + 128
hdr = struct.pack('>4sHHHHIH', b'SCMD', 3, 224, 320, 0, cells_off, 0) + bytes(2) + struct.pack('>IIII', 0, 0, cram_off, 0)
hdr += struct.pack('>IHHBBH', 0, 0, 0, 0, 0, 0) + struct.pack('>IHHBBH', 0, 1, 0, 0, 0, 0) + struct.pack('>IHHI', total, 0xFFFF, 0, 0)
assert len(hdr) == 72
open(sys.argv[1], 'wb').write(hdr + bytes(128))
