# PORT32X: PC host build of the Sonic CD engine (no SDL, no OpenGL, no Ogg).
# make            -> soniccd_host
# make ASAN=1     -> AddressSanitizer build (our gdb replacement)
ENGINE  := ../engine
SRCS    := $(wildcard $(ENGINE)/*.cpp) plat_host.cpp md_check.cpp
OBJDIR  := obj$(if $(SH2CFG),_sh2cfg,)$(if $(DRAW),_sh2draw,)$(if $(SCREEN198),_ys198,)$(if $(ASAN),_asan,)$(if $(ALIGN),_align,)
OBJS    := $(patsubst %.cpp,$(OBJDIR)/%.o,$(notdir $(SRCS))) $(OBJDIR)/md_planes.o $(OBJDIR)/md_record.o $(OBJDIR)/sgb.o
CXXFLAGS := -std=c++17 -DSONICCD32X -O1 -g -Wall -Wno-unused -Wno-sign-compare -Wno-format-overflow -Wno-class-memaccess \
            -Wno-restrict -Wno-stringop-truncation -Wno-format-truncation -Wno-misleading-indentation -Wno-parentheses \
            -Wno-maybe-uninitialized -Wno-char-subscripts -Wno-array-bounds -Wno-stringop-overflow -Wno-unknown-pragmas -Wno-format-zero-length -Wno-unused-result
# SH2CFG=1: the SH2 configuration on the PC (S5b): no-draw + small arrays, for the camera-log check
ifdef SH2CFG
CXXFLAGS += -DSCD32X_SH2
endif
# S7e: DRAW=1 = the SH2 configuration WITH the renderer (pixels from the cart image, 198 lines);
#      SCREEN198=1 = the normal engine with 198 lines (the reference for DRAW=1)
ifdef DRAW
CXXFLAGS += -DSCD32X_DRAW
endif
ifdef SCREEN198
CXXFLAGS += -DSCD32X_SCREEN198
endif
# S7e5: ALIGN=1 - report every misaligned 16 / 32-bit access (the SH2 faults on them; the PC does not)
ifdef ALIGN
CXXFLAGS += -fsanitize=alignment -fno-omit-frame-pointer
LDFLAGS  += -fsanitize=alignment
endif
ifdef ASAN
CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer -DSCD32X_NO_HEAPCOUNT
LDFLAGS  += -fsanitize=address
WRAP     :=
else
WRAP     := -Wl,--wrap=malloc,--wrap=free,--wrap=realloc,--wrap=calloc
endif
TARGET  := soniccd_host$(if $(SH2CFG),_sh2cfg,)$(if $(DRAW),_sh2draw,)$(if $(SCREEN198),_ys198,)$(if $(ASAN),_asan,)$(if $(ALIGN),_align,)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $(OBJS) $(WRAP) -o $@

$(OBJDIR)/%.o: $(ENGINE)/%.cpp $(wildcard $(ENGINE)/*.hpp) | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/plat_host.o: plat_host.cpp $(ENGINE)/plat.hpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/md_check.o: md_check.cpp ../md/md_planes.h ../md/sgb.h $(wildcard $(ENGINE)/*.hpp) | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@
# the plane streaming code is C (the 68000 compiles the same file)
# R1W: MD_SGB_RUNS - the record writer's run form, as the SH2 build uses it (SCD32X_SGBTEST checks it)
$(OBJDIR)/md_planes.o: ../md/md_planes.c ../md/md_planes.h ../md/sgb.h | $(OBJDIR)
	$(CC) -std=c99 -O1 -g -Wall -DMD_SGB_RUNS $(if $(ASAN),-fsanitize=address,) -c $< -o $@
$(OBJDIR)/md_record.o: ../md/md_record.c ../md/md_record.h ../md/md_planes.h | $(OBJDIR)
	$(CC) -std=c99 -O1 -g -Wall $(if $(ASAN),-fsanitize=address,) -c $< -o $@
# REV R1: the SH2 -> 68000 plane record (C, the SH2 compiles the same file)
$(OBJDIR)/sgb.o: ../md/sgb.c ../md/sgb.h ../md/md_planes.h | $(OBJDIR)
	$(CC) -std=c99 -O1 -g -Wall $(if $(ASAN),-fsanitize=address,) -c $< -o $@
$(OBJDIR):
	mkdir -p $@
clean:
	rm -rf obj obj_asan obj_sh2cfg obj_sh2cfg_asan obj_sh2draw obj_sh2draw_asan obj_ys198 obj_sh2draw_align soniccd_host_sh2draw_align soniccd_host soniccd_host_asan soniccd_host_sh2cfg soniccd_host_sh2cfg_asan soniccd_host_sh2draw soniccd_host_sh2draw_asan soniccd_host_ys198 test_reader test_config test_md_planes test_mdplane
.PHONY: clean

# The pack reader check: tools/test_rsdk_pack.sh
TEST_OBJS := $(filter-out $(OBJDIR)/main.o,$(OBJS)) $(OBJDIR)/test_reader.o
test_reader: $(TEST_OBJS)
	$(CXX) $(LDFLAGS) $(TEST_OBJS) $(WRAP) -o $@
$(OBJDIR)/test_reader.o: test_reader.cpp $(ENGINE)/plat.hpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# The config / bytecode parser check: tools/test_scd_report.sh
CFG_OBJS := $(filter-out $(OBJDIR)/main.o $(OBJDIR)/plat_host.o $(OBJDIR)/md_check.o $(OBJDIR)/md_planes.o $(OBJDIR)/md_record.o $(OBJDIR)/sgb.o,$(OBJS)) $(OBJDIR)/test_config.o
test_config: $(CFG_OBJS)
	$(CXX) $(LDFLAGS) $(CFG_OBJS) -o $@
$(OBJDIR)/test_config.o: test_config.cpp $(ENGINE)/plat.hpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@
