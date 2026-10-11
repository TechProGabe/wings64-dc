# Flycast on this Mac

**Resolved 2026-10-11:** `./run.sh emu` runs a native Flycast v2.7 built with
`docker/emu/flycast-v2.7-macos.patch` (startup race fix in core/hw/mem/addrspace.cpp + two build fixes),
installed at `~/Applications/Flycast-wings64.app`. `./run.sh play` (VNC) remains as a fallback.

## Status (2026-10-11)
- **Official Flycast 2.6 and 2.7 for macOS abort for every game** on this host (Darwin 27, Apple Silicon):
  `Verify Failed: &mem_b[0] == sq_buffer + sizeof(Sh4Context) + 0x0C000000` (core/hw/sh4/dyna/driver.cpp:349),
  dynarec on or off, our image or TuxRacer. The log shows `BASE` (ram_base) non-null but RAM not at
  ram_base + 0x0C000000, which core/hw/mem/addrspace.cpp initMappings() should make impossible.
- **Working today:** `./run.sh play` runs Flycast v2.7 built from source on Linux (docker/emu) under Xvfb
  and exports it over VNC on localhost:5900 (password `wings64`); macOS Screen Sharing opens automatically.
  Stop with `docker rm -f wings64-play`. Keyboard: Return = Start, X = A, C = B, S/D = X/Y (C-left/right),
  arrows = D-pad, I/J/K/L = stick, F/V = L/R triggers (Z/R). No audio exists before M4.
- The emulator gate (`./run.sh gate`, `./run.sh smoke`) uses the same Linux build and is unaffected.

## Native fix: found and verified (2026-10-11)

**Root cause: a startup race in the RAM reservation.** `addrspace::initMappings()` runs on a second thread
(`Emulator::init()` from the game-load thread, called from `Emulator::loadGame`), while the main thread is still
inside `flycast_init()` -> `addrspace::reserve()` (core/nullDC.cpp:77). Because `reserve()` has no lock and
`initMappings()` does not call it, `initMappings()` can read `ram_base == nullptr` (core/hw/mem/addrspace.cpp:408 in the
original file), take the malloc fallback (`mem_b.alloc`, `vram.alloc`), and then `reserve()` sets `ram_base` to the
vmem reservation. Result: `ram_base` non-null, `mem_b` in an unrelated malloc block, and
`Sh4Recompiler::Init()` fails `verify(&mem_b[0] == sq_buffer + sizeof(Sh4Context) + 0x0C000000)`
(core/hw/sh4/dyna/driver.cpp:349). Observed with `fprintf` + `backtrace_symbols_fd`: `initMappings enter ram_base=0x0`
on a pthread, `reserve` on the main thread (SDL_main via NSApplication notification) completing in between.

Reproduced on the unpatched build: `BASE 0x148010000 RAM(16 MB) 0x113cb8000 ...` (RAM not at ram_base + 0x0C000000),
then `Verify Failed`.

**Fix (core/hw/mem/addrspace.cpp):** make `reserve()` exactly-once and blocking (`std::call_once`), and have
`initMappings()` call `reserve()` first so the vmem-vs-fallback decision never sees an unreserved `ram_base`.
Preferred fix as requested: RAM lands at `ram_base + 0x0C000000`; no fallback needed.

After the fix: `BASE 0x148010000 RAM(16 MB) 0x154010000 VRAM64(8 MB) 0x14c010000 ARAM(2 MB) 0x168010000`
(0x148010000 + 0x0C000000 = 0x154010000). Log: `WINGS64_M2_GAMELOOP` present, `Verify Failed` absent.

### Patch (scratch clone, v2.7 `5aa091f`; Flycast source; build-only patches below)

