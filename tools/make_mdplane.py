#!/usr/bin/env python3
"""PORT32X: round S3b - MDPLANE.BIN = the Genesis demo program + the exported stage data.
  tools/make_mdplane.py program.bin <__blob_start hex> data.dat out.bin
The program is linked at 0x200000 (md/mdplane.ld) and reads its data at __blob_start; the
whole file must fit the Genesis' 128 KB Word RAM bank (1M mode)."""
import struct, sys

def main():
    if len(sys.argv) != 5:
        print(__doc__)
        sys.exit(1)
    prog = open(sys.argv[1], "rb").read()
    blob = int(sys.argv[2], 16)
    data = open(sys.argv[3], "rb").read()
    if data[:4] != b"SCMD":
        sys.exit(f"{sys.argv[3]}: not an MDPLANE data file (no SCMD magic) - run tools/run_md_check.sh with MDEXPORT")
    if prog[:2] != b"\x46\xFC":
        sys.exit(f"{sys.argv[1]}: does not start with move.w #imm,sr - wrong start-up order in the link?")
    off = blob - 0x200000
    if off < len(prog):
        sys.exit(f"__blob_start {blob:#x} lies inside the program ({len(prog)} bytes)")
    out = prog + bytes(off - len(prog)) + data
    if len(out) > 0x20000:
        sys.exit(f"MDPLANE.BIN would be {len(out)} bytes: more than the 128 KB Word RAM bank "
                 f"(program {len(prog)}, data {len(data)}) - export fewer frames")
    frames = struct.unpack_from(">H", data, 10)[0]
    open(sys.argv[4], "wb").write(out)
    print(f"{sys.argv[4]}: {len(out)} bytes (program {len(prog)}, data {len(data)} at {blob:#x}, "
          f"{frames} frames = {frames / 60:.1f} s), {0x20000 - len(out)} bytes free")

if __name__ == "__main__":
    main()
