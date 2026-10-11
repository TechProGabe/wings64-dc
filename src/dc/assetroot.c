/* assetroot.c — asset root probe (ssb64-dc assetroot pattern, trimmed for M0).
 * Order: DB_PC_ROOT (dcload host path, only when the knob names one) -> /cd -> /rd.
 */
#include "assetroot.h"
#include "db.h"

#include <kos.h>
#include <string.h>

static char disc_id[96];

static int try_root(const char *root) {
    char path[128];
    file_t f;
    ssize_t n;
    snprintf(path, sizeof path, "%s/disc.id", root);
    f = fs_open(path, O_RDONLY);
    if (f < 0) return 0;
    n = fs_read(f, disc_id, sizeof disc_id - 1);
    fs_close(f);
    if (n < 0) n = 0;
    disc_id[n] = 0;
    while (n > 0 && (disc_id[n - 1] == '\n' || disc_id[n - 1] == '\r')) disc_id[--n] = 0;
    return 1;
}

const char *assetroot_probe(void) {
    static const char *const roots[] = {
#ifdef DB_PC_ROOT
        DB_PC_ROOT,
#endif
        "/cd", "/rd",
    };
    for (unsigned i = 0; i < sizeof roots / sizeof roots[0]; i++) {
        if (try_root(roots[i])) {
            db_log("assetroot: %s (disc.id %s)\n", roots[i], disc_id);
            return roots[i];
        }
    }
    db_log("assetroot: no disc.id under any root\n");
    return NULL;
}

const char *assetroot_disc_id(void) { return disc_id; }
