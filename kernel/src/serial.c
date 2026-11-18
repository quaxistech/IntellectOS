#include "serial.h"
#include "ports.h"

#define COM1 0x3F8u

void serial_init(void) {
    outb(COM1 + 1, 0x00); /* Disable UART interrupts. */
    outb(COM1 + 3, 0x80); /* Enable divisor latch. */
    outb(COM1 + 0, 0x03); /* Divisor = 3 (38400 baud). */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8 bits, no parity, one stop bit. */
    outb(COM1 + 2, 0xC7); /* FIFO. */
    outb(COM1 + 4, 0x0B); /* DTR, RTS, OUT2. */
}

static void serial_write_raw(char c) {
    /* Bounded polling: absent/nonstandard COM1 must not hang the kernel. */
    for (uint32_t attempts = 0; attempts < 10000u; ++attempts) {
        if ((inb(COM1 + 5) & 0x20u) != 0u) {
            outb(COM1, (uint8_t)c);
            return;
        }
    }
}

void serial_putc(char c) {
    if (c == '\n') serial_write_raw('\r');
    serial_write_raw(c);
}
