# 026-fadeout-frozen-frame

**Why:** `func_8032D51C` (the game's `screen_fadeout`: black fade before menu and level changes, white fade
when the Birdman star is hit) calls `uvCopyFrameBuf(0)` and then draws only a translucent quad per frame,
relying on the N64 frame buffer to keep the last scene underneath. The PVR renders every frame from a clear
background, so on the port the scene vanished at once and the fade ran over black (the white box of the
Birdman star on black).

**What:**
- `app/replay_screen.c` `func_8032D51C`: after `uvCopyFrameBuf(0)` it calls `wings64_fb_freeze(1)`; at the
  end it waits for the fade's last gfx task and calls `wings64_fb_freeze(0)`. While the freeze is on,
  `src/dc/gfx/gfx_f3d.c` replays the last finished frame's PVR polygon lists (copied once) under each frame.

**Kill switch:** `WINGS64_FB_FREEZE` (default 1, `src/dc/decomp/wings64_fbfreeze.h`).
`EXTRA_CFLAGS=-DWINGS64_FB_FREEZE=0 ./run.sh build` compiles the hook out.

**Origin:** own.

**N64 build:** unchanged (the hooks are `#if defined(WINGS64_DC) && WINGS64_FB_FREEZE`; the include is DC-only).
