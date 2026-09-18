#!/bin/sh
# Builds the Win32 pieces of the pack with the llvm-mingw release and flags
# Wine-NX uses for its own war3-setup.exe (no C runtime, i686):
#   components/condemned_setup/condemned-setup.exe  from tools/src/condemned_setup
#   components/dinput8_proxy/dinput8.dll            from tools/src/dinput8_proxy
#   components/imaadp32/imaadp32.acm                from tools/src/imaadp32 (Wine 10.0's IMA ADPCM codec)
#   LLVM_MINGW=/path/to/llvm-mingw sh tools/build_win32.sh
# Without LLVM_MINGW, downloads the i686 parts of llvm-mingw 20260505 into a temp folder.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
if [ -z "$LLVM_MINGW" ]; then
    tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
    curl -fL -o "$tmp/lm.tar.xz" \
        https://github.com/mstorsjo/llvm-mingw/releases/download/20260505/llvm-mingw-20260505-ucrt-macos-universal.tar.xz
    top=llvm-mingw-20260505-ucrt-macos-universal
    tar -xJf "$tmp/lm.tar.xz" -C "$tmp" --exclude="$top/share" --exclude="$top/armv7-w64-mingw32" \
        --exclude="$top/x86_64-w64-mingw32" --exclude="$top/aarch64-w64-mingw32" --exclude="$top/arm64ec-w64-mingw32"
    LLVM_MINGW="$tmp/$top"
fi
CC="$LLVM_MINGW/bin/i686-w64-mingw32-clang"
FLAGS="-Os -Wall -Wextra -Werror -fno-builtin -nostdlib -Wl,--dynamicbase"

mkdir -p "$ROOT/components/condemned_setup" "$ROOT/components/dinput8_proxy"
"$CC" $FLAGS -Wl,--entry,_start@0 -Wl,--image-base,0x10000000 \
    -o "$ROOT/components/condemned_setup/condemned-setup.exe" "$ROOT/tools/src/condemned_setup/condemned_setup.c" \
    -lole32 -ladvapi32 -luser32 -lkernel32 -lntdll
"$CC" $FLAGS -shared -Wl,--entry,_DllMainCRTStartup@12 \
    -o "$ROOT/components/dinput8_proxy/dinput8.dll" \
    "$ROOT/tools/src/dinput8_proxy/dinput8_proxy.c" "$ROOT/tools/src/dinput8_proxy/dinput8.def" \
    -luser32 -lkernel32 -lntdll
# Wine's own source (LGPL): keeps Wine's warning level, and debug.h is a stand-in (see its comment)
mkdir -p "$ROOT/components/imaadp32"
"$CC" -Os -Wall -Werror -Wno-pragma-pack -fno-builtin -nostdlib -Wl,--dynamicbase -DNDEBUG \
    -I "$ROOT/tools/src/imaadp32/include" -I "$ROOT/tools/src/imaadp32" \
    -shared -Wl,--entry,_DllMainCRTStartup@12 \
    -o "$ROOT/components/imaadp32/imaadp32.acm" \
    "$ROOT/tools/src/imaadp32/imaadp32.c" "$ROOT/tools/src/imaadp32/dllmain.c" "$ROOT/tools/src/imaadp32/imaadp32.def" \
    -lwinmm -luser32 -lkernel32
echo "готово: components/condemned_setup/condemned-setup.exe, components/dinput8_proxy/dinput8.dll, components/imaadp32/imaadp32.acm"
