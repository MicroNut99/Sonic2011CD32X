# PORT32X: round S3b - the plane demo disc (Sega CD + 32X, boots from CD).
#   make -C cd32x DATA=../runs/scene0/MDPLANE.dat
# -> cd32x/build/SCDPLANE.iso (and .bin/.cue for emulators). Kobo's rules for Kobo's files
# (kobo/, unchanged copies); our files: sub_main.c, sh2_splash.c, splash.s, files.s, ../md/*.
# STATUS=1: the Genesis demo shows its status row (frame, overruns, peak words, peak line).
ifdef GENDEV
ROOTDIR = $(GENDEV)
else
ROOTDIR = /opt/toolchains/sega
endif
M68K     = $(ROOTDIR)/m68k-elf/bin/m68k-elf-
SH       = $(ROOTDIR)/sh-elf/bin/sh-elf-
LDSCRIPTSDIR = $(ROOTDIR)/ldscripts
BOOTBLOCK    = $(ROOTDIR)/bootblocks/US_BOOT.BIN
LIBPATH  = -L$(ROOTDIR)/m68k-elf/lib -L$(ROOTDIR)/m68k-elf/lib/gcc/m68k-elf/4.6.2 -L$(ROOTDIR)/m68k-elf/m68k-elf/lib
INCPATH  = -I. -Ikobo -I$(ROOTDIR)/m68k-elf/include -I$(ROOTDIR)/m68k-elf/m68k-elf/include
CCFLAGS  = -m68000 -Wall -Ofast -c -fomit-frame-pointer
HWFLAGS  = -m68000 -Wall -O1 -c -fomit-frame-pointer
ASFLAGS  = -m68000 --register-prefix-optional
LIBS     = $(LIBPATH) -lc -lgcc -lnosys
SH2FLAGS = -c -std=c11 -m2 -mb -O2 -Wall -ffunction-sections -fdata-sections -D__32X__ -DMARS -Ikobo/sh2
STATUS  ?= 1
MDFLAGS  = -m68000 -std=c99 -Wall -O2 -c -fomit-frame-pointer -I../md -DMDP_STATUS=$(STATUS)
DATA    ?= ../runs/scene0/MDPLANE.dat
B        = build$(if $(CART_TEST),_carttest,)$(if $(ENGINE),_engine,)

SUBOBJS = $(B)/crt0.o $(B)/sub_main.o $(B)/hw_md.o $(B)/scd.o $(B)/scd_fs.o $(B)/cd.o $(B)/kos.o $(B)/cdfs.o $(B)/sub_stubs.o $(B)/files.o

# S6b: CART_TEST=1 builds the cart test disc instead (build_carttest/SCDCART.cue): Kobo's Sub-CPU
# program serving the SH2 (no plane demo hand-over), the SH2 cart test, and one stage image as
# KOBOGFX.BIN (IMAGE=, default ../cart_images/R11A_1.bin from tools/build_cart_images.py)
ifdef ENGINE
all: $(B)/SCDENG.cue
else ifdef CART_TEST
all: $(B)/SCDCART.cue
else
all: $(B)/SCDPLANE.cue
endif

$(B):
	mkdir -p $(B) $(B)/disc

# ---- SH2: Kobo's start-up + sh2_splash.c + the splash (splash/SPLASH.SPL; a new image:
#      make SPLASH_IMAGE=path/to/image.jpg - needs Pillow)
$(B)/sh2_crt0.o: kobo/sh2/crt0.s | $(B)
	$(SH)gcc $(SH2FLAGS) $< -o $@
$(B)/sh2_splash.o: sh2_splash.c | $(B)
	$(SH)gcc $(SH2FLAGS) $< -o $@
$(B)/SPLASH.SPL: ../splash/SPLASH.SPL $(SPLASH_IMAGE) | $(B)
	if [ -n "$(SPLASH_IMAGE)" ]; then python3 ../tools/make_splash.py "$(SPLASH_IMAGE)" $@; else cp ../splash/SPLASH.SPL $@; fi
