#!/usr/bin/env bash
# Flash the IntellectOS test image to an explicitly selected USB disk.
# Writes the WHOLE USB device. Never selects a device automatically.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if (( $# < 1 || $# > 2 )); then
    echo "Usage: bash tools/flash_live_usb.sh /dev/sdX [path/to/image.img]" >&2
    echo "First identify the USB disk using: lsblk -p -o NAME,SIZE,MODEL,TRAN,RM,TYPE,MOUNTPOINTS" >&2
    exit 2
fi

selected="$1"
if (( $# == 2 )); then
    image="$(readlink -f -- "$2")"
else
    image="$repo/build/intellectos-live-usb.img"
fi

if [[ ! -f "$image" ]]; then
    echo "ERROR: missing image file: $image (run: make uefi-live-image or make uefi-ifs-image)" >&2
    exit 2
fi
device="$(readlink -f -- "$selected")"
if [[ "$device" != /dev/* || ! -b "$device" ]]; then
    echo "ERROR: not a block device: $selected" >&2
    exit 2
fi
if [[ "$(lsblk -dn -o TYPE "$device" | tr -d '[:space:]')" != disk ]]; then
    echo "ERROR: you must select the whole disk, not a partition" >&2
    exit 2
fi
transport="$(lsblk -dn -o TRAN "$device" | tr -d '[:space:]')"
if [[ "$transport" != usb ]]; then
    echo "REFUSED: transport is '$transport', not USB. This protects internal HDD/SSD." >&2
    exit 2
fi
# Refuse if any child filesystem is mounted (including the host root).
if lsblk -nrpo MOUNTPOINTS "$device" | grep -q '[^[:space:]]'; then
    echo "REFUSED: this device or its partitions have mounted filesystems." >&2
    lsblk -p -o NAME,SIZE,MODEL,TRAN,TYPE,MOUNTPOINTS "$device" >&2
    echo "Unmount its partitions manually, then retry." >&2
    exit 2
fi
size_bytes="$(lsblk -bdn -o SIZE "$device" | tr -d '[:space:]')"
image_bytes="$(stat -c %s -- "$image")"
if (( image_bytes < 1048576 || size_bytes < image_bytes )); then
    echo "REFUSED: image size or USB capacity invalid" >&2
    exit 2
fi
printf '\nUSB FLASH TARGET (ALL EXISTING CONTENTS WILL BE DESTROYED):\n'
lsblk -p -o NAME,SIZE,MODEL,TRAN,RM,TYPE,MOUNTPOINTS "$device"
printf '\nImage: %s (%s bytes)\n' "$image" "$image_bytes"
printf '\nTo proceed type EXACTLY: FLASH %s\n> ' "$device"
IFS= read -r answer
if [[ "$answer" != "FLASH $device" ]]; then
    echo "Cancelled: no changes were made."
    exit 1
fi
# Recheck immediately before privileged write.
if [[ "$(lsblk -dn -o TRAN "$device" | tr -d '[:space:]')" != usb ]]; then
    echo "REFUSED: USB device changed" >&2; exit 2
fi
if lsblk -nrpo MOUNTPOINTS "$device" | grep -q '[^[:space:]]'; then
    echo "REFUSED: a partition was mounted during confirmation" >&2; exit 2
fi
sudo_cmd=(sudo)
if [[ -n "${SUDO_ASKPASS:-}" ]]; then
    sudo_cmd+=( -A )
fi
"${sudo_cmd[@]}" dd if="$image" of="$device" bs=4M iflag=fullblock conv=fsync status=progress
sync
printf 'Reading image back for verification...\n'
"${sudo_cmd[@]}" cmp -n "$image_bytes" -- "$image" "$device"
echo "PASS: flash image written and byte-for-byte verified on USB: $device"
echo "Now boot in UEFI mode from this USB device."
