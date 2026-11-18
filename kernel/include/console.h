#ifndef INTELLECT_CONSOLE_H
#define INTELLECT_CONSOLE_H
#include <stdint.h>

void console_init(void);
void console_putc(char c);
void console_write(const char *text);
void console_hex32(uint32_t value);
void console_dec32(uint32_t value);
void console_newline(void);
#endif
