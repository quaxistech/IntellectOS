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
	@echo 'make check         Compile and test both BIOS and UEFI Stage E and host tests'
	@echo 'make bios-check    Validate unchanged BIOS 0.4 path'
	@echo 'make uefi-e-run    Boot native Stage E kernel in QEMU/OVMF'
	@echo 'make uefi-u1-smoke Boot U1 Ring3 terminal in QEMU/OVMF'
	@echo 'make uefi-live-image Produce 32 MiB live USB raw image'
	@echo 'make uefi-gpt-image Produce 1 GiB GPT+FAT32 preinstall image'
	@echo 'make uefi-ifs-image Produce 1 GiB GPT+FAT32+IFS installed image'
	@echo 'make uefi-ifs-smoke Boot QEMU with IFS verified root + terminal'
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
              -fno-unwind-tables -Ikernel/include -I$(BUILD)/kernel64
$(BUILD)/kernel64:
	mkdir -p $@
$(BUILD)/kernel64/entry.o: kernel/x86_64/entry.S | $(BUILD)/kernel64
	$(K64_CC) --target=x86_64-unknown-none-elf -c $< -o $@
$(BUILD)/kernel64/kernel.o: kernel/src64/kernel.c kernel/src64/font5x7.h kernel/include/bootinfo.h kernel/include/pmm64.h kernel/include/memcore64.h kernel/include/kheap64.h kernel/include/vmm64.h | $(BUILD)/kernel64
	$(K64_CC) $(K64_FLAGS) -c $< -o $@
MEMCORE64_OBJS := $(addprefix $(BUILD)/kernel64/,$(addsuffix .o,pmm64 memcore64 exceptions64 vmm64 kheap64))
CONSOLE64_OBJS := $(BUILD)/kernel64/serial64.o $(BUILD)/kernel64/kmonitor64.o
M3A_OBJS := $(BUILD)/kernel64/m3policy64.o $(BUILD)/kernel64/m3protect64.o
H1H2_OBJS := $(BUILD)/kernel64/hardware64.o $(BUILD)/kernel64/driver64.o $(BUILD)/kernel64/hwdiag64.o
S0_OBJS := $(BUILD)/kernel64/ata_pio64.o $(BUILD)/kernel64/block64.o $(BUILD)/kernel64/partition64.o $(BUILD)/kernel64/diskdiag64.o
WELCOME64_OBJS := $(BUILD)/kernel64/welcome64.o
I2_OBJS := $(BUILD)/kernel64/fat32_vfs64.o
IFS1_OBJS := $(BUILD)/kernel64/ifs_ro64.o $(BUILD)/kernel64/ifs_blake3_v1.o $(BUILD)/kernel64/ifs_exec64.o
U1_OBJS := $(addprefix $(BUILD)/kernel64/,$(addsuffix .o,userterminal64 userpolicy64 terminalfb64 input64)) $(BUILD)/kernel64/usergate64.o $(BUILD)/kernel64/user_terminal_embed.o
$(BUILD)/kernel64/%.o: kernel/src64/%.c kernel/include/bootinfo.h kernel/include/pmm64.h kernel/include/memcore64.h kernel/include/kheap64.h kernel/include/vmm64.h | $(BUILD)/kernel64
	$(K64_CC) $(K64_FLAGS) -c $< -o $@
$(BUILD)/kernel64/exceptions_asm.o: kernel/x86_64/exceptions64.S | $(BUILD)/kernel64
	$(K64_CC) --target=x86_64-unknown-none-elf -c $< -o $@
$(BUILD)/kernel64.elf: $(BUILD)/kernel64/entry.o $(BUILD)/kernel64/kernel.o $(MEMCORE64_OBJS) $(BUILD)/kernel64/exceptions_asm.o kernel/x86_64/linker.ld $(CONSOLE64_OBJS) $(M3A_OBJS) $(H1H2_OBJS) $(S0_OBJS) $(WELCOME64_OBJS) $(U1_OBJS) $(I2_OBJS) $(IFS1_OBJS)
	$(K64_LD) -m elf_x86_64 -nostdlib --build-id=none -T kernel/x86_64/linker.ld -o $@ $(BUILD)/kernel64/entry.o $(BUILD)/kernel64/kernel.o $(MEMCORE64_OBJS) $(BUILD)/kernel64/exceptions_asm.o $(CONSOLE64_OBJS) $(M3A_OBJS) $(H1H2_OBJS) $(S0_OBJS) $(WELCOME64_OBJS) $(U1_OBJS) $(I2_OBJS) $(IFS1_OBJS)
$(BUILD)/kernel64.bin: $(BUILD)/kernel64.elf
	$(K64_OBJCOPY) -O binary $< $@
	$(PYTHON) tools/verify_kernel64.py $(BUILD)/kernel64.elf $@
