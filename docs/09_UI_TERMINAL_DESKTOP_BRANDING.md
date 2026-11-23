# Терминал, графическая оболочка и фирменный стиль

## 1. UX philosophy

IntellectOS должна быть понятной, аккуратной и быстрой. Удобный терминал обязателен, desktop — опциональная надстройка. UEFI загрузочное меню может быть графическим даже до появления полноценного Desktop runtime. Визуальные эффекты не должны изменять correctness boot, EFI key, память или устройство ввода. Профиль `quiet boot` выводит красивый экран; `verbose/debug` показывает диагностический лог, а при panic — читаемую ошибку.

## 2. Универсальная графическая композиция

Оформление **не** привязывать к одной геометрии: реальные width/height/pitch, формат RGBX/BGRX/bitmask по возможностям, центрирование в нормализованных координатах, safe margins, letterboxing/crop для фона, отсутствие потери соотношения сторон логотипа, нескольких density/DPI профилей и portrait. Минимальные размеры отрисовки и text-only fallback; недопустимы hard-coded x/y из `1920×1080` как требование. Система должна работать с 4:3, 5:4, 16:9, 16:10, ultrawide, HiDPI, малыми экранами; неподдерживаемый GOP mode → понятный fallback.

## 3. Утверждённый стиль бренда (материалы пользователя)

Цвета: background **`#070B12`** (тёмный графит), emerald accent **`#00E5A3`**, основной текст **`#F8FAFC`**, secondary soft blue-gray. Фирменный знак: тёмный изометрический куб/геометрическая оболочка с внутренними изумрудными линиями и центральным светящимся ромбом. **Три текущих изображения пользователя** размещены в `/home/quaxis/IntellectOS/media/img/`: отдельный логотип с прозрачным фоном, независимый фон, композиционный образец. Они являются asset source; сами по себе в UEFI binary не загружаются и не должны считаться интегрированными в runtime до сборочного конвертера и тестов. PNG пользовательские исходники сохранять неизменными.

Предлагаемый pipeline: source PNG (альфа, original) → reproducible offline conversion/quantization/size validation → compact binary resources in `build/` → freestanding graphics renderer in UEFI (без PNG-runtime) → proportional fit and clear overlay. Для будущего Desktop можно дополнительно создать clean vectorized scalable logo; не предполагать, что raster PNG становится вектором без реконструкции.

## 4. Заставка/анимация

Мягкое проявление символа, 2–4 секунды **как максимальный художественный сценарий, а не блокирующая задержка загрузки**; при быстрой загрузке нет ожидания окончания ролика. Для первых запусков процедурная animation/time-based fade, без runtime MP4 decode. Reduced Motion: static. При серийном/headless boot — никаких графических предпосылок.

## 5. Звук

Короткий фирменный startup chime (примерно 1–2 секунды) — **опция после работающего native audio stack**, не обязательный boot feature. Если драйвера/HDA не обнаружены, volume=muted, аппаратная ошибка или timeout — загрузка идёт дальше. Audio assets не должны исполняться как код, декодер bounded. Не воспроизводить звук с firmware speaker как «универсальный звук» без поддержки оборудования. Отдельные UI sounds для ошибок/уведомлений, только по согласию пользователя.

## 6. Терминал

Ring3 `.ivx` shell с readable fixed/variable monospace, font 8x16 or scalable raster, строка подсказки, history, command editing, clipboard только при поддержке, stream output, backspace/tab completion, scrollback, UTF-8 по мере реализации, serial/framebuffer/native USB keyboard backends. Команды диагностики (`help`, `about`, `pwd`, `echo`, `mem`, `clear`, `exit`) в начальной U1; будущие `ls/cat/cp/mv/ps/kill/pkg/install/drivers/net` требуют реальных syscalls/VFS/services. C1 kernel monitor остаётся recovery/diagnostic, не полноценный shell.

## 7. Login и Desktop

Экран входа использует те же цвета/логотип и адаптивную геометрию. `root` + optional password; пустой = Enter/документированный auto-login; неверная/повреждённая конфигурация не открывает всем доступ. Desktop compositor — user service с window manager, HID mouse/keymap, fonts, rendering, accessibility, themes, applications `.ivx`, network settings, package/update manager, optional AI sidebar. Graphical Edition на стадии до верификации runtime должна показываться как недоступная, а не менять ядро.

## 8. Приёмка UX

Ни bitmap, ни text выходить за framebuffer bounds при любых supported dimensions; alpha blending uses pixel format; no fixed-size malloc on untrusted asset; independent host tests verify clip, line length, scaling; QEMU visual review in multiple GOP modes; no GUI dependency for server; invalid image triggers fallback not panic. Boot and idle latency measured with/without animation. Sources in `media/img/` commit only after license/rights review.
