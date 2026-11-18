#include <stdint.h>
#include "cpu.h"
#include "console.h"

static void cpuid(uint32_t leaf, uint32_t subleaf,
                  uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(subleaf));
}

static void write_le32(char *dst, uint32_t value) {
    dst[0] = (char)(value & 0xffu);
    dst[1] = (char)((value >> 8) & 0xffu);
    dst[2] = (char)((value >> 16) & 0xffu);
    dst[3] = (char)((value >> 24) & 0xffu);
}

void cpu_report(void) {
    uint32_t a, b, c, d;
    cpuid(0, 0, &a, &b, &c, &d);
    uint32_t max_basic = a;
    char vendor[13];
    write_le32(vendor, b);
    write_le32(vendor + 4, d);
    write_le32(vendor + 8, c);
    vendor[12] = '\0';
    console_write("CPU vendor : ");
    console_write(vendor);
    console_newline();

    cpuid(0x80000000u, 0, &a, &b, &c, &d);
    if (a >= 0x80000004u) {
        char brand[49];
        for (uint32_t i = 0; i < 3u; ++i) {
            cpuid(0x80000002u + i, 0, &a, &b, &c, &d);
            write_le32(brand + i * 16u + 0u, a);
            write_le32(brand + i * 16u + 4u, b);
            write_le32(brand + i * 16u + 8u, c);
            write_le32(brand + i * 16u + 12u, d);
        }
        brand[48] = '\0';
        console_write("CPU model  : ");
        console_write(brand);
        console_newline();
    }
    if (max_basic >= 1u) {
        cpuid(1, 0, &a, &b, &c, &d);
        uint32_t base_family = (a >> 8) & 15u;
        uint32_t family = base_family;
        uint32_t model = (a >> 4) & 15u;
        if (base_family == 15u) family += (a >> 20) & 255u;
        if (base_family == 6u || base_family == 15u) model += ((a >> 16) & 15u) << 4;
        console_write("CPU family: ");
        console_dec32(family);
        console_write("  model: ");
        console_dec32(model);
        console_newline();
        uint32_t logical_hint = (b >> 16) & 255u;
        console_write("Logical CPU/package hint: ");
        if (logical_hint != 0u) console_dec32(logical_hint);
        else console_write("not reported");
        console_newline();
    }
    console_write("Execution : one boot CPU, 32-bit protected mode\n");
}
