#ifndef INTELLECT_USERTERMINAL64_H
#define INTELLECT_USERTERMINAL64_H
#include <stdint.h>
#include <stddef.h>
#include "bootinfo.h"
#define INTELLECT_USER64_BASE UINT64_C(0x0000008000000000)
#define INTELLECT_USER64_PAGE UINT64_C(4096)
#define INTELLECT_U1_SYS_WRITE 1u
#define INTELLECT_U1_SYS_READ 2u
#define INTELLECT_U1_SYS_CLEAR 3u
#define INTELLECT_U1_SYS_INFO 4u
#define INTELLECT_U1_SYS_EXIT 5u
/* Called only after PMM, VMM, M3 and native framebuffer are initialized.
 * Serial output is optional, PS/2 keyboard is fallback. This is a single
 * process, synchronous diagnostic kernel->user transition, NOT a scheduler.
 */
int intellect_u1_external_program_set(const uint8_t *code,size_t bytes);
int intellect_u1_terminal_run(const intellect_bootinfo_t *boot, uint64_t cr3);
int intellect_u1_user_range(uint64_t addr, uint64_t bytes);
void intellect64_user_tss_set_rsp0(uint64_t stack_top);
void intellect64_user_trap_install(void);
__attribute__((noreturn)) void intellect_u1_abort_fault(uint64_t vector, uint64_t error, uint64_t rip, uint64_t cr2);
#endif
