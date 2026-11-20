#ifndef INTELLECT_TERMINALFB64_H
#define INTELLECT_TERMINALFB64_H
#include <stddef.h>
#include <stdint.h>
#include "bootinfo.h"
int intellect_terminalfb64_init(const intellect_bootinfo_t *boot);
void intellect_terminalfb64_putc(char c);
void intellect_terminalfb64_write(const char *s, size_t len);
void intellect_terminalfb64_clear(void);
#endif
