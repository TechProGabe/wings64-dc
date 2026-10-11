# Flycast on this Mac

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

## Native fix: paused
A native build was started from v2.7 (`5aa091f`) in `/private/tmp/claude-501/flycast-mac` (scratch, not in
the repo): CMake configured, the build was not finished (empty `Flycast.app`), no patch written yet.
Next steps when resumed:
1. Finish the build (Command Line Tools clang is 12.0.5; use `brew install llvm` if C++ features fail).
2. Reproduce the Verify failure with the local build; log `ram_base`, the reserve size and the result of
   each `mmap` in core/linux/posix_vmem.cpp (Apple branch) and in addrspace.cpp reserve()/initMappings().
3. Likely suspects: the 4 GB+ reservation landing differently on this macOS, MAP_FIXED remaps failing
   silently, or initMappings running twice. Fallback patch: make `virtmemEnabled()` false so the dynarec
   uses slow memory ops (slower, but games run).
