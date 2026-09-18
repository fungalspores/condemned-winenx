# Condemned на HOME: NSP-ярлыки

Два NSP ставят на главный экран Switch значки **Condemned: Criminal Origins** и
**Condemned: Setup**. По нажатию запускается Wine-NX, и программа стартует сразу, без
меню Wine-NX: игра или `condemned-setup.exe` (запускается один раз перед первой игрой:
правит заголовок `Condemned.exe`, регистрирует DirectSound, проверяет папку).

Это тот же форвардер, который `sphaira-32bit-forwaders.nro` делает на консоли (Address
Space «32-bit (no alias)»), только собранный на компьютере: со своим названием,
иконкой и путём к игре.

## Установка

1. Скопируйте на карту папку `switch` из пака: Wine-NX и игра окажутся в
   `sdmc:/switch/wine/`, игра — в `sdmc:/switch/wine/drive_c/condemned/`. NSP ищет
   файлы только там.
2. Установите оба NSP любым установщиком (sphaira, DBI, Tinfoil). Нужны sigpatches, как
   для любого homebrew-NSP: у NCA нет подписи Nintendo.
3. Один раз запустите **Condemned: Setup** и дождитесь окна «Настройка завершена».
4. Запускайте **Condemned: Criminal Origins** с главного экрана.

Требуется Atmosphère 1.8.0 или новее. Для более старого соберите NSP с `--old-atmosphere`.

## Сборка

```sh
pip install cryptography pillow lz4
python3 nsp/make_icon.py                  # icon.jpg и icon-setup.jpg из заставок игры (game/)
python3 nsp/build_nsp.py                  # оба NSP в nsp/out/
```

- Ключи: `~/.switch/prod.keys` со своей консоли (Lockpick_RCM), путь меняется `--keys`.
  Нужен только `header_key`, им шифруются заголовки NCA. Разделы не шифруются, как у sphaira.
- Иконка: `make_icon.py --art splash04|splash01|splash03|corridor`, или своя
  JPEG 256×256 под теми же именами в папке из `build_nsp.py --icons`.
- `--cores 4` отдаёт игре четвёртое ядро (как «CPU Cores: 4» в sphaira).
- Форвардер (nx-hbloader из sphaira, лицензия ISC) не лежит в репозитории: скрипт
  вырезает его из `components/wine-nx/switch/sphaira-32bit-forwaders.nro` и сверяет
  хеши сегментов NSO. NACP берётся из `wine-nx-runtime.nro` и переименовывается.

## Что внутри

| NCA | Содержимое |
|---|---|
| Program | exefs: `main` + `main.npdm` (адресное пространство 32-bit no alias, свой title ID); romfs: `nextNroPath` = `sdmc:/switch/wine/wine-nx-runtime.nro`, `nextArgv` = рантайм + `sdmc:/switch/wine/drive_c/condemned/Condemned.exe` |
| Control | `control.nacp` (название, версия 1.0.0, без выбора пользователя и сохранений) и иконка на 12 языках |
| Meta | CNMT на эти два NCA |

У каждого NSP свой title ID (из пути к программе): игра — `05AEE981A8588000`,
Setup — `05FA2D4E249CB000`.

Рантайм, получив путь к программе, пропускает своё меню и применяет файлы рядом с
игрой: `Condemned.wine-nx.txt` (DXVK) и `Condemned.keys.txt` (кнопки).

Title ID вычисляется из пути (префикс `05`, как у sphaira), поэтому NSP не пересекается с
форвардером, сделанным в sphaira. Проверено hactool: заголовки, хеши exefs/romfs и
NPDM. Подпись заголовка (Fixed-Key Signature) ожидаемо FAIL.
