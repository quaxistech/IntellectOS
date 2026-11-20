/* IntellectOS C1: bounded, polling 16550-compatible COM1 driver.
 * No UEFI services, heap, IRQ, syscalls or page faults are required.
 * This driver is deliberately not used before Memory Core M1 passes.
 */
#include <stdint.h>
#include "serial64.h"

#define COM1 UINT16_C(0x3f8)
#define UART_LSR_DATA UINT8_C(0x01)
#define UART_LSR_THR_EMPTY UINT8_C(0x20)
#define UART_POLL_LIMIT 200000u

static int uart_ready;

static inline void io_out(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t io_in(uint16_t port) {
    uint8_t result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

int intellect_serial64_init(void) {
    /* Probe scratch register first: unmapped I/O normally reads all ones.
     * Do not attach a monitor to an absent/non-compatible COM1.
     */
    uint8_t original=io_in(COM1 + 7u);
    io_out(COM1 + 7u, UINT8_C(0x5a));
    if (io_in(COM1 + 7u)!=UINT8_C(0x5a)) return 0;
    io_out(COM1 + 7u, UINT8_C(0xa5));
    if (io_in(COM1 + 7u)!=UINT8_C(0xa5)) {
        io_out(COM1 + 7u, original);
        return 0;
    }
    io_out(COM1 + 7u, original);
    io_out(COM1 + 1u, 0u);    /* disable UART interrupts */
    io_out(COM1 + 3u, 0x80u); /* DLAB */
    io_out(COM1 + 0u, 3u);    /* divisor=3 => 38400 baud */
    io_out(COM1 + 1u, 0u);
    io_out(COM1 + 3u, 3u);    /* 8N1 */
    io_out(COM1 + 2u, 0xc7u); /* FIFO */
    io_out(COM1 + 4u, 0x03u); /* DTR, RTS; polling, no OUT2 needed */
    uart_ready=1;
    return 1;
}

static void write_raw(char ch) {
    if (!uart_ready) return;
    for (uint32_t i=0; i<UART_POLL_LIMIT; ++i) {
        if (io_in(COM1 + 5u) & UART_LSR_THR_EMPTY) {
            io_out(COM1, (uint8_t)ch);
            return;
        }
        __asm__ volatile("pause");
    }
    /* Mark UART unavailable rather than spinning forever. */
    uart_ready=0;
}
void intellect_serial64_putc(char ch) {
    if (ch=='\n') write_raw('\r');
    write_raw(ch);
}
int intellect_serial64_getc_nonblocking(void) {
    if (!uart_ready) return -1;
    if ((io_in(COM1 + 5u) & UART_LSR_DATA)==0u) return -1;
    return (int)io_in(COM1);
}
