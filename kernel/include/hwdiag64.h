#ifndef INTELLECT_HWDIAG64_H
#define INTELLECT_HWDIAG64_H
#include "hardware64.h"
typedef void (*intellect_hwdiag64_emit_t)(void *ctx,char ch);
/* Returns 1 if command recognized, 0 for unsupported hw subcommand. */
int intellect_hwdiag64_command(const char *command,
                              intellect_hwdiag64_emit_t emit, void *ctx);
#endif
