#!/usr/bin/env python3
"""Assemble the SD card contents: Wine-NX + Condemned (game/ + components/ + pack/).

    assemble_sd.py [DEST] [--text ru|en] [--voice team_raccoon|vektor_norg|en]
                   [--no-runtime] [--reset-config]

DEST is the root of the SD card (or ./sd, the default, for a local build):
  DEST/switch/wine/wine-nx-runtime.nro       Wine-NX runtime (components/wine-nx)
  DEST/switch/sphaira-32bit-forwaders.nro    makes the "32-bit (no alias)" forwarder
  DEST/switch/wine/drive_c/condemned/        the game
Files already up to date are skipped, so re-running against the card only copies
what changed. On the same APFS volume files are cloned, not copied.
"""
import argparse, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_DIR = os.path.join("switch", "wine", "drive_c", "condemned")
FAT32_LIMIT = 4 * 2**30 - 1

# Taken out of game/ (the repack's DirectInput FPS fix and the SecuROM exe): see components/README.md
GAME_EXCLUDE = {"dinput8.dll", "condemned.old"}
# Removed from the card's game folder if an earlier copy left them there
STALE = {"condemned.old", "d3d9.dll", "dinput.dll", "xinput1_3.dll", "xinputplus.ini"}
# Wine-NX's WarCraft III part (its setup program, an empty game folder, its README):
# not copied, and removed from the card if an earlier copy left it there
RUNTIME_EXCLUDE = [os.path.join("switch", "wine", "WARCRAFT-III-README.txt"),
                   os.path.join("switch", "wine", "drive_c", "WarCraft III"),
                   os.path.join("switch", "wine", "drive_c", "WarCraft III Setup")]
# Wine-NX's target.txt preselects war3-setup.exe in its menu; Condemned instead
TARGET = (os.path.join("switch", "wine", "target.txt"), "sdmc:/switch/wine/drive_c/condemned/Condemned.exe\n")
# Written by the player or by Wine-NX; an existing copy on the card is kept unless --reset-config
KEEP_IF_PRESENT = {os.path.join(GAME_DIR, "autoexec.cfg")} | {
    os.path.join("switch", "wine", n) for n in ("keys.txt", "args.txt", "target.txt", "run-entry.txt")}


def component(name, hint):
    path = os.path.join(ROOT, "components", name)
    if not os.path.isdir(path) or not os.listdir(path):
        sys.exit(f"нет components/{name} — {hint}")
    return path


def layers(a):
    """(source folder, where it goes on the card, excluded file names); later layers win."""
    if not a.no_runtime:
        yield component("wine-nx", "запустите: sh tools/fetch_winenx.sh"), "", set()
    game = [os.path.join(ROOT, "game"),
            component("exe_fix", "Wine-NX build 108 не загрузит exe с неокруглённым SizeOfImage; "
                                 "запустите: python3 tools/fix_condemned_exe.py"),
            component("d3dx9", "без d3dx9_27.dll Condemned.exe не запустится; запустите: sh tools/fetch_d3dx9_27.sh"),
            component("condemned_setup", "без регистрации DirectSound игра зависнет на чёрном экране; "
                                         "запустите: sh tools/build_win32.sh"),
            component("dinput8_proxy", "без него мышь (атака, блок, обзор) до игры не доходит; "
                                       "запустите: sh tools/build_win32.sh"),
            component("imaadp32", "без кодека IMA ADPCM SndDrv.dll не запускает звук; "
                                  "запустите: sh tools/build_win32.sh")]
    if a.text == "ru":
        game.append(os.path.join(ROOT, "components", "ru_text"))
    if a.voice != "en":
        game.append(os.path.join(ROOT, "components", f"ru_voice_{a.voice}"))
    game.append(os.path.join(ROOT, "pack"))
    for g in game:
        yield g, GAME_DIR, GAME_EXCLUDE if g == os.path.join(ROOT, "game") else set()


def excluded(rel):
    rel = os.path.normpath(rel).lower()
    return any(rel == x.lower() or rel.startswith(x.lower() + os.sep) for x in RUNTIME_EXCLUDE)


