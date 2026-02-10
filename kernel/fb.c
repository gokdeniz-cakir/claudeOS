#include "fb.h"

#include <stdint.h>

#include "heap.h"
#include "serial.h"
#include "vbe.h"

#define FB_GLYPH_W          8U
#define FB_GLYPH_H          16U
#define FB_TAB_SIZE         8U

struct fb_state {
    uint8_t ready;
    uint8_t bytes_per_pixel;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t size;
    uint8_t *front;
    uint8_t *back;
    uint16_t text_rows;
    uint16_t text_cols;
    uint16_t cursor_row;
    uint16_t cursor_col;
    uint8_t dirty;
    uint32_t dirty_x0;
    uint32_t dirty_y0;
    uint32_t dirty_x1;
    uint32_t dirty_y1;
};

static struct fb_state g_fb;

/* ASCII 0x20..0x5F (space..underscore), adapted from font8x8_basic. */
static const uint8_t font8x8_upper_basic[64][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* ' ' */
    { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00 }, /* '!' */
    { 0x6C, 0x6C, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* '"' */
    { 0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00 }, /* '#' */
    { 0x18, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18, 0x00 }, /* '$' */
    { 0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00 }, /* '%' */
    { 0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00 }, /* '&' */
    { 0x30, 0x30, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* ''' */
    { 0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00 }, /* '(' */
    { 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00 }, /* ')' */
    { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00 }, /* '*' */
    { 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00 }, /* '+' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30 }, /* ',' */
    { 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00 }, /* '-' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00 }, /* '.' */
    { 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00 }, /* '/' */
    { 0x7C, 0xC6, 0xCE, 0xDE, 0xF6, 0xE6, 0x7C, 0x00 }, /* '0' */
    { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 }, /* '1' */
    { 0x7C, 0xC6, 0x0E, 0x1C, 0x70, 0xC0, 0xFE, 0x00 }, /* '2' */
    { 0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C, 0x00 }, /* '3' */
    { 0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x1E, 0x00 }, /* '4' */
    { 0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00 }, /* '5' */
    { 0x38, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00 }, /* '6' */
    { 0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00 }, /* '7' */
    { 0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00 }, /* '8' */
    { 0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00 }, /* '9' */
    { 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00 }, /* ':' */
    { 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30 }, /* ';' */
    { 0x0E, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0E, 0x00 }, /* '<' */
    { 0x00, 0x00, 0x7E, 0x00, 0x00, 0x7E, 0x00, 0x00 }, /* '=' */
    { 0x70, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x70, 0x00 }, /* '>' */
    { 0x7C, 0xC6, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00 }, /* '?' */
    { 0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x78, 0x00 }, /* '@' */
    { 0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0x00 }, /* 'A' */
    { 0xFC, 0x66, 0x66, 0x7C, 0x66, 0x66, 0xFC, 0x00 }, /* 'B' */
    { 0x3C, 0x66, 0xC0, 0xC0, 0xC0, 0x66, 0x3C, 0x00 }, /* 'C' */
    { 0xF8, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0xF8, 0x00 }, /* 'D' */
    { 0xFE, 0x62, 0x68, 0x78, 0x68, 0x62, 0xFE, 0x00 }, /* 'E' */
    { 0xFE, 0x62, 0x68, 0x78, 0x68, 0x60, 0xF0, 0x00 }, /* 'F' */
    { 0x3C, 0x66, 0xC0, 0xC0, 0xCE, 0x66, 0x3E, 0x00 }, /* 'G' */
    { 0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00 }, /* 'H' */
    { 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00 }, /* 'I' */
    { 0x1E, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00 }, /* 'J' */
    { 0xE6, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0xE6, 0x00 }, /* 'K' */
    { 0xF0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE, 0x00 }, /* 'L' */
    { 0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6, 0xC6, 0x00 }, /* 'M' */
    { 0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00 }, /* 'N' */
    { 0x38, 0x6C, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00 }, /* 'O' */
    { 0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00 }, /* 'P' */
    { 0x78, 0xCC, 0xCC, 0xCC, 0xDC, 0x78, 0x1C, 0x00 }, /* 'Q' */
    { 0xFC, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0xE6, 0x00 }, /* 'R' */
    { 0x7C, 0xC6, 0xE0, 0x78, 0x0E, 0xC6, 0x7C, 0x00 }, /* 'S' */
    { 0x7E, 0x7E, 0x5A, 0x18, 0x18, 0x18, 0x3C, 0x00 }, /* 'T' */
    { 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00 }, /* 'U' */
    { 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00 }, /* 'V' */
    { 0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00 }, /* 'W' */
    { 0xC6, 0xC6, 0x6C, 0x38, 0x38, 0x6C, 0xC6, 0x00 }, /* 'X' */
    { 0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0x00 }, /* 'Y' */
    { 0xFE, 0xC6, 0x8C, 0x18, 0x32, 0x66, 0xFE, 0x00 }, /* 'Z' */
    { 0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00 }, /* '[' */
    { 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00 }, /* '\' */
    { 0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00 }, /* ']' */
    { 0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00 }, /* '^' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF }, /* '_' */
};

static const uint32_t vga_palette_rgb[16] = {
    0x000000U, 0x0000AAU, 0x00AA00U, 0x00AAAAU,
    0xAA0000U, 0xAA00AAU, 0xAA5500U, 0xAAAAAAU,
    0x555555U, 0x5555FFU, 0x55FF55U, 0x55FFFFU,
    0xFF5555U, 0xFF55FFU, 0xFFFF55U, 0xFFFFFFU,
};

static void fb_memcpy(uint8_t *dst, const uint8_t *src, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }
}

static void fb_memmove(uint8_t *dst, const uint8_t *src, uint32_t size)
{
    if (dst == src || size == 0U) {
        return;
    }

    if (dst < src) {
        for (uint32_t i = 0; i < size; i++) {
            dst[i] = src[i];
        }
    } else {
        for (uint32_t i = size; i > 0U; i--) {
            dst[i - 1U] = src[i - 1U];
        }
    }
}

static void serial_put_dec(uint32_t value)
{
    char buf[11];
    uint32_t idx = 0U;

    if (value == 0U) {
        serial_putchar('0');
        return;
    }

    while (value > 0U && idx < sizeof(buf)) {
        buf[idx++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (idx > 0U) {
        serial_putchar(buf[--idx]);
    }
}

static uint32_t palette_to_rgb(uint8_t color)
{
    return vga_palette_rgb[color & 0x0FU];
}

static void fb_store_pixel(uint8_t *pixel, uint32_t rgb)
{
    uint8_t r = (uint8_t)((rgb >> 16) & 0xFFU);
    uint8_t g = (uint8_t)((rgb >> 8) & 0xFFU);
    uint8_t b = (uint8_t)(rgb & 0xFFU);

    if (g_fb.bytes_per_pixel == 4U) {
        pixel[0] = b;
        pixel[1] = g;
        pixel[2] = r;
        pixel[3] = 0U;
        return;
    }

    if (g_fb.bytes_per_pixel == 3U) {
        pixel[0] = b;
        pixel[1] = g;
        pixel[2] = r;
        return;
    }

    if (g_fb.bytes_per_pixel == 2U) {
        uint16_t packed = (uint16_t)(((uint16_t)(r >> 3) << 11)
            | ((uint16_t)(g >> 2) << 5)
            | ((uint16_t)(b >> 3)));
        pixel[0] = (uint8_t)(packed & 0xFFU);
        pixel[1] = (uint8_t)((packed >> 8) & 0xFFU);
    }
}

static void fb_mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint32_t x1;
    uint32_t y1;

    if (g_fb.ready == 0U || w == 0U || h == 0U) {
        return;
    }

    if (x >= g_fb.width || y >= g_fb.height) {
        return;
    }

    x1 = x + w;
    y1 = y + h;

    if (x1 > g_fb.width) {
        x1 = g_fb.width;
    }
    if (y1 > g_fb.height) {
        y1 = g_fb.height;
    }

    if (g_fb.dirty == 0U) {
        g_fb.dirty = 1U;
        g_fb.dirty_x0 = x;
        g_fb.dirty_y0 = y;
        g_fb.dirty_x1 = x1;
        g_fb.dirty_y1 = y1;
        return;
    }

    if (x < g_fb.dirty_x0) {
        g_fb.dirty_x0 = x;
    }
    if (y < g_fb.dirty_y0) {
        g_fb.dirty_y0 = y;
    }
    if (x1 > g_fb.dirty_x1) {
        g_fb.dirty_x1 = x1;
    }
    if (y1 > g_fb.dirty_y1) {
        g_fb.dirty_y1 = y1;
    }
}

static const uint8_t *fb_glyph_for_char(char c)
{
    uint8_t ch = (uint8_t)c;

    if (ch >= 'a' && ch <= 'z') {
        ch = (uint8_t)(ch - 32U);
    }

    if (ch >= 0x20U && ch <= 0x5FU) {
        return font8x8_upper_basic[ch - 0x20U];
    }

    return font8x8_upper_basic['?' - 0x20];
}

static void fb_console_draw_char_at(uint16_t row, uint16_t col, char c, uint8_t fg, uint8_t bg)
{
    uint32_t x0 = (uint32_t)col * FB_GLYPH_W;
    uint32_t y0 = (uint32_t)row * FB_GLYPH_H;
    const uint8_t *glyph = fb_glyph_for_char(c);
    uint32_t fg_rgb = palette_to_rgb(fg);
    uint32_t bg_rgb = palette_to_rgb(bg);

    fb_fill_rect(x0, y0, FB_GLYPH_W, FB_GLYPH_H, bg_rgb);

    for (uint32_t gy = 0; gy < 8U; gy++) {
        uint8_t row_bits = glyph[gy];
        uint32_t py0 = y0 + (gy * 2U);
        uint32_t py1 = py0 + 1U;

        for (uint32_t gx = 0; gx < FB_GLYPH_W; gx++) {
            if ((row_bits & (uint8_t)(1U << (7U - gx))) != 0U) {
                fb_put_pixel(x0 + gx, py0, fg_rgb);
                fb_put_pixel(x0 + gx, py1, fg_rgb);
            }
        }
    }
}

static void fb_console_scroll(uint8_t bg)
{
    uint32_t scroll_px = FB_GLYPH_H;
    uint32_t scroll_bytes = scroll_px * g_fb.pitch;
    uint32_t keep_bytes = g_fb.size - scroll_bytes;

    fb_memmove(g_fb.back, g_fb.back + scroll_bytes, keep_bytes);
    fb_fill_rect(0U, g_fb.height - scroll_px, g_fb.width, scroll_px, palette_to_rgb(bg));
    fb_mark_dirty(0U, 0U, g_fb.width, g_fb.height);
}

int fb_init(void)
{
    struct vbe_mode mode;
    void *front_ptr;
    uint8_t *back_ptr;
    uint32_t bpp_bytes;

    if (g_fb.ready != 0U) {
        return 0;
    }

    if (vbe_get_mode(&mode) != 0) {
        return -1;
    }

    bpp_bytes = (mode.bpp + 7U) / 8U;
    if (bpp_bytes < 2U || bpp_bytes > 4U) {
        serial_puts("[FB] unsupported bpp=");
        serial_put_dec(mode.bpp);
        serial_puts("\n");
        return -1;
    }

    front_ptr = vbe_get_framebuffer();
    if (front_ptr == 0 || mode.framebuffer_size == 0U || mode.pitch == 0U) {
        serial_puts("[FB] invalid VBE framebuffer metadata\n");
        return -1;
    }

    back_ptr = (uint8_t *)kmalloc(mode.framebuffer_size);
    if (back_ptr == 0) {
        serial_puts("[FB] back buffer alloc failed, using single buffer\n");
        back_ptr = (uint8_t *)front_ptr;
    } else {
        fb_memcpy(back_ptr, (const uint8_t *)front_ptr, mode.framebuffer_size);
    }

    g_fb.ready = 1U;
    g_fb.bytes_per_pixel = (uint8_t)bpp_bytes;
    g_fb.width = mode.width;
    g_fb.height = mode.height;
    g_fb.pitch = mode.pitch;
    g_fb.size = mode.framebuffer_size;
    g_fb.front = (uint8_t *)front_ptr;
    g_fb.back = back_ptr;
    g_fb.text_cols = (uint16_t)(mode.width / FB_GLYPH_W);
    g_fb.text_rows = (uint16_t)(mode.height / FB_GLYPH_H);
    g_fb.cursor_row = 0U;
    g_fb.cursor_col = 0U;
    g_fb.dirty = 0U;

    if (g_fb.text_cols == 0U || g_fb.text_rows == 0U) {
        g_fb.ready = 0U;
        serial_puts("[FB] framebuffer too small for console glyph grid\n");
        return -1;
    }

    fb_clear(palette_to_rgb(0));
    fb_swap_buffers();

    serial_puts("[FB] initialized ");
    serial_put_dec(g_fb.width);
    serial_putchar('x');
    serial_put_dec(g_fb.height);
    serial_puts(" bpp=");
    serial_put_dec(mode.bpp);
    serial_puts(" pitch=");
    serial_put_dec(g_fb.pitch);
    serial_puts("\n");

    return 0;
}

int fb_is_ready(void)
{
    return (g_fb.ready != 0U) ? 1 : 0;
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t rgb)
{
    uint32_t offset;

    if (g_fb.ready == 0U || x >= g_fb.width || y >= g_fb.height) {
        return;
    }

    offset = y * g_fb.pitch + (x * g_fb.bytes_per_pixel);
    if (offset + g_fb.bytes_per_pixel > g_fb.size) {
        return;
    }

    fb_store_pixel(g_fb.back + offset, rgb);
    fb_mark_dirty(x, y, 1U, 1U);
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb)
{
    uint32_t x_end;
    uint32_t y_end;

    if (g_fb.ready == 0U || w == 0U || h == 0U) {
        return;
    }

    if (x >= g_fb.width || y >= g_fb.height) {
        return;
    }

    x_end = x + w;
    y_end = y + h;

    if (x_end > g_fb.width) {
        x_end = g_fb.width;
    }
    if (y_end > g_fb.height) {
        y_end = g_fb.height;
    }

    for (uint32_t py = y; py < y_end; py++) {
        uint8_t *row = g_fb.back + (py * g_fb.pitch) + (x * g_fb.bytes_per_pixel);
        for (uint32_t px = x; px < x_end; px++) {
            fb_store_pixel(row, rgb);
            row += g_fb.bytes_per_pixel;
        }
    }

    fb_mark_dirty(x, y, x_end - x, y_end - y);
}

void fb_clear(uint32_t rgb)
{
    fb_fill_rect(0U, 0U, g_fb.width, g_fb.height, rgb);
}

void fb_swap_buffers(void)
{
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;
    uint32_t row_bytes;

    if (g_fb.ready == 0U) {
        return;
    }

    if (g_fb.dirty == 0U) {
        return;
    }

    if (g_fb.front == g_fb.back) {
        g_fb.dirty = 0U;
        return;
    }

    x0 = g_fb.dirty_x0;
    y0 = g_fb.dirty_y0;
    x1 = g_fb.dirty_x1;
    y1 = g_fb.dirty_y1;
    row_bytes = (x1 - x0) * g_fb.bytes_per_pixel;

    for (uint32_t y = y0; y < y1; y++) {
        uint32_t row_off = y * g_fb.pitch + (x0 * g_fb.bytes_per_pixel);
        fb_memcpy(g_fb.front + row_off, g_fb.back + row_off, row_bytes);
    }

    g_fb.dirty = 0U;
}

void fb_console_init(uint8_t fg, uint8_t bg)
{
    if (g_fb.ready == 0U) {
        return;
    }

    g_fb.cursor_row = 0U;
    g_fb.cursor_col = 0U;
    fb_clear(palette_to_rgb(bg));
    fb_swap_buffers();
    (void)fg;
}

void fb_console_putchar(char c, uint8_t fg, uint8_t bg)
{
    if (g_fb.ready == 0U) {
        return;
    }

    if (c == '\n') {
        g_fb.cursor_col = 0U;
        g_fb.cursor_row++;
    } else if (c == '\r') {
        g_fb.cursor_col = 0U;
    } else if (c == '\b') {
        if (g_fb.cursor_col > 0U) {
            g_fb.cursor_col--;
        } else if (g_fb.cursor_row > 0U) {
            g_fb.cursor_row--;
            g_fb.cursor_col = (uint16_t)(g_fb.text_cols - 1U);
        }
        fb_console_draw_char_at(g_fb.cursor_row, g_fb.cursor_col, ' ', fg, bg);
    } else if (c == '\t') {
        g_fb.cursor_col = (uint16_t)((g_fb.cursor_col + FB_TAB_SIZE) & ~(FB_TAB_SIZE - 1U));
        if (g_fb.cursor_col >= g_fb.text_cols) {
            g_fb.cursor_col = 0U;
            g_fb.cursor_row++;
        }
    } else {
        fb_console_draw_char_at(g_fb.cursor_row, g_fb.cursor_col, c, fg, bg);
        g_fb.cursor_col++;
        if (g_fb.cursor_col >= g_fb.text_cols) {
            g_fb.cursor_col = 0U;
            g_fb.cursor_row++;
        }
    }

    while (g_fb.cursor_row >= g_fb.text_rows) {
        fb_console_scroll(bg);
        g_fb.cursor_row--;
    }

    fb_swap_buffers();
}

void fb_console_set_cursor(uint16_t row, uint16_t col)
{
    if (g_fb.ready == 0U) {
        return;
    }

    if (g_fb.text_rows == 0U || g_fb.text_cols == 0U) {
        return;
    }

    if (row >= g_fb.text_rows) {
        row = (uint16_t)(g_fb.text_rows - 1U);
    }
    if (col >= g_fb.text_cols) {
        col = (uint16_t)(g_fb.text_cols - 1U);
    }

    g_fb.cursor_row = row;
    g_fb.cursor_col = col;
}

void fb_console_get_cursor(uint16_t *row, uint16_t *col)
{
    if (g_fb.ready == 0U) {
        if (row != 0) {
            *row = 0U;
        }
        if (col != 0) {
            *col = 0U;
        }
        return;
    }

    if (row != 0) {
        *row = g_fb.cursor_row;
    }
    if (col != 0) {
        *col = g_fb.cursor_col;
    }
}
