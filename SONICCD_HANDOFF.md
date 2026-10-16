# SonicCD 32X – handoff (memory between sessions)

Project: Sonic CD (RSDKv3 decomp = the 2011 remaster engine) on Sega CD + 32X + MicroNut99's 4 MB RAM
cart, built on the Kobo CD32X framework. Folder: S:\KoboPort\sega-toolchain-12.1\sega\SonicCD
(WSL /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD). Game data: `Sonic_CD/Data.rsdk` (Steam 2011
release, RSDKv3 pack, PC bytecode or mobile – the S2 report says which).

## Design decision (S2): option 2 – each chip does what it is good at
Micronut99: "Sonic is about smooth speed." Sega-style use of the hardware:
- **Mega Drive VDP**: both scroll planes with per-line parallax (as the 1993 original), and the
  original-style sprites (Sonic, badniks) so the per-tile priority bit handles front/back for free.
- **Genesis 68000**: streams plane rows/columns from the layout in the cart, gets camera and parallax
  values from the SH2 each frame; DMA of sprite tiles.
- **Master SH2**: the 2011 engine (scripts, physics, objects, Tails).
- **32X layer + slave SH2**: special stages (rotating floor; the 1993 game used the Sega CD ASIC),
  menus/Time Attack/Secrets (hundreds of colours), extras that do not fit the 4 palette lines,
  sound effects mixed into the PWM.
Rejected: all-SH2 software drawing (option 1) – speed risk, and the 32X adds no colour to the stages.

## Measurements (from Micronut99's inventory.md, S1/S2)
- Colours: stage tiles use 21–42 colours; reduced to Mega Drive 9-bit almost nothing merges. The 2011
  stage art is the original art: **the 32X adds no colour to stages.** Sprites: Global 39, Players 20,
  zones 50–80 (upper bound), Menu 515, Time Attack 156, Secrets 136, Special 133.
- Planes: every regular act = 1 background (per-line H scroll) + foreground. The foreground appears
  twice in the act's layer slots: once below the sprite line (tiles with visualPlane 0), once above
  (visualPlane 1) – exactly one Mega Drive plane with the priority bit (checked in DrawHLineScrollLayer).
- Video memory: regular acts 19–46% of ~1,728 cells (10–25 KB); special stages 49–68%.
  >15 colours in an 8x8 cell: 0 everywhere. Palette lines (old greedy): 2–3 mostly, 4 in four acts,
  6 in R12A, R62A, R71D, R72D, R73D – S2's optimiser recomputes this.
- Front (in front of sprites) 16x16 tiles per act: 20–320.
- Pack: 929 files, 75 MB; music 55 MB (68 Ogg tracks: JP set, US set, shared past themes R1B–R8B);
  sound effects 74 files, 44.1 kHz 8-bit mono, ~3.2 MB; bytecode 88 files, 2.4 MB; sprites 1.6 MB packed.
- The four time periods of an act share sprites, sound effects, most scripts → all four fit in the
  cart together → time travel without CD loading (the 1993 game loads at every warp).
- Engine static memory (host, 64-bit): 11.3 MB of arrays, mostly maximum sizes and PC-only buffers.
- Engine code (host x86-64 stand-in): ~230 KB at -O2, ~185 KB at -Os with the script compiler off.
  Biggest function: ProcessScript ~29 KB (the script VM = the SH2 hot spot). → The engine code will
  not fit SDRAM with its data; plan: run engine code from the cart through the SH2 cache.
- Floating point: almost only in PC drawing/scaling and analog sticks; game logic is integer.

## Decisions S2 → S3a
- **Music: two disc images** (Micronut99, S2): JP/EU soundtrack disc and US soundtrack disc, same
  program, different audio tracks (as Sega did in 1993). All music is 92 min, too long for one CD;
  per soundtrack ~50 / ~53 min.
- **S3 split** (Claude, S3a): S3a = stage conversion + plane streaming proven on the PC against the
  engine's own layers; S3b = the same md/md_planes.c on the 68000 with the real VDP.

## S2 results (Micronut99's run)
- SH2 compile: all engine files compile with sh-elf-g++ first time. Code 176 KB at -O2, 145 KB at -Os
  (no libraries yet). Drawing.o 45 KB is mostly the software renderer option 2 does not use.
  ProcessScript 28.7 KB = hot spot (7x the 4 KB cache) → plan: ProcessScript + collision in SDRAM,
  the rest of the engine code in the cart.
- Palette lines (optimised, 9-bit colours): every regular act's tiles fit 2–3 lines (lower bound
  reached almost everywhere); the old "6 lines" were the greedy's fault. Sprites: too many to fit
  together with all Global sheets (mobile D-pad, translated pause screens…) – palettes must be
  switched per situation, HUD/extras can go to the 32X. Design task, not a blocker.
- Cart: every act with all four time periods = 68–76% of 4 MB → time travel without CD loading.
- Sound effects at 22 kHz: global 725 KB + 70–165 KB per stage (TimeWarp 6.2 s is the longest;
  Achievement.wav can go). 11 kHz would halve it.
- Scripts expanded: global 144 KB + 57–98 KB per stage (read-only → cart). PC bytecode
  (GS000.bin, RSnnn.bin).

## S3a: the Mega Drive plane model (this round)
- Engine hooks (PORT32X, Drawing.cpp DrawHLineScrollLayer): plat_layer_begin / plat_layer_line /
  plat_layer_end report each layer slot's layer, its first visible layer line (tileYPos) and the
  x of every screen line – exactly what the 68000 needs. The host also draws each layer into a
  scratch buffer to get the layers-only reference; the game's frame is unchanged.
- md/md_planes.c (C99, no host deps → the 68000 will compile it): 64x32 planes; each plane row
  holds one layer cell row and a 64-column window; only entering columns are written; a layer
  whose width divides 512 px is repeated across the row (any scroll works). Per-line hscroll,
  per-plane vscroll. Rows whose 8 lines need >512 px are flagged ("rows the VDP cannot show").
- host/md_check.cpp: converts the loaded stage from the engine's own arrays (palette lines
  from engine palette INDICES so palette animation works; 8x8 cells deduplicated over flips
  and lines; name words per 16x16 tile and flip; chunk entries with front/behind/never-drawn),
  follows animated tiles and layout changes, renders a VDP model (priority: front over behind,
  then A over B; per-line palette bank = water) and compares with the reference in 9-bit colour.
  VRAM plan: cells 0x0000–0xBFFF (cell 0 blank, max 1535), plane A names 0xC000, plane B 0xE000.
- Tests: md/test_md_planes.c (7,200 frames of random layers, camera jumps, wrap, parallax: 0 wrong);
  tools/test_md_check.sh (engine runs a synthetic stage, camera driven by the host: 0 differences
  over 599 frames; three deliberate bugs each caught; ASan clean).
- Real data: tools/run_md_check.sh Sonic_CD/Data.rsdk [scene] [frames]. Starts a stage through
  settings.ini [Dev] StartingCategory=1 / StartingScene (NOT stage=/scene= arguments: those load
  the stage as bonus slot 0xFE and PC bytecode then looks for BS254.bin – no scripts, no Sonic).

## S3a results on real data (Micronut99's run)
- Palmtree Panic 1 (R11A), 3,599 frames: **0 differences** between the engine's layers and the
  Mega Drive model. 3 palette lines, 740 cells (23 KB) – exactly the S2 prediction. No animated
  tiles, no layout changes, no impossible rows. Streaming avg 6 / 1 name words per frame
  (A / B), max 1,984 (first full load).
- Frames showed the real game (HUD, Sonic, rings, timer) but Sonic stuck at the steep slope
  right of the start: the camera hardly moved. The pad script now spin-dashes (S3a2).

## Decision S3a2
- **Screen height 224 lines** (Micronut99, option 2): SCREEN_YSIZE = 224 for RETRO_SCD32X. Scripts
  read Screen.YSize from the engine; the only hard-coded 240s are sprite-sheet coordinates in
  the debug menu. HUD and title cards to be checked on screenshots.

## S3a2 result (Micronut99's run, 224 lines, spin-dash pad)
- R11A 3,599 frames: 0 differences; streaming avg 20 (A) / 10 (B) name words per frame, max 1,856
  (= 29 rows x 64, first full load). Sonic gets past the first slope.
- 224 lines: top HUD fine, **lives counter cut off** at the bottom (2011 scripts place it for 240).
- 224 lines = the 1993 original's screen (320x224); camera keeps Sonic 96 px from the top like the
  classic engines. Only 2011-positioned extras deviate (Micronut99 asked; answered S3b).

## S3b: the 68000 + real VDP demo (this round)
- **HUD rule** (engine, Script.cpp, PORT32X): screen sprites (DrawSpriteScreenXY, DrawNumbers,
  DrawSpriteScreenFX) of the objects in scdBottomObjects (only "HUD" so far - the name is an
  assumption to confirm) placed in the bottom 48 lines of a 240-line screen move up 16. Any other
  screen sprite that ends below line 224 is reported once per object type ("PORT32X: object ...").
  NOT tested with real scripts yet.
- md_planes.c made 68000-friendly: no division/modulo per line (one modulo per written row piece),
  16x16 multiplies only, per-row unwrap offset (rowK) instead of a loop that grew with time.
- Exporter (host/md_check.cpp, SCD32X_MDEXPORT): compact stage (used tiles/chunks only), palette
  lines, per frame: each plane's first line + per-line x as runs + palette changes; stops at the
  Word RAM budget (114 KB data) or on stage/background/tile/layout changes. file.ref = engine CRC
  per frame (lines 8..223, 9-bit). Format: md/mdplane_data.h.
- md/mdplane_main.c = the Genesis program (also builds on the PC, MDPLANE_HOST): VDP H40/V28,
  planes 64x32, A 0xC000, window (status row) 0xD000, B 0xE000, sprites 0xF800 (cleared), per-line
  hscroll 0xFC00; own vblank counter (BIOS level-6 jump at 0xFFFD06/08); computes during the
  picture into a queue, writes queue + hscroll + vscroll + palette changes in vblank; a big reload
  (> 600 words, start, loop) blanks one frame. Status row: frame, overruns, peak words, peak line.
  md/mdplane_crt.s (start-up), md/mdplane.ld (code at 0x200000 Word RAM, variables 0xFF8000).
- host/test_mdplane.c: the program's C code on the PC against a VDP model (control-port pairs,
  registers, auto-increment, bases, per-line scroll, priority): synthetic 675 frames incl. the loop
  restart and palette animation, 0 different; two deliberate VDP-level bugs caught.
