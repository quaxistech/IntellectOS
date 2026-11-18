#ifndef INTELLECT_UEFI_FRAMEBUFFER_CONSOLE_H
#define INTELLECT_UEFI_FRAMEBUFFER_CONSOLE_H
#include "bootinfo_builder.h"
/* Diagnostic raster output using framebuffer only, BEFORE ExitBootServices.
 * Returns zero if the discovered GOP mode is not compatible with this driver.
 */
int intellect_uefi_draw_pre_exit(const intellect_uefi_bootinfo_snapshot_t *snapshot);
/* Raster-only diagnostic on a failed transition, even if firmware is partially
 * shut down. Codes identify the failing stage without ConsoleOut access.
 */
void intellect_uefi_draw_failure(const intellect_uefi_bootinfo_snapshot_t *snapshot,
                                  unsigned code);
#endif
