/* wings64-dc: persist the 2 KB N64 EEPROM image as one VMU file.
 *
 * Layout on the VMU: "WINGS64.SAV" holding a vmu_pkg_t package (BIOS header,
 * one 32x32 icon, no eyecatch) followed by the raw 2048-byte image.
 *
 * Write strategy: the KOS VMU filesystem (kernel/arch/dreamcast/fs/fs_vmu.c)
 * has no atomic replace and no rename. Its write path is:
 *   open(O_WRONLY|O_TRUNC) -> fs_write() fills an in-memory copy ->
 *   close: vmufs_write(VMUFS_OVERWRITE), which deletes the old file and then
 *   allocates and writes the new one.
 * So nothing touches the card until fs_close(). A failed fs_open or fs_write
 * leaves the old save alone. A power cut or a full card during the close
 * can lose the old save (see the commit note in vmu_eeprom_save).
 * O_META is used so the fs layer does not re-wrap the payload with its own
 * default header. We build the full package ourselves, so the BIOS header
 * (desc, app_id, icon) is exactly what we set.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kos.h>
#include <dc/maple.h>
#include <dc/vmu_pkg.h>

#include "vmu_eeprom.h"

#define SAVE_NAME       "WINGS64.SAV"
#define SAVE_APP_ID     "wings64-dc"
#define SAVE_DESC_SHORT "PILOTWINGS 64"
#define SAVE_DESC_LONG  "wings64-dc save"

/* Largest file read on load. A valid save is 128 (header) + 512 (icon) + 2048
 * (image) = 2688 bytes, padded to 3072 on the card. 4 KB leaves room. */
#define SAVE_MAX_READ   4096

/* DIVERGES: the N64 EEPROM has no lock or status bits and no wear; the VMU file only stores the 2 KB payload plus its BIOS header. */
#define SAVE_STATUS_LEN 24

static char status_buf[SAVE_STATUS_LEN] = "not checked";

static void set_status(const char *s)
{
    snprintf(status_buf, sizeof status_buf, "%s", s);
}

