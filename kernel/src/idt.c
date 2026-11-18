#include <stdint.h>
#include "idt.h"
#include "console.h"

struct __attribute__((packed)) gate {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t offset_hi;
};
struct __attribute__((packed)) idtr {
    uint16_t limit;
    uint32_t base;
};
struct __attribute__((packed)) exception_frame {
    uint32_t vector;
    uint32_t error_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
};
static struct gate gates[256];
extern void unexpected_isr(void);
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

static void set_gate(uint32_t vector, uintptr_t address) {
    gates[vector].offset_lo = (uint16_t)(address & 0xFFFFu);
    gates[vector].selector = 0x08u;
    gates[vector].zero = 0u;
    gates[vector].flags = 0x8Eu;
    gates[vector].offset_hi = (uint16_t)(address >> 16u);
}

void idt_init(void) {
    for (uint32_t i = 0u; i < 256u; ++i) {
        set_gate(i, (uintptr_t)&unexpected_isr);
    }
    set_gate(0u, (uintptr_t)&isr0);
    set_gate(1u, (uintptr_t)&isr1);
    set_gate(2u, (uintptr_t)&isr2);
    set_gate(3u, (uintptr_t)&isr3);
    set_gate(4u, (uintptr_t)&isr4);
    set_gate(5u, (uintptr_t)&isr5);
    set_gate(6u, (uintptr_t)&isr6);
    set_gate(7u, (uintptr_t)&isr7);
    set_gate(8u, (uintptr_t)&isr8);
    set_gate(9u, (uintptr_t)&isr9);
    set_gate(10u, (uintptr_t)&isr10);
    set_gate(11u, (uintptr_t)&isr11);
    set_gate(12u, (uintptr_t)&isr12);
    set_gate(13u, (uintptr_t)&isr13);
    set_gate(14u, (uintptr_t)&isr14);
    set_gate(15u, (uintptr_t)&isr15);
    set_gate(16u, (uintptr_t)&isr16);
    set_gate(17u, (uintptr_t)&isr17);
    set_gate(18u, (uintptr_t)&isr18);
    set_gate(19u, (uintptr_t)&isr19);
    set_gate(20u, (uintptr_t)&isr20);
    set_gate(21u, (uintptr_t)&isr21);
    set_gate(22u, (uintptr_t)&isr22);
    set_gate(23u, (uintptr_t)&isr23);
    set_gate(24u, (uintptr_t)&isr24);
    set_gate(25u, (uintptr_t)&isr25);
    set_gate(26u, (uintptr_t)&isr26);
    set_gate(27u, (uintptr_t)&isr27);
    set_gate(28u, (uintptr_t)&isr28);
    set_gate(29u, (uintptr_t)&isr29);
    set_gate(30u, (uintptr_t)&isr30);
    set_gate(31u, (uintptr_t)&isr31);

    const struct idtr r = {(uint16_t)(sizeof gates - 1u), (uint32_t)(uintptr_t)gates};
    __asm__ volatile("lidt %0" : : "m"(r) : "memory");
}

__attribute__((noreturn))
void kernel_panic_exception(const struct exception_frame *f) {
    console_write("\nFATAL CPU EXCEPTION #");
    console_dec32(f->vector);
    console_write(" error=");
    console_hex32(f->error_code);
    console_write(" EIP=");
    console_hex32(f->eip);
    console_write("\nHALTED - interrupts masked.\n");
    for (;;) __asm__ volatile("cli; hlt");
}