$(BUILD)/uefi/kernel_embed.obj: boot/uefi/kernel_embed.S $(BUILD)/kernel64.bin | $(BUILD)/uefi
	$(UEFI_CC) --target=x86_64-pc-win32-coff -c $< -o $@
$(BUILD)/uefi/handoff_asm.obj: boot/uefi/handoff.S | $(BUILD)/uefi
	$(UEFI_CC) --target=x86_64-pc-win32-coff -c $< -o $@
BOOTMENU_EFI_OBJS := $(BUILD)/uefi/bootmenu.obj $(BUILD)/uefi/firmware_storage.obj
$(UEFI_EFI): $(BOOTMENU_EFI_OBJS) $(BUILD)/uefi/start.obj $(BUILD)/uefi/firmware_info.obj $(BUILD)/uefi/platform_probe.obj $(BUILD)/uefi/bootinfo_builder.obj $(BUILD)/uefi/framebuffer_console.obj $(BUILD)/uefi/handoff.obj $(BUILD)/uefi/handoff_asm.obj $(BUILD)/uefi/kernel_embed.obj
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
	$(PYTHON) -m unittest discover -s host_tests -v
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

.PHONY: uefi-m3-smoke
uefi-m3-smoke: uefi-check
	@python3 tools/m3a_qemu_smoke.py

.PHONY: uefi-hw-smoke
uefi-hw-smoke: uefi-check
	@python3 tools/h1_h2_qemu_smoke.py

.PHONY: uefi-s0-smoke
uefi-s0-smoke: uefi-check
	@python3 tools/s0_qemu_smoke.py

# L1: build a physically flashable UEFI image. Nothing writes to a device.
.PHONY: uefi-live-image
uefi-live-image: uefi-check $(TARGET_DISK_IMG)
	cp $(TARGET_DISK_IMG) $(BUILD)/intellectos-live-usb.img
	@echo "READY: $(BUILD)/intellectos-live-usb.img (Full IntellectOS Live Installation USB Image)"

.PHONY: uefi-live-smoke
uefi-live-smoke: uefi-check
	@python3 tools/l1_qemu_smoke.py
$(BUILD)/kernel64/welcome64.o: kernel/include/welcome64.h kernel/src64/font5x7.h
$(BUILD)/kernel64/kernel.o: kernel/include/welcome64.h

# B1: independent UEFI boot menu. The USB image remains read-only on host.
$(BUILD)/uefi/start.obj: boot/uefi/bootmenu.h boot/uefi/firmware_storage.h
$(BUILD)/uefi/bootmenu.obj: boot/uefi/bootmenu.h kernel/src64/font5x7.h
$(BUILD)/uefi/firmware_storage.obj: boot/uefi/firmware_storage.h
.PHONY: uefi-b1-smoke
uefi-b1-smoke: uefi-check
	@python3 tools/l1_qemu_smoke.py

# U1: real ring3 static user terminal, no filesystem or Vibelang compiler yet.
$(BUILD)/user:
	mkdir -p $@
U1_USER_FLAGS := --target=x86_64-unknown-none-elf -std=c11 -ffreestanding -fpie \
                 -fno-stack-protector -fno-builtin -mno-red-zone -mgeneral-regs-only \
                 -mcmodel=small -fno-jump-tables -O2 -Wall -Wextra -Werror \
                 -fno-asynchronous-unwind-tables -fno-unwind-tables
$(BUILD)/user/terminal.o: user/terminal.c | $(BUILD)/user
	$(K64_CC) $(U1_USER_FLAGS) -c $< -o $@
$(BUILD)/user/entry.o: user/entry.S | $(BUILD)/user
	$(K64_CC) --target=x86_64-unknown-none-elf -c $< -o $@
$(BUILD)/user/terminal.elf: $(BUILD)/user/entry.o $(BUILD)/user/terminal.o user/terminal.ld
	$(K64_LD) -m elf_x86_64 -nostdlib --build-id=none -T user/terminal.ld -o $@ $(BUILD)/user/entry.o $(BUILD)/user/terminal.o
$(BUILD)/user/terminal.bin: $(BUILD)/user/terminal.elf tools/verify_user_terminal.py
	$(K64_OBJCOPY) -O binary $< $@
	$(PYTHON) tools/verify_user_terminal.py $(BUILD)/user/terminal.elf $@
$(BUILD)/kernel64/usergate64.o: kernel/x86_64/usergate64.S | $(BUILD)/kernel64
	$(K64_CC) --target=x86_64-unknown-none-elf -c $< -o $@
$(BUILD)/kernel64/user_terminal_embed.o: kernel/x86_64/user_terminal_embed.S $(BUILD)/user/terminal.bin | $(BUILD)/kernel64
	$(K64_CC) --target=x86_64-unknown-none-elf -c $< -o $@
