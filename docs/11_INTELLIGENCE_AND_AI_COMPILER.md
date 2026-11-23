# Интеллектуальные службы IntellectOS и ИИ-компилятор

## 1. Главное разделение

**AI не участвует** в `ExitBootServices`, проверке криптографии, MMU, syscall access, scheduler correctness, page faults, fsync durability или базовом IVX execution. Без model/network IntellectOS стартует, читает файловую систему, запускает подписанные приложения, показывает диагностику и компилирует обычные программы Vibelang детерминированным трактом. ИИ-слой — отдельные user-mode службы, ограниченные capability и consent.

## 2. Интеллектуальные возможности

- **Intent Gateway:** перевод задачи пользователя в структурированный план с typed operations и rights, не в прямой IO.
- **System Health Agent:** мониторинг CPU/RAM/disk/network/kernel events, объяснение ошибок, предупреждения, рекомендации performance/thermal/power policy.
- **Developer Agent:** генерация/рефакторинг `.vibe`, сбор тестов, анализ VSG/VBIR, debug и code review.
- **Performance Agent:** измеряет workloads, предлагает PGO, layout, caching, scheduler policy, энергосбережение; каждое предложение валидируется.
- **File/Knowledge Agent:** локальный поиск, индекс, semantic catalog, пользовательские разрешения на чтение и облачный доступ.
- **Automation/Task Agent:** периодические/условные действия, workflow, typed side effects, rollback, explicit approval для destructive операций.
- **Security Adviser:** рекомендации/аномалии, не выдаёт привилегии и не обходит cryptographic policy.
- **Optional GUI/voice Agent:** человекочитаемое управление, accessibility, локальная модель, network opt-in.

## 3. Автономность и approvals

| Level | Пример | Полномочия |
|---|---|---|
| L0 Observe | отчёт о диске | read-only/limited |
| L1 Suggest | рекомендация настройки | никакой записи |
| L2 Prepare | source patch в staging | sandbox/limited write |
| L3 Approved Act | подтверждённая пользовательская операция | exact scoped capability, audit |
| L4 Privileged Act | boot/kernel/driver policy | отдельная admin authorization, безопасный recovery |
| L5 Autonomous Core Change | полная автоматическая замена ядра/политик | **запрещено в базовом V1** |

Все предложения содержат `base_root/revision`, typed action, expected result, affected resources, proof/test plan, expiration, rollback. Model text никогда не может превращать `deny` в `allow`.

## 4. ИИ-компилятор

Обязательный pipeline `Vibelang parser → VSG verify → VBIR verify → native IVO/IVX` работает без AI. Недоверенная AI-ветвь может предлагать `SOURCE_EDIT`, `IR_TRANSFORM`, `LAYOUT_POLICY`; после предложения обязательно: canonical parse, type/effect/ownership check, translation validation where applicable, differential tests, fuzz, performance benchmark, explicit approve/reject. Никакой исполняемый текст ИИ напрямую в kernel memory, никакая подпись от «model verdict».

Имя отдельного AI compiler продукта ещё **не утверждено** — не придумывать новый торговый бренд без решения. Toolchain and OS interfaces versioned separately.

## 5. Приватность/потребление ресурсов

Local-first, explicit remote sync policy, offline operation, clear model/input provenance, sensitive-data filtering, no hidden telemetry, quotas for CPU/GPU/RAM/storage and time. AI crash/leak or network outage cannot prevent boot/session/logout. Logs with explicit user consent; default deny external transmit of code/secrets.

## 6. Gates

1. System starts with AI completely absent.
2. Agent cannot gain file/network/kernel rights via prompt injection or tool parameter confusion.
3. Identical inputs produce deterministic compile artifacts without AI.
4. Invalid source/IR/manifest proposal fails before native code generation.
5. Automated optimization never overrides safety or externally observed behavior without verified proof/explicit opt-in.
6. Disabled agent, failed inference, bad model updates and interrupted tasks restore full normal OS operation.
