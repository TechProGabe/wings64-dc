# 021-port-settings-menu

**Why:** the port has settings the N64 never had (an FPS monitor, the SD card log). The maintainer wants
them on the title screen in a menu that looks and behaves like the game's own. This patch adds the
hooks. The menu itself is port code in `src/dc/settings.c`, which calls the game's own UI routines.

**What:**
- `app/title_screen.c`: the title menu gets a third item, "Settings", below "Start game" and "Option".
  Its text is set with `menuSetItem`, as a glyph string built from font 6. The menu's anchor moves down
  one row and its dark box grows down one row, so "Start game" and "Option" stay where they were.
  Choosing the item calls `wings64_settings_open()`, which returns `GAME_STATE_OPTIONS`.
  "Start game" is still the default item, so A on the title still starts the game.
- `app/game.c`: `gameUpdateStateOptions` keeps its level load. If the title asked for the settings
  panel (`wings64_settings_take_request()`), it runs `wings64_settings_screen()` instead of
  `optionsTopRender()`. It then returns to the title without the intro, as after Option.
- `kernel/graphics.c`: `uvGfxEnd` calls `wings64_settings_frame()` just before the display list is
  closed. That function samples the frame rate and draws the FPS monitor when it is on.
- `kernel/font.c`: adds `uvFontOverlayStr_dc`. It prints one string in a game font and emits its sprite
  display list right away, then restores the game's font id, width, scale, colour and pending-message
  count. It cannot go through `uvFontGenDlist`, which restarts its display-list cursor at
  base + messages-so-far. A second call in one frame would then overwrite the commands of text already
  emitted that frame. So the helper carries on from where the last batch ended (`rsp_dl_next`).

**Kill switch:** `WINGS64_SETTINGS_MENU` (default 1, `src/dc/decomp/wings64_settings.h`).
`EXTRA_CFLAGS=-DWINGS64_SETTINGS_MENU=0` compiles all four hooks out, and `src/game/Makefile` forwards
`-DWINGS64_*` from EXTRA_CFLAGS to the decomp TUs.

**Origin:** own.

**N64 build:** unchanged (`#ifdef WINGS64_DC` / `#else` original; the font.c helper is DC-only).
