# R6 – Sonic CD on the Mega EverDrive PRO (EDCD): what is fixed, and what still stops it

26 Oct 2026. The other four CD32X games (OpenJazz, Hurrican, SuperTux, D32XR/Doom) were converted to run
with the Mega EverDrive PRO as their 16 MB RAM cart with switches only (`CART=ED EDCD=1`). Sonic CD was
checked last. Two of its three problems are fixed here; the third is a decision for the owner.

## Fixed: the page switch (ED-PAGE)

The EverDrive's extended SSF mapper has no 2 MB pages. The 4 MB of cartridge space is **eight 512 KB
windows**, each with a bank register: `CTRL0` (`0xA130F0`, the mapper's P/X/W/L/C bits in the high byte and
window 0's bank in bits 0-4) and `CTRL1-7` (`0xA130F2`-`0xA130FE`, bits 0-4). One of this port's 2 MB pages
is four of those banks: **page p = banks 4p..4p+3.**

`sub_main.c`'s page switch (REV R4E, command 46 with `COMM2 = 0xF800 | window << 8 | page`) wrote the page
number as **one even byte** to `0xA130F0`/`0xA130F2`. Measured on the console (26 Oct 2026,
`cd32x-bootcamp/edpage`): **a single even-byte write to a bank register is ignored** - the mapper needs the
lower byte strobe; a word write is taken. That one byte is why every paged game on the EverDrive loaded
its data completely and then found nothing: the page never moved, so every chunk went to whatever was
mapped at reset and verified there.

Under `CART_ED` the switch is now **four word writes** through command 27 (`MD_CMD_SAFE_CART_XFER_W` -
interrupts off, RV = 1, one `move.w` per register; already in `hw_md.s`, nothing new there): window 0 ->
`CTRL0-3`, window 1 -> `CTRL4-7`, bank `page * 4 + k`. `CTRL0` keeps the flags `EDCD.32X` leaves set
(`ED_CTRL0`, default `0x8000` = P on, W off).

## Fixed: page 7 is not ours (the battery and the EverDrive's own software)

Of the EverDrive's 32 banks, **28-30 are the EverDrive's own** (its menu's space and its data buffer) and
**bank 31 IS the battery-backed memory** the saves live in (`MegaEverDrivePro_16MB_RAM/docs/01`, `/docs/04`).
Page 7 = banks 28-31, so page 7 is out. R4I's layout used it for two things:

| What was in page 7 | Under `CART_ED` |
|---|---|
| act file slots 1 and 2 (cart `0x300000`, `0x380000`) | **page 6** (banks 26, 27) - all four slots keep working |
| the menu's image tail (`L0S001`, 2.2 MB: page 6 + `0x98000` bytes in page 7) | **off** - the save screen loads in page 0, as on the 4 MB cart |

The cart test stops at page 6, and the Sub-CPU refuses page 7 as well. Pages 0-6 = 14 MB: the four act
pages (2-5) and all four act file slots are unchanged, so the preload (R4G) works as it does on the 16 MB
cart; only the save screen costs a CD load again.

Both changes are behind the switch: **with `CART_ED` undefined not one line of `sub_main.c` or
`plat_mars.cpp` changes** (the diff is additions only - `#ifdef`/`#else` with the old lines kept verbatim),
so the normal disc builds byte for byte as before.

## NOT fixed, and not fixable with a switch: the boot and the writes (mode 1)

The EverDrive cannot let go of the cartridge line (console, 25 Oct 2026, `EDPROBE`), so with one in the slot
**the console always boots the cartridge**: a disc is started by a launcher ROM (`EDCD.32X`,
`cd32x-bootcamp/edboot/`) that does the BIOS's and the boot block's work itself and leaves the console in
**cartridge mode ("mode 1")**. Two things follow, and both land on Kobo's listener, which this port froze:

1. **The map is different.** In mode 1 the cart is at `0x000000`-`0x3FFFFF` and the Sega CD at `0x400000`
   (Word RAM at `0x600000`). Kobo's `hw_md.s` and `sub_main.c` are written for the CD-boot map: the cart at
   `0x400000 + offset`, Word RAM at `0x200100`. The other four games have exactly one switch for this,
   `MODE1=1`, in their shared stack's listener: it moves every address argument (`0x2xxxxx` -> `0x6xxxxx`,
   `0x4xxxxx`-`0x7xxxxx` -> `0x0xxxxx`-`0x3xxxxx`) and copies through a 1 KB buffer in work RAM, because on
   the console a Word RAM read made with RV = 1 comes out garbled in cartridge mode. Kobo's listener has
   none of this.
2. **The cart is write-protected unless W is on inside each copy.** `EDCD.32X` hands over with `CTRL0` = P
   only: W (write enable) **off**, measured for a reason - with W left on, the 68000's writes elsewhere
   corrupt the cart and the SH2's reads of it (console, 25 Oct 2026, `edw0`). The other games' listener turns
   W on for the duration of a copy into the cart and off again (`m1_cpy_w`). Kobo's `chilly_rv_memcpy`
   (command 33) and `safe_cart_xfer` (26/27) only set RV, so on the EverDrive **every cart write would land
   nowhere**. Separately, the 68000 cannot write the EverDrive's window 0 at all with the 32X on (cart
   `0x000000`-`0x07FFFF` - where the engine lives): a copy there has to be routed through another window.

So Sonic CD needs a `MODE1`-style build of **its own** listener - the one file the owner's standing rule
keeps shut ("once you have a working boot stack then do not touch it"). The owner gave the go-ahead on 26
Oct 2026, and it is **done** (`SCD_MODE1=1`, in `cd32x/kobo/hw_md.s` and the Makefile):

