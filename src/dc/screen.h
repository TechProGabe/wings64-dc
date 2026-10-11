/* screen.h — boot/diagnostic text screen. On CD-R-only hardware this is the evidence channel:
 * BIOS font into a 1024x512 RGB565 texture, drawn as one full-screen quad through the PVR. */
#ifndef WINGS64_SCREEN_H
#define WINGS64_SCREEN_H

#include <stdint.h>

#define SCREEN_ROWS 18
#define SCREEN_WHITE  0xFFFF
#define SCREEN_GREY   0x8410
#define SCREEN_GREEN  0x07E0
#define SCREEN_RED    0xF800
#define SCREEN_YELLOW 0xFFE0
#define SCREEN_CYAN   0x07FF

void screen_init(void);
void screen_line(int row, uint16_t color, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
/* Re-uploads the texture if any line changed, then renders one PVR frame. */
void screen_frame(void);

#endif
