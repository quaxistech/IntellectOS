/* Minimal x86-64 exception foundation: 32 exception gates, three IST stacks,
 * GDT and 64-bit TSS. No IRQs, scheduler, user mode, PIC/APIC or recovery.
 */
#include <stddef.h>
#include <stdint.h>
#include "memcore64.h"

typedef struct __attribute__((packed)) {
    uint16_t offset_low, selector;
    uint8_t ist, type_attr;
    uint16_t offset_mid;
    uint32_t offset_high, zero;
} idt_gate_t;
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t address;
} descriptor_ptr_t;
typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0,rsp1,rsp2;
    uint64_t reserved1;
    uint64_t ist1,ist2,ist3,ist4,ist5,ist6,ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} tss64_t;
_Static_assert(sizeof(idt_gate_t)==16u,"IDT gate width");
_Static_assert(sizeof(descriptor_ptr_t)==10u,"Descriptor table pointer width");
_Static_assert(sizeof(tss64_t)==104u,"TSS64 width");

extern const int32_t intellect64_exception_offsets[32];
static uint64_t gdt[5] __attribute__((aligned(16)));
static idt_gate_t idt[32] __attribute__((aligned(16)));
static tss64_t tss __attribute__((aligned(16)));
static uint8_t df_stack[4096] __attribute__((aligned(16)));
static uint8_t pf_stack[4096] __attribute__((aligned(16)));
static uint8_t nmi_stack[4096] __attribute__((aligned(16)));

static void idt_set(unsigned vector, uintptr_t handler, uint8_t ist) {
    idt[vector] = (idt_gate_t){
        (uint16_t)handler, UINT16_C(0x08), ist, UINT8_C(0x8e),
        (uint16_t)(handler>>16), (uint32_t)(handler>>32), 0u
    };
}
void intellect64_idt_install(void) {
    /* 64-bit code selector 0x08, data selector 0x10, TSS selector 0x18. */
    gdt[0]=0;
    gdt[1]=UINT64_C(0x00AF9A000000FFFF);
    gdt[2]=UINT64_C(0x00CF92000000FFFF);
    uintptr_t base=(uintptr_t)&tss;
    const uint64_t limit = sizeof(tss)-1u;
    gdt[3] = (limit & UINT64_C(0xffff)) |
             ((uint64_t)(base & UINT64_C(0xffffff)) << 16) |
             (UINT64_C(0x89) << 40) |
             (((limit >> 16) & UINT64_C(0xf)) << 48) |
             (((uint64_t)(base >> 24) & UINT64_C(0xff)) << 56);
    gdt[4] = (uint64_t)(base >> 32);
    tss.ist1 = (uint64_t)(uintptr_t)(df_stack + sizeof(df_stack));
    tss.ist2 = (uint64_t)(uintptr_t)(pf_stack + sizeof(pf_stack));
    tss.ist3 = (uint64_t)(uintptr_t)(nmi_stack + sizeof(nmi_stack));
    tss.io_map_base = sizeof(tss);
    uintptr_t offset_base=(uintptr_t)&intellect64_exception_offsets[0];
    for (unsigned i=0;i<32u;++i) {
        uintptr_t handler=offset_base+(intptr_t)intellect64_exception_offsets[i];
        uint8_t ist = i==8u?1u:i==14u?2u:i==2u?3u:0u;
        idt_set(i,handler,ist);
    }
    /* M1_FIX_D2: this flat PIE kernel is relocated by physically copying its
     * bytes, without any ELF relocation processing.  A const initializer for
     * a descriptor-table pointer can become a link-address literal in .data!
     * Build both pseudo-descriptors at runtime using RIP-relative addresses.
     * Volatile stores prevent constant-folding into the linked image. */
    volatile descriptor_ptr_t gdtr;
    volatile descriptor_ptr_t idtr;
    gdtr.limit = (uint16_t)(sizeof(gdt) - 1u);
    gdtr.address = (uint64_t)(uintptr_t)gdt;
    idtr.limit = (uint16_t)(sizeof(idt) - 1u);
    idtr.address = (uint64_t)(uintptr_t)idt;
    /* M1_DIAG_D1: ensure CS is rebased to our GDT (selector 0x08).
     * Firmware CS 0x38 is outside this GDT.  PIE-safe far return.
     */
    __asm__ volatile(
        "cli\n\t"
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es"
        : : "m"(gdtr) : "rax", "memory");
    const uint16_t tr_selector=0x18u;
    __asm__ volatile("ltr %0" : : "r"(tr_selector) : "memory");
    __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}

__attribute__((noreturn)) void intellect64_exception_dispatch(const uint64_t *frame) {
    /* 15 saved GPRs, then vector, error, RIP, CS, RFLAGS.
     * Error-code vectors are 8, 10..14, 17, 21, 29, 30.
     */
    uint64_t cr2=0;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    intellect64_fault_report(frame[15],frame[16],frame[17],cr2);
}
