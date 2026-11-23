# Полный реестр идей и требований к реализации

**Статусы:** TARGET — обязательная цель, PARTIAL — неполный исходный код, TESTED — конкретная проверка; пустой статус никогда не означает PASS. Этот реестр является контрольным списком полноты, а не результатом тестов.

| ID | Идея/требование | Документы/формат | Реализация/acceptance |
|---|---|---|---|
| R-001 | Независимая native OS без Linux/GRUB | 01,02,03 | UEFI/BIOS boot; native kernel after EBS |
| R-002 | Для разных PC, не только Lenovo | 01,04 | AHCI/NVMe/USB/PCI/GOP conformance matrix |
| R-003 | Legacy BIOS fallback и первичный UEFI | 03 | BIOS+OVMF separate source and hardware tests |
| R-004 | BootInfo ABI v1 152B / memory 24B | 03 | `bootinfo.h`, producer/consumer fixtures |
| R-005 | Protected memory, PMM, VMM, heap, NX/W^X | 05 | M1/M2/M3-A, fault injection |
| R-006 | Single-core U1 and future scheduler/SMP | 05 | ring3, timers, multi-process, TLB shootdowns |
| R-007 | Typed bounded syscalls/IPC/capability | 05,07,12 | no privilege escalation, handles/generations |
| R-008 | Hardware discovery H1 and drivers H2 | 04 | inventory != actual adapter, negative hardware tests |
| R-009 | Disk detection and GPT/FAT32 ESP | 03,04,08 | CRC/readback, secure target selection |
| R-010 | Full **IntellectFS** native CoW file system | 06, specs/IntellectFS... | boot profile + full RW seal/GC/crash tests |
| R-011 | VFS native mounts and file namespaces | 06 | read/write/rename handles and mounted root |
| R-012 | IVX native `.ivx` signed image | 07, specs/IVX... | independent full parser and trusted loader |
| R-013 | `.ivl` libraries, closure, TLS, unwind | 07,10 | verifier, linker, load plan, per process isolation |
| R-014 | `.ivo`, `.ivpkg`, `.ivdbg`, lock, receipt | 10 | signed install, reloc/debug bindings |
| R-015 | `.vibe` Vibelang compiled language | 10 | lexer/parser, effects, ownership, FFI, diagnostics |
| R-016 | VSG/VBIR canonical verifiable IR | 10 | byte fixtures, semantic+SSA check |
| R-017 | Deterministic no-AI compiler | 10,11 | source→vsg→vbir→ivo→ivx, reproducible |
| R-018 | Optional AI compiler suggestions | 11 | proposal validation, differential tests, benchmark |
| R-019 | IntellectOS Health/Dev/Performance/Automation AI | 11 | local-first, broker, offline and opt-in |
| R-020 | User trust, capabilities, privacy | 12 | permission narrowing, no secret leakage, audit |
| R-021 | Signed package updates, snapshots, rollback | 06,12 | atomic root/closure, recover old image |
| R-022 | Real USB installer on target PC | 08 | UI, target identity, GPT/IFS/IVX verification |
| R-023 | Partition selection/create/delete/format | 08 | full disk first, manual partition operations after gate |
| R-024 | Standalone HDD boot after USB removed | 08,14 | 2-stage QEMU + physical test |
| R-025 | Initial `root` user, password optional even empty | 01,08,16 | Enter password or explicit auto-login, no 12+ gate |
| R-026 | Configured IANA timezone/RTC UTC | 08 | `/etc/system.json`, runtime verified |
| R-027 | Native `init` then real ring3 terminal `.ivx` | 02,07,08 | signed IFS file and process spawn |
| R-028 | Console-first personal and server mode | 01,02,09 | TTY, headless, remote serial, services |
| R-029 | Later Desktop/Graphical Edition | 01,09 | userland compositor, native HID, window manager |
| R-030 | Unified look logo/splash/installer/login/TTY | 09 | render at any viewport, original assets from `media/img` |
| R-031 | Startup animation and optional sound | 09 | non-blocking animation, native audio only, mute |
| R-032 | GUI responsive to size/shape/pitch | 04,09 | tested 4:3, 5:4, 16:9, 16:10, ultra, portrait |
| R-033 | Keyboard and mouse via native USB HID | 04,09 | xHCI after EBS; no reliance on firmware |
| R-034 | Modern storage and 4Kn/512e | 04,06 | AHCI/NVMe/USBMSC, geometry/flush tests |
| R-035 | Server networking and secure remote terminal | 02,04,12 | network stack, remote auth independent from empty root |
| R-036 | System status/logs/rollback/recovery console | 02,08,12,14 | safe degraded mode and crash drills |
| R-037 | Binary/FS v1 without fake migration generations | 00,16,19 | freeze fixtures, no fictional backward compatibility |
| R-038 | GitHub `main` and one local root | 01,18 | no duplicate project, commit only tested code |
| R-039 | Reuse known driver designs with licensing checks | 04,12 | source/license/SBOM provenance register |
| R-040 | Honest test/benchmark/hardware evidence | 14,15 | host/QEMU/hardware separate statuses |
| R-041 | Protect developer's Ubuntu host disk | 08,14,18 | fail-closed `/dev/*` handling, explicit user target |
| R-042 | Robust install writes with verification and fallback | 06,08 | byte-for-byte EFI, IFS root, crash behavior |
| R-043 | Run OS without AI, network or GPU | 01,02,11 | independent boot/tty/compiler baseline |
| R-044 | Portability to other ISA after explicit ports | 01,04,07 | AArch64 profiles after x86-64 maturity |
| R-045 | Fast native startup, efficient memory and I/O | 01,02,06,07,14 | comparative reproducible benchmarks |
| R-046 | Accessibility/quiet mode/no fixed effects | 09 | reduced motion, mute, simple visuals, terminal fallback |
| R-047 | Full password security if opted in | 08,12 | Argon2id salt/version, constant-time verify, recovery |
| R-048 | A checked, immutable installation closure | 07,12 | signed dependencies and atomic activation |
| R-049 | A clean, nonduplicating documentation set | 00,16,18 | root canonical, specs/research/history non-competing |
| R-050 | Safe human-directed system intelligence | 11,12 | L0-L4 caps, L5 core autonomy prohibited V1 |

## Контроль отсутствия забытых подсистем

До признания новой архитектурной редакции complete проверять: BIOS/UEFI, memory/interrupts/time, device drivers, block/partitions, IFS/VFS, ring3/TTY, IVX/ABI/lib/packages, init/login/root/timezone, installer/recovery, server/network, desktop/HID/graphics/audio/branding, Vibelang, AI compiler/agents, trust/update/rollback, tests/matrix, production workflows/licenses. Каждый новый запрос добавляется в этот реестр с уникальным ID и владельцем. Не удалять требования без решения в 16.

**Не все функции перечислены как разработанные.** «Сохранить идею» здесь означает наличие целей и критериев, не гарантию реализованного кода.
