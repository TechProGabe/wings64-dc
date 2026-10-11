/* game_probe.c — read-only view of the game's state machine for logs and the emulator gate.
 * Compiled with the decomp flags (needs app/game.h); main.c only sees the int. */
#include <ultra64.h>
#include "app/game.h"

s32 wings64_game_state(void) {
    return D_80362690 != NULL ? (s32)D_80362690->state : -1;
}
