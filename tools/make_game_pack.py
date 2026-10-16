#!/usr/bin/env python3
"""PORT32X S7c: SONICCD.PAK - the game in one file for the cart (Kobo / OpenLara style: one file, its
own file number 21 -> cart offset 0, loaded by CMD 38 / 46 after the boot sequence).
  tools/make_game_pack.py <engine image> <stage cart image> <stage data pack> <out SONICCD.PAK>
Layout = cart offsets the engine expects (cd32x/engine/plat_mars.cpp):
  0x000000 engine image (code + initial data)          max 512 KB
  0x080000 the stage's console cart image ("SCCB")     max 1 MB
  0x180000 the stage's mini data pack (RSDK)           to the end of the file
The loader takes the data pack's length as (file length - 0x180000)."""
import sys

ENGINE, STAGE, PACK = 0x000000, 0x080000, 0x180000


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        sys.exit(1)
    eng, stg, pak = (open(p, "rb").read() for p in sys.argv[1:4])
    if len(eng) > STAGE - ENGINE:
        sys.exit(f"engine image {len(eng)} bytes - over {STAGE - ENGINE}")
    if len(stg) > PACK - STAGE:
        sys.exit(f"stage image {len(stg)} bytes - over {PACK - STAGE}")
    if stg[:4] != b"SCCB":
        sys.exit("stage image: not a console cart image (SCCB)")
    out = bytearray(PACK + len(pak))
    out[ENGINE:ENGINE + len(eng)] = eng
    out[STAGE:STAGE + len(stg)] = stg
    out[PACK:] = pak
    open(sys.argv[4], "wb").write(out)
    print(f"{sys.argv[4]}: {len(out)} bytes ({(len(out) + 32767) // 32768} chunks) - engine {len(eng)}, stage {len(stg)}, pack {len(pak)}")


if __name__ == "__main__":
    main()
