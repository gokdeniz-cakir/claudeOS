#ifndef CLAUDE_FB_H
#define CLAUDE_FB_H

#include <stdint.h>

/* Initialize framebuffer backend from active VBE mode. Returns 0 on success. */
int fb_init(void);

/* Returns 1 when framebuffer backend is ready, otherwise 0. */
int fb_is_ready(void);

/* Primitive drawing operations on the back buffer. */
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t rgb);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb);
void fb_clear(uint32_t rgb);
void fb_swap_buffers(void);
void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg_rgb, uint32_t bg_rgb);
void fb_draw_text(uint32_t x, uint32_t y, const char *text, uint32_t fg_rgb, uint32_t bg_rgb);

/* Text-console helpers (8x16 bitmap font, double-buffered output). */
void fb_console_init(uint8_t fg, uint8_t bg);
void fb_console_putchar(char c, uint8_t fg, uint8_t bg);
void fb_console_set_cursor(uint16_t row, uint16_t col);
void fb_console_get_cursor(uint16_t *row, uint16_t *col);

#endif /* CLAUDE_FB_H */
