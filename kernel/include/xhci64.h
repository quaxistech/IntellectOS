#ifndef INTELLECT_XHCI64_H
#define INTELLECT_XHCI64_H
#include <stdint.h>
#include <stddef.h>
#include "hardware64.h"

/* Polled freestanding xHCI USB HID Keyboard Driver.
 * Discovers xHCI via PCI, performs BIOS handoff, initializes controller,
 * discovers USB keyboards on root hub ports, and polls HID reports.
 * Fully freestanding, zero heap, zero IRQ dependency.
 */
int intellect_xhci64_init(const intellect_hw64_snapshot_t *hw);
int intellect_xhci64_poll_char(void);
int intellect_xhci64_is_ready(void);

#endif
