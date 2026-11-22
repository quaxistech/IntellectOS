"""IntellectOS Full Target Disk Installer: USB to Disk.

Performs complete disk installation from USB media / build assets to a target disk:
- Disk safety validation (rejects host OS disks, requires explicit USB verification if real device)
- Disk wiping/cleaning (zeros MBR, GPT primary/backup, partition table signatures)
- Standard GPT partitioning (512 MiB FAT32 ESP + 1 MiB-aligned IntellectFS v1 system partition)
- FAT32 ESP formatting with EFI/BOOT/BOOTX64.EFI
- IntellectFS v1 system volume formatting with /sbin/terminal.ivx, /etc/system.json, /etc/timezone
- No password required by default (user-mode terminal boots directly)
- Independent readback and verification of both GPT copies and IFS filesystem files
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from installer import boot_disk_v1 as disk
from installer.ifs_disk_v1 import Volume, IFSError
from installer.ivx_v1 import verify_dev_terminal
from installer.system_config_v1 import provision, canonical_json, validate_timezone
from installer.blake3_ref import blake3

SECTOR = 512
MiB = 1024 * 1024
GiB = 1024 * MiB
ESP_SIZE = 512 * MiB
ALIGN = 2048

ESP_TYPE = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b')
IFS_GPT_GUID = uuid.uuid5(uuid.NAMESPACE_URL, 'https://intellectos.org/spec/ifs-1/gpt-type')


class TargetSafetyError(RuntimeError):
    pass


class InstallationError(RuntimeError):
    pass


def validate_target_safety(target: Path | str, allow_real_usb: bool = False,
                           confirm_token: str | None = None) -> Path:
    """Strictly protect host OS disks.

    Refuses host disks (/dev/sda, /dev/nvme*, /dev/vda, host root mounts).
    Only allows image files, or real block devices if verified as unmounted USB
    with explicit confirmation token.
    """
    path_str = str(target)
    if path_str.startswith('/dev/'):
        if not allow_real_usb:
            raise TargetSafetyError(
                f"BLOCKED: Direct block device target '{path_str}' forbidden to protect host OS. "
                "Use an image file or pass --allow-real-usb with explicit confirmation token."
            )
        # Check host disk safety
        dev_path = Path(path_str).resolve()
        name = dev_path.name
        if name.startswith('nvme') or name in ('sda', 'vda', 'hda'):
            raise TargetSafetyError(
                f"CRITICAL HOST SAFETY BLOCKED: '{path_str}' is identified as a host system disk!"
            )
        if confirm_token != f"INSTALL {path_str}":
            raise TargetSafetyError(
                f"Confirmation token mismatch. Must be exactly 'INSTALL {path_str}'"
            )
        # Check lsblk transport
        try:
            r = subprocess.run(['lsblk', '-dn', '-o', 'TRAN', str(dev_path)],
                               capture_output=True, text=True, check=True)
            if r.stdout.strip() != 'usb':
                raise TargetSafetyError(
                    f"BLOCKED: device transport '{r.stdout.strip()}' is not USB!"
                )
            # Check mounts
            m = subprocess.run(['lsblk', '-nrpo', 'MOUNTPOINTS', str(dev_path)],
                               capture_output=True, text=True, check=True)
            if any(line.strip() for line in m.stdout.splitlines()):
                raise TargetSafetyError(
                    f"BLOCKED: target device '{path_str}' has active mounted partitions!"
                )
        except (subprocess.CalledProcessError, FileNotFoundError) as exc:
            raise TargetSafetyError(f"Failed to inspect device with lsblk: {exc}") from exc
        return dev_path

    # Regular file target (image file)
    p = Path(target).resolve()
    if p.exists() and not p.is_file():
        raise TargetSafetyError(f"Target '{target}' exists and is not a regular file")
    return p


def wipe_target_disk(target: Path, size_bytes: int = 1 * GiB) -> int:
    """Wipes partition tables, boot sectors, and signatures from the target disk/image."""
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('wb') as f:
            f.truncate(size_bytes)
        disk_size = size_bytes
    else:
        # Check existing size; if regular file is smaller than size_bytes, expand it
        disk_size = target.stat().st_size
        if target.is_file() and disk_size < size_bytes:
            with target.open('r+b') as f:
                f.truncate(size_bytes)
            disk_size = size_bytes

    if disk_size < 1 * GiB:
        raise InstallationError(f"Target disk size ({disk_size} B) is below minimum 1 GiB requirement")

    sectors = disk_size // SECTOR
    zero_1mib = bytes(1 * MiB)

    with target.open('r+b', buffering=0) as f:
        # 1. Zero first 1 MiB (LBA 0..2047: MBR, GPT primary header, GPT primary entries)
        f.seek(0)
        f.write(zero_1mib)

        # 2. Zero last 1 MiB (LBA sectors-2048..sectors-1: GPT backup entries and header)
        if sectors >= 2048:
            f.seek((sectors - 2048) * SECTOR)
            f.write(zero_1mib)

        # 3. Zero 1 MiB at ESP/IFS boundary (LBA 1048576..1050624)
        boundary_lba = 2048 + (ESP_SIZE // SECTOR)
        if sectors > boundary_lba + 2048:
            f.seek(boundary_lba * SECTOR)
            f.write(zero_1mib)

        f.flush()
        os.fsync(f.fileno())

    return disk_size


def partition_target_disk(target: Path, disk_size: int,
                          disk_guid: uuid.UUID | None = None) -> dict:
    """Partitions the disk with standard protective MBR + GPT (ESP + IntellectFS)."""
    sectors = disk_size // SECTOR
    first_usable = 34
    last_usable = sectors - 34

    esp_start = 2048
    esp_count = ESP_SIZE // SECTOR
    ifs_start = esp_start + esp_count
    if (ifs_start % ALIGN) != 0:
        raise InstallationError('IFS partition alignment error (not 1 MiB aligned)')
    if last_usable <= ifs_start + (256 * MiB // SECTOR):
        raise InstallationError('Target disk too small for required IntellectFS partition (min 256 MiB)')

    d_guid = disk_guid or uuid.uuid4()
    esp_part_guid = uuid.uuid4()
    ifs_part_guid = uuid.uuid4()

    entries = bytearray(128 * 128)
    entries[:128] = disk.part_entry(ESP_TYPE, esp_part_guid, esp_start,
                                    ifs_start - 1, 'EFI System Partition')
    entries[128:256] = disk.part_entry(IFS_GPT_GUID, ifs_part_guid, ifs_start,
                                      last_usable, 'IntellectFS v1')

    entries_crc = zlib.crc32(entries) & 0xFFFFFFFF
    hdr1 = disk.gpt_header(1, sectors - 1, first_usable, last_usable, d_guid, 2, entries_crc)
    hdr2 = disk.gpt_header(sectors - 1, 1, first_usable, last_usable, d_guid, sectors - 33, entries_crc)
    mbr = disk.protective_mbr(sectors)

    with target.open('r+b', buffering=0) as f:
        disk.write_at(f, 0, mbr)
        disk.write_at(f, SECTOR, hdr1)
        disk.write_at(f, 2 * SECTOR, entries)
        disk.write_at(f, (sectors - 33) * SECTOR, entries)
        disk.write_at(f, (sectors - 1) * SECTOR, hdr2)
        f.flush()
        os.fsync(f.fileno())

    return {
        'disk_guid': str(d_guid),
        'disk_sectors': sectors,
        'esp_start_lba': esp_start,
        'esp_sectors': esp_count,
        'ifs_start_lba': ifs_start,
        'ifs_last_lba': last_usable,
        'ifs_blocks': (last_usable - ifs_start + 1) // 8
    }


def format_esp(target: Path, efi_bytes: bytes) -> None:
    """Formats FAT32 on the EFI System Partition and copies BOOTX64.EFI."""
    esp_start = 2048
    esp_count = ESP_SIZE // SECTOR
    notice = (
        b'INTELLECTOS INSTALLED SYSTEM\r\n'
        b'EFI SYSTEM PARTITION (FAT32)\r\n'
        b'BOOTLOADER: EFI/BOOT/BOOTX64.EFI\r\n'
    )
    with target.open('r+b', buffering=0) as f:
        disk.write_fat32_esp(f, esp_start, esp_count, efi_bytes, {'README.TXT': notice})
        f.flush()
        os.fsync(f.fileno())


def format_intellectfs(target: Path, part_info: dict, files: dict[str, bytes]) -> dict:
    """Formats the native IntellectFS v1 system volume and installs system files."""
    sys_start_byte = part_info['ifs_start_lba'] * SECTOR
    sys_size_byte = part_info['ifs_blocks'] * 4096
    v = Volume(target, sys_start_byte, sys_size_byte)
    ifs_result = v.format(files)
    verified = v.check()
    for key, payload in files.items():
        if verified[key].data != payload:
            raise InstallationError(f'IFS readback mismatch for installed file: {key}')
    return ifs_result


def build_default_system_config(timezone: str = 'UTC') -> bytes:
    """Generates standard system config with root UID=0, UTC timezone, RTC=UTC.

    Per user requirement ('пароли пока не надо'), uses standard default credential
    so that no password prompt is required to access the terminal.
    """
    validate_timezone(timezone)
    # Generate canonical configuration with standard default credential
    cfg_dict = provision('intellectos-default-root-key', timezone,
                         memory_kib=65536, iterations=2, parallelism=1)
    return canonical_json(cfg_dict)


def install_target_disk(target: Path | str,
                        efi_bytes: bytes,
                        ivx_bytes: bytes,
                        pub_bytes: bytes,
                        system_config_bytes: bytes | None = None,
                        size_gib: int = 1,
                        wipe: bool = True,
                        timezone: str = 'UTC',
                        allow_real_usb: bool = False,
                        confirm_token: str | None = None) -> dict:
    """Full-featured installer: wipe, partition GPT, format FAT32 ESP, format IntellectFS v1."""
    target_path = validate_target_safety(target, allow_real_usb=allow_real_usb,
                                         confirm_token=confirm_token)

    # Prepare system files
    if system_config_bytes is None:
        system_config_bytes = build_default_system_config(timezone)

    files = {
        '/sbin/terminal.ivx': ivx_bytes,
        '/etc/system.json': system_config_bytes,
        '/etc/timezone': (timezone + '\n').encode('utf-8'),
        '/etc/trust/developer.pub': pub_bytes
    }

    # Step 1: Wipe target disk
    if wipe or not target_path.exists():
        disk_size = wipe_target_disk(target_path, size_bytes=size_gib * GiB)
    else:
        disk_size = target_path.stat().st_size

    # Step 2: Partition target disk (GPT)
    part_info = partition_target_disk(target_path, disk_size)

    # Step 3: Format ESP (FAT32) and install BOOTX64.EFI
    format_esp(target_path, efi_bytes)

    # Step 4: Format IntellectFS v1 and install system files
    ifs_info = format_intellectfs(target_path, part_info, files)

    # Step 5: Independent Verification
    from tools.verify_installed_system import verify
    receipt = verify(target_path, efi=efi_bytes)

    return {
        'status': 'SUCCESS_INSTALLED',
        'target': str(target_path),
        'disk_size_bytes': disk_size,
        'efi_size_bytes': len(efi_bytes),
        'ivx_size_bytes': len(ivx_bytes),
        'files_installed': list(files.keys()),
        'part_info': part_info,
        'ifs_info': ifs_info,
        'verification': receipt
    }
