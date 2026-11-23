# Ядро: память, процессы, syscalls, IPC, runtime

## 1. Ядро как минимальное доверенное основание

TCB включает CPU exception/interrupt entries, page-table ownership, PMM/VMM, scheduler/process lifetime, IPC/capability mediation, time, device safety. Не встраивать сюда полноценный LLM, Vibelang compiler, архиватор, package resolver или произвольный пользовательский JSON parser. Критически привилегированный код small, bounded, freestanding и проверяемый.

## 2. Memory Core

- **PMM:** нормализация firmware memory ranges, физические страницы 4KiB, reserved kernel/stack/page tables/framebuffer/ACPI/SMBIOS/MMIO/firmware, double-free and overflow checks. Выдавать страницу лишь после доказанной доступности и ownership.
- **VMM:** explicit map/unmap flags `PRESENT`, `RW`, `USER`, `NX`, cache policy; high-half scratch map, guard pages, TLB flush, no stale alias. Bootstrap identity map не является финальным security boundary. Kernel TEXT RX, RODATA R+NX, DATA/heap RW+NX; CR0.WP и EFER.NXE.
- **Heap:** bounded kernel allocator, non-executable pages, fragmentation/budget, no allocation inside unbounded fault path, page ownership.
- **Threads:** kernel stack per thread with guard, saved registers, privilege transitions, cleanup of TLS/handles, synchronous + later preemptive scheduler.

## 3. Реальный Ring 3 и этап после U1

U1 уже имеет механизм одиночного CPL3 исполнения: `iretq`, TSS.RSP0, DPL3 `int 0x80`, user RX code и RW+NX stack, `write/read/clear/info/exit`. Это **не** ещё универсальная многопроцессная ОС. U2: отдельный process address space/CR3, независимые kernel stacks, scheduler (time source, timer IRQ), processes wait/exit/reap, async input, two-process memory isolation, resource quotas, fork не обязателен в V1. Dynamic command line / tty service не должна работать как вечный busy loop с IRQ выключенными.

## 4. Нормативный syscall подход

C ABI не передаёт произвольные kernel pointers userland. Every user buffer checked for canonical address, mapped range, overflow, permissions, length before copyin/copyout (включая race). Syscalls default deny; коды ошибок типизированы. Handles имеют type, rights, generation, close and transfer rules; закрытый handle нельзя использовать повторно после allocator reuse.

Группы будущего User ABI:

| Группа | Операции | Ограничение |
|---|---|---|
| process/thread | spawn_verified, wait, exit, yield | only signed/authorized artifacts |
| memory | reserve/map/unmap/protect | W^X, page limit, no kernel alias |
| IPC | channel create/send/recv, wait_many | typed payload bounds, credits/backpressure |
| filesystem | open/read/write/transaction/sync | pathname -> VFS -> capability |
| time | monotonic, timer arm/cancel | security never depends on wall clock |
| device | enumerate/open/io submission | exclusive DMA/IRQ permissions |
| user identity | session/current principal | no unauthenticated escalation |

## 5. IPC и службы

Службы общаются через канал/буфер, где envelope (event kind, version, sender principal, handle table, length, generation) проверяется до аллокации. Не доверять сериализованному rights claim отправителя. Отдельные процессы предпочтительнее неограниченного выполнения недоверенного плагина `.ivl` внутри того же address space.

## 6. Отказы и безопасность

User fault: terminate/notify only process when isolation allows; kernel fault: bounded panic/serial/recovery, no silent restart corrupted state. Page fault path не использует AI и не интерпретирует unbounded IO. Traps require consistent register frame and stack alignment. IRQ handlers must not reenter single-writer data. SMT, SMP and interrupts enablement are separate maturity gates; false security claims from NX bits only are forbidden.

## 7. Проверка

1. PMM excludes all reserved ranges, double alloc/free and OOM rollback.
2. W^X protection includes aliases, user cannot access kernel, stack NX, noncanonical access faults cleanly.
3. Two process spaces cannot read/write each other, stale handles rejected.
4. Scheduling continues with blocked TTY and I/O, no starvation, monotonic deadlines, SMP TLB coherence when enabled.
5. Fuzzer tests malformed user pointers/IPC bytes; no arbitrary kernel writes.
6. U1 regression still passes after generic IVX process manager replaces static one-user executor.
