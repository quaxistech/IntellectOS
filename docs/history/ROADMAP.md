# Roadmap — current implementation and next gates

## Current checkpoint

- BIOS Foundation 0.4: QEMU and physical Lenovo observed, i386 protected-mode kernel with BootInfo v1.
- UEFI Stage E: QEMU/OVMF and physical Lenovo observed, `ExitBootServices()` and independent native x86-64 framebuffer kernel with BootInfo v1.
- `make clean check`: BIOS/UEFI image verification plus 72 `tests/` and 15 `host_tests/` tests pass.
- M1 D2 is the verified Memory Core baseline. M2 and M3-A, C1, H1/H2, S0, B1, L1, I2 and U1 have implementation and host-test coverage unless a checkpoint explicitly says runtime verification is pending.
- These are development milestones, **not** a security, portability or production certification.

## Next engineering priorities

1. **Kernel bootstrap hardening:** independent assertions for BootInfo and memory-range bounds, exception/IDT handling for the x86-64 path, useful serial or framebuffer panic diagnostics, stack and page-table invariants.
2. **Memory Core:** normalize and verify non-overlapping physical memory regions; track reserved ranges (kernel, stack, page tables, framebuffer, firmware/runtime, ACPI, MMIO), add a bounded physical page allocator with deterministic tests, then controlled virtual mappings.
3. **Uniform native kernel architecture:** keep BIOS/UEFI entry mechanisms separate but establish one documented 64-bit kernel entry protocol, where hardware permits; avoid silently breaking the tested i386 fallback.
4. **Safe hardware capabilities:** deterministic discovery, explicit permissions, independent drivers with guarded access, reproducible device diagnostics. Enumeration is not equivalent to a functional driver.
5. **Native-code synthesis foundation:** typed restricted IR → validated code generation → independently checked native code → controlled execution in an isolated environment. Test generated code against an oracle before allowing privileged operations.
6. **Local AI experimentation:** only after reliable allocation and isolation. The model selects/constructs validated plans and components; safety-critical memory, permissions and interrupt paths stay deterministic.

No scheduler, GUI, disk installer, USB HID implementation or Lenovo-specific repair workaround is a prerequisite for the immediate Memory Core milestone. Do not claim an intelligent self-generating OS before the code generation and local inference stages exist.
