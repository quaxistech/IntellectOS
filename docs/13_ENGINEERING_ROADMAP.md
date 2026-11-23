# План реализации и зависимости — один последовательный продукт

**Основное правило:** приоритет — полноценный установленный терминал с проверенным `.ivx`/IntellectFS/`root`, затем переносимость и остальные редакции. Не принимать отдельные 80%-готовые модули за проход end-to-end gate. Не переписывать существующую рабочую базу ради нового UI.

## Фазы и закрытие задач

| Gate | Зависимости и обязательная работа | Условие завершения |
|---|---|---|
| **G0 Git/Build** | `main`, tests, toolchain pin, reproducible clean build, CI gates | чистый build, diff check, smoke QEMU |
| **G1 Boot kernel** | BIOS fallback, UEFI GOP/BootInfo, PMM/VMM/M3, exceptions | independent kernel after EBS on QEMU + hardware profiles |
| **G2 Ring3/TTY** | U1 process trap, input/output, terminal | actual `int 0x80` executable, read/write, graceful exit |
| **G3 Disk boot** | installer, GPT/ESP, disk verification | 2-stage QEMU USB→HDD without USB; physical trial after risk gates |
| **G4 IFS strict boot** | freeze boot profile, formatter, C-reader, fsck inspect, corruption tests | correct `/etc`, `/sbin` from sealed system partition, no fake FAT32 root |
| **G5 IVX general v1** | signed parser/trust policy, load plan, isolated process, root file | executable from IFS, real signature/permissions, no embedded pin dependency |
| **G6 Identity/init** | startup supervisor `init`, root/session, optional password, timezone | valid login/Enter with empty password, no bypass on corrupt record |
| **G7 IFS RW durability** | CoW mutator, seal, barriers, allocator/GC, recovery | crash exploration all write boundaries, reboot persistence and fsck |
| **G8 Modern hardware** | AHCI/NVMe/USB HID and mass storage, 4Kn/512e, timers/IRQ | multiple PC/QEMU configs install and boot, runtime input works |
| **G9 Terminal beta/server** | tty abstraction, networking, remote auth, service manager, pkg updates | headless boot, secured remote TTY, recovery/rollback |
| **G10 Vibelang compiler** | language parser/VSG/VBIR/IVO/linker, independent verifiers | `.vibe → signed .ivx` non-AI end-to-end |
| **G11 AI tools** | broker/caps/sandbox, deterministic compiler backend | safe opt-in AI, model failure leaves OS working |
| **G12 Desktop** | graphics driver, compositor, window manager, HID/fonts/input | responsive multi-resolution UI no terminal regression |
| **G13 Release** | independent audit, fuzz, hardware matrix, signed boot/update, docs | defined supported systems, reproducible release, recovery drill |

**Dependency note:** G3–G6 are tightly coupled for a real installed terminal; a partial success of GPT creation must not be marked as installer release. Developer compiler parsing/VSG and graphics resources may proceed in parallel, but never displace critical path. Device/IFS RW development can parallelize with G5 only under strict on-disk ABI coordination.

## Mandatory engineering sequence per gate

1. Define exact contracts and invariants, specify negative cases and resource budgets.
2. Implement pure/parser/host model; include independent validator with golden fixtures and corrupt inputs.
3. Integrate freestanding code without changing boot ABI silently; compile -Werror and inspect relocations/binary sizes.
4. Run `make clean check`, focused smoke QEMU, then 2-stage end-to-end if storage affected; capture output and code commit.
5. Validate physical hardware **only after** QEMU gate; guard all destructive operations.
6. Update `15_CURRENT_IMPLEMENTATION`, decisions and requirements matrix in same main commit.
7. Mark gate PASS only with reproducible logs and exact SHA/host profile.

## Immediate outstanding actions (per 2026-11-23 main)

- Align `tools/provision_root_config.py` prompt with accepted optional password; do not repeat 12+ requirement.
- Verify runtime `login` is more than stored `/etc/system.json` and host Argon2; implement policy when absent.
- Replace pinned one-program loader with generic signed IVX and trust store; preserve working U1.
- Verify actual filesystem writer beyond boot image; complete IFS transaction durability before full files service.
- Prove UEFI on-firmware installer writes safe correct bytes and same target boots without USB in QEMU; independently test physical Lenovo when agreed.
- Add native modern storage and USB input; do not say universal because firmware preboot drivers worked.
- Integrate user-supplied images into build/UEFI only after deterministic conversion, asset sizes, resolution-independent rendering and QEMU visual test.

## Parallel engineering areas

**Safe now:** parser fuzzing/ABI freeze; build CI; fonts/branding conversion; CLI ergonomics; host language frontend; hardware capability discovery. **After kernel process manager:** full IVX library loading, packages, network services. **After trusted broker:** AI assistants and autonomous operations. **After graphics/input:** Desktop edition and optional audio.
