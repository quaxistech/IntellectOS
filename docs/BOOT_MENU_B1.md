# IntellectOS B1 — Clean UEFI Boot Menu, Terminal Edition Preview

## Scope and invariants

- Replaces Stage E firmware *on-screen* diagnostics and the old graphical debug
  screen with a single boot menu drawn through UEFI GOP before ExitBootServices.
- The menu uses UEFI Simple Text Input for USB/PS2 keyboard support **only
  before ExitBootServices**. No firmware input calls are made by the kernel.
- `1`/Enter selects **Install Terminal Edition**; it boots a **terminal edition
  preview** with the existing H1/H2/S0 and L1 welcome. **There is NO disk
  installation dialogue and NO write operation yet.**
- `2`/Down selects **Install Graphical Edition**, which is deliberately disabled.
  Enter displays an explanation; it never falls through to install or boot it.
- After 12 seconds of keyboard inactivity with Terminal selected, boot the
  terminal preview, so a headless serial QEMU boot remains testable. When the
  Graphical entry is selected, the timeout does *not* override that choice.
- The kernel still writes hardware and error information to COM1 for QEMU, but
  normal M1/M2/M3/H1/S0 boot messages do not overwrite the screen. Fatal kernel
  exceptions may show framebuffer error text. Native L1 splash appears once,
  after the memory/hardware/storage subsystems initialize.
- The UEFI Block I/O adapter enumerates physical media-backed handles *before*
  ExitBootServices, **does not read/write blocks**, and is not a native driver.
  Existing read-only ATA PIO support remains the only native storage driver.
- BootInfo ABI v1, native kernel image/linker, Vibelang/IVX, BIOS boot path,
  IntellectFS format and on-disk partitions are unchanged.
- The image has no installer, disk partitioning, filesystem formatter, graphics
  compositor, USB HID runtime, NVMe/AHCI runtime or generalized driver installer.
  These are explicitly future implementation gates.

## Provenance

- Protocol signatures/layouts follow UEFI 2.x Simple Text Input, GOP,
  Block I/O, Boot Services. This package includes **original adapter code**;
  no third-party driver implementation is misrepresented as integrated.
- Existing `third_party/DRIVER_SOURCE_REGISTRY.md` documents EDK II,
  FreeBSD, TinyUSB source/license links for future native driver adaptation.

## Test strategy

1. `make clean check` covers host simulation, ABI assertions, image verification.
2. `make uefi-b1-smoke` boots UEFI and checks native kernel on virtual COM1,
   including the 12-second menu timeout.
3. `make uefi-console-gui`: inspect menu; press `2`, Enter (must not boot), then
   `1` to boot. Inspect absence of prior Stage E/debug screens and L1 welcome.
4. On Lenovo, install no partitions and avoid enabling write operations. Verify
   USB keyboard controls the firmware-stage menu. After `ExitBootServices`,
   keyboard use is not yet supported by our kernel (no native USB HID driver).
5. A successful QEMU boot does not prove USB/Lenovo keyboard compatibility.

## Next work

Implement a capability-constrained native HID input stack, USB xHCI or PS/2
keyboard transport, robust block adapters for AHCI/NVMe, separate storage
installer capability, GPT/IntellectFS creation, explicit destination selection,
and a transactional install workflow with rollback and recovery image.
