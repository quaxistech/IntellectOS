#!/usr/bin/env bash
# IntellectOS C1 serial CLI in QEMU. NEVER touches a physical block device.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
if [[ ! -f build/intellectos-uefi-stage-e.img ]]; then
    echo 'Missing build/intellectos-uefi-stage-e.img; run make check' >&2
    exit 1
fi
code=''; vars=''
if [[ -f /usr/share/OVMF/OVMF_CODE_4M.fd && -f /usr/share/OVMF/OVMF_VARS_4M.fd ]]; then
    code=/usr/share/OVMF/OVMF_CODE_4M.fd
    vars=/usr/share/OVMF/OVMF_VARS_4M.fd
elif [[ -f /usr/share/OVMF/OVMF_CODE.fd && -f /usr/share/OVMF/OVMF_VARS.fd ]]; then
    code=/usr/share/OVMF/OVMF_CODE.fd
    vars=/usr/share/OVMF/OVMF_VARS.fd
else
    echo 'Missing OVMF images: sudo apt install ovmf' >&2
    exit 1
fi
cp -- "$vars" build/uefi/OVMF_VARS_CONSOLE.fd
arg=(-display none)
if [[ "${1:-}" == '--gui' ]]; then arg=(-display gtk); fi
printf '\nC1: OVMF starts, then IntellectOS prints its prompt to COM1.\n'
printf 'Type help, mem, boot, vmm, cpu, echo hi, halt. Stop QEMU with Ctrl+C.\n\n'
exec qemu-system-x86_64 -m 256M -net none -monitor none -no-reboot \
    -drive "if=pflash,format=raw,readonly=on,file=$code" \
    -drive "if=pflash,format=raw,file=build/uefi/OVMF_VARS_CONSOLE.fd" \
    -drive 'if=ide,format=raw,file=build/intellectos-uefi-stage-e.img' \
    -serial stdio "${arg[@]}"
