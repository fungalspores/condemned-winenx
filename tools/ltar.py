#!/usr/bin/env python3
"""Read LithTech Jupiter EX archives (.Arch00, "LTAR" v3) used by Condemned and F.E.A.R.

    ltar.py list ARCHIVE [SUBSTRING]
    ltar.py extract ARCHIVE PATH_IN_ARCHIVE OUTFILE

Layout: 48-byte header, name table, file entries (32 bytes), dir entries (16 bytes).
The stored length is the entry's first size field. Fan patches (e.g. the Russian
CondemnedL.Arch00) append new data and leave the second size field stale, so it is
only trusted for zlib-compressed entries. *.crc entries hold junk and are skipped.
"""
import struct, sys, zlib

HEADER = struct.Struct("<4sIIIIIII16s")
FILE = struct.Struct("<IQQQI")        # name offset, data offset, compressed size, size, compressed flag
DIR = struct.Struct("<IIII")          # name offset, first subdir, next sibling, file count


def read_index(path):
    f = open(path, "rb")
    magic, version, names_size, dir_count, file_count, *_ = HEADER.unpack(f.read(HEADER.size))
    if magic != b"LTAR" or version != 3:
        sys.exit(f"{path}: не LTAR v3 ({magic!r} v{version})")
    names = f.read(names_size)
    files = [FILE.unpack(f.read(FILE.size)) for _ in range(file_count)]
    dirs = [DIR.unpack(f.read(DIR.size)) for _ in range(dir_count)]

    def name(off):
        return names[off:names.index(b"\0", off)].decode("latin-1")

    # Directory names are full paths; files are stored dir by dir in tree order
    entries, fi = [], 0
    def walk(d):
        nonlocal fi
        dname, sub, nxt, fcount = dirs[d]
        base = name(dname)
        base = base + "\\" if base else ""
        for _ in range(fcount):
            noff, off, csize, size, comp = files[fi]
            fi += 1
            path = base + name(noff)
            if path.lower().endswith(".crc"):
                continue
            entries.append((path, off, csize, size if comp else csize, comp))
        if sub != 0xFFFFFFFF:
            walk(sub)
        if nxt != 0xFFFFFFFF:
            walk(nxt)
    walk(0)
    return f, entries


def main():
    cmd, archive = sys.argv[1], sys.argv[2]
    f, entries = read_index(archive)
    if cmd == "list":
        needle = sys.argv[3].lower() if len(sys.argv) > 3 else ""
        for p, off, csize, size, comp in entries:
            if needle in p.lower():
                print(f"{size:12d} {'z' if comp else ' '} {p}")
        print(f"# файлов: {len(entries)}", file=sys.stderr)
    elif cmd == "extract":
        want = sys.argv[3].lower().replace("/", "\\")
        for p, off, csize, size, comp in entries:
            if p.lower() == want:
                f.seek(off)
                data = f.read(csize)
                if comp:
                    data = zlib.decompress(data)[:size]
                open(sys.argv[4], "wb").write(data)
                return
        sys.exit("нет такого файла")


if __name__ == "__main__":
    main()