$(B)/splash.o: splash.s $(B)/SPLASH.SPL
	$(SH)gcc $(SH2FLAGS) $< -o $@
$(B)/sh2_idle.o: sh2_idle.c | $(B)
	$(SH)gcc $(SH2FLAGS) $< -o $@
# S4: the SH2 computes the planes live (sh2_planes.c + md/sh2_frames.c, md_record.c, md_planes.c).
# Older SH2 programs kept: sh2_idle.o (handshake only), sh2_splash.o + splash.o (splash screen).
$(B)/sh2_planes.o: sh2_planes.c ../md/sh2_frames.h ../md/mdplane_data.h | $(B)
	$(SH)gcc $(SH2FLAGS) -I../md $< -o $@
$(B)/sh2_frames.o: ../md/sh2_frames.c ../md/sh2_frames.h ../md/md_record.h ../md/md_planes.h | $(B)
	$(SH)gcc $(SH2FLAGS) -I../md $< -o $@
$(B)/sh2_md_record.o: ../md/md_record.c ../md/md_record.h ../md/md_planes.h | $(B)
	$(SH)gcc $(SH2FLAGS) -I../md $< -o $@
$(B)/sh2_md_planes.o: ../md/md_planes.c ../md/md_planes.h | $(B)
	$(SH)gcc $(SH2FLAGS) -I../md $< -o $@
ifdef ENGINE
SH2OBJS = $(B)/sh2_crt0.o $(B)/sh2_engboot.o $(B)/sh2_cart_verify.o
else ifdef CART_TEST
SH2OBJS = $(B)/sh2_crt0.o $(B)/sh2_carttest.o $(B)/sh2_cart_verify.o
else
SH2OBJS = $(B)/sh2_crt0.o $(B)/sh2_planes.o $(B)/sh2_frames.o $(B)/sh2_md_record.o $(B)/sh2_md_planes.o
endif
$(B)/sh2_carttest.o: sh2_carttest.c ../md/cart_verify.h | $(B)
	$(SH)gcc $(SH2FLAGS) -I../md $< -o $@
$(B)/sh2_cart_verify.o: ../md/cart_verify.c ../md/cart_verify.h | $(B)
	$(SH)gcc $(SH2FLAGS) -I../md $< -o $@
$(B)/SCDSH2.BIN: $(SH2OBJS)
	@# linked through gcc for libgcc (md_planes.c has one modulo per written row)
	$(SH)gcc -m2 -mb -nostdlib -T kobo/sh2/sh2_sdram.ld -Wl,-Map=$(B)/sh2.map $(SH2OBJS) -lgcc -o $(B)/sh2.elf
	$(SH)objcopy -O binary $(B)/sh2.elf $@

# ---- Sub-CPU: Kobo's files + sub_main.c
$(B)/crt0.o: kobo/crt0.s | $(B)
	$(M68K)as $(ASFLAGS) $(INCPATH) $< -o $@
$(B)/hw_md.o: kobo/hw_md.s kobo/font.s | $(B)
	$(M68K)as $(ASFLAGS) $(INCPATH) $< -o $@
$(B)/kos.o: kobo/kos.s | $(B)
	$(M68K)as $(ASFLAGS) $(INCPATH) $< -o $@
$(B)/cdfs.o: kobo/cdfs.s | $(B)
	$(M68K)as $(ASFLAGS) $(INCPATH) $< -o $@
$(B)/scd.o: kobo/scd.c | $(B)
	$(M68K)gcc $(CCFLAGS) $(INCPATH) $< -o $@
$(B)/scd_fs.o: kobo/scd_fs.c | $(B)
	$(M68K)gcc $(CCFLAGS) $(INCPATH) $< -o $@
$(B)/sub_stubs.o: kobo/sub_stubs.c | $(B)
	$(M68K)gcc $(CCFLAGS) $(INCPATH) $< -o $@
