/* IntellectOS High-Definition Framebuffer TTY Sink.
 * Fully freestanding, bounds-checked, screen-resolution adaptive console.
 * Uses sharp 8x16 monospace font with full ASCII coverage.
 * No UEFI runtime or host dependencies.
 */
#include <stddef.h>
#include <stdint.h>
#include "terminalfb64.h"
#include "serial64.h"
#include "font8x16.h"

static volatile uint8_t *fb;
static uint32_t width, height, pitch, fmt;
static uint32_t xcol, yrow, cols, rows;
static uint32_t top, left, cell_w, cell_h, scale;
static uint32_t bg_screen, bg_term, fg_text, accent_color, bar_color;
static int enabled;

static inline uint32_t pixel32(uint32_t rgb) {
    return fmt == INTELLECT_PIXEL_RGBX8888 ?
      ((rgb & 255u) << 16) | (rgb & 0xff00u) | ((rgb >> 16) & 255u) : rgb;
}

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb) {
    if (!enabled || x >= width || y >= height || !w || !h) return;
    if (w > width - x) w = width - x;
    if (h > height - y) h = height - y;
    const uint32_t color = pixel32(rgb);
    for (uint32_t j = 0; j < h; ++j) {
        volatile uint32_t *p = (volatile uint32_t *)(void *)(fb + (size_t)(y + j) * pitch + (size_t)x * 4u);
        for (uint32_t i = 0; i < w; ++i) p[i] = color;
    }
}

static void glyph(uint32_t x, uint32_t y, char ch, uint32_t color) {
    if (!enabled || (unsigned char)ch < 32u || (unsigned char)ch > 126u) return;
    const uint32_t c = pixel32(color);
    for (unsigned gy = 0; gy < 16u; ++gy) {
        const uint8_t bits = intellect_glyph8x16(ch, gy);
        if (!bits) continue;
        for (unsigned gx = 0; gx < 8u; ++gx) {
            if (bits & (1u << (7u - gx))) {
                if (scale == 1u) {
                    uint32_t px = x + gx, py = y + gy;
                    if (px < width && py < height) {
                        volatile uint32_t *p = (volatile uint32_t *)(void *)(fb + (size_t)py * pitch + (size_t)px * 4u);
                        *p = c;
                    }
                } else {
                    for (unsigned dy = 0; dy < scale; ++dy) {
                        uint32_t py = y + gy * scale + dy;
                        if (py >= height) continue;
                        volatile uint32_t *p = (volatile uint32_t *)(void *)(fb + (size_t)py * pitch + (size_t)(x + gx * scale) * 4u);
                        for (unsigned dx = 0; dx < scale; ++dx) {
                            if (x + gx * scale + dx < width) p[dx] = c;
                        }
                    }
                }
            }
        }
    }
}

static void label(uint32_t x, uint32_t y, const char *s, uint32_t color) {
    while (*s && x + 8u * scale <= width) {
        glyph(x, y, *s++, color);
        x += (8u * scale + (scale > 1u ? 2u : 1u));
    }
}

static void draw_cursor(int visible) {
    if (!enabled || xcol >= cols || yrow >= rows) return;
    uint32_t cx = left + xcol * cell_w;
    uint32_t cy = top + yrow * cell_h + 14u * scale;
    uint32_t ch_h = 2u * scale;
    fill(cx, cy, 8u * scale, ch_h, visible ? accent_color : bg_term);
}

static void scroll(void) {
    const uint32_t box_w = cols * cell_w;
    const uint32_t box_h = rows * cell_h;
    if (top + box_h > height || left + box_w > width) return;
    for (uint32_t y = top; y + cell_h < top + box_h; ++y) {
        volatile uint32_t *dst = (volatile uint32_t *)(void *)(fb + (size_t)y * pitch + (size_t)left * 4u);
        const volatile uint32_t *src = (const volatile uint32_t *)(const void *)(fb + (size_t)(y + cell_h) * pitch + (size_t)left * 4u);
        for (uint32_t x = 0; x < box_w; ++x) dst[x] = src[x];
    }
    fill(left, top + box_h - cell_h, box_w, cell_h, bg_term);
}

