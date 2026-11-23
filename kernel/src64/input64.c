/* Polled x86 i8042 scan-set-1 and scan-set-2 keyboard + COM1 serial fallback.
 * Supports native PS/2 and USB keyboards via BIOS SMM legacy emulation.
 * Interrupts are not enabled in U1; polling handles non-blocking and blocking read.
 */
#include <stdint.h>
#include "serial64.h"
#include "input64.h"
#include "hardware64.h"
#include "xhci64.h"

static unsigned shift_pressed;
static unsigned extended;
static unsigned set2_break;
static unsigned active_set = 1u;
static int input_initialized = 0;

#ifdef INTELLECT_INPUT64_HOST_TEST
uint8_t intellect_input64_mock_inb(uint16_t port);
void intellect_input64_mock_outb(uint16_t port, uint8_t val);
#define inb(p) intellect_input64_mock_inb(p)
#define outb(p, v) intellect_input64_mock_outb(p, v)
#else
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
#endif

static inline void io_wait(void) {
    outb(0x80u, 0u);
}

static int i8042_wait_write(void) {
    for (unsigned timeout = 100000u; timeout > 0u; --timeout) {
        if ((inb(0x64u) & 0x02u) == 0u) return 1;
        io_wait();
    }
    return 0;
}

static int i8042_wait_read(void) {
    for (unsigned timeout = 100000u; timeout > 0u; --timeout) {
        if (inb(0x64u) & 0x01u) return 1;
        io_wait();
    }
    return 0;
}

static void i8042_flush(void) {
    for (unsigned i = 0u; i < 32u; ++i) {
        uint8_t status = inb(0x64u);
        if (status == 0xFFu || !(status & 0x01u)) break;
        (void)inb(0x60u);
        io_wait();
    }
}

void intellect_input64_init(void) {
    input_initialized = 1;
#ifndef INTELLECT_INPUT64_HOST_TEST
    (void)intellect_xhci64_init(intellect_hw64_snapshot());
#endif
    uint8_t status = inb(0x64u);
    if (status == 0xFFu) {
        /* Floating bus: pure UEFI or no i8042 controller */
        return;
    }

    /* 1. Flush any stale bytes */
    i8042_flush();

    /* 2. Enable first PS/2 port (keyboard) */
    if (i8042_wait_write()) {
        outb(0x64u, 0xAEu);
        io_wait();
    }

    /* 3. Enable second PS/2 port (some SMM emulations map USB to AUX) */
    if (i8042_wait_write()) {
        outb(0x64u, 0xA8u);
        io_wait();
    }

    /* 4. Read Controller Configuration Byte */
    uint8_t cfg = 0u;
    if (i8042_wait_write()) {
        outb(0x64u, 0x20u);
        io_wait();
        if (i8042_wait_read()) {
            cfg = inb(0x60u);
        }
    }

    /* 5. Update configuration:
     * Enable clocks (clear Bit 4 and Bit 5).
     * Keep IRQ1 enabled (Bit 0) for BIOS SMM legacy USB emulation.
     * Enable translation (Bit 6 = 1) if supported.
     */
    cfg |= (1u << 0) | (1u << 6);
    cfg &= ~((1u << 4) | (1u << 5));

    if (i8042_wait_write()) {
        outb(0x64u, 0x60u);
        io_wait();
        if (i8042_wait_write()) {
            outb(0x60u, cfg);
            io_wait();
        }
    }

    /* 6. Verify whether translation bit was accepted */
    if (i8042_wait_write()) {
        outb(0x64u, 0x20u);
        io_wait();
        if (i8042_wait_read()) {
            cfg = inb(0x60u);
            if (!(cfg & (1u << 6))) {
                /* Controller does not translate, native Set 2 */
                active_set = 2u;
            } else {
                active_set = 1u;
            }
        }
    }

    i8042_flush();
}

