#ifndef INTELLECT_DISKDIAG64_H
#define INTELLECT_DISKDIAG64_H
#include <stddef.h>
#include <stdint.h>
typedef void (*intellect_diskdiag64_emit_t)(void *,char);
int intellect_diskdiag64_command(const char *command,
                                intellect_diskdiag64_emit_t emit,void *ctx);
#endif