- `hw_md.s`: address translation (`m1_arg`) + the work-RAM buffer (`m1_xfer`, `m1_buf`) + W on/off around the
  cart side of every copy (`m1_cpy_w`), the window-0-through-window-1 route (`m1_cart`), the page switch
  (`m1_page`, `m1_w0bank`) and RV on the mapper-register writes (`m1_regcpy`), all under `SCD_MODE1`; and
  `init_32x`'s MODE1 branch (no SH2 reset, no CD boot table). The fixes from the 26 Oct review are in
  (`SCD_M1CTRL0_W` was a gas comment, the bank shadow was an absolute store, CTRL0 was clobbered, and the
  mapper-register write ran with RV = 0 so no page ever switched).
- the engine carries the 68000 jump table itself (`cd32x/engine/m1_boot.s`, `engine_cart_ed.ld`, `CART_ED=1`):
  the engine overwrites the launcher's image in the EverDrive, so the table must come back with the engine at
  cart `0x200`. No separate `prep_rom.py` step - it is in the engine image.
- the launcher: a second build of the Sub-CPU program with `CART_ED=1 SCD_MODE1=1`, and `SCDSH2.BIN` as its SH2
  loader, fed to `cd32x-bootcamp/edboot/build_edcd.sh <app_mode1.bin> <SCDSH2.BIN> EDCD_SONICCD.32X`.

A `CART_ED=1` disc is the 16 MB RAM cart's disc with the EverDrive's bank rules obeyed; `EDCD_SONICCD.32X`
(above) boots it from the EverDrive, and the same disc still boots the normal way with a RAM cart.

## Build

The CART_ED disc builds exactly like the normal disc (README "Building the game"), with `CART_ED=1` added to
the `make -C cd32x` line of steps 5 and 11:

```
make -C cd32x ENGINE=1 START="0 0" build_engine/engine.bin CART_ED=1          # step 5
make -C cd32x ENGINE=1 START="0 0" ROUND=R6 CART_ED=1                         # step 11 (the data track)
```

`CART_ED=1` adds `-DCART_ED` to `sub_main.c` and the engine, uses `engine/engine_cart_ed.ld` and adds
`m1_boot.o` (the jump table). Leave it off for the normal disc. Then the **launcher**, built from a second,
MODE1 pass of the Sub-CPU program and Sonic's own SH2 loader (the CD32X BootCamp supplies `build_edcd.sh`):

```
make -C cd32x ENGINE=1 START="0 0" ROUND=R6 CART_ED=1 SCD_MODE1=1 build_engine/disc/APP.BIN   # APP_mode1.bin
cp cd32x/build_engine/disc/APP.BIN APP_mode1.bin
bash <CD32X-BootCamp>/edboot/build_edcd.sh APP_mode1.bin cd32x/build_engine/SCDSH2.BIN EDCD_SONICCD.32X
```

(rebuild the disc's own `APP.BIN` without `SCD_MODE1` afterwards - the disc keeps the normal listener). Copy
`EDCD_SONICCD.32X` to the EverDrive's SD card, put the burned CART_ED disc in the Sega CD, and start the
launcher from the EverDrive menu. `EDCD_SAVE=1` in front of `build_edcd.sh` declares bank 31 so saves persist.

## Checked here (no console, no toolchain in the sandbox)

- Both files preprocessed with the engine's own defines, with and without `CART_ED`: with it, **no reference to
  page 7 is left** in `plat_mars.cpp` (`big_w1(7)` 2 -> 0, `big_engine(BIG_MENU)` 3 -> 0, `big_show(BIG_MENU)`
  2 -> 0), the act file slots read `bigN == 1 ? 3 : 6` and the cart test's last page `w1_is_page0() ? 3 : 6`;
  without it every one of those lines is the old one. `scene_resident` needs no change: with no menu pages
  `big_scene_of(BIG_MENU)` is 0 and `bigEng[BIG_MENU]` never set, so the save screen is never "resident".
- The register arithmetic: page p, window 0 -> `0xA130F0/F2/F4/F6` = banks 4p..4p+3, window 1 ->
  `0xA130F8/FA/FC/FE` = the same four. Page 6 -> banks 24-27, page 7 -> 28-31 (refused).
- `git diff` is **additions only** in both files and the Makefile: with `CART_ED`/`SCD_MODE1` undefined the
  normal disc is byte for byte what it was (checked: all 349 R5H md5 + `APP.BIN` md5 after every change).

## Tested in ares (26 Oct 2026)

ares v148 with the CD32X BootCamp's EverDrive PRO board (the launcher path: a `SEGA SSF` ROM in the cart slot
+ the disc), the same setup that boots the BootCamp and OpenLara EDCD discs:

- `EDCD_SONICCD.32X` + the CART_ED disc boots: the 32X handshake completes (`M_OK`/`S_OK` at ~frame 120), the
  loader copies the engine into the EverDrive, the **title renders** (clouds on the floor chip), and the R4G
  first-time preload runs ("SETTING UP THE CART") - i.e. the `m1_page`/`m1_regcpy` page switches work, which
  is the fix the console flagged as the reason paged games "loaded everything and then found nothing".
- a 60,000-frame run (1,000 s emulated) gets through the preload and plays the title's attract demos -
  special stage 3 and regular acts, each from its own page of the EverDrive - and back to the title: the
  scenes the preload put into pages 1-6 are found and played.
- ares does not model the EverDrive's write-enable bit, so it cannot prove the `m1_cpy_w` W-toggle or the
  RV-garbage rule; those, and the whole boot, still need the console. But the boot no longer stops at the
  splash, and the page switch - the specific thing that was broken - is exercised.