- cd32x/: the disc. kobo/ = unchanged copies of Kobo's Sub-CPU/listener/CD/SH2 start-up files.
  sh2_idle.c (handshake, 32X picture off, idle), sub_main.c (Kobo's start-up, then MDPLANE.BIN
  from CD into the Sub-CPU's Word RAM bank, switch_banks, 'M' sent without waiting - the listener
  never answers it), Makefile (Kobo's rules), tools/make_mdplane.py (program + data, 128 KB check).
  No cart used: runs in emulators (Fusion cannot emulate the RAM cart).
- Run on real data: tools/build_plane_disc.sh Sonic_CD/Data.rsdk -> plane check + recording +
  the 68000 program on the PC + cd32x/build/SCDPLANE.iso / .bin / .cue.
- NOT tested: anything compiled by m68k-elf/sh-elf (no such compilers in Claude's sandbox), the
  disc, emulators, hardware. Expect a fix round.
- Known limits of the demo: 128 KB Word RAM -> a few seconds of recording (exporter reports how
  many); the Sub-CPU can no longer talk to the Genesis after 'M' (the final design must merge the
  plane work into the listener loop - S3c).

## S3b2: splash screen, progress bar, no boot text (Micronut99's request)
- Micronut99's S3b host frame: the lives counter is whole at 224 lines -> the "HUD" name was right.
- **No green or white text**: Genesis display off at the very start of sub_main.c (Kobo K30);
  SCD_BOOT_TEXT 1 brings the white step lines back. The Genesis demo's status row is a build
  switch, off by default (make -C cd32x STATUS=1 for a measurement build).
- **Splash** (the 32X layer, 256 colours, 320x224) for every load from now on: splash/splash.jpg
  (Micronut99's art) -> tools/make_splash.py -> splash/SPLASH.SPL (palette + indices; fitted to
  the height, sides = the same art enlarged and darkened; 253 colours dithered; index 0 never
  used = see-through, 254 bar frame, 255 red; 0x0000 avoided, Kobo rule 5). Preview:
  splash/preview.png. Built into the SH2 program (cd32x/splash.s).
- cd32x/sh2_splash.c replaces sh2_idle.c: handshake, Kobo's take_fb / flip_wait / line table,
  splash into both buffers, 256-colour mode on; **red progress bar** (x 40..279, y 204..211):
  COMM12 = bar width in pixels (0..238, written by the Sub-CPU through the 68K; the SH2 only
  reads, with Kobo's comm_pause); COMM2 = 0x5344 "SD" -> 32X layer off. One writer per register.
- sub_main.c: COMM2/COMM12 cleared before 0x0BBB; bar 10% after the handshake, 20% after
  InitCD, 30% after opening MDPLANE.BIN, then 8 read pieces up to 100%. A stop hides the splash
  (COMM2 "SD"), turns the Genesis display on and prints one red line.
- mdplane_main.c: writes COMM2 "SD" (0xA15122) once its first picture is on screen -> the
  splash goes away exactly when the planes appear.
- NOT tested: all of it on the console side (no m68k/sh compilers here). Host test still 0 different.

## S3b2 on Micronut99's PC
- Two link fixes (Genesis program linked through m68k-elf-gcc - gcc 12.1, Kobo's LIBPATH names
  4.6.2; own memset/memcpy as Kobo K21b). All programs built, disc made.
- Fusion: splash shown, crash at the end of the loading screen.

## S3b3: diagnosis with what is already in the code (Micronut99: no new diagnostics)
- sub_main.c SCD_BOOT_TEXT 1 (white step lines, display stays on).
- kobo/hw_md.s green text colour back to 0x00A0 (Kobo's documented switch).
- SH2: the S3b sh2_idle.c (no splash). Makefile STATUS ?= 1 (status row).
- New splash art (Micronut99) converted and stored in splash/ for later; make_splash.py fits any
  shape inside 320x224.

## S3b3 on the REAL CONSOLE (Sega CD + 32X + RAM cart) - and the S3b4 fix
- S3b3 stopped after my line "STARTING THE GENESIS PROGRAM...": the Genesis program never started
  (the text stayed - the program resets the VDP first). The stop is in my own Sub-CPU code
  (read into Word RAM / file check / my hand-written 'M' handshake).
- The real mistake: I replaced Kobo's proven main.c (and its diagnostics) with my own
  sub_main.c. Micronut99: never touch rock-solid code. Known-good diagnostics look like his
  "Normal" photo: Chilly's white 32X lines first, Kobo's green lines written over them to the end.
- S3b4: cd32x/sub_main.c = Kobo's subcpu/main.c ("KOBO CD32X FINAL 6 FUSION") unchanged except
  KOBO_DIAG_TEXT 1, the BUILD: label, and one block before Kobo's main loop: MDPLANE.BIN ->
  Word RAM (8 pieces), Kobo's print_diag on rows 22/23/24/27 ("SCD: ..."), first piece read
  again after printing (print_diag writes into the same bank), switch_banks, then Kobo's own
  send_md_cmd0('M') (send, wait for the echo, return - no hand-written handshake any more).
  kobo/files.h copied (Kobo's main.c includes it).
- The "Normal" photo is another proven build ("D32XR CD M1C QUICK CHECK", OpenLara, cart checks);
  its source is not in what Claude has.

## S3b4 result and S3c (60 fps)
- Micronut99 got the hand-over working ("fixed. its loading"): Palmtree Panic's two planes on the
  VDP in Fusion, parallax correct. Status row: overruns rose with every frame (0x20 per 0x20
  frames) -> ~17 fps: the 68000 doing md_plane_update's per-line maths for 224 lines x 2 planes.
- S3c: format 2. The exporter runs md_planes.c on the PC (own planes from reset at the recording
  start, as the 68000 replays) and stores per frame: V scroll A/B, H scroll as runs of VDP
  values, name-table writes as groups of consecutive addresses, palette changes. The Genesis
  program only expands the H scroll runs during the picture and copies groups + H scroll table
  + V scroll + palette in vertical blank (16-bit reads, no maths). Stage tables no longer in
  Word RAM (only cells + CRAM). Status row 4th number = flushes that ran past vblank into the
  picture. md_planes.c no longer linked into the 68000 program. Host test: 0 different.
- In the game the SH2 will produce the same write list (it owns the engine and is ~3x faster).

## THE WORKING HAND-OVER (S3b4/S3c - record it, it was missing here)
- The listener's 'M' NEVER jumped (its RET test does not pass in this set-up), even with the
  bank parity fix. What works (Fusion, S3c video): cd32x/sub_main.c, after loading MDPLANE.BIN
  and the last switch_banks(): do_md_cmd2(MD_CMD_WRITE_32X_32, 0xFFFD08, 0x200000) - the
  listener's own write command sets the vertical-blank jump address (where Kobo's listener has
  its handler) to 0x200000; at the next vertical blank the 68000 enters the Genesis program,
  which masks interrupts and installs its own handler. Screen line: "SCD: VBLANK -> 200000".
  No 68K commands from the Sub-CPU after that.
- Micronut99 did NOT change any file: this fix is Claude's, shipped in the S3c package.
  Claude forgot it and wrongly asked him for "his" change.

## S3c result (Fusion video)
- 60 fps: overruns 0000 through the whole recording. Peak words 0E80 (full reload at loop start).
- Fourth number (copies running past vertical blank) rises on most frames (00E5 by frame 00E1):
  the copy ends just after vblank (never a whole frame late; nothing visible). Cure later:
  DMA for the H scroll table.
- Kobo's boot and diagnostics as Micronut99 knows them; SAFE CART: NOT FOUND in Fusion (no cart).

## Instructions to Micronut99 (rule)
- Every command starts with a full path (cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/... and full
  paths in make -C / tee / file names). Micronut99, S3c.
- After unzipping a package, rebuild the disc from scratch (rm -rf .../cd32x/build): unzip keeps
  the package's older file times, so make may reuse old objects.

## S4: the SH2 computes the planes live (from the recorded camera, until the engine runs there)
- md/md_record.c: one frame's record (format 2) - used by the PC exporter AND the SH2, so both
  produce the same bytes (PC test: identical VDP write counts to S3c).
- Format 3 data file (exporter default now; SCD32X_MDEXPORT_FORMAT=2 for the S3c replay): S3b's
  layout - cells, compact quads/chunks, CRAM, layouts, per-frame camera runs + palette changes.
- md/sh2_frames.c: walks the camera frames, keeps both planes + palette state, builds records;
  at the loop restart: planes from scratch, palette catch-up against what the 68000 shows.
- cd32x/sh2_planes.c (master SH2): Kobo handshake, COMM8 cleared at start, 32X layer off; waits
  for COMM10 = 0xDA7A with FM = 1, copies the file from the frame buffer to SDRAM 0x06008000;
  loop: wait COMM10 = previous id and FM = 1, build record, write it to the frame buffer (16-bit),
  COMM8 = id. SDRAM: program < 0x06008000 (checked: COMM12 = 4BE6), data 0x06008000-0x06027FFF,
  write lists 0x06028000/0x0602A000, record 0x0602C000. Stops with COMM12 4BE5 (file too big),
  4BE3 (not format 3), 4BE2 (record too big). Linked through sh-elf-gcc with libgcc; own memset /
  memcpy; C extern _bss_end = linker __bss_end (sh-elf underscore).
- md/mdplane_main.c format 3: after cells + CRAM, copies the whole file into the frame buffer
  (FM = 0, 16-bit writes, FM = 1), COMM10 = 0xDA7A; per frame: wait COMM8 = id (pauses), FM = 0,
  read the record from 0x840000, vblank copy as format 2, FM = 1, COMM10 = id. Ids never 0 or
  0xDA7A (MDP_NEXT_ID, both sides). Only the 68000 switches FM (as Kobo's init_32x).
- PC test (tools/test_mdplane.sh): format 3 with the SH2 model - 675 frames 0 different, 0
  frame-buffer protocol violations, loop restart included.
- NOT tested: SH2 compile/link, real timing (can the SH2 build a record within one frame).

## S4 on Micronut99's PC: black screen - S4b fix
- S4 disc: black after the boot text, also with an FM read-back loop on the 68000 side (that
  "Yes! working" message was about another project - NOT this one).
- Real cause: S4 did not follow Kobo's proven CMD 50 pattern. In Kobo only the SH2 switches FM
  (give_fb: write 0, wait until it reads 0; take_fb), the 68000 only uses the frame buffer in
  between - and Kobo K14: zero words written by the 68000 may not land, the SH2 program's boot
  copy shows through -> the SH2 clears the area first. S4 let the 68000 switch FM and wrote the
  data file (full of zero words) over the uncleared boot copy -> corrupted tables -> SH2 crash.
- S4b: sh2_planes.c clears the frame buffer, give_fb, COMM8 = 0xDA7A; the 68000 copies the file,
  COMM10 = 0xDA7A; take_fb. Per frame: record, give_fb, COMM8 = id; 68000 copies in vblank,
  COMM10 = id; take_fb. The 68000 never touches FM (fm_take/fm_give removed).
- PC model now as hardware: frame buffer starts with junk, 68000 zero words dropped, FM by the
  SH2 only, violations counted. Without the clear the SH2 code crashes (as on the console).
- Rule learned: copy Kobo's hardware PATTERNS (who does what, in which order), not just its
  instructions; read Kobo's K-notes before using any shared hardware.

## S4b result (Fusion video)
- The SH2 computes the planes live: 60 fps (724 frames in ~12 s), picture correct. Overruns +1
  per recording loop (the full reload of both planes, 3,712 words, at each restart) - expected.
  Late copies on most frames as in S3c (DMA later).

## S5: the engine on the SH2 (Micronut99: "do it") - plan
- S5a: link the real engine for the SH2 (tools/sh2_link_probe.sh, probe layout
  cd32x/engine/probe.ld: code in the cart window, arrays in an oversized pseudo-region) with the
  platform layer cd32x/engine/plat_mars.cpp (stubs for every plat.hpp function + __dso_handle,
  _init, _fini). Output sh2_link.txt: sections, per-file sizes, biggest arrays, library pieces,
  missing symbols. -> the real memory plan.
- S5b: shrink the arrays to what Sonic CD uses; read-only stage data (expanded scripts, chunks,
  collision) prepared on the PC (like the plane export) for the cart. Host build must stay exact.
- S5c: boot - Kobo's CMD 38/46 path (proven, KOBO_NOCART 0) loads engine + stage into the cart
  BEFORE the 68000 hand-over (the hand-over becomes a Sub-CPU command in Kobo's main loop); the
  SH2 runs the engine and reports progress. Console with RAM cart only (Fusion has no cart).
- S5d: the engine's own layer scroll (plat_layer_*) feeds md/sh2_frames.c instead of the recording.

## S5a result (Micronut99's link probe)
- The whole engine LINKS for the SH2 (after two fixes: own system calls in plat_mars.cpp - the SH
  libc's syscalls.o defines errno twice; libnosys dropped). .text 290 KB (112 KB library), rodata
  18 KB, eh_frame 6.5 KB, data 3 KB, bss 11.4 MB. Library: cp-demangle 29 KB + unwind 15 KB
  (pulled by operator new / terminate handler), printf/scanf family ~35 KB.

## S5b: the SH2 configuration (no-draw + small arrays)
- engine: SCD32X_SH2 -> SCD32X_NODRAW + SCD32X_SMALL (RetroEngine.hpp; defaults 0 for all builds).
  NODRAW: SCD_NODRAW_RETURN() at the top of every pixel writer (sprites, rects, text, faces, V/3D
  layers, ClearScreen, debug overlays, Draw3DScene); DrawHLineScrollLayer keeps the scroll and the
  vertical bookkeeping, only the per-line pixel block is cut. Draw SCRIPTS still run (they can
  change game state). SMALL: graphicData, tilesetGFXData, texBuffer, poly lists, tint / blend /
  subtract tables, 32-bit palette table, 3D floor buffer shrink to 1-4 entries; their writers
  guarded (GIF sprite loader keeps sizes/positions, BMP/GFX loaders refuse with a log - Sonic CD
  is all GIF; stage tileset loaders keep the palette, skip pixels; Copy16x16Tile and
  Init3DFloorBuffer return - animated tiles = TODO S5d; HW set-up functions return). On the real
  SH2 (__sh__) the software frame buffer is one line.
- plat_mars.cpp: plain operator new/delete (malloc), __cxa_pure_virtual, own
  __gnu_cxx::__verbose_terminate_handler (keeps demangler + unwinder out); probe adds
  -fno-asynchronous-unwind-tables -fno-unwind-tables.
- Test: host build in the SH2 configuration (host Makefile SH2CFG=1, ASan) vs the normal build:
  camera log (SCD32X_CAMLOG: camera, active layers, per-line scroll CRC per layer, CRC of all
  palettes and of objectEntityList) identical. tools/test_sh2cfg.sh [Data.rsdk]: synthetic 601
  frames identical; a deliberate scroll bug caught at frame 2. build.sh step 8.
- Expected bss after S5b: ~4 MB (stageLayouts 1.4 MB, scriptCode 1 MB, chunks 320 KB, stageList
  197 KB, collision 141 KB, sprite/anim frames 224 KB, 3D 184 KB, entities 80 KB ...) -> S5c:
  real sizes + cart placement.

## S5b result (Micronut99's run)
- Real Palmtree Panic, 1,801 frames: camera logs identical (SH2 configuration vs normal). Link probe:
  code 227 KB (-63), bss 4.18 MB (-7.2 MB). Still pulled: unwind-dw2 15 KB, printf/scanf ~35 KB.

## S5c: cart image + layout pool (SH2 configuration; normal builds unchanged)
- SCD32X_CARTDATA (= SCD32X_SH2): scriptCode, jumpTable, tiles128x128, collisionMasks are C++
  references bound to plat_cart_data(kind) - every use in the engine unchanged. LoadBytecode
  parses as before but stores nothing (SCD_CODE(p) = a local sink); ClearScriptData's memsets,
  LoadStageChunks and LoadStageCollisions skipped. plat_stage_loaded() at the end of
  LoadStageFiles: the PC normal build writes the image (SCD32X_CARTIMG_OUT); the PC SH2
  configuration reads it (SCD32X_CARTIMG_IN). PC image = native order (test only); the console
  image (big-endian, SH2 struct layout, per stage) is still to build. SH2: placeholder cart
  addresses 0x02200000.. (plat_mars.cpp).
- FOUND: FUNC_SET16X16TILEINFO writes tiles128x128 and collisionMasks at RUN TIME (breakable
  walls etc.). The PC test cannot see the problem (its image is writable) - the SH2 configuration
  logs "PORT32X: Set16x16TileInfo used ..." once. If Sonic CD uses it: a writable SDRAM overlay
  for those tables is needed. Still to do: scan all bytecode for it.
- Layout pool (SCD32X_SMALL): TileLayer.tiles / lineScroll are pointers into a 96 KB pool
  (plat_layout_pool; SH2: frame buffer 0x24008000), each layer gets exactly its file's rows (row
  width stays 0x100). Reset in LoadStageBackground; the foreground keeps its rows on an act
  reload. Guards: SetTileLayerEntry / GetTileLayerEntry outside the rows dropped / 0 (logged
  once); a script growing ysize past the rows is kept at the rows (logged). FOUND: player sensor
  collision reads the foreground with no upper bound (falling below the layout) - the original
  reads cleared rows (0): SCD_FG_TILE() (Scene.hpp) returns 0 outside the rows; 16 collision reads
  + 2 script reads (Get/Set16x16TileInfo) routed through it.
- Real layout sizes (inventory): regular stages <= 18 rows (widest 130 chunks), special stages
  32, Help 34, Credits 128 (4 wide) -> the 96 KB pool covers Credits.
- Test: tools/test_sh2cfg.sh - the normal build writes the cart image, the SH2 configuration runs
  from it; camera log now also has a CRC of the foreground layout. Synthetic 601 frames identical;
  deliberate bugs caught (one row too few). The script prints the SH2 configuration's PORT32X
  messages (pool full, outside rows, Set16x16TileInfo).
- Expected bss now ~1.2 MB: stageList 197 KB, sprite/anim frames 224 KB, 3D 184 KB, entities
  80 KB, arcTan 64 KB, menus 50 KB, draw list 38 KB, save 32 KB ... -> next round.

## S5c result (Micronut99's run)
- Real Palmtree Panic 1,801 frames identical with scripts/chunks/collision from the cart image and
  layouts from the pool; no PORT32X messages (no Set16x16TileInfo, no writes outside rows, pool
  ok in that act). Probe: bss 4.18 MB -> 1.12 MB, code 227 KB.

## S5d (this round)
- tools/scan_bytecode.py Data.rsdk [report]: decodes every stage's bytecode (as LoadBytecode),
  walks the instructions exactly as ProcessScript reads operands (1 var: mode +2 unless 0 + var;
  2 int; 3 string: len, 4 chars/word, +1); function names / operand counts and variable numbers
  read from engine/Script.cpp. Reports which stages use Set16x16TileInfo, Copy16x16Tile,
  Get/SetTileLayerEntry, 3D functions, LoadAnimation, SpriteFrame, ... and the 3D-buffer and
  layer-size variables. "walk OK" for every file proves the decoding. Synthetic tests incl.
  strings of length 0/3/4/5/14.
- Cart image +4 parts: scriptFrames, animFrames, animationList (filled while the stage loads:
  SpriteFrame only in SUB_SETUP, LoadAnimationFile from setup) and arcTan256LookupTable (also
  saves 65,536 soft-float atan2f at boot). Writers count but store into a local sink; clearing
  skipped; CalculateTrigAngles skips the table. scdStageLoaded (false at LoadStageFiles start,
  true after plat_stage_loaded): an animation file loaded during play logs "PORT32X: an animation
  file was loaded after the stage load".
- sh2_link_probe.sh prints why the unwinder / scanf / strtod / printf pieces are linked (the
  map's "Archive member included" list).

## S5d results (Micronut99's run)
- Real Palmtree Panic identical with frame tables + arctangent from the cart image. Probe bss
  803 KB. Library chain: unwinder <- libstdc++ eh_personality; strtod <- atof; scanf <- sscanf;
  printf <- sprintf.
- Bytecode scan, 88/88 files walk OK. Set16x16TileInfo: R32* (CC2, all periods), R33C/D, R83C/D
  (4 calls each) -> those stages need a writable overlay for the chunk / collision tables.
  Copy16x16Tile (animated tiles): 30 stages incl. SS1-8. 3D buffers (Draw3DScene / 3D vars): Title,
  Menu, TAttack, R71-R73 (Stardust Speedway), SS1-8. LoadAnimation in stage scripts: R73C/D, R82*.
  LoadVideo: Menu, R83C/D. Layer-size variables: GLOBAL.

## S5e (this round)
- stageList (193 KB) into the cart image (LoadGameConfig parses into a sink; stage= / scene=
  arguments ignored and logged in the SH2 configuration). DEBUG_HITBOX_COUNT 1 in the SH2
  configuration (the bound and the array shrink together).
- Temp objects live in the top 0x80 entity slots (TEMPENTITY_START = ENTITY_COUNT - 0x80):
  the entity array cannot simply be shortened.
- Host maxima log (with the plane check on): "MAXIMA stage objslot objects drawlists(8) vertices
  faces menurows sfx scriptframes animframes animations code jumps fgrows bgrows".
- tools/measure_stages.py Data.rsdk [frames] [report]: runs EVERY stage (regular, special,
  presentation) with the spin-dash pad: maxima per stage + overall, and the SH2 configuration
  (cart image written by the normal run of that stage) compared by camera log, ASan, PORT32X
  messages. Report: stages_report.md.
- Probe follows one level further (eh_personality, eh_throw, atof, sscanf, sprintf).

## S5e results (Micronut99's whole-game run, 87 stages x 900 frames)
- Maxima (lower bounds - 15 s per stage): object slot 793 (R72C; temp slots are the top 0x80, so
  the 0x4A0 entity array stays), objects 738, draw list 56 per layer (of 1,184), 3D vertices 640 /
  faces 160 (Title), menu rows 397 (Credits), sfx 70, script frames 619, anim frames 318,
  animations 90, script code 69,993 words (of 262,144), jumps 4,844, foreground rows 128
  (Credits), background rows 16. Probe bss 581 KB; library chain: eh_personality <-
  functexcept.o (libstdc++), atof/sscanf <- Ini.o, sprintf/vsprintf <- Debug.o.
- SH2 configuration vs normal: identical on 67 stages. Set16x16TileInfo logged on R32B-D (as the
  scan said; the PC image is writable, so no difference shows). DIFFERS without a message:
  R33C/D, R41A-D, R42A/C/D, R52B, R72B, SS1/2/3/5/8, DAgdn. ASAN ERROR: Title, Secrets/2, Secrets/3.
  HUD-type sprites below 224 lines reported for Water, R7Setup, special-stage Sonic/UFO and
  menus - screen layout work for later.

## S5f (this round)
- FOUND: DrawVLineScrollLayer advances the layer's auto-scroll and the V parallax positions
  (scrollPos, lastYSize, vParallax.linePos/scrollPos) while it draws - S5b's early return cut
  that too. Now the no-draw return comes after that bookkeeping (and after activePalette). Checked
  every other early-returned draw function for global writes: only activePalette (SetLimitedFade
  and the other readers set it first - no logic effect), Draw3DFloorLayer's floor3D* (drawing-
  only), faceLineStart/End (drawing-only).
- measure_stages.py reports WHAT differs (first frame + fields: camera, layer slot scroll,
  palettes, object state, foreground layout) and, for ASan errors, the error type and the first
  engine source line.

## S5f results -> S5g
- DA Garden now identical (the V-layer fix). Left: object state differs on R33C/D, R41A-D,
  R42A/C/D, R52B, R72B, SS1/2/3/5/8; ASan in ProcessScript on Title, Secrets/2, Secrets/3
  (function number / jump table read past the code in the image).
- Causes (both in the TEST, not the SH2 configuration):
  1. Math.cpp seeded rand() from time(NULL): the two runs had different random sequences ->
     object state differed wherever scripts call Rand early. Now srand(0) on RETRO_SCD32X (the
     console has no clock anyway - time() is 0 there).
  2. The PC wrote ONE cart image (first stage load); scenes that move on (Title -> menu, sound
     test, special stage results, act changes) ran new scripts against the old image. Now one
     image per stage load: plat_stage_loading() (start of LoadStageFiles) - the SH2
     configuration reads <base>.<n>; plat_stage_loaded() - the normal build writes <base>.<n>.
     This is also the console's shape: plat_stage_loading selects the stage's cart image.
- measure_stages.py field labels fixed (a checksum starting with a hex letter read as a field).

## Working folder (Micronut99, after S5g)
- The project folder is now S:\KoboPort\sega-toolchain-12.1\sega\SonicCD32X
  (WSL /mnt/s/KoboPort/sega-toolchain-12.1/sega/SonicCD32X). Packages unpack into a top folder
  SonicCD32X; every command uses that full path.

## S5g result (Micronut99's whole-game run)
- All 87 stages identical, 0 ASan errors (fixed seed + one cart image per stage load). Only
  messages: Set16x16TileInfo on R32B-D; sprites below line 224 (screen layout, later).
- Process lesson: S5g was repacked under the same name and never re-posted; Claude then assumed
  Micronut99 had the wrong file. Rule: every changed package gets a NEW name and is posted; ask
  about missing files instead of assuming; install = unzip to /tmp and copy the contents.

## S5h: chunk patch + collision overlay (SH2 configuration)
- tiles128x128 is a proxy (Scene.hpp ScdChunkTable / ScdChunkField / ScdChunkRef): reads go to
  the cart image unless the chunk is patched (bitmap scdChunkPatched); a write copies the chunk's
  entry into a patch list (256, overflow logged "chunk patch list full") and changes it there.
  scd_chunk_patches_reset() in LoadStageChunks (every stage load, as the original reloads chunks).
- collisionMasks: ScdCollisionMasks - floor/wall/roof masks are pointers into the cart image;
  angles and flags are RAM copies (10 KB), refilled in LoadStageCollisions (new stages only, as
  the original).
- PC test: the SH2 configuration's image buffers are page-aligned and mprotect'ed READ-ONLY
  between loads - any write past the overlay stops (deliberate write: SEGV WRITE caught).
  Overlay self-test (SCD32X_TEST_OVERLAY=1, run by test_sh2cfg.sh): patched read, other field,
  neighbour, second write, image unchanged, reset, collision copy - OK; a reset that keeps the
  patches: FAILED: reset.

## S5h result
- All 87 stages identical with the cart image READ-ONLY on the PC; R32B-D log "Set16x16TileInfo
  used (chunk patch / collision overlay, S5h)" - their run-time chunk/collision changes went
  through the overlay.

## Splash / loading screen (Micronut99, after S5h) - NOT switched on yet
- splash/splash.jpg = the new art (Sonic vs Metal Sonic, 738x384); the S3b2 art is kept as
  splash/splash_previous.jpg. The same picture is the splash AND the loading screen.
- Switch it on only after a good boot on real hardware.
- No white flash between the splash and the loading screen (find its source when switching on:
  likely a palette / background colour reset between the two - Kobo: colour 0x0000 is never used,
  black is 0x0400).
- The progress indicator (bar or text) is drawn ONLY on the loading screen, never on the splash.

## S5i: SDRAM - first cuts (SH2 configuration)
- Budget: SDRAM 256 KB; hot code + stack + plane/record buffers ~100 KB -> ~150 KB for data.
- 3D: VERTEXBUFFER_SIZE 0x400, FACEBUFFER_SIZE 0x100 (whole game max 640 / 160 at Title).
  TransformVertexBuffer + Sort3DDrawList skipped in the no-draw build (they fill vertexBufferT /
  drawList3D, which only drawing reads - scripts cannot see them) -> those two are 1 entry.
  TransformVerticies (rewrites vertexBuffer, which scripts read) clamped; script variable reads /
  writes of vertexBuffer / faceBuffer past the buffers return 0 / are dropped; the vertex / face
  counts are clamped. Logged once: "a script used a 3D vertex / face past the SH2 buffers".
- Draw lists: DRAWLIST_SIZE 0x100 per layer (max 56). Adds past it (Object.cpp x2, script
  AddDrawListEntityRef) dropped; Get/SetDrawListEntityRef index checked. Logged once: "a draw
  list is full (256)".
- tileUVArray (hardware renderer only) 1 entry.
- Expected bss ~400 KB (from 581). Still to place: objects 80 KB, menus 50 KB, save 32 KB, font
  20 KB, sfx list 19 KB, type names 16 KB, ...

## S5i result
- All 87 stages identical, no 3D / draw-list messages. Probe: bss 391 KB (from 581). Code grew to
  256 KB: the chunk proxy inlines a field switch at ~200 uses (Collision +12 KB, Drawing +16 KB -
  dead in no-draw). TODO: compile-time field accessors (smaller, faster collision path).

## S5j: SDRAM part two - load-only tables into the cart image
- typeNames (16 KB), globalVariableNames (8 KB), animationFileList (11 KB), fontCharacterList
  (20 KB) -> cart image parts (SCD_CART_TYPENAMES / GLOBALNAMES / ANIMFILES / FONT). Writers parse
  into sinks: SetObjectTypeName, ClearScriptData, LoadGameConfig (+ LoadXMLVariables), LoadAnimationFile,
  LoadFontFile (24 field writes via SCD_FONT). ClearAnimationData skips the list.
- AddAnimationFile on the SH2: the stage's files are already in the image, so the lookup finds
  each at the index the normal build loads it into; an empty slot logs "an animation file is not
  in the cart image". LoadFontFile after the stage load logs "a font file was loaded after the
  stage load".
- Measured now (maxima, measure_stages.py): menutext (highest TextMenu.textDataPos, 0x2800
  reserved per menu, 2 menus = 50 KB) and savetop (highest save slot in use, 0x2000 reserved =
  32 KB) - to size gameMenu / saveRAM next.
- Expected bss ~336 KB. Live data still above the ~150 KB SDRAM budget -> next: the second 128 KB
  frame buffer as a RAM area (menus, 3D, save data), objectScriptList / sfxList (load-time
  tables with run-time fields - to check).

## S5j result -> S5k
- Probe bss 335 KB. Maxima: menutext 4,992 (Credits; 0x2800 reserved per menu), savetop 173
  (TAttack; 0x2000 reserved).
- REGRESSION (Claude's): 70 stages DIFFER from frame 0-9 (object state). S5j's AddAnimationFile
  on the SH2 found each file already in the cart list and returned it WITHOUT calling
  LoadAnimationFile - which also loads the hitboxes (hitboxList, RAM) and the animation's sprite
  sheets (gfxSurface, RAM). Objects lost their hitboxes.
- S5k fix: on the SH2 the lookup only searches the files this load has seen (animationFileCount,
  RAM, reset by ClearAnimationData) - exactly the original order - and a new file still goes
  through LoadAnimationFile (cart tables into sinks, hitboxes / sheets into RAM); the image's
  entry at that index must carry the same name (else logged).
- Camera log gains H = CRC of hitboxList + gfxSurface (loader-built RAM state); report label
  "hitboxes/sheets". The synthetic stage has no objects, so only the real run tests this.
- typeNames checked: no load-time reader depends on a name being empty.

## S5k result
- All 87 stages identical again (hitbox regression fixed); only R32B-D Set16x16TileInfo messages.

## S5l: menu text + save data cut, pool measured (SH2 configuration)
- TEXTDATA_COUNT 0x1800 per menu (max used 4,992 - Credits): the 7 text writes in Text.cpp go
  through SCD_TEXT_PUT (dropped + logged "menu text full"), the script's text read is bounded.
- SAVEDATA_SIZE 0x400 (highest slot used 173 - TAttack): VAR_SAVERAM reads (0) / writes
  (dropped) outside it, logged "a script used a save slot past the SH2's 1,024". (The PC save file
  stays 32 KB; the SH2 configuration reads its first 4 KB. The console's save storage is later.)
- Maxima gain poolkb: the layout pool each stage needs (every layer's rows), to size the frame
  buffer budget (record 32 KB + pool).
- Memory reality noted: the SH2 can write only SDRAM (256 KB) and the ONE frame buffer it sees
  (128 KB; an FS swap only takes effect in vertical blank - no second 128 KB as RAM). The cart is
  read-only for the SH2. Live data must fit SDRAM + frame-buffer spare.

## S5m: faster whole-game check (tools only)
- measure_stages.py ran one stage at a time on ONE core (Micronut99: "the CPU is asleep") and with
  the full plane check. Now: stages in parallel (all cores; -j N to limit; results in stage
  order) and light mode SCD32X_MDCHECK_LIGHT=1 (camera log + maxima only, no tile conversion /
  VDP comparison): 1,800 frames 4.9 s -> 1.9 s per run here. Report identical with -j 1 and -j 4.

## S5l result -> S5m
- All 87 identical. Probe bss 290 KB. poolkb: regular stages <= 20 KB, special 24 KB, Credits 80.
- FOUND: every regular stage + Credits index save slots PAST 1,024 (message), although the
  measured "savetop" was 173 - savetop only saw slots holding a non-zero value. The 15-s runs stay
  identical because nothing reads the dropped writes back yet. S5m measures what scripts really
  index: scdSaveIndexMax (both builds, RETRO_SCD32X) -> report column "saveidx"; the SH2 message
  names the first index. SAVEDATA_SIZE to be set from saveidx next.
- Test speed (Micronut99: the script left the CPU idle): measure_stages.py runs stages in
  parallel (all cores; -j N) and in light mode (SCD32X_MDCHECK_LIGHT=1: camera log + maxima, no
  plane check) - 2.5x per run here, times the core count.

## S5m result -> S5n
- All 87 identical. saveidx 8,191 in most stages; first slot past the SH2's 1,024: 7,168 (0x1C00).
  Sonic CD uses the high end of the save area too (likely per-stage state). The 1,024 cut is wrong
  and must change; the 15-s runs only stay identical because nothing reads those values back yet.
- S5n measures WHICH 256-slot blocks scripts touch (scdSaveBlocks, report column "saveblocks",
  OR-ed over all stages in the summary). Two windows -> map them (e.g. 2 x 4 KB); scattered -> keep
  the full 32 KB.

## S5n result -> S5o: two save regions
- saveblocks over the whole game: f0000001 - scripts use slots 0-255 (4 stages: TAttack, menus)
  and 7,168-8,191 (71 stages, per-stage state); 6,912 slots between never.
- SH2 configuration: SAVEDATA_SIZE 0x500 (5 KB); scd_save_slot(i) (Userdata.hpp) maps 0-255 ->
  0-255 and 7,168-8,191 -> 256-1,279, else -1 (script access dropped / 0, logged with the slot).
  Engine's direct writes (33, 34, 35) are in the low region. The save file keeps the game's
  8,192-slot layout: read / written through the mapping in 1 KB blocks (no 32 KB buffer).
  Mapping checked with the engine header: 1,280 slots one-to-one, edges right.

## S5o result -> S5p
- All 87 identical, no save-slot message (the two-region mapping covers all script use). bss 291 KB.
- Memory picture: data 291 KB + hot code ~60-80 KB + stack/plane buffers ~40 KB = ~400 KB against
  SDRAM 256 KB + frame-buffer spare (~70 KB in regular stages; 128 - 32 record - <=24 pool).
  Plan: boot tables -> cart; menus (34 KB) + 3D (26 KB) -> frame-buffer spare (careful: Credits'
  pool is 80 KB); sfx names -> cart (with the PWM sound work); functions (constructor copies at
  start-up) / achievements (none on the console) small items; hot code SDRAM vs cart = a console
  speed question.
- S5p: the six trig tables (10 KB, soft-float sin/cos at boot) -> one cart part SCD_CART_TRIG
  (CalculateTrigAngles skips the fills on the SH2; srand stays). Chunk proxy: compile-time field
  (ScdChunkRef<T, F>), patched path out of line (scd_chunk_patched_get), solidity plane via
  ScdSolidityRef. Host Collision.o (SH2 config) 40.4 KB vs 32.5 KB normal - the bitmap test per read.

## S5p result (after an install retry - the first S5p run was S5o code)
- All 87 identical. Code 257 -> 227 KB (Collision 36.5 -> 29.3 KB, Drawing 26.7 -> 18.1 KB);
  bss 291 -> 281 KB (trig tables in the cart).

## S5q: the menus in the SH2's frame buffer
- SH2 frame buffer (128 KB): record 12 KB (a full reload is ~5,000 words) | menus 34 KB
  (0x24003000, gameMenu 33,840 bytes) | layout pool 82 KB (0x2400B800; Credits need 80 KB).
  plat_fb_data(SCD_FB_MENUS, size): the SH2 clears the area once (the frame buffer is not zeroed at
  power-on; 16-bit writes, Kobo rule 4). PC: separate zeroed allocation; the PC pool is 82 KB too,
  so the Credits test the tight fit.
- TODO for the live engine: the plane record must stay within 12 KB (S4's RECORD_MAX was 32 KB).
- 3D buffers stay in SDRAM (in the frame buffer they would collide with the Credits' pool).
- SDRAM data ~247 KB after this; fits only if the hot code runs from the cart (console question).

## S5q result -> S5r
- All 87 identical, Credits fit the 82 KB pool (no "pool full"). bss 247 KB.
- S5r: FunctionInfo constexpr -> the functions table is read-only data (4.9 KB out of RAM, all
  builds). FILEBUFFER_SIZE 0x400 in the SH2 configuration (FillFileBuffer's two 0x2000 too).
- Not moved, and why: objectScriptList holds a POINTER (animFile) - a PC-built image cannot carry
  SH2 pointers - and LoadSpriteSheet can set spriteSheetID during play; achievements are indexed
  without bounds checks (4 KB, not worth the risk now). sfxList (19 KB) holds malloc'ed sample
  buffers - it is replaced by the console sound system (PWM from the cart); until then the SH2 build
  skips loading sound effects.
- Memory plan: data ~235 KB - sfxList 19 + stack / SH2 plane streaming ~28 = ~244 of 256 KB SDRAM,
  all code from the cart (SH2 cache); frame buffer: record 12 | menus 34 | layout pool 82 KB.
- NEXT: the console cart image (PC-built, big-endian, SH2 struct layout, per stage) and loading it
  into the RAM cart (Kobo CMD 38/46) before the hand-over.

## S5r result: memory step done
- All 87 identical; bss 235 KB; code 222 KB (all from the cart). SDRAM ~244 of 256 KB once
  sfxList is replaced by the console sound system.

## S6a: the console cart image builder
- tools/cartimg/cart_layout.h: one description of every cart part (element type, every field:
  offset, scalar size, count), used by the builder, the PC probe and the SH2 probe.
- host/cart_build (tools/cartimg/cart_build.cpp): --check = every element byte covered once
  (padding: SpriteFrame 2, FontCharacter 2 - the int alignment), round trip, big-endian test; else
  PC image -> SH2 image "SCCB" v1: 14 parts, per part (offset, bytes, element size, elements) BE,
  16-byte aligned; multi-element parts trimmed after their last non-zero element; chunks /
  collision whole (fields at fixed struct offsets). build.sh step 9.
- PC image (SCD32X_CARTIMG_OUT) gains a trailer "STAG" + "<folder>_<id>" (readers ignore it).
- tools/cart_layout_check.sh: the same 142 layout numbers from g++ (run) and sh-elf-g++ (read BE out
  of the object's .rodata) must be identical - the SH2 compiler's own word on the layout.
- tools/build_cart_images.py Data.rsdk [out] [-j N]: every stage's console image
  (out/<folder>_<id>.bin) + out/cart_budget.md (image sizes, each act's A-D total, parts identical in
  every stage = store once).
- Checked here: image trig part reads back big-endian as the engine's sine values; the SH2
  comparison flags a 2-byte difference.

## S6a result -> S6a2
- tools/cart_layout_check.sh on Micronut99's PC: "SH2 layout identical: 142 numbers".
- Budget (87 images): largest act R61 = 3,014 KB over 4 periods + shared 223 KB + code 222 KB =
  3,459 of 4,096 KB - fits, but sound effects (~725 KB global), sprites and plane cells are not in
  it yet. Chunks 320 KB per image (one struct, untrimmable; 128 KB gfxDataPos is drawing-only).
- S6a2: the chunk table is separate arrays (scdChunkIndex / Dir / Plane / Solid[2], scdChunkCount):
  console part = u32 n, u16 tileIndex[n], u8 direction[n], plane[n], solidity A[n], B[n], trimmed
  after the last used chunk tile; no gfxDataPos (= tileIndex << 8, software render, as loader and
  Set16x16TileInfo compute it). Proxy reads past n return 0 (what the untrimmed table holds).
  plat_chunk_arrays() binds them in LoadStageChunks (every stage load). PC: exact-size allocations
  per array, so ASan catches any read past n. Builder writes the same; checked byte-exact against
  the native table.
- cart_budget.md gains: the global script code size (prefix shared by every stage), and the parts
  identical across each act's time periods.

## S6a2 result
- All 87 identical with trimmed chunk arrays (ASan exact-size). Budget: largest act R61 2,502 KB +
  shared 223 + code 222 = 2,947 of 4,096 KB. Chunks did not trim in regular stages (last used chunk
  near the end) - the saving was gfxDataPos. "Global scripts 0 KB" was a measuring error (Title /
  menus / special stages do not start with them) - now measured over regular stages only.

## S6b: the cart test disc (console, RAM cart)
- Builder header v2: per part (offset, bytes, element size, elements, CRC32 - zlib-compatible).
- md/cart_verify.c: checks an image where it lies, via read16(offset) only (16-bit, Kobo rule):
  header + every part's CRC32. PC tool host/cart_verify_host: OK on a good image, BAD (part 3) on one
  flipped bit, NOT AN IMAGE on a broken header.
- cd32x/sh2_carttest.c: Kobo handshake; Kobo load_pack() (CMD 38 file 16 -> "KOBOGFX.BIN", CMD 46 per
  32 KB chunk -> cart at 2 MB; the Sub-CPU prints its own "CART I16 C.. OK" read-back per chunk);
  cart_verify on 0x22200000; result in a black box at the bottom of the 32X layer (Kobo 3x5 font,
  16-bit writes; rest transparent so Kobo's green text shows).
- sub_main.c: SCD_CART_TEST=1 switches off only the S3b4 plane-demo hand-over block - Kobo's loop
  serves the SH2. Makefile CART_TEST=1 -> build_carttest/SCDCART.cue (APP.BIN + KOBOGFX.BIN =
  IMAGE=, default ../cart_images/R11A_1.bin). Real hardware with the cart only (Fusion has no cart).

## S6b2: the SH2's result box no longer hides the white diagnostics
- Micronut99: Chilly's WHITE start-up lines are the first diagnostics and must stay readable (green
  is written over them). The S6b box (rows 22-27) covered them. Now: a 2-line box in the bottom-right
  corner (x 208-319, y 208-223; the white lines end at about x 200), messages <= 27 characters; the
  rest of the 32X layer see-through. Run in Fusion first for the boot (white + green + box), then on
  hardware with the cart for the result.

## S6b2 on the console -> S6b3
- Real hardware: white + green diagnostics fine, SAFE CART: FOUND, WROTE 5AA5 VIA 900000: 5AA5 - but
  "32X PAYLOAD: 6528 BYTES SH2-VS4" and CKPT DA7A: the disc carried the S4 PLANE SH2 program (waiting
  for a 68000 hand-over that this disc never does), not the cart test (SH2-VS6B).
- Cause (Claude's): files.s did .incbin "build/SCDSH2.BIN" - a fixed path - so the cart test build
  embedded the plane demo's SH2 program from the other build folder. S6b3: .incbin "SCDSH2.BIN" with
  -I$(B) on the files.o rule (it already depended on $(B)/SCDSH2.BIN). cd.s already used -I$(B).
- Check on every run: the "32X PAYLOAD ... SH2-V.." line names the SH2 program actually on the disc.

## S6b result -> S6b3 (Claude's build bug)
- Console and Fusion: same stop. Kobo's line "32X PAYLOAD: 6528 BYTES SH2-VS4" = the S4 PLANE
  program, not the cart test (SH2-VS6B); "CKPT:DA7A" = sh2_planes waiting for the 68000's data copy
  that this disc never does. SAFE CART: FOUND, WROTE 5AA5 VIA 900000:5AA5 - the cart was fine.
- Cause: files.s had .incbin "build/SCDSH2.BIN" (fixed path) - the cart-test disc embedded the
  plane demo's SH2 program left in build/. Fix: .incbin "SCDSH2.BIN" + -I$(B) on the files.o rule
  (the rule already depended on $(B)/SCDSH2.BIN). kobo/cd.s already used -I$(B).
- Check on every new disc: Kobo's "32X PAYLOAD ... SH2-V..." line names the SH2 program inside
  (S6b test: SH2-VS6B).

## S6b3 result (real hardware, RAM cart): THE CART PATH WORKS
- Kobo: "32X PAYLOAD 4392 BYTES SH2-VS6B", "KOBOGFX.BIN 844496 BYTES", "CART I16 C25 @6C8000 OK",
  "32X WIN C25 900000: OK"; SH2 box: "IMG 0CE2D0 BADCHUNK 00", "OK  PARTS 0E OF 0E". PC-built image
  -> CD -> Sub-CPU -> RAM cart at 2 MB -> SH2 reads it back, all 14 part CRCs right.
- Cart layout for more files: Kobo CMD 38 opens IMAGEn.RAW, CMD 46 places it at n x 128 KB (file 16 =
  KOBOGFX.BIN at 2 MB) - choosing file numbers lays out the cart, no Kobo change.

## S7a: per-stage data-pack subsets (the SH2 has no file system)
- engine: FileRead reports (file actually open, position in it) via plat_file_read (PC builds
  only, !__sh__). scdReadName = the open file: set when LoadFile succeeds and in SetFileInfo (from
  the saved FileInfo - its data-pack branch does not update fileName), cleared by CloseFile.
  (fileName is set before a load can fail; GetFilePosition's branch did not subtract the file's
  start in the pack.) Host: SCD32X_FILELOG=file -> "<highest position> <path>" at exit.
- tools/build_stage_packs.py Data.rsdk [out] [frames] [-j]: per stage 1. normal (cart image, camera
  log), 2. SH2 configuration on the full pack with the file log, 3. SH2 configuration on the
  MINI-PACK (each file cut after its last byte read, re-encrypted for its new size - RSDK v3 keys
  depend on the file size): camera logs must be identical. Report stage_packs.md.
- Synthetic stage: 8 files, 2 KB (the tileset GIF -> 791 bytes: header + palette); chunk / collision
  files not in it (cart image). A pack missing Act1.bin: camera logs differ (noticed).

## S7a result -> S7a2
- 87 stages: mini-packs 1.5-3 MB, 80 ASAN ERROR on the mini-pack (identical: R71A-C, Credits),
  Title/Menu/TAttack/DAgdn/Help DIFFER. Size: the SH2 configuration still loaded sound effects (WAV,
  whole files). S7a2: LoadSfx in the SH2 configuration keeps only the name and returns before
  opening the WAV (sound from the cart for the PWM later; no malloc'ed sample buffers either).
- The crash cause is NOT known yet: build_stage_packs.py now reports the ASan error type and the
  first engine line, the 3 biggest files per stage, and takes --only R11A,R71A,... .

## S7a2 result -> S7a3
- Without sound effects: mini-packs 18-107 KB per stage (mostly bytecode). R71A identical; R11A,
  SS1, Secrets 1-3: ASAN stack-buffer-overflow in StrAdd (String.hpp:22); Title DIFFERS.
- Likely: a file OPENED but never read (an existence check) was not logged -> missing from the
  mini-pack -> LoadFile fails there -> another code path appends an unset string. S7a3: LoadFile
  success logs position 0 (listed even if unread); the ASan detail shows three stack frames.

## S7a3 result -> S7a4
- ASan chain: StrAdd <- LoadSfx (Audio.cpp:700) <- LoadStageFiles (Scene.cpp:687): a stage
  sound-effect NAME too long; Title: SEGV in ReleaseStageSfx (garbage stage sfx count).
- Cause (Claude's tracking): LoadStageFiles saves StageConfig (GetFileInfo), loads bytecode, reopens
  (SetFileInfo - data-pack branch leaves fileName = the bytecode's). In the sfx loop GetFileInfo
  saves that STALE name with StageConfig's position; S7a3 took the name from FileInfo -> StageConfig
  reads were logged as the bytecode file -> the mini-pack cut StageConfig before the sfx names.
- S7a4: PC-only table scdInfoNames: GetFileInfo saves scdReadName per FileInfo pointer, SetFileInfo
  restores it (engine field untouched). Synthetic stage gains 2 stage sfx names: reproduced (RS000
  "117 of 12", StageConfig 108 of 117) and fixed (12 of 12, 117 of 117). build_stage_packs reports
  "(file log: N reads under a wrong name)" = positions past a file's size.

## S7a4 result
- All 87 stages identical on their mini-packs; 35-130 KB per stage (mostly bytecode). 3 stages log
  one read under a wrong name (still identical) - look at later.

## S7b: the engine boot disc (make ENGINE=1 -> cd32x/build_engine/SCDENG.cue, real hardware + cart)
- Cart (Kobo CMD 38/46, unchanged): IMAGE.RAW = engine image at 0x000000 (slots 0-3), IMAGE4.RAW =
  R11A console cart image at 0x080000, IMAGE12.RAW = R11A mini-pack at 0x180000.
- cd32x/sh2_engboot.c (SDRAM, "SH2-VS7B"): Kobo handshake, loads the three files, cart_verify on the
  stage image, boot record 0x06001F00 ("SCDB", lengths), interrupts off, CCR = 0x11 (cache purge +
  on), jump to 0x02000000. Stops with a red line on any failure (LOADER TOO BIG, ... NOT ON THE CD,
  CART LOAD BAD, STAGE IMAGE BAD PART nn).
- cd32x/engine/engine_cart.ld: .text/.rodata/.init_array in the cart (0x02000000, max 512 KB);
  .data copied to SDRAM 0x06002000+, .bss after it, heap after that up to 0x0603B400 (master stack
  16 KB above). ASSERTs: RAM + 16 KB heap below the stack; image within 512 KB.
- cd32x/engine/engine_start.cpp: entry (section .text.entry, first in the image): interrupts off,
  copy data, clear bss, static constructors, main(). Status lines "ENGINE: DATA READY", "...: STATIC
  INIT OK".
- cd32x/engine/plat_mars.cpp - the real SH2 platform layer: cart parts from the stage image's SCCB
  header (0x02080000); the mini-pack as the only file (Reader.hpp SH2 branch: FileIO = ScdMemFile,
  scd_fopen opens any *.rsdk for reading, size from the boot record); time = vertical-blank edges
  (MARS VDP FBCTL bit 15, polled - no interrupts); plat_present waits one vertical blank and shows
  "FRAME nnnnnn" every 16 frames; plat_log / plat_fatal in the status box; _sbrk heap up to the
  stack ("HEAP FULL"). Status box = bottom-right 2 lines of the 32X layer, line table + text in the
  first 6 KB of the frame buffer (record area) - menus and the pool untouched.
- Engine: SH2 build starts at list 1 / stage 0 (R11A) (Userdata console block, __sh__ only).
  Sound on the SH2: sfxList 1 entry; LoadSfx returns before opening the WAV; PlaySfx,
  SetSfxAttributes, the mixer, ReleaseGlobalSfx / ReleaseStageSfx return at once (PWM later).
- No input yet (plat_pad 0), no planes yet (the 68000 runs Kobo's listener, not the plane program).

## S7b first console run -> S7b2
- Micronut99's video showed the S4 PLANE DEMO flow ("32X PAYLOAD 6528 BYTES SH2-VS4", "SCD: MDPLANE.BIN
  119840 BYTES"), not the engine loader (SH2-VS7B, IMAGE*.RAW). Which disc / build ran is open.
- Problem found on Claude's side: every disc still printed "BUILD: SONIC CD S3B4 ON KOBO FINAL 6".
  S7b2: the first green line is set by the Makefile: "BUILD: SONIC CD <ROUND> PLANE DEMO / CART TEST /
  ENGINE BOOT" (ROUND ?= S7B2; sub_main.o rebuilds when the Makefile changes).

## S7b2 build -> S7b3 (engine link, Micronut99's PC)
- 1st: plat_mars.cpp: strcasecmp not declared (<strings.h> on the SH2) -> own ".rsdk" suffix test.
  (So the earlier video was the old plane demo: no engine disc had been built.)
- 2nd: the link pulled newlib's lib_a-syscalls.o again ("multiple definition of __gettimeofday") and
  "engine RAM too big". S7b3: -Wl,--allow-multiple-definition (plat_mars.o comes first - ours win);
  the Makefile prints which member pulled syscalls.o (from engine.map), the object data+bss totals,
  --print-memory-usage and the RAM end (_end). Heap reserve 16 -> 8 KB, master stack 16 -> 12 KB
  (0x0603C400-0x0603F400); engine_cart.ld ASSERT and plat_mars SCD_HEAP_LIMIT follow.

## S7b3 result (real hardware): THE ENGINE BOOTS ON THE SH2
- Disc right ("S7B2 ENGINE BOOT", SH2-VS7B). Kobo loaded IMAGE.RAW 241088, IMAGE4.RAW 844496,
  IMAGE12.RAW 110831, all chunks OK. Box: ENGINE / STAGE / PACK BAD 00, then "FRAME 0001.." (digits not
  readable on video) - the engine booted from the cart, loaded R11A, ran frames - then red HEAP FULL.
- Not the cause: LoadMusic (our platform's own, no malloc), mod loader (0 for SONICCD32X), frame
  buffer (one line on the SH2). Unknown yet: one big request or a per-frame leak.
- S7b4: status text double size (2 lines x 13 characters, box y 196-223, still in the record area);
  every 16 frames "F0123 H04E00" = frames, heap in use; on failure "H04E00 A01000" (in use, asked) +
  "HEAP FULL 123" (frame) and the SH2 stops there. Messages shortened to 13 characters.

## S7b4 result -> S7b5: back in line with Kobo (Micronut99: "do not deviate"; points 1-3)
- Console: "H04B08 A03000" + "HEAP FULL 000": 19,208 bytes heap in use, 12,288 asked, at frame 0 (start-up).
  Cause: IniParser keeps ConfigItems (~360 bytes) in a std::vector; ~40-60 defaults; growth 32 -> 64.
- Kobo's documents (KOBO-CD32X_BOOT_PROCESS, Boot Sequence and Command Ledger) checked; deviations:
  1. heap / C library (rule 8) -> NO heap on the SH2: IniParser Set* / Write return at once on the SH2
     (values are assigned in the call arguments), the one-line frame buffer is a fixed array (512),
     _sbrk stops with STATE 5CE4 "NO HEAP A<size>" "AT FRAME <n>" if anything still asks.
  2. status -> STATE codes in COMM12 (16-bit; posted only between loads - CMD 46 replies use COMM12):
     5C01-03 engine / stage / pack loaded, 5C04 stage image OK, 5C05 jump, 5C10 data ready, 5C11
     static init, 5C13 / 5C14 stage loading / loaded, 4B10 first frame (Kobo: running - the Sub stops
     reprinting, HW5); errors 5CE1 file missing, 5CE2 chunk bad, 5CE3 stage image bad, 5CE4 heap,
     5CE5 fatal, 5CE6 main returned, 5CE7 loader too big. The 32X box stays as an extra.
  3. interrupts stay ON (no ldc 0xF0 before the jump or in engine_start); the loader's
     pri_vbi_handler counts frames into 0x06001F10 = the engine's clock (plat_ticks_ms,
     plat_present waits for it to change).
  Remaining known exceptions (agreed): code runs from the cart (4); libgcc division / shift helpers (5).

## S7b5 result -> S7b6
- Console: "S7B5 ENGINE BOOT", Kobo line 10 STATE:5CE4, box "NO HEAP A00091" "AT FRAME 0000": a
  145-byte request at start-up. Ruled out by reading: PrintLog's printf (debug / console off),
  LoadFile's std::string (only with forceUseScripts, off).
- S7b6: engine_cart.ld puts the engine's own code (eng/*.o) first and marks _scd_engine_code_end; the
  no-heap stop scans the stack upward for the first saved return address below that mark (the engine
  function behind the request) and shows "AT 0201xxxx". The build writes build_engine/
  engine_symbols.txt (nm -n -C) for address -> function. Label S7B6.

## S7b6 result -> S7b7
- Console: STATE:5CE4, "NO HEAP A0918" (2,328 bytes; S7b5's "A00091" was the same, cut to 13
  characters), "AT 0201FFDA" -> scd_status: the stack search found a STALE return address (Claude's
  method was wrong - cost a burn).
- S7b7: plat_mars.cpp defines malloc / calloc / realloc / free, _malloc_r / _calloc_r / _realloc_r /
  _free_r, operator new / new[] / delete (the S5a forwarding versions removed): each stops with
  STATE 5CE4, "NO HEAP A<size>", "AT <__builtin_return_address(0)>" = the exact caller (PR at entry).
  Ours win: plat_mars.o comes before the libraries, --allow-multiple-definition. Label S7B7.
  Lookup: awk on build_engine/engine_symbols.txt (function at or below the address).
- Kobo note (Micronut99): IMAGEn.RAW really were pictures - Kobo began as a 32X CD screen saver.
  This port reuses those cart slots for data; that is new, not Kobo's meaning.

## S7b7 result -> S7c: the boot after OpenLara / Kobo
- Console: "NO HEAP A0900" "AT 020208AA" -> _GLOBAL__sub_I__ZN9__gnu_cxx9__freeresEv = libstdc++
  eh_alloc.o's static constructor (emergency exception pool). engine_cart.ld: EXCLUDE_FILE(*eh_alloc.o)
  in .init_array + /DISCARD/ of its constructor sections (no exceptions in the engine).
- Micronut99: IMAGE.RAW = Kobo's real splash / loading picture (CMD 51 -> frame buffer); the game's
  loading is attached at the END of the boot sequence, which starts with the hardware requirements.
  Reference boot code received: Kobo final (KOBO_CD32X_FINAL77hw) and OpenLara CD32X (its SH2 loader
  sh2/cdloader_sh2.c: Kobo start-up, 32X layer blank, splash, one game file via an added Sub-CPU
  mapping line - file 20 OPENLARA.32X at cart 0 - status codes, hand-over).
- S7c loader (cd32x/sh2_engboot.c, "SH2-VS7C"), as OpenLara: start-up identical (COMM0 = 0, COMM10
  0006 / 0007, INTMSK, ints on, 0AAA / 0BBB, take_fb); DISPMODE = 0 (nothing covers the boot text);
  [splash slot, SCD_SPLASH 0]; SONICCD.PAK = file 21 (sub_main.c: one line, cart offset 0), CMD 38 +
  CMD 46 per chunk, STATE 5Dnn per chunk; cart_verify on the stage image; boot record; jump (ints on).
  STATE: 5C01 open, 5Dnn chunk, 5C02 loaded, 5C04 stage image OK, 5C05 jump, engine 5C10 / 5C11 /
  5C13 / 5C14 / 4B10; errors 5CE1 not on CD, 5CE2 chunk bad, 5CE3 pack / stage image bad, 5CE4 heap,
  5CE5 fatal, 5CE6 returned, 5CE7 loader too big.
- tools/make_game_pack.py: SONICCD.PAK = engine (0x000000, max 512 KB) + stage image (0x080000, max
  1 MB) + data pack (0x180000 .. end). Checked on the PC: offsets right, stage image CRCs OK in it.
- Engine: no 32X drawing during normal running (plat_log silent, no frame counter); the corner box
  only for errors (NO HEAP / fatal / main returned). IMAGE*.RAW no longer used by the engine disc.

## S7c result (real hardware): THE ENGINE RUNS ITS MAIN LOOP
- "S7C ENGINE BOOT", SONICCD.PAK 1683695 bytes, CART I0 C51 OK, 32X WIN C51 OK, STATE:4B10.
  Boot text fully visible. Kobo's "(TEXT BLINKS ...)" line is printed once (leftover, Kobo doc §7) -
  it shows nothing about the SH2.
- S7c3: plat_present posts STATE:6Cnn every second for 8 s (nn = engine frames in that second, hex;
  3C = full speed), then 4B10. Label S7C3. Files changed: cd32x/engine/plat_mars.cpp, cd32x/Makefile.
- NEXT: the planes - the engine's frames drive the Genesis planes (S4 streaming fed by the engine).

## S7c3 result (video): the engine runs steadily
- Line 10: 5C02 ~12.5 s, 5C13 14.5-16 s, 5C14 16.5-17 s, 6C1E every second 19-24 s (30 frames per
  second, paced), 4B10 from 24.5 s. Paced 30 = each frame takes 16.7-33.3 ms (waits for the 2nd blank).
- S7c4: during the 8 measuring seconds no pacing - 6Cnn = frames the engine really finishes per
  second (then paced again). Label S7C4.

## S7c3 / S7c4 -> S7d: START THE GAME (Micronut99: priority = the game starting; splash / loading screens
## are cosmetic, later; IMAGE*.RAW are pictures - never game data)
- S7c3 video: 6C1E every second (30 fps paced), engine running steadily. S7c4 (unpaced speed) skipped.
- S7d: no forced start stage (Userdata __sh__ block removed) - the game starts at list 0 / scene 0.
  Sub-CPU mapping (sub_main.c): 21 SONICCD.PAK = the engine only (cart 0); 22 SCDDATA.PAK = data pack
  (cart 0x180000); 0x100 + list * 0x100 + scene -> L<list>S<scene>.IMG (cart 0x080000).
- tools/build_game_data.py Data.rsdk [out] [--scenes 0:*,1:0]: every included scene's image in a
  FIXED layout (each part at the same offset - capacity = largest over all stages; the engine binds
  the addresses once at start-up) + one combined data pack (longest prefix of every file of the
  included scenes' mini-packs; those are checked identical) + game_data.md.
- Loader (SH2-VS7D): loads 21, 22 and 0x100 (L0S000), checks the scene image, boot record [1] engine
  [2] data pack [3] scene bytes [4] address of its resident scd_load_file() [5] scene in the cart.
  The engine calls scd_load_file (SDRAM) at scene changes: Kobo's CMD 46 copies with RV set - the SH2
  cannot read the cart, where the engine runs, meanwhile.
- Engine plat_stage_loading: scene file from activeStageList / stageListPosition; if not in the cart:
  load via the resident routine, purge the cache, cart_verify, offsets = the start-up layout.
  plat_stage_loaded: STATE:5Lnn (5000 = list 0 scene 0). No 4B10 any more (the Sub-CPU would stop
  reprinting line 10). Errors (red box + STATE): 5CE8 NO SCENE <file>, 5CE2 BAD LOAD, 5CE3 BAD IMAGE,
  5CE9 LAYOUT. Label "BUILD: SONIC CD S7D GAME START".

## S7d result (real hardware): THE GAME STARTS
- SONICCD.PAK 233,976 / SCDDATA.PAK 303,130 / L0S000.IMG 916,208 bytes, all chunks OK, STATE:5C03 then
  STATE:5000 = list 0 scene 0 (Title/1) loaded and running. Loading ~1 chunk per second.

## S7e: the game's own first screen (Micronut99: display the very first thing the game shows; no splash /
## loading screen - those are not the game)
- Title/1 (bytecode report): 7 sprite sheets, 39 sprite frames, one Draw3DScene, small tile layers, no
  Copy16x16Tile -> the engine's own software renderer is needed.
- SCD32X_DRAW (RetroEngine.hpp): the SH2 configuration with the renderer on (NODRAW 0, SCD32X_DRAWCART 1);
  SCREEN_YSIZE 198 (512 + 328 x 198 x 2 = 130,400 of 131,072 frame-buffer bytes; the software renderer's
  line is 328). New cart parts GFXDATA (graphicData), TILEGFX (tilesetGFXData), BLEND (tint, blend,
  subtract tables, ushort) - references bound at start-up like the others; GenerateBlendLookupTable skips.
- PC proof: tools/test_draw.py Data.rsdk [out] [--scene 0:0] [--frames 300]: host soniccd_host_ys198
  (normal, 198 lines, writes cart images + CRC every 10th frame) vs soniccd_host_sh2draw_asan (drawing
  configuration from those images): CRCs identical; out/frames/*.png = the pictures the console shows.
  Synthetic stage: identical (13 frames). host/Makefile DRAW=1 / SCREEN198=1.
- SH2 (plat_mars.cpp, SCD32X_DRAWCART): plat_frame_target = 32X frame buffer + 0x200; plat_present ->
  draw_show: 198 rows converted in place RGB565 -> 32X BGR555 (0 -> 0x0400, Kobo), black line after the
  picture, line table (rows 13-210 picture), DISPMODE 32K (Kobo's splash mode), flip_wait. Menus and pool
  in SDRAM (TEXTDATA 0x400, TEXTENTRY 0x40, pool 6 KB - the title needs no menus, 5 KB pool).
- Cart map: scene image 0x080000-0x300000 (2.5 MB), data pack 0x300000 (Sub-CPU file 22 cur_img 24).
  build_game_data.py: fixed layout over the INCLUDED scenes; default --scenes 0:0 (the title).
- Engine disc: -DSCD32X_DRAW, label "S7E GAME START". engine_cart.ld: no heap reserve any more.

## S7e result -> S7e2
- PC proof on the real title (Micronut99): identical, 31 of 31 frames; f000290 = the Sonic CD title screen
  (fading in), f000060 = Palmtree Panic (the PC test's scripted pad).
- Engine link: SDRAM overflowed by 17,608 bytes. Growth over the S7d configuration (measured by nm): vertexBufferT
  +20 KB, pool + menus +11.4 KB, drawList3D +2 KB. S7e2: VERTEXBUFFER_SIZE 0x280 / FACEBUFFER_SIZE 0xA0 in the
  drawing build (= the whole-game maxima 640 / 160), TEXTDATA 0x100: growth 10.8 KB of 14.7 KB room.

## S7e3: the addenda (OpenLara, OpenTyrian, OpenJazz) applied before the burn
- Already in line: OpenLara's boot path (loader in SDRAM, engine as cart payload, STATE codes); CMD 46 loops in
  SDRAM while the engine runs from the cart (Tyrian RV rule); cache purge after loads; 0x0000 -> 0x0400 on every
  pixel incl. game palettes (OpenJazz rule 3); build label on screen, fresh build folder, grep per delivery.
- Added: CMD 52 (Genesis display off) at the first game picture (OpenJazz frame-buffer rule 5: real hardware
  washes the picture out otherwise). PC double-buffer check (OpenJazz rule 2): host DRAW build with
  SCD32X_DOUBLEBUF=1 alternates two buffers, the second junk; test_draw.py uses it - must stay identical
  (a pixel not redrawn would show stale AND be colour-converted twice by draw_show).
- Expect slow drawing (~10 SH2 cycles per frame-buffer access, Tyrian); the in-place conversion pass costs about
  as much again - speed comes after the title is on screen (Tyrian/OpenJazz ideas: 32-bit writes, palette in
  32X format to drop the conversion, slave SH2 not useful for drawing).
- Later, from the addenda: quick start (CMD 47 compare, OpenJazz), bigger chunks per CMD 46 (~30 KB/s now),
  -MMD -MP header dependencies in the engine Makefile (OpenJazz build bug), SH2 exception screen with PC/PR.

## S7e3 result -> S7e4 (drawing test disc)
- Console: boot fine (L0S000.IMG 1,264,720 bytes, all chunks OK, 5C03), Genesis text off, then FLAT green /
  blue alternating, no Sonic. PC frames: 0 black, 20/50 dark blue + Sonic, 100 the logo. The colours fit
  draw_show converting UNCHANGED pixels again and again (0 -> 0x0400 -> 0x0200 ...): the engine's pixels do not
  seem to reach the frame buffer. Not the cause (checked): render type (forced SW, Userdata 684), the pointer
  (plat_frame_target), memset/memcpy (start-up only).
- S7e4 (SCD_DRAW_DIAG 1): no CMD 52 (line 10 readable); marker 0x0841 filled into the back buffer after each
  flip, counted before the next conversion; line 10 per second in turn: 6Cnn frames, Ennn pixels NOT drawn / 64
  (E3DE = none drawn), D/C148 = frame pointer is / is not the 32X frame buffer + line size. Label S7E4.

## S7e4 result -> S7e5
- Console: STATE:5000, the 32X layer one even colour (the marker: the first frame was drawn, converted, flipped),
  then NOTHING more for minutes - no reading ever posted. The engine stops early in its 2nd frame / 1st second.
- Kobo's crt0 exception handler pri_err is just "rte": after an SH2 address error it returns to the faulting
  instruction and faults forever = a silent hang. The addenda: the SH2 faults on misaligned 16/32-bit accesses.
- S7e5: engine_start.cpp writes its own handlers into the SDRAM vector table (VBR 0x06000000: vectors 4 illegal,
  6 slot, 9 CPU address error, 10 DMA address error) - Kobo's crt0 unchanged. On a fault: STATE 5CEA/5CEB/5CEC/
  5CED + box "PC <where>", "PR <caller>" -> cd32x/build_engine/engine_symbols.txt.
- PC: host ALIGN=1 (-fsanitize=alignment); tools/test_draw.py --align lists every misaligned access (file:line).
  Synthetic stage: 0 (no sprites / 3D there) - the real title decides. Label S7E5.

## S7e5 result -> S7e6 (PC sampling, after the OpenLara / OpenJazz builders' advice)
- Console: STATE:5000 forever, NO exception (no 5CEx): stuck in a loop / wait, not a fault. PC alignment check on
  the real title: 0 misaligned accesses; double-buffered drawing identical (31 frames).
- Ruled out against our code: flip not awaited (flip_wait reads FS back), 16-bit writes to 0x20004000 (none in
  the engine; loader as Kobo), COMM6/8/10 use while drawing (none), Sub-CPU commands while drawing (none in the
  test builds), give_fb after the loader (none).
- S7e6: the loader (SDRAM - never cart code in an interrupt: CMD 46 sets RV during scene loads) puts a stub in
  vector 70 (V blank) just before the jump: it saves the interrupted PC and chains to Kobo's pri_irq (jmp with
  r0 restored in the delay slot). pri_vbi_handler posts it every 30 V blanks in COMM12: 8xxx = cart 0x02000000 +
  (xxx & 0x7FFF) * 16, 7xxx = SDRAM 0x06000000 + (xxx & 0xFFF) * 64. Line 10 frozen = V blank stopped.
  tools/where.py <codes> names the functions (engine_symbols.txt of the last build). Label S7E6.

## S7e6 result -> S7e7
- Line 10 after 5000: 97DA-97DC and A9EA-A9EC alternating -> ProcessObjects() +0x174..0x194 and plat_present()
  +0x2c..0x4c. Disassembly (sh-elf-objdump): plat_present = marker count, COLOUR CONVERSION LOOP (the samples),
  black line, line table, flip_wait, marker fill, presented++, rts - the per-second readings block was MISSING:
  SCD_DRAW_DIAG was defined below plat_present (above draw_show) -> "#if SCD_DRAW_DIAG" in plat_present was 0.
  S7e4-S7e6's "no readings" never meant a hang. Micronut99's frame-by-frame look: the 32X layer changes at ~24.5 s
  = a new frame shown. The engine RUNS, slowly (drawing + 3 full frame-buffer passes of test code per frame).
- Pixel format checked: the engine's software renderer packs RGB565 (Palette.hpp RGB888_TO_RGB565), the host PNG
  decodes the same; draw_show's conversion matches.
- S7e7: SCD_DRAW_DIAG and diagUntouched defined at the top (checked in the compiled plat_present); readings per
  second: Fnnn = frames shown so far, Ennn = not drawn / 64, D/C148; the loader's sampler no longer posts
  (scdSampling 0). Label S7E7.

## Open after S3a
- Water stages: the model uses the engine's per-line palette bank; on hardware that is a CRAM
  change from an H-interrupt at the water line (the 1993 game did this).
- V-scroll and 3D layers (special stages) are "unsupported" in the check by design (32X job).
- Streaming bandwidth: synthetic runs average 6–56 name words per frame, worst case ~1,984 after
  a full reload (≈4 KB, fits one vblank DMA).

## Toolchain (checked S1)
sh-elf-gcc 12.1 built `--with-cpu=m2 --with-endian=big`; one multilib (`.`); libgcc, libstdc++
(`sh-elf/lib/!m2/`), libc for -m2 -mb all present. Plan: C++17, `-m2 -mb -O2 -fno-exceptions
-fno-rtti`, link libgcc (variable shifts/divisions are fine – the Kobo "no divisions" rule does not
apply here). Engine compiles without exceptions/RTTI (checked on the host).

## Status after round S2 (kept for history)
Tested in Claude's sandbox:
- `engine/` = decomp + `patches/engine.patch` (12 files, all `PORT32X`). S2 added: script compiler
  off and `<iostream>`/`<thread>` out of Audio.cpp for RETRO_SCD32X.
- Host build, ASan build, `tools/test_rsdk_pack.sh` (pack reader vs engine: 0 different),
  `tools/test_scd_report.sh` (GameConfig stage lists + global sfx + bytecode sizes vs the engine's
  LoadGameConfig/LoadGlobalSfx/LoadBytecode: 0 different, ASan clean).
- `tools/scd_report.py`: palette optimiser (valid on 105 synthetic puzzles, never worse than the old
  greedy), Ogg durations (exact on 3 Vorbis files), partial decryption (key stream cycles every 13,176
  bytes; matches full decryption), sound effects per stage, script sizes, cart budget per act.
- `tools/sh2_size.sh`: tested with host tools only.
NOT tested: scd_report.py on the real pack; the SH2 compile (no sh-elf in the sandbox).

## Decisions
- Original code kept; every change marked PORT32X; mod loader and script compiler off.
- RETRO_GAMEPLATFORMID = RETRO_WIN (scripts see the PC version).
- build.sh is ours (Micronut99 allowed overwriting Kobo's, S1). build_host.sh / run_host.sh in the
  folder are not from these packages – never overwrite them.
- Option 2 (above). Sound effects: 22,050 Hz 8-bit, PWM on the slave SH2, read from the cart.
- Music: all tracks as CD audio (both soundtracks + past themes).

## Open
- Done in S2: scd_report.py numbers and the SH2 compile (see "S2 results").
- Next: S3a results on real data, then S3b (68000 + VDP).
- Later: sprite split MD/32X; special stages on the 32X; animated tiles streaming; videos.