int intellect_terminalfb64_init(const intellect_bootinfo_t *b) {
    enabled = 0;
    if (!b || b->video_kind != INTELLECT_VIDEO_FRAMEBUFFER ||
        (b->pixel_format != INTELLECT_PIXEL_RGBX8888 && b->pixel_format != INTELLECT_PIXEL_BGRX8888) ||
        b->framebuffer_addr == 0 || b->framebuffer_width < 640u || b->framebuffer_height < 480u ||
        b->framebuffer_width > UINT32_MAX / 4u || b->framebuffer_pitch < b->framebuffer_width * 4u ||
        b->framebuffer_size < (uint64_t)b->framebuffer_pitch * b->framebuffer_height) return 0;

    fb = (volatile uint8_t *)(uintptr_t)b->framebuffer_addr;
    width = b->framebuffer_width;
    height = b->framebuffer_height;
    pitch = b->framebuffer_pitch;
    fmt = b->pixel_format;

    /* Theme colors */
    bg_screen    = 0x070b12u; /* Deep midnight obsidian */
    bg_term      = 0x0b111bu; /* Rich dark slate console */
    fg_text      = 0xe2e8f0u; /* Crisp, high-contrast soft white */
    accent_color = 0x00e5a3u; /* Modern electric emerald cyan */
    bar_color    = 0x111c2cu; /* Top / bottom navigation bars */

    /* Resolution adaptation: Scale font for HD / Full HD / 4K; 1x for small displays */
    scale = (width >= 1200u && height >= 700u) ? 2u : 1u;
    cell_w = 8u * scale + (scale > 1u ? 2u : 1u);
    cell_h = 16u * scale + (scale > 1u ? 2u : 2u);

    uint32_t hdr_h = (scale > 1u ? 54u : 38u);
    uint32_t footer_h = (scale > 1u ? 32u : 22u);
    uint32_t pad_x = (width >= 1200u ? 32u : (width >= 800u ? 20u : 12u));

    uint32_t usable_w = (width > 2u * pad_x) ? width - 2u * pad_x : width;
    uint32_t usable_h = (height > hdr_h + footer_h + 16u) ? height - hdr_h - footer_h - 16u : 0u;

    cols = usable_w / cell_w;
    rows = usable_h / cell_h;

    /* Cap to prevent memory/performance issues on ultra-wide screens */
    if (cols > 160u) cols = 160u;
    if (rows > 60u) rows = 60u;
    if (!cols || !rows) return 0;

    /* Center terminal window horizontally for beautiful balanced layout */
    left = (width - cols * cell_w) / 2u;
    top = hdr_h + 6u;

    enabled = 1;
    intellect_terminalfb64_clear();
    return 1;
}

void intellect_terminalfb64_clear(void) {
    if (!enabled) return;

    /* 1. Fill entire screen background */
    fill(0, 0, width, height, bg_screen);

    /* 2. Top header bar */
    uint32_t hdr_h = (scale > 1u ? 54u : 38u);
    fill(0, 0, width, hdr_h, bar_color);
    fill(0, 0, width, 3u, accent_color); /* Top neon accent glow */
    fill(0, hdr_h - 1u, width, 1u, 0x1d2e45u); /* Bottom border */

    uint32_t title_y = (hdr_h > 16u * scale ? (hdr_h - 16u * scale) / 2u : 4u);
    label(left > 12u ? left : 16u, title_y, "IntellectOS", accent_color);
    label((left > 12u ? left : 16u) + 11u * (8u * scale + (scale > 1u ? 2u : 1u)),
          title_y, "- Terminal Edition v1.0", 0x89a8c2u);

    if (width >= 750u) {
        const char *info = "x86-64 Native | Direct Terminal Boot";
        size_t slen = 0; while (info[slen]) ++slen;
        uint32_t info_w = (uint32_t)slen * (8u * scale + (scale > 1u ? 2u : 1u));
        if (width > info_w + 24u)
            label(width - info_w - 24u, title_y, info, 0x5a7590u);
    }

    /* 3. Terminal container card with subtle border */
    uint32_t card_w = cols * cell_w + 12u;
    uint32_t card_h = rows * cell_h + 10u;
    if (left >= 6u && top >= 5u && left - 6u + card_w <= width && top - 5u + card_h <= height) {
        fill(left - 6u, top - 5u, card_w, card_h, 0x19283bu); /* Border */
        fill(left - 5u, top - 4u, card_w - 2u, card_h - 2u, bg_term); /* Interior */
    } else {
        fill(left, top, cols * cell_w, rows * cell_h, bg_term);
    }

    /* 4. Bottom status bar */
    uint32_t foot_y = height >= (scale > 1u ? 28u : 20u) ? height - (scale > 1u ? 28u : 20u) : 0u;
    fill(0, foot_y, width, height - foot_y, bar_color);
    fill(0, foot_y, width, 1u, 0x1d2e45u);
    label(left > 12u ? left : 16u, foot_y + 3u,
          "Commands: help | drivers | disk | install | clear | mem | echo | exit",
          0x6885a0u);

    xcol = 0;
    yrow = 0;
    draw_cursor(1);
}

void intellect_terminalfb64_putc(char c) {
    if (!enabled) return;
    draw_cursor(0);

    if (c == '\r') {
        xcol = 0;
        draw_cursor(1);
        return;
    }
    if (c == '\b') {
        if (xcol) {
            --xcol;
            fill(left + xcol * cell_w, top + yrow * cell_h, cell_w, cell_h, bg_term);
        }
        draw_cursor(1);
        return;
    }
    if (c == '\n') {
        xcol = 0;
        ++yrow;
    } else if (c == '\t') {
        do { intellect_terminalfb64_putc(' '); } while (xcol % 4u);
        return;
    } else if (c >= 32 && c <= 126) {
        fill(left + xcol * cell_w, top + yrow * cell_h, cell_w, cell_h, bg_term);
        glyph(left + xcol * cell_w, top + yrow * cell_h + 1u, c, fg_text);
        if (++xcol >= cols) {
            xcol = 0;
            ++yrow;
        }
    }
    if (yrow >= rows) {
        scroll();
        yrow = rows - 1u;
    }
    draw_cursor(1);
}

void intellect_terminalfb64_write(const char *s, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        intellect_terminalfb64_putc(s[i]);
        intellect_serial64_putc(s[i]);
    }
}
