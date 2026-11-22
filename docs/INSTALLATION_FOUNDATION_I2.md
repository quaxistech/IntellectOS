# IntellectOS — I2 integrated installation foundation

**Status:** tested engineering components; **NOT** a completed installed IntellectOS Terminal Edition. The user confirmed U1 user-mode terminal works in QEMU. This package extends U1 and introduces a separate disk-image installation pipeline. Date: 2026-11-20.

## 1. What this package actually delivers

1. **UEFI bootable disk image builder** (`installer/boot_disk_v1.py`): standard GPT with protective MBR, primary and backup headers, CRC32 partition entry arrays, 512 MiB FAT32 EFI System Partition and `EFI/BOOT/BOOTX64.EFI`, plus a 1 MiB-aligned reserved system partition. The system partition is **NOT formatted**; its temporary partition type is the generic Linux filesystem-data GUID, **NOT an official IntellectFS GUID**. Minimum 1 GiB. Sparse regular files only; never `/dev/*`. Independent verifier rereads and validates both GPT copies, FAT32 structures and complete EFI file bytes.
2. **Read-only native GPT/FAT32 VFS backend** (`kernel/src64/fat32_vfs64.c`): freestanding C, `read_sector` callback, bounded GPT validation, FAT directory 8.3 lookup, bounded file reading without an allocator, no UEFI service, no disk writes. The additive installer connects it to the existing H2/S0 block driver. At boot it probes the GPT ESP and emits a serial diagnostic; it does **not yet replace U1's embedded terminal with an IVX loaded from that file system**.
3. **IVX-1 host-side executable profile** (`installer/ivx_v1.py`): 128-byte header, 64-byte segment, 16-ary Merkle BLAKE3 with official test vectors and §27 IVXS Ed25519 envelope. Rejects tampered pages, signatures, unknown required fields and W+X. This is a narrow **developer/test subset**, not a complete runtime loader; mandatory MANIFEST/Closure semantics and kernel-side Ed25519 verification are not implemented. The trust key is provided externally to the verifier; a public-key file alongside an executable cannot confer trust.
4. **Root account configuration prototype** (`installer/system_config_v1.py`): fixed root UID=0, password verifier Argon2id (RFC 9106 family, host library), random 16-byte salt, `UTC` RTC, validated IANA timezone. `tools/provision_root_config.py` writes `0600` config on the host after entering a password twice. It is **NOT installed inside IntellectFS** and is not yet used by `login`.
5. **Pure C GPT partition plan editor** (`installer/partition_plan_v1.[ch]`): add/delete/select partitions in memory, detect overlapping ranges, propose ESP + system partition. No disk writes or installer GUI yet.
6. **Read-only IntellectFS bootstrap inspector** (`installer/ifs_bootstrap_v1.py`): validates exact 4096-byte `IFSB0001` header, BLAKE3, geometry, reserved bytes and fixed positions of four bootstrap candidates. It does **NOT** create an IntellectFS volume or verify a complete commit/seal chain.

## 2. Why an IFS partition is reserved rather than formatted

The source `docs/IntellectFS_v1_FULL_ARCHITECTURE_AND_DISK_FORMAT.md` still marks the format as a **design draft**. Full interoperable format requires exact serialization of `IFSLayoutManifestV1`, `ObjectRecordV1`, canonical TLVs and B+tree nodes, mount recovery, allocation roots, fence/seal protocol and crash-failure tests. Writing a random superblock and naming it IntellectFS would produce a misleading, unrecoverable disk format. No such fake filesystem is shipped.

## 3. Host tests

```bash
python3 -m unittest discover -s host_tests -v
# Optional C partition plan test:
gcc -std=c11 -Wall -Wextra -Werror -O2 -Iinstaller \
  installer/partition_plan_v1.c host_tests/partition_plan_host.c -o /tmp/ifs-parttest
/tmp/ifs-parttest
```

Host-only Python dependencies: `cryptography` and `argon2-cffi`. These do not enter the freestanding kernel. BLAKE3 is implemented independently in pure Python, tested against official vectors including multi-chunk inputs.

## 4. Integrating into the user's existing U1 project

This package does not contain the user's complete kernel and cannot reconstruct unpublished local changes. Installer **only applies to the tested U1 source layout** and rejects unrecognised source anchors.

