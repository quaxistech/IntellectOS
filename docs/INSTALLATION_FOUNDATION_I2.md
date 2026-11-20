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
| Separate CPL3 terminal from U1 | USER-CONFIRMED PASS on QEMU, but embedded raw user bytes |
| Native GPT/FAT32 read-only file access | CODE + HOST PASS; QEMU pending |
| UEFI GPT + bootable FAT32 ESP image | CODE + HOST PASS; QEMU pending |
| IVX-1 signed reference profile | HOST PASS; actual kernel `exec(.ivx)` NOT IMPLEMENTED |
| IntellectFS `mkifs`, read, write, crash recovery | NOT IMPLEMENTED |
| `root` Argon2id login within IntellectOS | HOST CONFIG ONLY; NOT IMPLEMENTED |
| Installer interactive GUI and partition writer | PLAN ONLY; NOT IMPLEMENTED |
| Self-hosted boot after removing USB | GPT EFI boot image prepared; QEMU/hardware pending |
| Automatic xHCI/HID, NVMe/AHCI for Lenovo | NOT IMPLEMENTED |

## 7. Required work to reach the complete user's requested installer

- Freeze `IVX-1` MANIFEST, trust policy and kernel-side Ed25519/BLAKE3; implement real `.ivx` file loader using VFS, and an init/login/shell process model (currently one single U1 program and no scheduler).
- Freeze IntellectFS on-disk records and complete `mkifs`, CoW transactional writer, independent `ifsck`, read-only then RW mount and crash-injection engine.
- Complete native storage (AHCI/NVMe/USB mass storage) and input (xHCI/HID), rather than assume firmware input works after ExitBootServices.
- Implement UEFI installer UI/selection and a BlockIO-backed *confirmed* GPT writer, install the verified root into IntellectFS, EFI boot assets into ESP, then reboot and authenticate as root.
- Acceptance must include install onto disposable QEMU disk, remove install USB, boot to login, password verification, `ls/cat`, reboot, and power-loss tests. Only after that trial on the user's disposable Lenovo HDD.

## 8. References

- IVX-1 source: `docs/05_IVX_BINARY_EXECUTABLE_SPEC.md`, §3, §8, §27.
- IFS: `docs/IntellectFS_v1_FULL_ARCHITECTURE_AND_DISK_FORMAT.md`, §5–§9.
- Official BLAKE3: https://github.com/BLAKE3-team/BLAKE3 (vectors: `test_vectors/test_vectors.json`).
- UEFI 2.11 GPT: https://uefi.org/specs/UEFI/2.11/05_GUID_Partition_Table_Format.html
- RFC 9106 Argon2: https://www.rfc-editor.org/rfc/rfc9106
