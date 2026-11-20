#ifndef INTELLECT_KMONITOR64_H
#define INTELLECT_KMONITOR64_H
#include <stddef.h>
#include <stdint.h>
#include "bootinfo.h"

typedef void (*intellect_monitor64_output_t)(void *ctx, char ch);
typedef struct {
    const intellect_bootinfo_t *boot;
    intellect_monitor64_output_t output;
    void *ctx;
    char line[128];
    uint16_t length;
    uint8_t stop_requested;
    uint8_t last_was_cr;
    uint8_t dropping_line;
} intellect_monitor64_t;

/* Pure command parser, host-testable without privileged I/O. */
void intellect_monitor64_start(intellect_monitor64_t *m,
                              const intellect_bootinfo_t *boot,
                              intellect_monitor64_output_t output, void *ctx);
void intellect_monitor64_feed(intellect_monitor64_t *m, char ch);

/* Early kernel mode: serial is the only input transport; returns on halt.
 * False means the COM1 UART could not be detected.
 */
int intellect_monitor64_run(const intellect_bootinfo_t *boot);
#endif
