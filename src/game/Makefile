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
	src/dc/rom/pw64_swap.c \
	src/dc/ultra/mio0.c \
	src/dc/ultra/fatal.c \
	src/dc/ultra/kos_bridge.c \
	src/dc/save/vmu_eeprom.c \
	src/dc/autoplay.c \
	src/dc/gfx/gfx_texconv.c \
	src/dc/gfx/gfx_pvr.c \
	src/dc/gfx/gfx_f3d.c \
	src/dc/ultra/rdram.c

SHZ_C := $(SHZ)/source/shz_matrix.c $(SHZ)/source/shz_quat.c $(SHZ)/source/shz_version.c \
	 $(SHZ)/source/shz_xmtrx.c $(SHZ)/source/sh4/shz_complex_sh4.c
SHZ_S := $(SHZ)/source/sh4/shz_xmtrx_sh4.s $(SHZ)/source/sh4/shz_mem_sh4.s

# Decomp TUs (fetched + patched copy; scripts/fetch_decomp.sh). libultra os/io/libc/debug/vimodes
# and rsp are replaced by src/dc/ultra; gu does not exist in this decomp (game uses no gu* but sqrtf).
DECOMP      := external/pw64-decomp
DECOMP_DIRS := src/kernel src/app src/libultra/audio src/libultra/sp
DECOMP_SRC  := $(sort $(foreach d,$(DECOMP_DIRS),$(wildcard $(DECOMP)/$(d)/*.c)))
# Port TUs that include decomp headers (ultra64.h): compiled with the decomp flags.
DECOMPLIKE_SRC := src/dc/rom/pw64_audio_load.c src/dc/rom/pw64_rom_structs.c src/dc/rom/pw64_arena.c \
	src/dc/ultra/ultra_data.c src/dc/rom/layout_checks.c src/dc/ultra/os_shim.c \
	src/dc/ultra/game_probe.c src/dc/settings.c

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
# Port feature switches (-DWINGS64_*, e.g. WINGS64_SETTINGS_MENU=0) reach the decomp TUs too; DB_* knobs do not.
WINGS64_KNOBS := $(filter -DWINGS64_%,$(EXTRA_CFLAGS))
# Decomp TUs: plain sh-elf-gcc (no KOS include paths, so decomp libc headers win), same ABI flags
# as KOS_CFLAGS. IDO semantics: unsigned char, wrapping ints, no aliasing assumptions, no fmac.
DECOMP_CC := $(KOS_CC_BASE)/bin/$(KOS_CC_PREFIX)-gcc
DECOMP_ABI := -ml -m4-single-only -matomic-model=soft-imask -ftls-model=local-exec
DECOMP_DEFS := -D_LANGUAGE_C -DVERSION_US -DBUILD_VERSION=VERSION_D -D_FINALROM -DNDEBUG -DTARGET_N64 \
	-DNON_MATCHING -DAVOID_UB -DRECOMP_BUILD -DWINGS64_DC
DECOMP_INC := -nostdinc -isystem $(shell $(DECOMP_CC) -print-file-name=include) \
	-Isrc/dc/decomp -I$(DECOMP) -I$(DECOMP)/src -I$(DECOMP)/include -I$(DECOMP)/include/kernel \
	-I$(DECOMP)/include/libultra -I$(DECOMP)/include/libultra/PR -I$(DECOMP)/include/libultra/compiler/modern_gcc
DECOMP_CFLAGS := $(DECOMP_ABI) $(COMMON) -Os -std=gnu11 -funsigned-char -fwrapv -fno-strict-aliasing \
	-ffp-contract=off -ftrivial-auto-var-init=zero -fno-aggressive-loop-optimizations -include src/dc/decomp/wings64_prelude.h \
	$(DECOMP_DEFS) $(DECOMP_INC) -Wno-multichar -Wno-implicit-int -Wno-implicit-function-declaration \
	-Wno-return-type -Wno-int-conversion -Wno-incompatible-pointer-types $(REL_DEFS) $(WINGS64_KNOBS)
# ^ -fno-aggressive-loop-optimizations: the decomp indexes past declared array bounds (IDO-era structs,
#   patches/020); GCC must not shorten loops from those bounds.
# ^ ILP32 like the VR4300: decomp int<->pointer casts are lossless on SH-4 (GCC 14 makes them errors).
DECOMP_OBJ := $(patsubst $(DECOMP)/%.c,$(OBJDIR)/decomp/%.o,$(DECOMP_SRC)) \
	$(patsubst %.c,$(OBJDIR)/dl/%.o,$(DECOMPLIKE_SRC))

LDFLAGS  := -Wl,--gc-sections -Wl,-Map=$(TARGET:.elf=.map)
LIBS     := -lkosfat -lm

PORT_OBJ := $(patsubst %.c,$(OBJDIR)/%.o,$(PORT_SRC))
SHZ_OBJ  := $(patsubst $(SHZ)/%.c,$(OBJDIR)/sh4zam/%.o,$(SHZ_C)) $(patsubst $(SHZ)/%.s,$(OBJDIR)/sh4zam/%.o,$(SHZ_S))
SHZ_LIB  := $(OBJDIR)/sh4zam/libsh4zam.a

all: $(TARGET)

# .cflags stamp: any flag change rebuilds everything (header deps come from -MMD). Adding or removing a
# shadow header changes which file an #include resolves to, which -MMD cannot see: the list is stamped.
SHADOW_HDRS := $(sort $(shell find src/dc/decomp -name '*.h'))
CFLAGS_STAMP := $(OBJDIR)/.cflags
$(CFLAGS_STAMP): FORCE
	@mkdir -p $(@D)
	@echo '$(PORT_CFLAGS) | $(SHZ_CFLAGS) | $(DECOMP_CFLAGS) | $(OPT_QUARANTINE_SRC) | $(SHADOW_HDRS)' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

$(OBJDIR)/%.o: %.c $(CFLAGS_STAMP)
	@mkdir -p $(@D)
	@echo "  CC      $<"
	@$(CC) $(PORT_CFLAGS) $(if $(filter $<,$(OPT_QUARANTINE_SRC)),-O0) -c $< -o $@

$(OBJDIR)/decomp/%.o: $(DECOMP)/%.c $(CFLAGS_STAMP) $(DECOMP)/.wings64-stamp
	@mkdir -p $(@D)
	@echo "  CC      $*.c"
	@$(DECOMP_CC) $(DECOMP_CFLAGS) $(if $(filter $<,$(OPT_QUARANTINE_SRC)),-O0) -c $< -o $@

$(OBJDIR)/dl/%.o: %.c $(CFLAGS_STAMP) $(DECOMP)/.wings64-stamp
	@mkdir -p $(@D)
	@echo "  CC      $<"
	@$(DECOMP_CC) $(DECOMP_CFLAGS) -Wall -c $< -o $@

decomp-objs: $(DECOMP_OBJ)

# Purity (PLAN D7, H6): decomp float code must match the VR4300 (no FMA) and stay deterministic for
# fdr.c replays (no FSRRA/FSCA approximations). Checked on GCC's assembly, not on objdump output,
# which decodes literal-pool data as instructions.
DECOMP_ASM := $(patsubst $(DECOMP)/%.c,$(OBJDIR)/asm/%.s,$(DECOMP_SRC)) \
	$(patsubst %.c,$(OBJDIR)/asm/dl/%.s,$(DECOMPLIKE_SRC))
$(OBJDIR)/asm/%.s: $(DECOMP)/%.c $(CFLAGS_STAMP) $(DECOMP)/.wings64-stamp
	@mkdir -p $(@D)
	@$(DECOMP_CC) $(DECOMP_CFLAGS) $(if $(filter $<,$(OPT_QUARANTINE_SRC)),-O0) -S $< -o $@
$(OBJDIR)/asm/dl/%.s: %.c $(CFLAGS_STAMP)
	@mkdir -p $(@D)
	@$(DECOMP_CC) $(DECOMP_CFLAGS) -S $< -o $@
purity: $(DECOMP_ASM)
	@bad=$$(grep -lE '^[[:space:]]+(fsrra|fsca|fmac)[[:space:]]' $(DECOMP_ASM) || true); \
	if [ -n "$$bad" ]; then echo "purity: FSRRA/FSCA/FMAC in decomp code:"; echo "$$bad"; exit 1; fi; \
	echo "purity: OK ($(words $(DECOMP_ASM)) decomp TUs, no fsrra/fsca/fmac)"

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

$(TARGET): $(PORT_OBJ) $(DECOMP_OBJ) $(SHZ_LIB)
	@echo "  LD      $@"
	@$(CC) -o $@ $(PORT_OBJ) $(DECOMP_OBJ) $(LDFLAGS) -L$(OBJDIR)/sh4zam -lsh4zam $(LIBS)
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

.PHONY: all clean decomp-objs purity FORCE
-include $(PORT_OBJ:.o=.d) $(SHZ_OBJ:.o=.d) $(DECOMP_OBJ:.o=.d)
