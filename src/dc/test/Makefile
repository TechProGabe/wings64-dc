# Host test build (PLAN M1): decomp + portable port code + sh4zam SW backend, for 32-bit little-endian ARM
# (ILP32 like the VR4300 and SH-4), run under qemu-arm. From the repo root inside the container:
#   make -f src/dc/test/Makefile        -> build/host/hosttest
# Caveat: ARM EABI aligns 8-byte types to 8, SH-4 to 4; layout-sensitive checks also compile for SH-4.

HOST_CC := arm-linux-gnueabihf-gcc
OUT     := build/host
DECOMP  := external/pw64-decomp
SHZ     := external/sh4zam

DECOMP_DIRS := src/kernel src/app src/libultra/audio src/libultra/sp
DECOMP_SRC  := $(sort $(foreach d,$(DECOMP_DIRS),$(wildcard $(DECOMP)/$(d)/*.c)))
DECOMPLIKE_SRC := src/dc/rom/pw64_audio_load.c src/dc/rom/pw64_rom_structs.c src/dc/rom/pw64_arena.c \
	src/dc/ultra/ultra_data.c src/dc/rom/layout_checks.c
PORT_SRC := src/dc/test/hosttest.c src/dc/test/host_fatal.c src/dc/rom/romfs.c src/dc/rom/pw64_swap.c \
	src/dc/ultra/mio0.c src/dc/ultra/stubs_todo.c
SHZ_SRC  := $(SHZ)/source/shz_matrix.c $(SHZ)/source/shz_quat.c $(SHZ)/source/shz_version.c \
	$(SHZ)/source/shz_xmtrx.c $(SHZ)/source/sw/shz_xmtrx_sw.c $(SHZ)/source/sw/shz_complex_sw.c

SHZ_DEFS := -DSHZ_BACKEND=SHZ_SW -DSHZ_TLS_MODEL=SHZ_TLS_DISABLED -I$(SHZ)/include
COMMON   := -O1 -g -MMD -MP -ffunction-sections -fdata-sections
DECOMP_DEFS := -D_LANGUAGE_C -DVERSION_US -DBUILD_VERSION=VERSION_D -D_FINALROM -DNDEBUG -DTARGET_N64 \
	-DNON_MATCHING -DAVOID_UB -DRECOMP_BUILD -DWINGS64_DC -DWINGS64_HOSTTEST
DECOMP_INC := -nostdinc -isystem $(shell $(HOST_CC) -print-file-name=include) \
	-Isrc/dc/decomp -I$(DECOMP) -I$(DECOMP)/src -I$(DECOMP)/include -I$(DECOMP)/include/kernel \
	-I$(DECOMP)/include/libultra -I$(DECOMP)/include/libultra/PR -I$(DECOMP)/include/libultra/compiler/modern_gcc
DECOMP_CFLAGS := $(COMMON) -std=gnu11 -funsigned-char -fwrapv -fno-strict-aliasing -ffp-contract=off \
	-ftrivial-auto-var-init=zero -include src/dc/decomp/wings64_prelude.h $(DECOMP_DEFS) $(DECOMP_INC) \
	-w
PORT_CFLAGS := $(COMMON) -std=gnu2x -Wall -Wextra -Wno-unused-parameter -Isrc/dc/test/hoststubs -Isrc/dc \
	$(SHZ_DEFS) -DWINGS64_HOSTTEST
SHZ_CFLAGS := $(COMMON) -std=gnu2x -w $(SHZ_DEFS)

OBJ := $(patsubst $(DECOMP)/%.c,$(OUT)/decomp/%.o,$(DECOMP_SRC)) \
	$(patsubst %.c,$(OUT)/dl/%.o,$(DECOMPLIKE_SRC)) \
	$(patsubst %.c,$(OUT)/port/%.o,$(PORT_SRC)) \
	$(patsubst $(SHZ)/%.c,$(OUT)/shz/%.o,$(SHZ_SRC))

all: $(OUT)/hosttest

$(OUT)/decomp/%.o: $(DECOMP)/%.c $(DECOMP)/.wings64-stamp
	@mkdir -p $(@D)
	@$(HOST_CC) $(DECOMP_CFLAGS) -c $< -o $@
$(OUT)/dl/%.o: %.c
	@mkdir -p $(@D)
	@$(HOST_CC) $(DECOMP_CFLAGS) -c $< -o $@
$(OUT)/port/%.o: %.c
	@mkdir -p $(@D)
	@$(HOST_CC) $(PORT_CFLAGS) -c $< -o $@
$(OUT)/shz/%.o: $(SHZ)/%.c
	@mkdir -p $(@D)
	@$(HOST_CC) $(SHZ_CFLAGS) -c $< -o $@

$(OUT)/hosttest: $(OBJ)
	@echo "  LD      $@ (host, armhf)"
	@$(HOST_CC) -static -Wl,--gc-sections -Wl,--defsym=kernel_TEXT_START=0x802000A0 \
		-Wl,--defsym=D_803805E0=0x803805E0 -o $@ $(OBJ) -lm

clean:
	rm -rf $(OUT)
.PHONY: all clean
-include $(OBJ:.o=.d)
