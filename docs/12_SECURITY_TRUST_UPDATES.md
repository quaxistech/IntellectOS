# Безопасность, доверие, поставка и обновления

## 1. Разделение свойств

Целостность файла, происхождение по ключу, совместимость ISA/ABI, полномочия пользователя и корректность поведения — **разные проверки**. Signed IVX с запросом `device.write` не получает доступ к диску, пока OS capability broker не выдаст конкретный handle. Подписанное malware не становится безопасным, слабый пароль/пустой пароль не должен позволять silent remote login.

## 2. Поверхность атак

Malicious/corrupt USB or GPT/IFS metadata, invalid firmware maps, DMA and MMIO, compressed bombs, IVX parser differentials, mutable signed data/TOCTOU, failed power-loss transaction, IRQ race and user pointers, sideloaded drivers, ABI confusion/relocation overflow, compromised build host/key, malicious package registry, rollback replay, prompt injection against AI agent, remote network/TTY.

## 3. Слои доверия

1. **Boot:** UEFI secure boot compatibility/keys/recovery where supported; signed EFI binary and measured chain as a later policy. Development unsigned boot may require Secure Boot disabled and is not production grade.
2. **Kernel:** fail closed on memory map and page-table errors; validate privilege traps, W^X and all aliases; prevent user reads/writes to kernel and device MMIO.
3. **Storage:** IFS verified root/descriptor/seal/data; BLAKE3 only integrity, not antirollback vs privileged attacker without external trusted monotonic/root anchor.
4. **IVX:** exact hashed content, detached cryptographic signature and external trust-store/key scope. Developer-pinned one-file IVX is a bootstrap only; full generic signed executable loader still a separate gate.
5. **User/session:** root optional local password with explicit policy; KDF Argon2id if configured, constant-time verify; remote authentication not implied by empty local credential.
6. **Packages:** immutable artifact content roots, source receipts, SBOM, pinned dependencies, no silent install-time capability elevation; old closure stays runnable until commit switch.
7. **AI:** untrusted user-mode recommendations, sandbox and policy approval, logging and ability to disable.

## 4. Привилегии и capability model

Rights cannot be broadened by `import`, `manifest`, source `effect`, component launch or AI text. Handle carries object/rights/generation/holder. Transfer explicitly narrows permissions; stale handles rejected; audit privileged actions, use staged operations and rollback. Same-address-space `.ivl` plugin cannot be treated as isolated; security boundary requires process/IPC or equivalent hardware protection.

## 5. Ключи и доверие

Developer test keys separate from production release keys. No private keys or credentials committed to Git. Signing keys stored outside source tree with restricted access; reproducible builder receives authorized sign request, release signatures bound to content_root, policy and ABI. Trust-store activation, revocation and antirollback require durable trusted anchor. Support more than one signature algorithm through explicit registry, no algorithm downgrade fallback.

## 6. Атомарные обновления

Build new signed IVX/IVL/package closure; validate; write to reserved IFS generation; durable commit and seal; publish active pointer; revalidate at next boot; retain previous known-good root/bootloader and rollback path. Limit outstanding references before GC. Update the user-facing OS without breaking running process pinned closure. Split rollback for system image/firmware and user data, never overwrite user files with outdated backup silently.

## 7. Threat-model test matrix

Fuzz every parser and graph; mutate signature data and reserved fields; forge GPT, IFS seal and metadata; test unsigned/revoked/expired keys, malformed claims, wrong platform, privilege escalation and remote session; simulate crash on each write/barrier. Monitor logs without leaking passwords, keys, file contents; tests should not use real user disks.