static const char keymap_set1[128] = {
    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
    [0x07]='6',[0x08]='7',[0x09]='8',[0x0a]='9',[0x0b]='0',
    [0x0c]='-',[0x0d]='=',[0x0e]='\b',[0x0f]='\t',
    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',
    [0x15]='y',[0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',
    [0x1a]='[',[0x1b]=']',[0x1c]='\n',
    [0x1e]='a',[0x1f]='s',[0x20]='d',[0x21]='f',[0x22]='g',
    [0x23]='h',[0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',
    [0x28]='\'',[0x29]='`',[0x2b]='\\',
    [0x2c]='z',[0x2d]='x',[0x2e]='c',[0x2f]='v',[0x30]='b',
    [0x31]='n',[0x32]='m',[0x33]=',',[0x34]='.',[0x35]='/',
    [0x39]=' '
};

static const char keymap_set2[128] = {
    [0x0d] = '\t', [0x0e] = '`',
    [0x15] = 'q', [0x16] = '1', [0x1a] = 'z', [0x1b] = 's', [0x1c] = 'a',
    [0x1d] = 'w', [0x1e] = '2',
    [0x21] = 'c', [0x22] = 'x', [0x23] = 'd', [0x24] = 'e', [0x25] = '4',
    [0x26] = '3', [0x29] = ' ', [0x2a] = 'v', [0x2b] = 'f', [0x2c] = 't',
    [0x2d] = 'r', [0x2e] = '5',
    [0x31] = 'n', [0x32] = 'b', [0x33] = 'h', [0x34] = 'g', [0x35] = 'y',
    [0x36] = '6', [0x3a] = 'm', [0x3b] = 'j', [0x3c] = 'u', [0x3d] = '7',
    [0x3e] = '8',
    [0x41] = ',', [0x42] = 'k', [0x43] = 'i', [0x44] = 'o', [0x45] = '0',
    [0x46] = '9', [0x49] = '.', [0x4a] = '/', [0x4b] = 'l', [0x4c] = ';',
    [0x4d] = 'p', [0x4e] = '-',
    [0x52] = '\'', [0x54] = '[', [0x55] = '=', [0x5a] = '\n', [0x5b] = ']',
    [0x5d] = '\\', [0x66] = '\b'
};

static char shifted(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    switch (c) {
      case '1': return '!'; case '2': return '@'; case '3': return '#';
      case '4': return '$'; case '5': return '%'; case '6': return '^';
      case '7': return '&'; case '8': return '*'; case '9': return '(';
      case '0': return ')'; case '-': return '_'; case '=': return '+';
      case '[': return '{'; case ']': return '}'; case ';': return ':';
      case '\'': return '"'; case ',': return '<'; case '.': return '>';
      case '/': return '?'; case '`': return '~'; case '\\': return '|';
      default: return c;
    }
}

int intellect_input64_poll_char(void) {
    if (!input_initialized) {
        intellect_input64_init();
    }
    const int serial = intellect_serial64_getc_nonblocking();
    if (serial >= 0) return serial == '\r' ? '\n' : serial;

#ifndef INTELLECT_INPUT64_HOST_TEST
    const int usb_char = intellect_xhci64_poll_char();
    if (usb_char >= 0) return usb_char;
#endif

    const uint8_t status = inb(0x64u);
    if (status == 0xFFu || !(status & 0x01u)) return -1;

    const uint8_t sc = inb(0x60u);
    if (sc == 0u || sc == 0xFFu) return -1;

    /* Extended scancode prefixes (0xE0, 0xE1) */
    if (sc == 0xE0u || sc == 0xE1u) {
        extended = 1u;
        return -1;
    }

    /* Scan Code Set 2 Break code prefix (0xF0) */
    if (sc == 0xF0u) {
        active_set = 2u;
        set2_break = 1u;
        return -1;
    }

    if (set2_break) {
        set2_break = 0u;
        extended = 0u;
        if (sc == 0x12u || sc == 0x59u) {
            shift_pressed = 0u;
        }
        return -1;
    }

    if (extended) {
        extended = 0u;
        /* Keypad enter in Set 1 (0x1C) / Set 2 (0x5A) */
        if (sc == 0x1Cu || sc == 0x5Au) return '\n';
        return -1;
    }

    /* Distinctive Set 2 scancodes switch active decoder to Set 2 */
    if (sc == 0x5Au || sc == 0x66u) {
        active_set = 2u;
    }

    if (active_set == 2u) {
        if (sc == 0x12u || sc == 0x59u) {
            shift_pressed = 1u;
            return -1;
        }
        if (sc < 128u) {
            char c = keymap_set2[sc];
            if (shift_pressed) c = shifted(c);
            return c ? (int)(unsigned char)c : -1;
        }
        return -1;
    }

    /* Scan Code Set 1 */
    const uint8_t key = (uint8_t)(sc & 0x7Fu);
    if (key == 0x2Au || key == 0x36u) {
        shift_pressed = (sc & 0x80u) ? 0u : 1u;
        return -1;
    }
    if (sc & 0x80u) return -1; /* Break code in Set 1 */

    char c = keymap_set1[key];
    if (shift_pressed) c = shifted(c);
    return c ? (int)(unsigned char)c : -1;
}

int intellect_input64_read_char(void) {
    for (;;) {
        const int c = intellect_input64_poll_char();
        if (c >= 0) return c;
        io_wait();
        __asm__ volatile("pause");
    }
}
