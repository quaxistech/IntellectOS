# Uploaded Stage E runtime baseline (2026-11-18)

The SHA-256 values below describe binary artifacts **found inside the user-uploaded `IntellectOS(1).zip`** after the reported QEMU and Lenovo Stage E tests. They are not Git-tracked outputs and are recorded only for build provenance.

| Archive member | SHA-256 |
|---|---|
| `build/intellectos-bios.img` | c26811cefa67ebcd4eb838c81413e8a708ad08cd16b026005d24471938821316 |
| `build/intellectos-uefi-stage-e.img` | a2bfb40012692692a352f1a09bff7bf50354e2b5a109c643fce92d40c96a7be2 |
| `build/uefi/BOOTX64.EFI` | 8018e5829dd86c4658d652c5b77127e57f8d777a4e4a04dbc71b53aac5b2da77 |
| `build/kernel64.bin` | 4ad14f65ac7ae47a839327e044673fe7b45ec41ee1a4e344909709feed868572 |

The audit environment regenerated the BIOS image byte-for-byte, but regenerated EFI/kernel artifacts were not byte-identical; differences can arise from Clang/LLVM/linker versions, flags and environmental assumptions. This is **not evidence of a runtime defect**, nor proof the regenerated output is equivalent. Use the same pinned toolchain and compare hashes before marking a future build reproducible.