# S7b2: the first green line names the round and the disc type
ROUND ?= R1T
DISCTYPE = $(if $(ENGINE),GAME START,$(if $(CART_TEST),CART TEST,PLANE DEMO))
$(B)/sub_main.o: sub_main.c Makefile | $(B)
	$(M68K)gcc $(HWFLAGS) $(INCPATH) $(if $(CART_TEST)$(ENGINE),-DSCD_CART_TEST=1,) '-DSCD_BUILD_TEXT="BUILD: SONIC CD $(ROUND) $(DISCTYPE)"' $< -o $@
$(B)/crt.o: kobo/crt.s | $(B)
	$(M68K)as $(ASFLAGS) $(INCPATH) $< -o $@
$(B)/crt.bin: $(B)/crt.o $(B)/sub_stubs.o
	$(M68K)ld -Ttext 0x6000 $^ -o $(B)/crt.elf
	$(M68K)objcopy -O binary $(B)/crt.elf $@
$(B)/cd.o: kobo/cd.s $(B)/crt.bin
	$(M68K)as $(ASFLAGS) $(INCPATH) -I$(B) $< -o $@
$(B)/files.o: files.s $(B)/SCDSH2.BIN
	$(M68K)as $(ASFLAGS) $(INCPATH) -I$(B) $< -o $@   # S6b3: SCDSH2.BIN of THIS build folder
$(B)/disc/APP.BIN: $(SUBOBJS)
	$(M68K)gcc -T $(LDSCRIPTSDIR)/cd.ld -Wl,-Map=$(B)/sub.map -nostdlib $(SUBOBJS) $(LIBS) -o $(B)/sub.elf
	$(M68K)objcopy -O binary $(B)/sub.elf $(B)/sub.bin
	mkdir -p $(B)/disc
	dd if=$(B)/sub.bin of=$@ bs=2048 conv=sync

# ---- Genesis demo program: md/mdplane_* + md/md_planes.c, data appended
$(B)/mdplane_crt.o: ../md/mdplane_crt.s | $(B)
	$(M68K)as $(ASFLAGS) $< -o $@
$(B)/mdplane_main.o: ../md/mdplane_main.c ../md/mdplane_data.h | $(B)
	$(M68K)gcc $(MDFLAGS) $< -o $@
$(B)/mdplane.bin: $(B)/mdplane_crt.o $(B)/mdplane_main.o ../md/mdplane.ld
	@# link through gcc (as Kobo's Sub-CPU link): it knows its own libgcc (68000 version with
	@# -m68000) - the 4.6.2 path in LIBPATH is from an older toolchain (S3b2 fix)
	$(M68K)gcc -m68000 -nostdlib -T ../md/mdplane.ld -Wl,-Map=$(B)/mdplane.map $(B)/mdplane_crt.o $(B)/mdplane_main.o -lgcc -o $(B)/mdplane.elf
	$(M68K)objcopy -O binary $(B)/mdplane.elf $@
$(B)/disc/MDPLANE.BIN: $(B)/mdplane.bin $(DATA)
	mkdir -p $(B)/disc
	python3 ../tools/make_mdplane.py $< $$($(M68K)nm $(B)/mdplane.elf | sed -n 's/^\([0-9a-fA-F]*\) . __blob_start$$/\1/p') $(DATA) $@

# ---- the disc
$(B)/SCDPLANE.iso: $(B)/disc/APP.BIN $(B)/disc/MDPLANE.BIN
	mkdir -p $(B)/disc
	genisoimage -sysid "SEGA SEGACD" -volid "SCDPLANE" -generic-boot $(BOOTBLOCK) -full-iso9660-filenames -o $@ $(B)/disc
$(B)/SCDPLANE.cue: $(B)/SCDPLANE.iso
	python3 kobo/iso2bin.py $< $(B)/SCDPLANE.bin
	printf 'FILE "SCDPLANE.bin" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n' > $@
	@echo "disc: $(B)/SCDPLANE.iso and $(B)/SCDPLANE.bin + .cue"

