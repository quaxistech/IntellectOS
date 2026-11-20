#!/usr/bin/env bash
# Boot an explicitly passed regular disk image on OVMF (no host block writes).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
[[ $# -eq 1 ]] || { echo "Usage: $0 path/to/preinstall.img" >&2; exit 2; }
img=$(realpath -- "$1")
[[ -f "$img" && ! -b "$img" ]] || { echo "Refusing non-regular image" >&2; exit 2; }
if [[ -f /usr/share/OVMF/OVMF_CODE_4M.fd && -f /usr/share/OVMF/OVMF_VARS_4M.fd ]]; then
    code=/usr/share/OVMF/OVMF_CODE_4M.fd
    vars=/usr/share/OVMF/OVMF_VARS_4M.fd
elif [[ -f /usr/share/OVMF/OVMF_CODE.fd && -f /usr/share/OVMF/OVMF_VARS.fd ]]; then
    code=/usr/share/OVMF/OVMF_CODE.fd
    vars=/usr/share/OVMF/OVMF_VARS.fd
else
    echo 'OVMF missing (sudo apt install ovmf)' >&2;exit 1
fi
command -v qemu-system-x86_64 >/dev/null || { echo 'QEMU missing' >&2;exit 1; }
mkdir -p build/i2
cp -- "$vars" build/i2/OVMF_VARS.fd
qemu-system-x86_64 -m 256M -net none -monitor none -no-reboot \
    -drive "if=pflash,format=raw,readonly=on,file=$code" \
    -drive 'if=pflash,format=raw,file=build/i2/OVMF_VARS.fd' \
    -drive "if=ide,format=raw,file=$img" \
    -serial stdio -display gtk
