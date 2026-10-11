/* assetroot.h — find where disc/ was mounted: optional dcload root, the CD, then the romdisk. */
#ifndef WINGS64_ASSETROOT_H
#define WINGS64_ASSETROOT_H

/* Probes each candidate for "<root>/disc.id". Returns the root ("/cd", ...) or NULL. */
const char *assetroot_probe(void);
/* Contents of disc.id (the build stamp the disc was mastered with), "" if unread. */
const char *assetroot_disc_id(void);

#endif