clean:
	rm -rf $(B)
.PHONY: all clean

# ---- S6b: the cart test disc
IMAGE ?= ../cart_images/R11A_1.bin
$(B)/disc/KOBOGFX.BIN: $(IMAGE) | $(B)
	mkdir -p $(B)/disc
	cp $(IMAGE) $@
$(B)/SCDCART.iso: $(B)/disc/APP.BIN $(B)/disc/KOBOGFX.BIN
	mkdir -p $(B)/disc
	genisoimage -sysid "SEGA SEGACD" -volid "SCDCART" -generic-boot $(BOOTBLOCK) -full-iso9660-filenames -o $@ $(B)/disc
$(B)/SCDCART.cue: $(B)/SCDCART.iso
	python3 kobo/iso2bin.py $< $(B)/SCDCART.bin
	printf 'FILE "SCDCART.bin" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n' > $@
	@echo "cart test disc: $(B)/SCDCART.bin + .cue (image $(IMAGE))"

# ---- S7c: the engine boot disc (make ENGINE=1) - after the OpenLara CD32X loader: Kobo's start-up,
# the 32X layer blank, then ONE game file SONICCD.PAK (file 21 -> cart 0) loaded by Kobo's CMD 38 / 46,
# then the jump into the engine. IMAGE.RAW stays Kobo's splash picture (splash off for now).
#   SONICCD.PAK = engine image (cart 0x000000) + stage image (0x080000) + data pack (0x180000)
STAGE_IMAGE ?= ../cart_images/R11A_1.bin
STAGE_PACK  ?= ../stage_packs/R11A_1.rsdk
$(B)/sh2_engboot.o: sh2_engboot.c ../md/cart_verify.h | $(B)
	$(SH)gcc $(SH2FLAGS) -I../md $< -o $@
# REV R1: PLANES=1 (default) - the Genesis draws the level (md/sgb.h): the engine without its renderer
# (SCD32X_SH2, 224 lines), the act's G<list>S<pos>.MDG files from GAME_DATA on the disc.
# START=1 0: start straight in list 1 scene 0 (Palmtree Panic 1 Present); DEMO_PAD=1: the PC check's
# pad (spin dash, run right) instead of the controller - the camera moves without anyone playing.
# PLANES=0: the S7e drawing build (the 32X draws everything).
PLANES   ?= 1
START    ?= 1 0
DEMO_PAD ?= 1
ifeq ($(PLANES),1)
ENGCFG   = -DSCD32X_SH2 -DSCD_PLANES=1 -DSCD_DEMO_PAD=$(DEMO_PAD) $(if $(START),-DSCD32X_START_LIST=$(word 1,$(START)) -DSCD32X_START_STAGE=$(word 2,$(START)),)
else
ENGCFG   = -DSCD32X_DRAW
endif
ENGFLAGS = -std=c++17 -DSONICCD32X $(ENGCFG) -m2 -mb -O2 -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables \
           -fno-unwind-tables -ffunction-sections -fdata-sections -w -Ikobo/sh2
