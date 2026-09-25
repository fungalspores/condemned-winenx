# Condemned: Criminal Origins on Nintendo Switch (Wine-NX)

[English](#english) · [Русский](#русский)

The 2005 PC game running on a (modded) Nintendo Switch through
[Wine-NX](https://github.com/danfromtico/wine-nx): menus, camera, combat, sound,
and a HOME menu icon that starts the game directly. This repository is the
overlay only — **no game files**. You need your own PC copy of Condemned.

![status](https://img.shields.io/badge/status-playable-brightgreen)

---

## English

### What you get

- `dinput8.dll` — a DirectInput proxy that gives the game a mouse and keyboard
  Wine-NX does not deliver (right stick = look, D-pad = move and menus, full pad map),
  and makes saves take a fraction of a second instead of minutes.
- `imaadp32.acm` — Wine's IMA ADPCM codec; without it the game's sound driver
  silently turns sound off.
- `condemned-setup.exe` — run once on the console: fixes `Condemned.exe`'s header,
  disables conflicting files, checks the folder. (It also registers DirectSound, which
  Wine-NX has done by itself since Test Build 4.)
- One NSP: **Condemned: Criminal Origins**, which starts the game in Wine-NX without
  its menu. The setup program is no longer a second icon: the screen before the game
  ([winenx-start](https://github.com/fungalspores/winenx-start)) checks whether
  `Condemned.exe`'s header has been fixed and runs the setup itself when it has not.
  That screen also opens the game's settings on **+**, five seconds before it starts.
- `Condemned.keys.txt`, `Condemned.wine-nx.txt` (DXVK), `autoexec.cfg` (1280×720).

About 30 fps on the author's console, overclocked: CPU 1683 MHz, GPU 537 MHz, RAM 2333 MHz.
The log of the last run is `switch/wine/logs/Condemned.log`.

### Requirements

- Switch with Atmosphère **1.8.0+** and sigpatches (for the NSPs).
- SD card formatted **exFAT** (`CondemnedA.Arch00` is 6.2 GB).
- [Wine-NX Test Build 4](https://github.com/danfromtico/autorun/releases/tag/test-build-4) (Autorun).
- Condemned: Criminal Origins for PC, installed, **without SecuROM** — Wine-NX
  cannot run the disc protection. Tested with the English game plus the Spirit Team
  text and Team Raccoon voice translations.

### Install

1. Copy Wine-NX Test Build 4's `switch` folder to the SD card root.
2. Copy `wine-nx-start.nro` from
   [winenx-start](https://github.com/fungalspores/winenx-start) to `sdmc:/switch/wine/`:
   the screen the icon opens before the game, and what runs the setup below.
3. Copy your installed game (the folder with `Condemned.exe`) to
   `sdmc:/switch/wine/drive_c/condemned/`.
4. Copy `release/switch` from this repository over the card, **replacing files**
   (the game's own `dinput8.dll` must be replaced by the pack's).
5. Install the NSP from `release/` (DBI, sphaira, Tinfoil).
6. Start **Condemned: Criminal Origins**. The first time, the screen sees that the game
   has not been set up and runs `condemned-setup.exe` instead: wait for "Setup done",
   press A, then start the same icon again and play.

The taser is on the right stick press (R3) out of the box. Raise the mouse
sensitivity in Options → Controls to taste.

### Controls

| Switch | Key sent | In menus | In game |
|---|---|---|---|
| Left stick / D-pad | arrows (+W/S/A/D in game) | navigate | move |
| Right stick | mouse | — | look |
| A | Enter (+left mouse) | select | attack |
| B | right mouse | — | block |
| X | E | — | use / pick up |
| Y | F | — | flashlight |
| L | Tab | — | melee / firearm |
| R | Ctrl | — | free |
| ZL | Shift | — | run |
| ZR | Space | — | kick |
| L3 | R | — | check ammo |
| R3 | middle mouse | — | taser |
| + | Esc | back | pause |
| − | T | — | forensic tools |

### Build from source

```sh
sh tools/fetch_winenx.sh                 # Wine-NX Test Build 4 into components/wine-nx
sh tools/fetch_d3dx9_27.sh               # d3dx9_27.dll from Microsoft's DirectX redist
sh tools/build_win32.sh                  # dinput8.dll, condemned-setup.exe, imaadp32.acm (llvm-mingw)
pip install cryptography pillow lz4
python3 nsp/make_icon.py                 # icons from the game's splash screens (needs game/)
python3 nsp/build_nsp.py                 # both NSPs; header_key from ~/.switch/prod.keys
python3 tools/assemble_sd.py             # full card layout in ./sd from game/ + components/ + pack/
```

`tools/build_win32.sh` downloads llvm-mingw 20260505 if `LLVM_MINGW` is not set.
The NSP builder needs your own `prod.keys` (only `header_key` is used).

### How it was made

Each problem, what caused it and the fix, in the order they showed up.

1. **`map target status=c000007b`.** The no-SecuROM `Condemned.exe` has
   `SizeOfImage` 0x18e2f4, not rounded to the page size, which Wine-NX build 108
   rejects (fixed upstream in 109). It also has a writable *shared* section
   (`.SHARED`), which Wine maps from a shared file the Horizon server never
   provides. Fix: round `SizeOfImage` and clear `MEM_SHARED` — 3 bytes, done by
   `condemned-setup.exe` on the console (`tools/fix_condemned_exe.py` on a computer).
2. **Missing `d3dx9_27.dll`.** Not in Wine-NX; Microsoft's redistributable DLL is used.
3. **Black screen forever.** `EAX.DLL` creates DirectSound through COM, and
   Wine-NX never ran wineboot, so `CLSID_DirectSound8` was not registered. The
   game's error box was hidden behind the Vulkan surface. Fix: the setup program
   registers `dsound.dll`. Test Build 4 registers it on its own (`config/classes.reg`),
   so the setup program only repeats what is already there.
4. **No mouse.** Wine's DirectInput 8 reads the mouse only through raw input, and
   Wine-NX's Horizon server queues raw input for the keyboard but not the mouse.
   Fix: the `dinput8.dll` proxy loads Wine's `dinput8.dll` and fills the system
   mouse's `GetDeviceState`/`GetDeviceData` itself — cursor movement from the right
   stick (smoothed, re-centred while the game hides the cursor) and buttons from the
   key state.
5. **No keyboard in play.** The game's 696-byte DirectInput keyboard format also
   got nothing. Fix: the proxy builds it from the key state; each arrow also presses
   W/S/A/D, so the D-pad drives both menus (window messages) and movement.
6. **No sound.** Found by wrapping all 175 methods of `SndDrv.dll`'s sound object
   and logging which the engine calls: its ACM setup needs PCM, IMA ADPCM and MP3
   codecs and, when they are not registered, loads `IMAADP32.ACM` and
   `L3CODECA.ACM` itself. Wine-NX ships only the MP3 one, so the engine silently
   disabled sound. Fix: Wine 10.0's `imaadp32.acm`, built with llvm-mingw, next to
   the exe. The proxy also turns the game's hardware DirectSound buffers into
   software ones (Wine has no hardware mixing).
7. **Stutter on every start.** DXVK keeps its shader cache in
   `DXVK_SHADER_CACHE_PATH` or `%LOCALAPPDATA%`; Wine-NX's fixed environment has
   neither. Fix: the proxy sets `DXVK_SHADER_CACHE_PATH` to `C:\condemned\dxvk-cache`.
8. **HOME icon.** `nsp/build_nsp.py` is sphaira's on-console forwarder builder
   (`owo.cpp`) ported to Python: sphaira's nx-hbloader forwarder with its NPDM set
   to the **32-bit (no alias)** address space (the exe has no relocations and must
   load at 0x400000), and a romfs `nextArgv` that starts Wine-NX with the game's
   path, which makes the runtime skip its menu. Lesson learned: CNMT content records
   use `NcmContentType` numbers (Program = 1, Control = 3), not the NCA header's
   (0 and 2) — with the wrong ones HOME shows an endless loading tile.
9. **Every autosave froze the game for 2–3 minutes.** The log's busiest system call
   during the freeze was `NtWriteFile`, ~900 a second with the CPU idle: the engine
   writes a 1.2 MB save a few bytes per `WriteFile` (135,000 calls), and on Wine-NX
   each one goes through the Horizon server to the SD card. Fix: the proxy hooks the
   file imports of the five modules that write files and keeps a 256 KB window per
   file, tracking the file position itself, so writes and the engine's constant
   seeks back (to fill in chunk sizes) cost nothing; the window goes to the card in
   one piece. A plain buffer that every seek flushed still took ~7 s (8,000 card
   writes); the window takes **0.15 s (13 card writes)**. It was checked against
   direct writes with 6 million random operations before going to the console.

The engine is LithTech Jupiter EX; the released
[No One Lives Forever 2 source](https://github.com/wilkie/no-one-lives-forever-2)
helped read its input and sound code.

### Repository

| Path | What |
|---|---|
| `pack/` | Files for the game folder: pad map, Wine-NX sidecars, `autoexec.cfg`, `default.archcfg` (Russian text only) |
| `tools/src/dinput8_proxy` | The DirectInput proxy (C, no C runtime) |
| `tools/src/condemned_setup` | The setup program and the header fix (`pe_fix.h`) |
| `tools/src/imaadp32` | Wine 10.0's IMA ADPCM codec (LGPL-2.1) with build shims |
| `tools/build_win32.sh` | Builds the three Win32 files with llvm-mingw |
| `nsp/` | NSP builder and icon maker |
| `tools/assemble_sd.py` | Builds the whole card layout from `game/`, `components/` and `pack/` |
| `release/` | Prebuilt files and both NSPs |
| `LICENSE` | GPL-3.0 |

### Licence and credits

The pack is **GPL-3.0**, see [LICENSE](LICENSE). `nsp/build_nsp.py` is a port of
sphaira's `owo.cpp` (GPL-3.0), so the whole repository follows it. Parts keep their
own terms:

- `tools/src/imaadp32/` is Wine's code, **LGPL-2.1-or-later** (headers in the files).
- The NSPs in `release/` contain sphaira's nx-hbloader forwarder, **ISC**.
- `release/.../d3dx9_27.dll` is Microsoft's DirectX redistributable.

Wine-NX and mesa-switch by danfromtico · sphaira by ITotalJustice · Wine · DXVK ·
Mesa · hactool (used to check the NSPs) · Condemned: Criminal Origins by Monolith
Productions.

### Support the work

The pack is free and always will be. If it earned it:

- **Boosty** — <https://boosty.to/fspores>
- **USDT (TRC-20 only)** — `TZ5kQSx4HSe8GWr9SfVunYJDofcGLtPWZ7`
- **Channel** — <https://t.me/fspores>

---

## Русский

### Что это

PC-игра 2005 года на (прошитой) Nintendo Switch через
[Wine-NX](https://github.com/danfromtico/wine-nx): меню, камера, бой, звук и иконка на
главном экране, которая сразу запускает игру. В репозитории только оверлей —
**файлов игры нет**, нужна своя PC-копия Condemned.

- `dinput8.dll` — прокси DirectInput: даёт игре мышь и клавиатуру, которых Wine-NX не
  передаёт (правый стик — обзор, крестовина — ходьба и меню, вся раскладка пада), и
  ускоряет сохранения с минут до долей секунды.
- `imaadp32.acm` — кодек IMA ADPCM из Wine; без него звуковой драйвер игры молча
  выключает звук.
- `condemned-setup.exe` — запустить один раз на консоли: правит заголовок
  `Condemned.exe`, отключает мешающие файлы, проверяет папку. (DirectSound он тоже
  регистрирует, но начиная с Test Build 4 это делает сам Wine-NX.)
- Один NSP: **Condemned: Criminal Origins** — запускает игру в Wine-NX без его меню.
  Программа настройки больше не второй значок: экран перед игрой
  ([winenx-start](https://github.com/fungalspores/winenx-start)) смотрит, исправлен ли
  заголовок `Condemned.exe`, и сам запускает настройку, если ещё нет. Там же по **+**
  открываются настройки игры — пять секунд до запуска.
- `Condemned.keys.txt`, `Condemned.wine-nx.txt` (DXVK), `autoexec.cfg` (1280×720).

На консоли автора — около 30 кадров в секунду, с разгоном: CPU 1683 МГц, GPU 537 МГц, RAM 2333 МГц.
Лог последнего запуска — `switch/wine/logs/Condemned.log`.

### Что нужно

- Switch с Atmosphère **1.8.0+** и sigpatches (для NSP).
- Карта в **exFAT** (`CondemnedA.Arch00` весит 6,2 ГБ).
- [Wine-NX Test Build 4](https://github.com/danfromtico/autorun/releases/tag/test-build-4) (Autorun).
- Установленная PC-версия Condemned: Criminal Origins **без SecuROM** — защиту диска
  Wine-NX не запускает. Проверено с английской версией, текстом Spirit Team и
  озвучкой Team Raccoon.

### Установка

1. Скопируйте папку `switch` из Wine-NX Test Build 4 в корень карты.
2. Положите `wine-nx-start.nro` из
   [winenx-start](https://github.com/fungalspores/winenx-start) в `sdmc:/switch/wine/` —
   это экран перед игрой, он же запускает настройку из пункта 6.
3. Скопируйте установленную игру (папку с `Condemned.exe`) в
   `sdmc:/switch/wine/drive_c/condemned/`.
4. Скопируйте `release/switch` из репозитория на карту **с заменой**
   (`dinput8.dll` игры должен замениться на файл из пака).
5. Установите NSP из `release/` (DBI, sphaira, Tinfoil).
6. Запускайте **Condemned: Criminal Origins**. В первый раз экран увидит, что игра ещё
   не настроена, и запустит `condemned-setup.exe`: дождитесь окна «Настройка завершена»,
   нажмите A, потом откройте тот же значок ещё раз — и играйте.

Шокер сразу работает на нажатии правого стика (R3). Чувствительность мыши — в игре,
Настройки → Управление, по вкусу.

### Управление

| Switch | Клавиша | В меню | В игре |
|---|---|---|---|
| Левый стик / крестовина | стрелки (+W/S/A/D в игре) | выбор | ходьба |
| Правый стик | мышь | — | обзор |
| A | Enter (+левая кнопка мыши) | выбрать | удар |
| B | правая кнопка мыши | — | блок |
| X | E | — | использовать / поднять |
| Y | F | — | фонарик |
| L | Tab | — | ближний бой / оружие |
| R | Ctrl | — | свободна |
| ZL | Shift | — | бег |
| ZR | Space | — | пинок |
| L3 | R | — | проверить патроны |
| R3 | средняя кнопка мыши | — | шокер |
| + | Esc | назад | пауза |
| − | T | — | криминалистика |

### Сборка из исходников

Команды — в английском разделе выше. `tools/build_win32.sh` сам скачает llvm-mingw
20260505, если не задан `LLVM_MINGW`. Для NSP нужен свой `prod.keys` (используется
только `header_key`).

### Как это сделано

Проблемы в том порядке, в каком они встретились, их причины и решения.

1. **`map target status=c000007b`.** У `Condemned.exe` без SecuROM `SizeOfImage` =
   0x18e2f4, не кратен странице — Wine-NX build 108 такой exe не грузит (в 109
   исправлено). И есть записываемая *общая* секция `.SHARED`, которую Wine отображает
   из общего файла, а сервер Horizon его не даёт. Решение: округлить `SizeOfImage` и
   снять `MEM_SHARED` — 3 байта; делает `condemned-setup.exe` прямо на консоли
   (на компьютере — `tools/fix_condemned_exe.py`).
2. **Нет `d3dx9_27.dll`.** В Wine-NX её нет; берётся DLL из редистрибутива Microsoft.
3. **Вечный чёрный экран.** `EAX.DLL` создаёт DirectSound через COM, а Wine-NX не
   запускал wineboot, поэтому `CLSID_DirectSound8` не был зарегистрирован. Окно с
   ошибкой пряталось за поверхностью Vulkan. Решение: setup регистрирует `dsound.dll`.
   В Test Build 4 сборка делает это сама (`config/classes.reg`), и setup лишь повторяет
   уже сделанное.
4. **Нет мыши.** DirectInput 8 в Wine читает мышь только через raw input, а сервер
   Horizon в Wine-NX ставит raw input в очередь для клавиатуры, но не для мыши.
   Решение: прокси `dinput8.dll` загружает `dinput8.dll` из Wine и сам заполняет
   `GetDeviceState`/`GetDeviceData` системной мыши — движение курсора от правого
   стика (со сглаживанием и возвратом в центр, пока игра прячет курсор) и кнопки из
   состояния клавиш.
5. **Нет клавиатуры в игре.** Клавиатура DirectInput (формат игры на 696 байт) тоже
   была пустой. Решение: прокси собирает её из состояния клавиш, и каждая стрелка
   заодно нажимает W/S/A/D — крестовина работает и в меню, и для ходьбы.
6. **Нет звука.** Нашли, обернув все 175 методов звукового объекта `SndDrv.dll` и
   записав, какие вызывает движок: при запуске он требует кодеки ACM PCM, IMA ADPCM и
   MP3 и, если их нет в реестре, сам грузит `IMAADP32.ACM` и `L3CODECA.ACM`. В Wine-NX
   есть только MP3, и движок молча выключал звук. Решение: `imaadp32.acm` из Wine 10.0,
   собранный llvm-mingw, рядом с exe. Ещё прокси делает аппаратные буферы DirectSound
   программными (аппаратного микширования в Wine нет).
7. **Подтормаживания при каждом запуске.** DXVK хранит кэш шейдеров в
   `DXVK_SHADER_CACHE_PATH` или `%LOCALAPPDATA%`, а в фиксированном окружении Wine-NX
   нет ни того, ни другого. Решение: прокси задаёт `DXVK_SHADER_CACHE_PATH` =
   `C:\condemned\dxvk-cache`.
8. **Иконка на главном экране.** `nsp/build_nsp.py` — перенесённый на Python сборщик
   форвардеров sphaira (`owo.cpp`): форвардер nx-hbloader из sphaira с NPDM под
   адресное пространство **32-bit (no alias)** (у exe нет релокаций, он грузится только
   по 0x400000) и `nextArgv` в romfs, который запускает Wine-NX с путём к игре — тогда
   рантайм пропускает своё меню. Урок: в CNMT типы контента нумеруются по
   `NcmContentType` (Program = 1, Control = 3), а не как в заголовке NCA (0 и 2) —
   с неверными значок на главном экране вечно грузится.
9. **Каждое автосохранение замораживало игру на 2–3 минуты.** Чаще всего во время
   зависания в логе был системный вызов `NtWriteFile`, ~900 в секунду при простаивающем
   процессоре: движок пишет сохранение на 1,2 МБ по нескольку байт за `WriteFile`
   (135 000 вызовов), а в Wine-NX каждый идёт через сервер Horizon на SD-карту. Решение:
   прокси перехватывает файловые импорты пяти модулей, которые пишут файлы, и держит
   для каждого файла окно на 256 КБ, сам ведя позицию в файле, — записи и постоянные
   перемотки движка назад (вписать размер блока) ничего не стоят, а на карту окно уходит
   одним куском. Простой буфер, который сбрасывался на каждой перемотке, давал ~7 с
   (8 000 записей на карту); окно — **0,15 с (13 записей)**. Перед консолью его сверили
   с прямой записью на 6 миллионах случайных операций.

Движок — LithTech Jupiter EX; разобраться в его вводе и звуке помогли
[исходники No One Lives Forever 2](https://github.com/wilkie/no-one-lives-forever-2).

### Лицензия и благодарности

Пак распространяется под **GPL-3.0**, см. [LICENSE](LICENSE). `nsp/build_nsp.py` —
перенос `owo.cpp` из sphaira (GPL-3.0), поэтому весь репозиторий под ней же. У частей
свои условия:

- `tools/src/imaadp32/` — код Wine, **LGPL-2.1-or-later** (заголовки в файлах).
- NSP в `release/` содержат форвардер nx-hbloader из sphaira, **ISC**.
- `release/.../d3dx9_27.dll` — редистрибутив DirectX от Microsoft.

Wine-NX и mesa-switch — danfromtico · sphaira — ITotalJustice · Wine · DXVK · Mesa ·
hactool (проверка NSP) · Condemned: Criminal Origins — Monolith Productions.

### Поддержать

Пак бесплатный и таким останется. Если пригодился:

- **Boosty** — <https://boosty.to/fspores>
- **USDT (только TRC-20)** — `TZ5kQSx4HSe8GWr9SfVunYJDofcGLtPWZ7`
- **Канал** — <https://t.me/fspores>

---

<sub>This is an independent, unofficial fan project, not affiliated with, endorsed by
or connected to Nintendo, Monolith Productions or Warner Bros. Games; their names
and marks belong to their owners. It contains no game files. · Это независимый
неофициальный фанатский проект, не связанный с Nintendo, Monolith Productions и
Warner Bros. Games; их названия и знаки принадлежат владельцам. Файлов игры в нём
нет.</sub>