/* Icon palette, ARGB4444. Index 0 is white, index 1 is blue. */
static const uint16_t icon_pal[16] = {
    0xFFFF, 0xF00F,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Build a 32x32 4bpp icon: all index 0, with a filled 16x16 square of index 1
 * in the middle. Pixel x sits in byte x/2, high nibble for even x. */
static void draw_icon(uint8_t icon[512])
{
    int x, y;

    memset(icon, 0, 512);
    for (y = 8; y < 24; y++) {
        for (x = 8; x < 24; x++) {
            int idx = y * 16 + x / 2;
            if (x & 1)
                icon[idx] |= 0x01;
            else
                icon[idx] |= 0x10;
        }
    }
}

/* Pick the VMU to use: ports a..d, units 1..2, first one present.
 * Uses maple_enum_type() so only memory-card devices are seen. The loop is
 * bounded by the number of Maple slots, so it cannot spin forever. */
static maple_device_t *find_vmu(void)
{
    maple_device_t *best = NULL;
    int best_key = 0;
    int i;

    for (i = 0; i < MAPLE_PORT_COUNT * MAPLE_UNIT_COUNT; i++) {
        maple_device_t *dev = maple_enum_type(i, MAPLE_FUNC_MEMCARD);
        int key;

        if (!dev)
            break;
        if (!dev->valid || dev->port < 0 || dev->port > 3 ||
                dev->unit < 1 || dev->unit > 2)
            continue;

        key = dev->port * 2 + (dev->unit - 1);
        if (!best || key < best_key) {
            best = dev;
            best_key = key;
        }
    }
    return best;
}

/* Build "/vmu/<port><unit>/WINGS64.SAV", e.g. "/vmu/a1/WINGS64.SAV". */
static void build_path(const maple_device_t *dev, char *path, size_t n)
{
    snprintf(path, n, "/vmu/%c%d/%s", 'a' + dev->port, dev->unit, SAVE_NAME);
}

static void set_status_vmu(const maple_device_t *dev)
{
    snprintf(status_buf, sizeof status_buf, "VMU %c%d",
             'a' + dev->port, dev->unit);
}

const char *vmu_eeprom_status(void)
{
    return status_buf;
}

int vmu_eeprom_load(unsigned char img[VMU_EEPROM_SIZE])
{
    maple_device_t *dev;
    char path[32];
    uint8_t *buf;
    vmu_pkg_t pkg;
    file_t fd;
    ssize_t got = 0;
    int rc;

    memset(img, 0, VMU_EEPROM_SIZE);

    dev = find_vmu();
    if (!dev) {
        set_status("no VMU");
        dbglog(DBG_INFO, "vmu_eeprom: load: no VMU found, image zero-filled\n");
        return 0;
    }
    set_status_vmu(dev);
    build_path(dev, path, sizeof path);

    /* Raw open: we parse the package ourselves, so the fs layer must not. */
    fd = fs_open(path, O_RDONLY | O_META);
    if (fd < 0) {
        set_status("no save");
        dbglog(DBG_INFO, "vmu_eeprom: load: no %s on %s\n", SAVE_NAME, status_buf);
        return 0;
    }

    buf = malloc(SAVE_MAX_READ);
    if (!buf) {
        fs_close(fd);
        set_status("no memory");
        dbglog(DBG_ERROR, "vmu_eeprom: load: malloc(%d) failed\n", SAVE_MAX_READ);
        return 0;
    }

    while (got < SAVE_MAX_READ) {
        ssize_t n = fs_read(fd, buf + got, SAVE_MAX_READ - got);
        if (n <= 0)
            break;
        got += n;
    }
    fs_close(fd);

    /* vmu_pkg_parse reads the header before checking sizes, so require the
     * whole fixed header to be present first. */
    if (got < (ssize_t)sizeof(vmu_hdr_t)) {
        free(buf);
        set_status("bad save");
        dbglog(DBG_ERROR, "vmu_eeprom: load: %s too short (%d bytes)\n",
               SAVE_NAME, (int)got);
        return 0;
    }

    rc = vmu_pkg_parse(buf, (size_t)got, &pkg);
    if (rc < 0 || strcmp(pkg.app_id, SAVE_APP_ID) != 0 ||
            pkg.data_len != VMU_EEPROM_SIZE) {
        free(buf);
        set_status("bad save");
        dbglog(DBG_ERROR, "vmu_eeprom: load: %s is not a valid save (rc=%d)\n",
               SAVE_NAME, rc);
        return 0;
    }

    /* pkg.data points into buf, so copy before freeing. */
    memcpy(img, pkg.data, VMU_EEPROM_SIZE);
    free(buf);

    dbglog(DBG_INFO, "vmu_eeprom: load: %d bytes from %s\n",
           VMU_EEPROM_SIZE, path);
    return 1;
}

int vmu_eeprom_save(const unsigned char img[VMU_EEPROM_SIZE])
{
    static uint8_t icon[512];
    static uint8_t eyecatch[1];
    maple_device_t *dev;
    char path[32];
    vmu_pkg_t pkg;
    uint8_t *out = NULL;
    int out_size = 0;
    file_t fd;
    ssize_t n;
    int close_rc;
    int saved_errno;

    dev = find_vmu();
    if (!dev) {
        set_status("no VMU");
        dbglog(DBG_INFO, "vmu_eeprom: save: no VMU found, nothing written\n");
        return 0;
    }
    set_status_vmu(dev);
    build_path(dev, path, sizeof path);

    draw_icon(icon);

    memset(&pkg, 0, sizeof pkg);
    strcpy(pkg.desc_short, SAVE_DESC_SHORT);
    strcpy(pkg.desc_long, SAVE_DESC_LONG);
    strcpy(pkg.app_id, SAVE_APP_ID);
    pkg.icon_cnt = 1;
    pkg.icon_anim_speed = 0;
    pkg.eyecatch_type = VMUPKG_EC_NONE;
    memcpy(pkg.icon_pal, icon_pal, sizeof pkg.icon_pal);
    pkg.icon_data = icon;
    pkg.eyecatch_data = eyecatch;
    pkg.data_len = VMU_EEPROM_SIZE;
    pkg.data = img;

    /* vmu_pkg_build mallocs *out; the caller frees it on every path. */
    if (vmu_pkg_build(&pkg, &out, &out_size) < 0) {
        set_status("build failed");
        dbglog(DBG_ERROR, "vmu_eeprom: save: vmu_pkg_build failed\n");
        return 0;
    }

    /* DIVERGES: a save is a whole-file commit, so a power cut or full card during fs_close can lose the previous save; a real EEPROM never loses other blocks. */
    /* O_TRUNC defers the real work to fs_close(); see the file header. */
    fd = fs_open(path, O_WRONLY | O_CREAT | O_TRUNC | O_META);
    if (fd < 0) {
        free(out);
        set_status("VMU open failed");
        dbglog(DBG_ERROR, "vmu_eeprom: save: cannot open %s for writing\n", path);
        return 0;
    }

    errno = 0;
    n = fs_write(fd, out, out_size);
    saved_errno = errno;

    /* DIVERGES: N64 EEPROM commits each 8-byte block as it is written; here the whole 2 KB image is one VMU file, committed at fs_close, so each save costs one VMU write. */
    /* Always close, to release the handle. This is the commit point. If the
     * write came up short, the card gets a short file that fails its CRC on
     * the next load. That only happens on an out-of-memory realloc in the fs
     * layer. */
    errno = 0;
    close_rc = fs_close(fd);
    if (close_rc < 0 && errno != 0)
        saved_errno = errno;
    free(out);

    if (n != out_size || close_rc < 0) {
        if (saved_errno == ENOSPC) {
            set_status("VMU full");
            dbglog(DBG_ERROR, "vmu_eeprom: save: %s: VMU full, old save may be lost\n", path);
        } else {
            set_status("VMU write failed");
            dbglog(DBG_ERROR, "vmu_eeprom: save: write to %s failed (errno %d)\n",
                   path, saved_errno);
        }
        return 0;
    }

    dbglog(DBG_INFO, "vmu_eeprom: save: %d bytes to %s\n", VMU_EEPROM_SIZE, path);
    return 1;
}
