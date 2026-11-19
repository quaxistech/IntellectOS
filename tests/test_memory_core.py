"""Memory Core regression tests: pure allocator plus static loader/ELF guards."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class TestMemoryCore(unittest.TestCase):
    def test_physical_memory_allocator_on_host(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "pmm64-host"
            subprocess.run(
                ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                 "-Ikernel/include", "kernel/src64/pmm64.c",
                 "tests/pmm64_host.c", "-o", str(binary)],
                cwd=ROOT, capture_output=True, text=True, check=True,
            )
            out = subprocess.run([str(binary)], cwd=ROOT, check=True,
                                 capture_output=True, text=True)
            self.assertIn("PASS: PMM64", out.stdout)

    def test_runtime_image_budget_has_linker_assertion(self):
        linker = (ROOT / "kernel/x86_64/linker.ld").read_text()
        handoff = (ROOT / "boot/uefi/handoff.c").read_text()
        self.assertIn("__kernel_image_end <= 0x80000", linker)
        self.assertIn("#define KERNEL_PAGES INTELLECT_KERNEL64_PAGES", handoff)

    def test_uefi_bridge_and_bootinfo_abi_unchanged(self):
        asm = (ROOT / "boot/uefi/handoff.S").read_text()
        self.assertIn("mov %r8, %rdi", asm)
        self.assertIn("mov %r9, %rsi", asm)
        info = (ROOT / "kernel/include/bootinfo.h").read_text()
        self.assertIn("sizeof(intellect_bootinfo_t) == 152u", info)

if __name__ == "__main__":
    unittest.main()
