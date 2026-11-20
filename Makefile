# Intellect OS BIOS foundation 0.4 — freestanding Legacy BIOS -> i386 protected-mode kernel.
# Build and QEMU only; this Makefile NEVER writes to physical disks.
SHELL := /bin/bash
CC := gcc
AS := as
LD := ld
OBJCOPY := objcopy
PYTHON := python3
QEMU := qemu-system-i386
BUILD := build

CFLAGS := -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -Werror \
          -fno-pic -fno-pie -fno-stack-protector -fno-builtin \
          -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -fno-tree-loop-distribute-patterns -fno-omit-frame-pointer \
          -Ikernel/include
LINKFLAGS := -m elf_i386 --build-id=none -z max-page-size=0x1000 \
             -nostdlib -T kernel/linker.ld
KERNEL_C := console cpu idt kernel pci serial
KERNEL_OBJS := $(addprefix $(BUILD)/kernel/,$(addsuffix .o,$(KERNEL_C)))
KERNEL_ASM := $(BUILD)/kernel/start.o $(BUILD)/kernel/exceptions.o

.PHONY: all check bios-check image run run-headless clean help
all: image uefi-image

$(BUILD)/boot $(BUILD)/kernel:
	mkdir -p $@

$(BUILD)/boot/mbr.o: boot/bios/mbr.S | $(BUILD)/boot
	$(AS) --32 -o $@ $<
$(BUILD)/boot/stage1.o: boot/bios/stage1.S | $(BUILD)/boot
	$(AS) --32 -o $@ $<
$(BUILD)/boot/%.bin: $(BUILD)/boot/%.o
	$(LD) -m elf_i386 -nostdlib -Ttext $(if $(findstring mbr,$*),0x7c00,0x8000) --oformat binary -o $@ $<