```diff
diff --git a/core/hw/mem/addrspace.cpp b/core/hw/mem/addrspace.cpp
index c08cb8ef6..596d3a4b2 100644
--- a/core/hw/mem/addrspace.cpp
+++ b/core/hw/mem/addrspace.cpp
@@ -7,6 +7,7 @@
 #include "oslib/oslib.h"
 #include "oslib/virtmem.h"
 #include <cassert>
+#include <mutex>
 
 namespace addrspace
 {
@@ -380,10 +381,13 @@ bool bm_lockedWrite(u8* address)
 
 bool reserve()
 {
-	if (ram_base != nullptr)
-		return true;
-
-	virtmem::init((void**)&ram_base, (void**)&p_sh4rcb, RAM_SIZE_MAX + VRAM_SIZE_MAX + ARAM_SIZE_MAX + elan::ERAM_SIZE_MAX);
+	// initMappings() can run on another thread (Emulator::init from the game-load thread) while the
+	// main thread is still in flycast_init(). Make the reservation exactly-once and blocking so that
+	// initMappings() never observes ram_base == nullptr and falls back to malloc'd RAM.
+	static std::once_flag reserveOnce;
+	std::call_once(reserveOnce, [] {
+		virtmem::init((void**)&ram_base, (void**)&p_sh4rcb, RAM_SIZE_MAX + VRAM_SIZE_MAX + ARAM_SIZE_MAX + elan::ERAM_SIZE_MAX);
+	});
 	return true;
 }
 
@@ -403,6 +407,7 @@ static void termMappings()
 
 void initMappings()
 {
+	reserve();	// idempotent; guarantees ram_base is set before choosing the vmem or fallback path
 	termMappings();
 	// Fallback to statically allocated buffers, this results in slow-ops being generated.
 	if (ram_base == nullptr)
```

