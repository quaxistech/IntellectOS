#include "console.h"
#include "serial.h"

#define VGA_WIDTH 80u
#define VGA_HEIGHT 25u
#define VGA_ATTRIBUTE 0x0Fu
static volatile uint16_t *const VGA_BUFFER = (volatile uint16_t *)0xB8000u;
static uint32_t current_row;
static uint32_t current_col;

static uint16_t vga_cell(char c) {
    return (uint16_t)((uint16_t)(uint8_t)c | (uint16_t)(VGA_ATTRIBUTE << 8));
}

static void scroll_if_needed(void) {
    if (current_row < VGA_HEIGHT) return;
    for (uint32_t row = 1; row < VGA_HEIGHT; ++row) {
        for (uint32_t col = 0; col < VGA_WIDTH; ++col) {
            VGA_BUFFER[(row - 1u) * VGA_WIDTH + col] =
                VGA_BUFFER[row * VGA_WIDTH + col];
        }
    }
    for (uint32_t col = 0; col < VGA_WIDTH; ++col) {
        VGA_BUFFER[(VGA_HEIGHT - 1u) * VGA_WIDTH + col] = vga_cell(' ');
    }
    current_row = VGA_HEIGHT - 1u;
}

void console_init(void) {
    current_row = 0;
    current_col = 0;
    serial_init();
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) {
        VGA_BUFFER[i] = vga_cell(' ');
    }
}

void console_putc(char c) {
    serial_putc(c);
    if (c == '\r') { current_col = 0; return; }
    if (c == '\n') {
        current_col = 0;
        ++current_row;
        scroll_if_needed();
        return;
    }
    if (c == '\t') {
        do { console_putc(' '); } while ((current_col % 4u) != 0u);
        return;
    }
    if ((unsigned char)c < 32u || (unsigned char)c > 126u) c = '?';
    VGA_BUFFER[current_row * VGA_WIDTH + current_col] = vga_cell(c);
    ++current_col;
    if (current_col >= VGA_WIDTH) {
        current_col = 0;
        ++current_row;
        scroll_if_needed();
    }
}

void console_write(const char *text) {
    if (text == 0) return;
    while (*text) console_putc(*text++);
}

void console_newline(void) { console_putc('\n'); }

void console_hex32(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    console_write("0x");
    for (int i = 7; i >= 0; --i) {
        console_putc(digits[(value >> (i * 4)) & 15u]);
    }
}

void console_dec32(uint32_t value) {
    char buf[11];
    uint32_t n = 0;
    do {
        buf[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (n != 0u) console_putc(buf[--n]);
}