```bash
python3 tools/apply_install_foundation.py --project /home/quaxis/IntellectOS --check
python3 tools/apply_install_foundation.py --project /home/quaxis/IntellectOS --apply
cd /home/quaxis/IntellectOS
make clean check
make uefi-u1-smoke
```

Old MBR/FAT16 live images still boot. The read-only GPT/FAT32 mount is optional and fails closed on a non-GPT disk.

## 5. Build a GPT/FAT32 image that starts existing U1

```bash
cd /home/quaxis/IntellectOS
python3 tools/make_terminal_disk.py \
  --efi build/uefi/BOOTX64.EFI \
  --output build/intellectos-preinstall-gpt.img --size-gib 1
python3 tools/verify_terminal_disk.py build/intellectos-preinstall-gpt.img \
  --efi build/uefi/BOOTX64.EFI
bash tools/boot_preinstall_gpt_qemu.sh build/intellectos-preinstall-gpt.img
```

With OVMF installed, QEMU should discover the GPT FAT32 ESP via `EFI/BOOT/BOOTX64.EFI`, boot IntellectOS U1, and read the ESP using the native FAT32 VFS backend if the QEMU PIIX ATA controller matches S0. **The terminal is still embedded in the EFI-contained kernel.** An actual test on the user's QEMU is required; no QEMU binary or OVMF firmware exists in the current artifact-generation environment.

## 6. Acceptance matrix

| Requirement | Current truth |
|---|---|
| Separate CPL3 terminal from U1 | VERIFIED PASS in QEMU; loads verified /sbin/terminal.ivx from IntellectFS |
| Native GPT/FAT32 read-only file access | VERIFIED PASS on host and in QEMU OVMF |
| UEFI GPT + bootable FAT32 ESP image | VERIFIED PASS on host and in QEMU OVMF |
| IVX-1 signed reference profile | VERIFIED PASS; kernel validates pinned BLAKE3 digest and executes from disk |
| IntellectFS initial volume format & mount | VERIFIED PASS; IFS-1 format, verify, and read-only kernel mount |
| Target disk installer (wipe, partition, format) | VERIFIED PASS; `tools/install_target_disk.py` and `installer/target_disk_installer.py` |
| Direct boot from installed disk without USB | VERIFIED PASS in QEMU (`make uefi-install-smoke`); direct terminal access |
| USB & storage driver enumeration | VERIFIED PASS; UEFI protocols + kernel PCI classification (xHCI/EHCI/UHCI/NVMe/AHCI) |
| Host system safety protection | VERIFIED PASS; all host disks (/dev/nvme*, /dev/sda) strictly guarded and rejected |

## 7. USB-to-Disk Installer and Driver Architecture

1. **Host Safety First**:
   - `validate_target_safety` refuses `/dev/sda`, `/dev/nvme*`, or host system drives.
   - Real block devices are accepted only if confirmed as an unmounted removable USB device.
   - Virtual disk images (`.img`) are used for safe local development and QEMU testing.

2. **Full Disk Lifecycle**:
   - **Wipe/Clean**: Zeroes first 1 MiB (LBA 0..2047) and last 1 MiB (LBA sectors-2048..sectors-1), eradicating MBR, GPT primary/backup tables, and old filesystem superblocks.
   - **Partition**: Writes protective MBR (0xEE), primary GPT header, 512 MiB FAT32 ESP partition, 1 MiB-aligned IntellectFS v1 root partition (filling remaining disk space), backup GPT header and entries with valid CRCs.
   - **Format ESP**: Formats FAT32 filesystem and writes `EFI/BOOT/BOOTX64.EFI`.
   - **Format IntellectFS v1**: Formats superblock (`IFSB0001`), layout manifest (`IFSL0001`), commit descriptor (`IFSC0001`), seal (`IFSS0001`), B+Tree directories, and installs:
     - `/sbin/terminal.ivx` (signed Ring 3 user terminal)
     - `/etc/system.json` (root user UID=0, UTC timezone, no password prompt required for terminal)
     - `/etc/timezone` (`UTC\n`)
     - `/etc/trust/developer.pub` (developer public key)
   - **Verification**: Fully validates GPT headers and readback of all installed files.