Build-only patches (needed for this Mac's toolchain, not part of the race fix):

```diff
diff --git a/CMakeLists.txt b/CMakeLists.txt
index 1cd5694..0b38e61 100644
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -112,6 +112,7 @@ endif()
 target_compile_options(Syphon PRIVATE "-fobjc-arc")
 
 target_precompile_headers(Syphon PUBLIC "Syphon_Prefix.pch")
+target_compile_options(Syphon PRIVATE "-include" "${CMAKE_CURRENT_SOURCE_DIR}/Syphon_Prefix.pch")
 
 target_include_directories(Syphon PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>")
 target_compile_definitions(Syphon PRIVATE SYPHON_CORE_SHARE)
diff --git a/src/DppDeviceImp.hpp b/src/DppDeviceImp.hpp
index 1186699..0b94ede 100644
--- a/src/DppDeviceImp.hpp
+++ b/src/DppDeviceImp.hpp
@@ -32,6 +32,7 @@
 #include <condition_variable>
 #include <chrono>
 #include <map>
+#include <unordered_map>
 #include <thread>
 
 namespace dpp_api
```

- Syphon/CMakeLists.txt: `-include Syphon_Prefix.pch` is needed because `SYPHONLOG` is only defined in the
  prefix header and PCH is disabled.
- DppDeviceImp.hpp: missing `#include <unordered_map>` (Apple clang 12 does not get it transitively).

### Build (exact commands)

Toolchain: CLT clang 12.0.5 for C/C++/ObjC; SDK `MacOSX11.3.sdk` (the only SDK installed); CMake 4.4.4; Ninja.
SDL2 built from the bundled `core/deps/SDL` (Homebrew sdl2 cannot be linked against this SDK; see notes).

```sh
# 1. SDL2 static, ObjC compiled without msgSend class stubs (Apple clang 12 does not emit them)
cd /private/tmp/claude-501
cmake -S flycast-mac/core/deps/SDL -B sdl-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_OBJC_COMPILER=/Library/Developer/CommandLineTools/usr/bin/clang \
  -DCMAKE_OBJC_COMPILE_OBJECT="<CMAKE_OBJC_COMPILER> <DEFINES> <INCLUDES> -x objective-c <FLAGS> -o <OBJECT> -c <SOURCE>" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX11.3.sdk \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF
cmake --build sdl-build -j8
cp sdl-build/libSDL2.a sdl-prefix/lib/libSDL2.a   # install step tries /usr/local and fails; copy by hand
# (sdl-prefix/lib/cmake/SDL2 comes from `cmake --install`; keep it)

# 2. Flycast (fresh dir, all C/C++/ObjC from Apple clang 12)
cd /private/tmp/claude-501/flycast-mac
cmake -S . -B build4 -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_VULKAN=OFF -DUSE_BREAKPAD=OFF \
  -DUSE_LUA=OFF -DUSE_DISCORD=OFF -DUSE_HOST_SDL=ON \
  -DCMAKE_PREFIX_PATH=/private/tmp/claude-501/sdl-prefix -DSDL2_DIR=/private/tmp/claude-501/sdl-prefix/lib/cmake/SDL2 \
  -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
  -DCMAKE_C_COMPILER=/Library/Developer/CommandLineTools/usr/bin/clang \
  -DCMAKE_CXX_COMPILER=/Library/Developer/CommandLineTools/usr/bin/clang++ \
  -DCMAKE_OBJC_COMPILER=/Library/Developer/CommandLineTools/usr/bin/clang \
  -DCMAKE_OBJC_COMPILE_OBJECT="<CMAKE_OBJC_COMPILER> <DEFINES> <INCLUDES> -x objective-c <FLAGS> -o <OBJECT> -c <SOURCE>" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX11.3.sdk \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DZLIB_LIBRARY=/Library/Developer/CommandLineTools/SDKs/MacOSX11.3.sdk/usr/lib/libz.tbd \
  -DZLIB_INCLUDE_DIR=/Library/Developer/CommandLineTools/SDKs/MacOSX11.3.sdk/usr/include \
  -DCMAKE_EXE_LINKER_FLAGS="/private/tmp/claude-501/shim/iokit_shim.o"
cmake --build build4 -j8
# the link succeeds; the last step ("Error copying /lib/libMoltenVK.dylib") is a Vulkan post-build copy and
# exits 1 with USE_VULKAN=OFF. The binary is complete.
```

`iokit_shim.o` is a one-line C file, `unsigned int kIOMainPortDefault = 0;`, compiled with
`clang -arch arm64 -mmacosx-version-min=11.0 -isysroot .../MacOSX11.3.sdk -c`. It supplies a symbol that is
missing from the 11.3 IOKit headers (`MACH_PORT_NULL` = 0 is the documented default main port).

Binary: `/private/tmp/claude-501/flycast-mac/build4/Flycast.app/Contents/MacOS/Flycast` (arm64 Mach-O).

### Run

```sh
cd /private/tmp/claude-501
/private/tmp/claude-501/flycast-mac/build4/Flycast.app/Contents/MacOS/Flycast \
  -config config:Debug.SerialConsoleEnabled=yes -config config:UseReios=yes \
  /Users/gabe/Documents/GitHub/wings64-dc/build/wings64-emu.cdi > fly.log 2>&1 &
sleep 25; pkill -f "Flycast.app/Contents/MacOS/Flycast"
grep -E "BASE|Verify|WINGS64_M2" fly.log
```

Expected: a `BASE` line with `RAM` at `ram_base + 0x0C000000`, `WINGS64_M2_GAMELOOP`, and no `Verify Failed`.

### Tools installed

- `brew install lld` (23.1.3). Not used in the final build. It only helped test the linker path.
- Homebrew LLVM 22.1.7 was already installed and is not used in the final build (C/C++/ObjC all use CLT clang).
- Nothing was installed under /Applications or /usr/local (the SDL install's /usr/local copy was denied and skipped).

### Notes

- Homebrew sdl2 (2.32.10) cannot be linked here: it was built against a newer SDK (`_kIOMainPortDefault`, objc
  `_objc_msgSend$` stubs). Building SDL from the bundled source avoids that.
- Apple clang 12 emits no `_objc_msgSendClass$` stubs. Llvm clang 22 does, and ld64 on this SDK cannot resolve them,
  so the whole tree is compiled with CLT clang.