def plan(a):
    files, dirs = {}, set()
    for layer, prefix, exclude in layers(a):
        if not os.path.isdir(layer):
            sys.exit(f"нет папки {layer}")
        for dirpath, subdirs, names in os.walk(layer):
            if excluded(os.path.join(prefix, os.path.relpath(dirpath, layer))):
                continue
            if not subdirs and not names:   # keep empty folders
                dirs.add(os.path.join(prefix, os.path.relpath(dirpath, layer)))
            for n in names:
                if n == "README.md" or n.startswith(".") or n.lower() in exclude:
                    continue
                rel = os.path.join(prefix, os.path.relpath(os.path.join(dirpath, n), layer))
                if excluded(rel) or rel == TARGET[0]:
                    continue
                files[rel.lower()] = (rel, os.path.join(dirpath, n))
    return sorted(files.values()), sorted(dirs)


def filesystem(path):
    """Filesystem type of the mount holding path, from `mount`: apfs, msdos (FAT32), exfat..."""
    path = os.path.realpath(os.path.abspath(path))
    best, fs = "", "?"
    for line in subprocess.run(["mount"], capture_output=True, text=True).stdout.splitlines():
        if " on " not in line or " (" not in line:
            continue
        mountpoint, opts = line.split(" on ", 1)[1].rsplit(" (", 1)
        if (path == mountpoint or path.startswith(mountpoint.rstrip("/") + "/")) and len(mountpoint) > len(best):
            best, fs = mountpoint, opts.split(",")[0]
    return fs


def up_to_date(src, dst):
    return (os.path.exists(dst) and os.path.getsize(dst) == os.path.getsize(src)
            and os.path.getmtime(dst) >= os.path.getmtime(src) - 2)   # FAT/exFAT mtime granularity


def copy(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.exists(dst):
        os.remove(dst)
    if subprocess.run(["cp", "-c", src, dst], capture_output=True).returncode != 0:
        shutil.copy2(src, dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dest", nargs="?", default=os.path.join(ROOT, "sd"))
    ap.add_argument("--text", choices=["ru", "en"], default="ru")
    ap.add_argument("--voice", choices=["team_raccoon", "vektor_norg", "en"], default="team_raccoon")
    ap.add_argument("--no-runtime", action="store_true", help="не копировать Wine-NX (на карте уже есть)")
    ap.add_argument("--reset-config", action="store_true", help="перезаписать autoexec.cfg и keys.txt/args.txt Wine-NX")
    a = ap.parse_args()

    dest = os.path.abspath(a.dest)
    files, empty_dirs = plan(a)
    fs = filesystem(dest)
    too_big = [rel for rel, src in files if os.path.getsize(src) > FAT32_LIMIT]
    if fs == "msdos" and too_big:
        sys.exit(f"{dest}: файловая система FAT32, а {', '.join(too_big)} больше 4 ГБ.\n"
                 "Нужна карта в exFAT, иначе файл не записать.")

    print(f"карта: {dest}  ({fs})\nWine-NX: {'нет' if a.no_runtime else 'да'}, текст {a.text}, озвучка {a.voice}")
    copied = skipped = kept = 0
    for rel, src in files:
        dst = os.path.join(dest, rel)
        if rel in KEEP_IF_PRESENT and os.path.exists(dst) and not a.reset_config:
            kept += 1
        elif up_to_date(src, dst):
            skipped += 1
        else:
            copy(src, dst)
            copied += 1
    for d in empty_dirs:
        os.makedirs(os.path.join(dest, d), exist_ok=True)
    target = os.path.join(dest, TARGET[0])
    if not a.no_runtime and (a.reset_config or not os.path.exists(target) or
                             "WarCraft III" in open(target, errors="replace").read()):
        with open(target, "w") as f:
            f.write(TARGET[1])
    removed = 0
    for x in RUNTIME_EXCLUDE:
        path = os.path.join(dest, x)
        if os.path.isdir(path):
            shutil.rmtree(path)
            removed += 1
        elif os.path.isfile(path):
            os.remove(path)
            removed += 1
    for dirpath, _, names in os.walk(os.path.join(dest, GAME_DIR)):
        for n in names:
            if n.lower() in STALE:
                os.remove(os.path.join(dirpath, n))
                removed += 1
    print(f"файлов: {len(files)}; скопировано {copied}, уже на месте {skipped}, "
          f"сохранены существующие настройки {kept}, удалено лишнего (WarCraft III, старые файлы игры) {removed}")


if __name__ == "__main__":
    main()