$(BUILD)/kernel64/kernel.o: kernel/include/userterminal64.h
.PHONY: uefi-u1-smoke
uefi-u1-smoke: uefi-check
	@$(PYTHON) tools/u1_qemu_smoke.py

# I2: native read-only GPT/FAT32 backend (does not mount IntellectFS).
$(BUILD)/kernel64/kernel.o: kernel/include/fat32_vfs64.h

$(BUILD)/kernel64/ifs_ro64.o: kernel/include/ifs_ro64.h kernel/include/ifs_blake3_v1.h
$(BUILD)/kernel64/ifs_blake3_v1.o: kernel/include/ifs_blake3_v1.h

# IFS-1: bind the signed bootstrap IVX to the exact native kernel image.
$(BUILD)/kernel64/ifs_ivx_pin.h: $(BUILD)/user/terminal.bin tools/make_pinned_dev_ivx.py installer/ivx_v1.py installer/blake3_ref.py | $(BUILD)/kernel64
	$(PYTHON) tools/make_pinned_dev_ivx.py --program $(BUILD)/user/terminal.bin --ivx $(BUILD)/user/terminal.ivx --pub $(BUILD)/user/terminal.pub --header $@
$(BUILD)/kernel64/ifs_exec64.o: $(BUILD)/kernel64/ifs_ivx_pin.h kernel/include/ifs_exec64.h kernel/include/ifs_ro64.h kernel/include/userterminal64.h

# I2: preinstall GPT/FAT32 disk image
GPT_IMG := $(BUILD)/intellectos-preinstall-gpt.img
.PHONY: uefi-gpt-image uefi-gpt-run
$(GPT_IMG): $(UEFI_EFI) tools/make_terminal_disk.py tools/verify_terminal_disk.py
	@rm -f $@
	$(PYTHON) tools/make_terminal_disk.py --efi $(UEFI_EFI) --output $@ --size-gib 1
	$(PYTHON) tools/verify_terminal_disk.py $@ --efi $(UEFI_EFI)

uefi-gpt-image: $(GPT_IMG)
	@echo "READY: $(GPT_IMG)"

uefi-gpt-run: $(GPT_IMG)
	@bash tools/boot_preinstall_gpt_qemu.sh $(GPT_IMG)

# IFS-1: install disk image with FAT32 ESP + IntellectFS sealed root
IFS_IMG := $(BUILD)/intellectos-installed-ifs.img
.PHONY: uefi-ifs-image uefi-ifs-smoke
$(IFS_IMG): $(UEFI_EFI) $(BUILD)/user/terminal.ivx $(BUILD)/user/terminal.pub $(BUILD)/kernel64/ifs_ivx_pin.h tools/make_ifs_install_image.py tools/verify_installed_system.py
	@$(PYTHON) -c "import sys; from pathlib import Path; sys.path.insert(0, '.'); \
	from installer.system_config_v1 import provision, canonical_json; \
	from tools.make_ifs_install_image import create, verify_kernel_pin; \
	root_cfg = canonical_json(provision('RootPass123!', 'UTC', memory_kib=65536, iterations=2)); \
	efi = Path('$(UEFI_EFI)').read_bytes(); \
	ivx = Path('$(BUILD)/user/terminal.ivx').read_bytes(); \
	pub = Path('$(BUILD)/user/terminal.pub').read_bytes(); \
	verify_kernel_pin(Path('$(BUILD)/kernel64/ifs_ivx_pin.h'), ivx); \
	Path('$@').unlink(missing_ok=True); \
	create(Path('$@'), efi, ivx, pub, root_cfg, 1)"
	$(PYTHON) tools/verify_installed_system.py $@ --efi $(UEFI_EFI)

uefi-ifs-image: $(IFS_IMG)
	@echo "READY: $(IFS_IMG)"

uefi-ifs-smoke: uefi-check $(IFS_IMG)
	@$(PYTHON) tools/test_ifs_system_qemu.py $(IFS_IMG)

# USB-to-Disk Installer workflow: wipe, partition, format ESP & IFS, standalone boot
TARGET_DISK_IMG := $(BUILD)/intellectos-target-disk.img
.PHONY: uefi-install-disk uefi-install-smoke
$(TARGET_DISK_IMG): $(UEFI_EFI) $(BUILD)/user/terminal.ivx $(BUILD)/user/terminal.pub tools/install_target_disk.py installer/target_disk_installer.py
	$(PYTHON) tools/install_target_disk.py --target $@ --size-gib 1 --wipe

uefi-install-disk: $(TARGET_DISK_IMG)
	@echo "READY: $(TARGET_DISK_IMG)"

uefi-install-smoke: uefi-check $(TARGET_DISK_IMG)
	@$(PYTHON) tools/test_usb_to_disk_install_qemu.py --target-image $(TARGET_DISK_IMG)


