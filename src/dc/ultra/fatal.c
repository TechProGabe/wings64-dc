/* fatal.c — pw64_fatal: the decomp-side "stop with a message" (patches, loaders, swap checks).
 * Logs WINGS64_FATAL on serial/SD (the smoke gate fails on it) and halts. */
#include "../db.h"

#include <kos.h>

__attribute__((noreturn)) void pw64_fatal(const char *msg, unsigned int value) {
    db_log("WINGS64_FATAL %s (0x%08x)\n", msg, value);
    dbglog(DBG_CRITICAL, "WINGS64_FATAL %s (0x%08x)\n", msg, value);
    db_sd_flush();
    arch_abort();
}
