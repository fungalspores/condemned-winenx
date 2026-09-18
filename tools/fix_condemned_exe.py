#!/usr/bin/env python3
"""Fix two header problems that stop Wine-NX build 108 (Test Build 2) from loading Condemned.exe.

    fix_condemned_exe.py [SRC] [DST]      (default: game/Condemned.exe -> components/exe_fix/Condemned.exe)

Both make "map target" fail with STATUS_INVALID_IMAGE_FORMAT (c000007b):

1. SizeOfImage 0x18e2f4 ends inside the last section's page: the repack's no-CD
   exe had SecuROM's sections cut off without rounding. Windows rounds it up;
   build 108 does not and judges the last section too large (fixed upstream in
   build 109, commit 0e933a66). -> rounded up to the section alignment.

2. ".SHARED" is a writable shared section (IMAGE_SCN_MEM_SHARED). Wine maps those
   from a shared file, but the Horizon server always answers shared_file = 0
   (dlls/ntdll/unix/horizon.c), so mmap gets fd -1 and the image is rejected.
   Monolith keeps a cross-instance counter there; with one program at a time on
   the Switch a private section behaves the same. -> MEM_SHARED cleared.

Only those header fields change.
"""
import hashlib, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCN_MEM_SHARED, SCN_MEM_WRITE = 0x10000000, 0x80000000

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "game", "Condemned.exe")
dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "components", "exe_fix", "Condemned.exe")

data = bytearray(open(src, "rb").read())
pe = struct.unpack_from("<I", data, 0x3C)[0]
if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", data, pe + 24)[0] != 0x10B:
    sys.exit(f"{src}: не 32-битный PE")
print(f"{src}\n  sha1 {hashlib.sha1(data).hexdigest()}")
changes = 0

opt = pe + 24
section_alignment = struct.unpack_from("<I", data, opt + 32)[0]
size_of_image = struct.unpack_from("<I", data, opt + 56)[0]
align = max(section_alignment, 0x1000)
fixed = (size_of_image + align - 1) & ~(align - 1)
if fixed != size_of_image:
    struct.pack_into("<I", data, opt + 56, fixed)
    print(f"  SizeOfImage {size_of_image:#x} -> {fixed:#x}")
    changes += 1

count = struct.unpack_from("<H", data, pe + 6)[0]
first = opt + struct.unpack_from("<H", data, pe + 20)[0]
for i in range(count):
    hdr = first + i * 40
    chr_off = hdr + 36
    name = bytes(data[hdr:hdr + 8]).rstrip(b"\0").decode(errors="replace")
    chars = struct.unpack_from("<I", data, chr_off)[0]
    if chars & SCN_MEM_SHARED and chars & SCN_MEM_WRITE:
        struct.pack_into("<I", data, chr_off, chars & ~SCN_MEM_SHARED)
        print(f"  секция {name!r}: {chars:#010x} -> {chars & ~SCN_MEM_SHARED:#010x} (снят MEM_SHARED)")
        changes += 1

os.makedirs(os.path.dirname(dst), exist_ok=True)
open(dst, "wb").write(data)
print(f"  записано: {dst}" + ("" if changes else " (исправлять было нечего)"))
