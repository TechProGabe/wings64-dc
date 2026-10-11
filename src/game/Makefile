# wings64-dc — target build. Run from the repo root inside the container (./run.sh build):
#   make -f src/game/Makefile [RELEASE=1] [EXTRA_CFLAGS=-DDB_X=1]
# Flags follow docs/toolchain.md; KOS_CFLAGS (via kos-cc) supplies -m4-single-only from KOS_SH4_PRECISION.

ifeq ($(origin KOS_BASE),undefined)
$(error KOS_BASE not set: build through ./run.sh build)
endif
ifneq ($(KOS_SH4_PRECISION),-m4-single-only)
$(error KOS_SH4_PRECISION is '$(KOS_SH4_PRECISION)', the port requires -m4-single-only)
endif

OUT     := build
OBJDIR  := $(OUT)/dc
SHZ     := external/sh4zam
ifeq ($(RELEASE),1)
  ifneq ($(strip $(EXTRA_CFLAGS)),)
    $(error RELEASE=1 refuses EXTRA_CFLAGS (debug knobs never ship))
  endif
  TARGET  := $(OUT)/wings64-release.elf
  OBJDIR  := $(OUT)/dc-release
  REL_DEFS := -DNDEBUG -DWINGS64_RELEASE=1 -DDB_ENABLE=0
else
  TARGET  := $(OUT)/wings64.elf
endif

CC := kos-cc
AR := $(KOS_CC_BASE)/bin/$(KOS_CC_PREFIX)-ar

# ---- sources --------------------------------------------------------------
PORT_SRC := \
	src/dc/main.c \
	src/dc/db.c \
	src/dc/screen.c \
	src/dc/assetroot.c \
	src/dc/rom/romfs.c \
	src/dc/ultra/mio0.c

SHZ_C := $(SHZ)/source/shz_matrix.c $(SHZ)/source/shz_quat.c $(SHZ)/source/shz_version.c \
	 $(SHZ)/source/shz_xmtrx.c $(SHZ)/source/sh4/shz_complex_sh4.c
SHZ_S := $(SHZ)/source/sh4/shz_xmtrx_sh4.s $(SHZ)/source/sh4/shz_mem_sh4.s

# Per-TU -O0 quarantine (GCC miscompile evidence goes in kb/traps.md). Starts empty.
OPT_QUARANTINE_SRC :=

# ---- flags ----------------------------------------------------------------
SHZ_DEFS := -DSHZ_BACKEND=SHZ_SH4 -DSHZ_TLS_MODEL=SHZ_TLS_IMPLICIT -I$(SHZ)/include
COMMON   := -fno-PIC -fno-PIE -fomit-frame-pointer -mfsrra -mfsca -ffunction-sections -fdata-sections -MMD -MP
PORT_CFLAGS := $(COMMON) -O2 -std=gnu2x -Wall -Wextra -Wno-unused-parameter \
	-Isrc/dc -I$(OBJDIR) $(SHZ_DEFS) -DWINGS64_DC $(REL_DEFS) $(EXTRA_CFLAGS)
# sh4zam library: docs/sh4zam-mandate.md N.1.10 minus LTO (no LTO before M6).
SHZ_CFLAGS := $(COMMON) -O3 -std=gnu2x -fbuiltin -ffast-math -ffp-contract=fast -fmerge-all-constants \
	-funroll-loops -fipa-pta -ftree-vectorize $(SHZ_DEFS) $(REL_DEFS)
LDFLAGS  := -Wl,--gc-sections -Wl,-Map=$(TARGET:.elf=.map)
LIBS     := -lkosfat -lm

PORT_OBJ := $(patsubst %.c,$(OBJDIR)/%.o,$(PORT_SRC))
SHZ_OBJ  := $(patsubst $(SHZ)/%.c,$(OBJDIR)/sh4zam/%.o,$(SHZ_C)) $(patsubst $(SHZ)/%.s,$(OBJDIR)/sh4zam/%.o,$(SHZ_S))
SHZ_LIB  := $(OBJDIR)/sh4zam/libsh4zam.a

all: $(TARGET)

# .cflags stamp: any flag change rebuilds everything (header deps come from -MMD).
CFLAGS_STAMP := $(OBJDIR)/.cflags
$(CFLAGS_STAMP): FORCE
	@mkdir -p $(@D)
	@echo '$(PORT_CFLAGS) | $(SHZ_CFLAGS) | $(OPT_QUARANTINE_SRC)' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

$(OBJDIR)/%.o: %.c $(CFLAGS_STAMP)
	@mkdir -p $(@D)
	@echo "  CC      $<"
	@$(CC) $(PORT_CFLAGS) $(if $(filter $<,$(OPT_QUARANTINE_SRC)),-O0) -c $< -o $@

$(OBJDIR)/sh4zam/%.o: $(SHZ)/%.c $(CFLAGS_STAMP)
	@mkdir -p $(@D)
	@echo "  CC      $<"
	@$(CC) $(SHZ_CFLAGS) -c $< -o $@

$(OBJDIR)/sh4zam/%.o: $(SHZ)/%.s $(CFLAGS_STAMP)
	@mkdir -p $(@D)
	@echo "  AS      $<"
	@$(CC) $(SHZ_CFLAGS) -x assembler-with-cpp -c $< -o $@

$(SHZ_LIB): $(SHZ_OBJ)
	@rm -f $@
	@$(AR) rcs $@ $^

$(TARGET): $(PORT_OBJ) $(SHZ_LIB)
	@echo "  LD      $@"
	@$(CC) -o $@ $(PORT_OBJ) $(LDFLAGS) -L$(OBJDIR)/sh4zam -lsh4zam $(LIBS)
	@$(KOS_CC_BASE)/bin/$(KOS_CC_PREFIX)-size $@

# Build stamp (scripts/build_target.sh passes BUILD_ID); rewritten only when it changes.
BUILD_ID ?= unknown
$(OBJDIR)/stamp.h: FORCE
	@mkdir -p $(@D)
	@echo '#define WINGS64_BUILD_ID "$(BUILD_ID)"' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@
$(OBJDIR)/src/dc/db.o: $(OBJDIR)/stamp.h

clean:
	rm -rf $(OUT)/dc $(OUT)/dc-release $(OUT)/*.elf $(OUT)/*.map

.PHONY: all clean FORCE
-include $(PORT_OBJ:.o=.d) $(SHZ_OBJ:.o=.d)
