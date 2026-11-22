/* Embed our dedicated installed OS EFI loader in the installer EFI binary.
 * This ensures the firmware installer can write BOOTX64_INSTALLED.EFI to target disk.
 */
.section .rdata,"dr"
.p2align 4
.globl intellect_installed_efi_start
intellect_installed_efi_start:
.incbin "build/uefi/BOOTX64_INSTALLED.EFI"
.globl intellect_installed_efi_end
intellect_installed_efi_end:
