#ifndef INTELLECT_MEMCORE64_H
#define INTELLECT_MEMCORE64_H
#include <stdint.h>
#include "bootinfo.h"
/* Native UEFI Stage E register handoff. No change to BootInfo v1 or EFI ABI. */
#define INTELLECT_KERNEL64_PAGES 128u
#define INTELLECT_STACK64_PAGES 16u
#define INTELLECT_TABLE64_PAGES 8u
#define INTELLECT_BOOTINFO64_PAGES 64u

int intellect64_memory_core_start(const intellect_bootinfo_t *boot,
                                 uint64_t bootstrap_cr3,
                                 uint64_t stack_top,
                                 uint64_t kernel_base);
/* Crash callback owned by kernel framebuffer console. Must never return. */
__attribute__((noreturn)) void intellect64_fault_report(uint64_t vector,
    uint64_t error, uint64_t rip, uint64_t cr2);
#endif
