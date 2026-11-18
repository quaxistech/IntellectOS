"""Cross-platform BootInfo ABI v1 layout regression checks."""
import ctypes
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class Range(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [("base", ctypes.c_uint64), ("length", ctypes.c_uint64),
                ("type", ctypes.c_uint32), ("attributes", ctypes.c_uint32)]


class BootInfo(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("magic", ctypes.c_uint32), ("version", ctypes.c_uint16),
        ("header_size", ctypes.c_uint16), ("total_size", ctypes.c_uint32),
        ("boot_method", ctypes.c_uint32), ("flags", ctypes.c_uint32),
        ("boot_drive", ctypes.c_uint32), ("firmware_revision", ctypes.c_uint32),
        ("uefi_revision", ctypes.c_uint32), ("video_kind", ctypes.c_uint32),
        ("bios_video_mode", ctypes.c_uint32), ("framebuffer_addr", ctypes.c_uint64),
        ("framebuffer_size", ctypes.c_uint64), ("framebuffer_width", ctypes.c_uint32),
        ("framebuffer_height", ctypes.c_uint32), ("framebuffer_pitch", ctypes.c_uint32),
        ("pixel_format", ctypes.c_uint32), ("memory_map_addr", ctypes.c_uint64),
        ("memory_map_count", ctypes.c_uint32), ("memory_map_stride", ctypes.c_uint32),
        ("acpi_rsdp_addr", ctypes.c_uint64), ("smbios_addr", ctypes.c_uint64),
        ("pixel_red_mask", ctypes.c_uint32),
        ("pixel_green_mask", ctypes.c_uint32),
        ("pixel_blue_mask", ctypes.c_uint32),
        ("pixel_reserved_mask", ctypes.c_uint32),
        ("firmware_vendor", ctypes.c_char * 32),
    ]


class BootInfoTests(unittest.TestCase):
    def test_abi_sizes_and_offsets(self):
        self.assertEqual(ctypes.sizeof(Range), 24)
        self.assertEqual(ctypes.sizeof(BootInfo), 152)
        self.assertEqual(BootInfo.boot_method.offset, 12)
        self.assertEqual(BootInfo.flags.offset, 16)
        self.assertEqual(BootInfo.memory_map_addr.offset, 72)
        self.assertEqual(BootInfo.memory_map_count.offset, 80)
        self.assertEqual(BootInfo.memory_map_stride.offset, 84)
        self.assertEqual(BootInfo.acpi_rsdp_addr.offset, 88)
        self.assertEqual(BootInfo.smbios_addr.offset, 96)
        self.assertEqual(BootInfo.firmware_vendor.offset, 120)

    def test_bios_stage_initializes_abi_fields(self):
        src = (ROOT / "boot/bios/stage1.S").read_text()
        for instruction in (
            "movl $0x49534f33,0x5000", "movw $1,0x5004",
            "movw $152,0x5006", "movl $152,0x5008",
            "movl $1,0x500c", "movl $1,0x5020",
            "movl $0x6000,0x5048", "movl $24,0x5054",
            "movl %eax,0x5050", "movl %eax,0x5058", "movl %eax,0x5060",
        ):
            self.assertIn(instruction, src)

    def test_power_and_input_experiments_removed(self):
        self.assertFalse((ROOT / "kernel/src/acpi.c").exists())
        self.assertFalse((ROOT / "kernel/src/keyboard.c").exists())
        main = (ROOT / "kernel/src/kernel.c").read_text()
        self.assertNotIn("keyboard_poll", main)
        self.assertNotIn("acpi_s5_probe", main)


if __name__ == "__main__":
    unittest.main()