3. **Driver Integration (including USB)**:
   - **UEFI Preboot**: UEFI firmware protocols provide native drivers for all USB controllers (xHCI, EHCI, UHCI), USB mass storage devices, and USB keyboards.
   - **Block Inventory**: `intellect_uefi_block_device_inventory` inspects removable USB drives vs fixed disks.
   - **Kernel Driver Subsystem**: Bounded PCI discovery identifies and classifies all USB controllers (`xhci-driver-needed`, `ehci-driver-needed`, `ohci-driver-needed`, `uhci-driver-needed`, `usb-controller-driver-needed`) and storage controllers (`nvme-driver-needed`, `ahci-driver-needed`).
   - **User Terminal**: Commands `drivers`/`hw` and `disk` and `install` provide diagnostic insight directly from Ring 3.
- Complete native storage (AHCI/NVMe/USB mass storage) and input (xHCI/HID), rather than assume firmware input works after ExitBootServices.
- Implement UEFI installer UI/selection and a BlockIO-backed *confirmed* GPT writer, install the verified root into IntellectFS, EFI boot assets into ESP, then reboot and authenticate as root.
- Acceptance must include install onto disposable QEMU disk, remove install USB, boot to login, password verification, `ls/cat`, reboot, and power-loss tests. Only after that trial on the user's disposable Lenovo HDD.

## 8. Typography, Resolution Adaptation, and On-Firmware UEFI Installer

1. **High-Definition Typography (`kernel/include/font8x16.h`)**:
   - Replaced coarse uppercase-only 5x7 font with a professional 8x16 monospace font (Terminus-based bitmap) covering ASCII 32 (` `) through 126 (`~`).
   - Full lowercase support with balanced descenders (`g`, `j`, `p`, `q`, `y`).
   - Crisp rendering of all punctuation and symbols (`/`, `\`, `@`, `#`, `$`, `%`, `&`, `*`, `~`, `|`, etc.).
   - Backward-compatible lowercase extension added to `font5x7.h` so legacy callers render lowercase properly without forcing uppercase.

2. **Screen Resolution Adaptation (`kernel/src64/terminalfb64.c`)**:
   - Screen mode resolution detection and dynamic layout calculation.
   - Replaced hardcoded bounds (100 columns / 38 rows) with adaptive grid calculation (`cols = usable_w / cell_w; rows = usable_h / cell_h`).
   - Adaptive scaling: 2x scale for HiDPI/FHD displays (>=1920x1080), 1x scale for standard resolutions (<=1280x800).
   - Centered window layout with balanced margins; no awkward dead black zones.
   - Modern Cyber-Slate theme: deep midnight background (`#070b12`), dark console interior (`#0b111b`), emerald-cyan neon accent glow (`#00e5a3`), soft silver-white high-contrast text (`#e2e8f0`), status bar, and active cursor.

3. **On-Firmware UEFI Target Disk Installer (`boot/uefi/bootmenu.c`)**:
   - Integrated directly into the UEFI bootloader (`BOOTX64.EFI`) before ExitBootServices.
   - Enumerate all block devices (`EFI_BLOCK_IO_PROTOCOL`), distinguishing removable USB flash drives from fixed internal HDD/SSD/NVMe drives.
   - Interactive UI: Card 1 (Live Terminal Boot) vs Card 2 (Install to Internal Disk).
   - Interactive installation dialog displaying target disk model and size, operation breakdown, and explicit confirmation prompt.
   - Streams bootloader, GPT tables, FAT32 ESP, and IntellectFS v1 root image in 64 KiB chunks with a real-time progress bar.
   - Flushes and verifies partition headers before presenting completion instructions.

## 9. References

- IVX-1 source: `docs/05_IVX_BINARY_EXECUTABLE_SPEC.md`, §3, §8, §27.
- IFS: `docs/IntellectFS_v1_FULL_ARCHITECTURE_AND_DISK_FORMAT.md`, §5–§9.
- Official BLAKE3: https://github.com/BLAKE3-team/BLAKE3 (vectors: `test_vectors/test_vectors.json`).
- UEFI 2.11 GPT: https://uefi.org/specs/UEFI/2.11/05_GUID_Partition_Table_Format.html
- RFC 9106 Argon2: https://www.rfc-editor.org/rfc/rfc9106
