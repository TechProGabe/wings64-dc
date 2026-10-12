/* wings64_fbfreeze.h — frozen frame under the game's screen fade (src/dc/gfx/gfx_f3d.c), as seen from the
 * decomp hook (patches/026). Plain prototypes only: decomp TUs and port TUs both include it.
 *
 * The N64 fade (replay_screen.c func_8032D51C) copies the last frame buffer and then only draws a fade quad
 * each frame: the frame buffer keeps the scene underneath. The PVR clears every frame, so while the freeze
 * is on the interpreter replays the last frame's polygon lists under each new frame.
 *
 * WINGS64_FB_FREEZE (default 1) is the kill switch: 0 compiles the hook out (fades draw over black).
 *   EXTRA_CFLAGS=-DWINGS64_FB_FREEZE=0 ./run.sh build
 */
#ifndef WINGS64_FBFREEZE_H
#define WINGS64_FBFREEZE_H

#ifndef WINGS64_FB_FREEZE
#define WINGS64_FB_FREEZE 1
#endif

/* 1: keep the last finished frame as the background of the frames that follow; 0: back to normal.
   Called on the game thread between gfx tasks; the display-list thread picks it up at its next frame. */
void wings64_fb_freeze(int on);

/* Persistent frame buffer (patches/028): the photo album (snap.c func_8033E3A8) draws each thumbnail once
 * and relies on the N64 frame buffer keeping it. While on, every frame starts from the previous finished
 * frame (read back from VRAM) instead of a clear. WINGS64_FB_PERSIST (default 1) is the kill switch. */
#ifndef WINGS64_FB_PERSIST
#define WINGS64_FB_PERSIST 1
#endif
void wings64_fb_persist(int on);

#endif
