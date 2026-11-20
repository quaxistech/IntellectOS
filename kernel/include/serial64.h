#ifndef INTELLECT_SERIAL64_H
#define INTELLECT_SERIAL64_H

/* Early x86-64 COM1: polling only, not an IRQ driver or a TTY. */
int intellect_serial64_init(void);
void intellect_serial64_putc(char ch);
int intellect_serial64_getc_nonblocking(void); /* -1 when no data */

#endif