$(BUILD)/kernel/%.o: kernel/src/%.c $(wildcard kernel/include/*.h) | $(BUILD)/kernel
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/kernel/start.o: kernel/i386/start.S | $(BUILD)/kernel
	$(AS) --32 -o $@ $<
$(BUILD)/kernel/exceptions.o: kernel/i386/exceptions.S | $(BUILD)/kernel
	$(AS) --32 -o $@ $<
$(BUILD)/kernel.elf: $(KERNEL_ASM) $(KERNEL_OBJS) kernel/linker.ld
	$(LD) $(LINKFLAGS) -o $@ $(KERNEL_ASM) $(KERNEL_OBJS)
$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/intellectos-bios.img: $(BUILD)/boot/mbr.bin $(BUILD)/boot/stage1.bin $(BUILD)/kernel.bin tools/mkimage.py
	$(PYTHON) tools/mkimage.py $(BUILD)/boot/mbr.bin $(BUILD)/boot/stage1.bin $(BUILD)/kernel.bin $@

image: $(BUILD)/intellectos-bios.img
	@echo 'READY: $(BUILD)/intellectos-bios.img'
bios-check: image
	$(PYTHON) tools/verify.py $(BUILD)/intellectos-bios.img $(BUILD)/boot/stage1.bin $(BUILD)/kernel.bin $(BUILD)/kernel.elf

run: bios-check
	$(QEMU) -m 256M -drive format=raw,file=$(BUILD)/intellectos-bios.img,if=ide -boot c -serial stdio -monitor none -no-reboot
run-headless: bios-check
	$(QEMU) -m 256M -drive format=raw,file=$(BUILD)/intellectos-bios.img,if=ide -boot c -display none -serial stdio -monitor none -no-reboot
clean:
	rm -rf $(BUILD)
help:
	@echo 'make check         Compile and test both BIOS and UEFI Stage E (non-destructive)'
	@echo 'make bios-check    Validate unchanged BIOS 0.4 path'
	@echo 'make uefi-e-run    Boot native Stage E kernel in QEMU/OVMF'
	@echo 'make run           Boot in QEMU with VGA window and COM1 in terminal'
	@echo 'make run-headless  QEMU with COM1 output in terminal'
	@echo 'make image         Produce raw BIOS USB-HDD image'
	@echo 'make clean         Delete build outputs'

# UEFI Stage E: independent x64 kernel embedded in PE32+ EFI loader.
# BIOS image, check and run targets remain unchanged.
UEFI_CC := clang
UEFI_LD := lld-link
UEFI_IMG := $(BUILD)/intellectos-uefi-stage-e.img
UEFI_EFI := $(BUILD)/uefi/BOOTX64.EFI

.PHONY: uefi-image uefi-check uefi-run uefi-e-check uefi-e-run
$(BUILD)/uefi:
	mkdir -p $@
$(BUILD)/uefi/%.obj: boot/uefi/%.c boot/uefi/efi_min.h boot/uefi/firmware_info.h boot/uefi/platform_probe.h boot/uefi/bootinfo_builder.h boot/uefi/handoff.h boot/uefi/framebuffer_console.h kernel/src64/font5x7.h kernel/include/bootinfo.h kernel/include/memcore64.h | $(BUILD)/uefi
	$(UEFI_CC) --target=x86_64-pc-win32-coff -std=c11 -ffreestanding -fshort-wchar \
	  -fno-stack-protector -fno-builtin -mno-red-zone -O2 -Wall -Wextra -Werror \
	  -c $< -o $@
# Separate native x86-64 kernel, no EFI imports and no dynamic relocations.
K64_CC := clang
K64_LD := ld.lld
K64_OBJCOPY := llvm-objcopy
K64_FLAGS := --target=x86_64-unknown-none-elf -std=c11 -ffreestanding -fpie \
              -fno-stack-protector -fno-builtin -mno-red-zone -mgeneral-regs-only -mcmodel=small \
              -O2 -Wall -Wextra -Werror -fno-asynchronous-unwind-tables \
              -fno-unwind-tables -Ikernel/include
$(BUILD)/kernel64:
	mkdir -p $@
$(BUILD)/kernel64/entry.o: kernel/x86_64/entry.S | $(BUILD)/kernel64
	$(K64_CC) --target=x86_64-unknown-none-elf -c $< -o $@
$(BUILD)/kernel64/kernel.o: kernel/src64/kernel.c kernel/src64/font5x7.h kernel/include/bootinfo.h kernel/include/pmm64.h kernel/include/memcore64.h kernel/include/kheap64.h kernel/include/vmm64.h | $(BUILD)/kernel64
	$(K64_CC) $(K64_FLAGS) -c $< -o $@
MEMCORE64_OBJS := $(addprefix $(BUILD)/kernel64/,$(addsuffix .o,pmm64 memcore64 exceptions64 vmm64 kheap64))
CONSOLE64_OBJS := $(BUILD)/kernel64/serial64.o $(BUILD)/kernel64/kmonitor64.o
$(BUILD)/kernel64/%.o: kernel/src64/%.c kernel/include/bootinfo.h kernel/include/pmm64.h kernel/include/memcore64.h kernel/include/kheap64.h kernel/include/vmm64.h | $(BUILD)/kernel64
	$(K64_CC) $(K64_FLAGS) -c $< -o $@
$(BUILD)/kernel64/exceptions_asm.o: kernel/x86_64/exceptions64.S | $(BUILD)/kernel64
	$(K64_CC) --target=x86_64-unknown-none-elf -c $< -o $@
$(BUILD)/kernel64.elf: $(BUILD)/kernel64/entry.o $(BUILD)/kernel64/kernel.o $(MEMCORE64_OBJS) $(BUILD)/kernel64/exceptions_asm.o kernel/x86_64/linker.ld $(CONSOLE64_OBJS)
	$(K64_LD) -m elf_x86_64 -nostdlib --build-id=none -T kernel/x86_64/linker.ld -o $@ $(BUILD)/kernel64/entry.o $(BUILD)/kernel64/kernel.o $(MEMCORE64_OBJS) $(BUILD)/kernel64/exceptions_asm.o $(CONSOLE64_OBJS)
$(BUILD)/kernel64.bin: $(BUILD)/kernel64.elf
	$(K64_OBJCOPY) -O binary $< $@
	$(PYTHON) tools/verify_kernel64.py $(BUILD)/kernel64.elf $@
$(BUILD)/uefi/kernel_embed.obj: boot/uefi/kernel_embed.S $(BUILD)/kernel64.bin | $(BUILD)/uefi
	$(UEFI_CC) --target=x86_64-pc-win32-coff -c $< -o $@
$(BUILD)/uefi/handoff_asm.obj: boot/uefi/handoff.S | $(BUILD)/uefi
	$(UEFI_CC) --target=x86_64-pc-win32-coff -c $< -o $@
$(UEFI_EFI): $(BUILD)/uefi/start.obj $(BUILD)/uefi/firmware_info.obj $(BUILD)/uefi/platform_probe.obj $(BUILD)/uefi/bootinfo_builder.obj $(BUILD)/uefi/framebuffer_console.obj $(BUILD)/uefi/handoff.obj $(BUILD)/uefi/handoff_asm.obj $(BUILD)/uefi/kernel_embed.obj
	$(UEFI_LD) /machine:x64 /subsystem:efi_application /entry:efi_main /nodefaultlib \
	  /dll /timestamp:0 /out:$@ $^
$(UEFI_IMG): $(BUILD)/intellectos-bios.img $(UEFI_EFI) tools/add_uefi_fat16.py
	$(PYTHON) tools/add_uefi_fat16.py $(BUILD)/intellectos-bios.img $(UEFI_EFI) $@
uefi-image: $(UEFI_IMG)
	@echo 'READY: $(UEFI_IMG)'
uefi-check: uefi-image bios-check
	$(PYTHON) tools/verify_uefi_image.py $(BUILD)/intellectos-bios.img $(UEFI_EFI) $(UEFI_IMG)
	$(PYTHON) tools/verify_kernel64.py $(BUILD)/kernel64.elf $(BUILD)/kernel64.bin
	$(PYTHON) -m unittest discover -s tests -v
check: uefi-check
uefi-run: uefi-check
	@code=''; vars=''; \
	if [[ -f /usr/share/OVMF/OVMF_CODE_4M.fd && -f /usr/share/OVMF/OVMF_VARS_4M.fd ]]; then \
	  code=/usr/share/OVMF/OVMF_CODE_4M.fd; vars=/usr/share/OVMF/OVMF_VARS_4M.fd; \
	elif [[ -f /usr/share/OVMF/OVMF_CODE.fd && -f /usr/share/OVMF/OVMF_VARS.fd ]]; then \
	  code=/usr/share/OVMF/OVMF_CODE.fd; vars=/usr/share/OVMF/OVMF_VARS.fd; \
	else echo 'Missing OVMF files: install ovmf'; exit 1; fi; \
	cp "$$vars" $(BUILD)/uefi/OVMF_VARS_TEST.fd; \
	qemu-system-x86_64 -m 256M \
	  -drive if=pflash,format=raw,readonly=on,file="$$code" \
	  -drive if=pflash,format=raw,file=$(BUILD)/uefi/OVMF_VARS_TEST.fd \
	  -drive if=ide,format=raw,file=$(UEFI_IMG) \
	  -net none -monitor none -no-reboot

uefi-e-check: uefi-check
uefi-e-run: uefi-run

# IntellectOS C1 Console Foundation
.PHONY: uefi-console uefi-console-gui
uefi-console: uefi-check
	@bash tools/run_uefi_console.sh
uefi-console-gui: uefi-check
	@bash tools/run_uefi_console.sh --gui
