/* host_fatal.c — host pw64_fatal and db_log (the target versions live in src/dc/ultra/fatal.c, db.c). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

__attribute__((noreturn)) void pw64_fatal(const char *msg, unsigned int value) {
    fprintf(stderr, "WINGS64_FATAL %s (0x%08x)\n", msg, value);
    exit(70);
}

void db_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}