ENGSRC   = $(wildcard ../engine/*.cpp)
ENGOBJS  = $(patsubst ../engine/%.cpp,$(B)/eng/%.o,$(ENGSRC)) $(B)/eng/plat_mars.o $(B)/eng/engine_start.o $(B)/eng/cart_verify.o \
           $(if $(filter 1,$(PLANES)),$(B)/eng/md_planes.o $(B)/eng/sgb.o,)
$(B)/eng/md_planes.o: ../md/md_planes.c ../md/md_planes.h | $(B)/eng
	$(SH)gcc -std=c99 -m2 -mb -O2 -ffunction-sections -fdata-sections -c $< -o $@
$(B)/eng/sgb.o: ../md/sgb.c ../md/sgb.h ../md/md_planes.h | $(B)/eng
	$(SH)gcc -std=c99 -m2 -mb -O2 -ffunction-sections -fdata-sections -c $< -o $@
$(B)/eng/cart_verify.o: ../md/cart_verify.c ../md/cart_verify.h | $(B)/eng
	$(SH)gcc -std=c99 -m2 -mb -O2 -ffunction-sections -fdata-sections -c $< -o $@
$(B)/eng:
	mkdir -p $@
$(B)/eng/%.o: ../engine/%.cpp | $(B)/eng
	$(SH)g++ $(ENGFLAGS) -c $< -o $@
$(B)/eng/plat_mars.o: engine/plat_mars.cpp ../md/sgb.h ../md/md_planes.h | $(B)/eng
	$(SH)g++ $(ENGFLAGS) -c $< -o $@
$(B)/eng/engine_start.o: engine/engine_start.cpp | $(B)/eng
	$(SH)g++ $(ENGFLAGS) -c $< -o $@
# our system calls / allocation entry points (plat_mars.cpp) come before libc and win
$(B)/engine.elf: $(ENGOBJS) engine/engine_cart.ld
	@echo "engine objects: data + zeroed arrays (before --gc-sections):"; $(SH)size -t $(ENGOBJS) | tail -1
	$(SH)g++ -m2 -mb -nostartfiles -T engine/engine_cart.ld -Wl,--gc-sections -Wl,--allow-multiple-definition \
	    -Wl,--print-memory-usage -Wl,-Map=$(B)/engine.map $(ENGOBJS) -lstdc++ -lc -lgcc -o $@
	$(SH)size -A $@ | grep -E "^\.(text|rodata|init_array|data|bss) "
	$(SH)nm -n -C $@ > $(B)/engine_symbols.txt   # address -> function (sorted)
	@echo "RAM end (engine data + zeroed arrays): $$($(SH)nm $@ | grep ' _end$$' | cut -d' ' -f1)  (must stay below 0603A400)"
$(B)/engine.bin: $(B)/engine.elf
	$(SH)objcopy -O binary $< $@
# S7d: SONICCD.PAK = the engine only (cart 0); the game's data from tools/build_game_data.py:
# SCDDATA.PAK (cart 0x180000) and every scene's L<list>S<scene>.IMG (loaded into cart 0x080000 at
# each scene change)
GAME_DATA ?= ../game_data
$(B)/disc/SONICCD.PAK: $(B)/engine.bin | $(B)
	mkdir -p $(B)/disc
	cp $< $@
	@echo "SONICCD.PAK (engine): $$(stat -c %s $@) bytes (max 524288)"
$(B)/disc/SCDDATA.PAK: $(GAME_DATA)/SCDDATA.PAK | $(B)
	mkdir -p $(B)/disc
	cp $(GAME_DATA)/SCDDATA.PAK $(GAME_DATA)/L*.IMG $(B)/disc/
	@echo "game data: $$(ls $(GAME_DATA)/L*.IMG | wc -l) scene images + SCDDATA.PAK from $(GAME_DATA)"
	@if [ "$(PLANES)" = 1 ]; then cp $(GAME_DATA)/G*.MDG $(B)/disc/ 2>/dev/null; \
	  echo "REV R1 planes: $$(ls $(B)/disc/G*.MDG 2>/dev/null | wc -l) act files G*.MDG from $(GAME_DATA) (tools/build_mdg.sh)"; fi
$(B)/SCDENG.iso: $(B)/disc/APP.BIN $(B)/disc/SONICCD.PAK $(B)/disc/SCDDATA.PAK
	genisoimage -sysid "SEGA SEGACD" -volid "SCDENG" -generic-boot $(BOOTBLOCK) -full-iso9660-filenames -o $@ $(B)/disc
$(B)/SCDENG.cue: $(B)/SCDENG.iso
	python3 kobo/iso2bin.py $< $(B)/SCDENG.bin
	printf 'FILE "SCDENG.bin" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n' > $@
	@echo "game start disc: $(B)/SCDENG.bin + .cue (game data $(GAME_DATA))"
