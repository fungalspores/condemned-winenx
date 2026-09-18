#!/usr/bin/env python3
"""Build the NSPs that start Condemned in Wine-NX straight from the HOME menu.

    build_nsp.py [--keys ~/.switch/prod.keys] [--icons nsp] [--out nsp/out]
                 [--old-atmosphere] [--cores 3|4]

Two NSPs, one per program in the game folder (APPS): Condemned itself, and
condemned-setup.exe, which is run once before the first game (it fixes
Condemned.exe's header, registers DirectSound and checks the folder).

It is the forwarder sphaira-32bit-forwaders.nro creates on the console (sphaira's
owo.cpp), built on the computer instead:
  program NCA  exefs: sphaira's forwarder (nx-hbloader) main + main.npdm, the NPDM
               patched to this title, the "32-bit (no alias)" address space Wine-NX
               needs and 3 or 4 CPU cores;
               romfs: nextNroPath / nextArgv, which make it load
               sdmc:/switch/wine/wine-nx-runtime.nro with the program's path as argv[1].
               Given a program, the runtime skips its menu and still applies
               Condemned.wine-nx.txt (DXVK) and Condemned.keys.txt.
  control NCA  control.nacp (Wine-NX's, renamed, no save data, no user selection)
               and the icon for each supported language.
  meta NCA     the CNMT listing the two.
Sections are not encrypted, as in sphaira; only the NCA headers are, with
header_key from the player's own prod.keys. The NCAs are not signed: installing
and starting the NSP needs sigpatches, like any homebrew NSP.

The forwarder binaries are not in the repository: they are cut out of
components/wine-nx/switch/sphaira-32bit-forwaders.nro (Wine-NX Test Build 2) and
checked (NSO segment hashes, NPDM magics).
Needs the cryptography package (and lz4 to check the NSO).
"""
import argparse, hashlib, os, struct, sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPHAIRA = os.path.join(ROOT, "components", "wine-nx", "switch", "sphaira-32bit-forwaders.nro")
WINENX_NRO = os.path.join(ROOT, "components", "wine-nx", "switch", "wine", "wine-nx-runtime.nro")

RUNTIME = "sdmc:/switch/wine/wine-nx-runtime.nro"
GAME_DIR = "sdmc:/switch/wine/drive_c/condemned/"
# One NSP each: (title on HOME, program in the game folder, icon, file name)
APPS = [
    ("Condemned: Criminal Origins", "Condemned.exe", "icon.jpg", "Condemned Criminal Origins (Wine-NX)"),
    ("Condemned: Setup", "condemned-setup.exe", "icon-setup.jpg", "Condemned Setup (Wine-NX)"),
]
AUTHOR = "Monolith Productions / Wine-NX"
VERSION = "1.0.0"

# NACP language entries 0-11 get the title and an icon; 12-15 only the title
LANGUAGES = ["AmericanEnglish", "BritishEnglish", "Japanese", "French", "German",
             "LatinAmericanSpanish", "Spanish", "Italian", "Dutch", "CanadianFrench",
             "Portuguese", "Russian"]

ADDRESS_SPACE_32BIT_NO_ALIAS = 2
IVFC_BLOCK = 0x4000
EXEFS_BLOCK, META_BLOCK = 0x10000, 0x1000
CONTENT_PROGRAM, CONTENT_META, CONTENT_CONTROL = 0, 1, 2      # NCA header content types
NCM_PROGRAM, NCM_CONTROL = 1, 3                               # CNMT content record types (NcmContentType)


def sha256(data):
    return hashlib.sha256(data).digest()


def align(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def padded(data, alignment):
    return data + bytes(align(len(data), alignment) - len(data))


# ---- inputs ----

def header_key(path):
    for line in open(path, encoding="utf-8", errors="replace"):
        name, _, value = line.partition("=")
        if name.strip().lower() == "header_key":
            key = bytes.fromhex(value.strip())
            if len(key) == 32:
                return key
    sys.exit(f"{path}: нет header_key")


def forwarder_exefs(path):
    """sphaira's embedded forwarder: an NPDM ("META") directly followed by an NSO."""
    data = open(path, "rb").read()
    npdm_at = data.find(b"META")
    while npdm_at >= 0:
        aci0, aci0_size, acid, acid_size = struct.unpack_from("<4I", data, npdm_at + 0x70)
        end = npdm_at + max(aci0 + aci0_size, acid + acid_size)
        if (data[npdm_at + aci0:npdm_at + aci0 + 4] == b"ACI0" and
                data[npdm_at + acid + 0x200:npdm_at + acid + 0x204] == b"ACID" and data[end:end + 4] == b"NSO0"):
            break
        npdm_at = data.find(b"META", npdm_at + 4)
    else:
        sys.exit(f"{path}: не найден форвардер (NPDM + NSO)")
    npdm = bytearray(data[npdm_at:end])

    nso_header = data[end:end + 0x100]
    segments = [struct.unpack_from("<3I", nso_header, o) for o in (0x10, 0x20, 0x30)]
    file_sizes = struct.unpack_from("<3I", nso_header, 0x60)
    nso = data[end:end + max(s[0] + fs for s, fs in zip(segments, file_sizes))]
    try:
        import lz4.block
        for i, ((offset, _, size), file_size) in enumerate(zip(segments, file_sizes)):
            segment = lz4.block.decompress(nso[offset:offset + file_size], uncompressed_size=size)
            if sha256(segment) != nso_header[0xa0 + 0x20 * i:0xc0 + 0x20 * i]:
                sys.exit(f"{path}: NSO форвардера повреждён (сегмент {i})")
    except ImportError:
        print("lz4 нет: NSO форвардера не проверен")
    return nso, npdm


def winenx_nacp(path):
    data = open(path, "rb").read()
    asset = data[struct.unpack_from("<I", data, 0x18)[0]:]
    if asset[:4] != b"ASET":
        sys.exit(f"{path}: нет ASET")
    offset, size = struct.unpack_from("<QQ", asset, 0x18)
    if size != 0x4000:
        sys.exit(f"{path}: NACP не 0x4000 байт")
    return bytearray(asset[offset:offset + size])


# ---- NPDM and NACP ----

def patch_kac(kac, type_bits, value):
    """Replace the kernel capability whose low bits are type_bits ones and then a zero."""
    pattern = (1 << type_bits) - 1
    mask = (1 << type_bits) | pattern
    for i in range(0, len(kac), 4):
        if struct.unpack_from("<I", kac, i)[0] & mask == pattern:
            struct.pack_into("<I", kac, i, value | pattern)
            return True
    return False


def patch_npdm(npdm, tid, cores, old_atmosphere):
    npdm[0xc] = (npdm[0xc] & ~0xe) | (ADDRESS_SPACE_32BIT_NO_ALIAS << 1)
    aci0, aci0_size, acid, acid_size = struct.unpack_from("<4I", npdm, 0x70)
    struct.pack_into("<Q", npdm, aci0 + 0x10, tid)
    struct.pack_into("<QQ", npdm, acid + 0x210, tid, tid)

    # thread info: priorities 28-59 on cores 0-2, or 28-63 on 0-3 (sphaira's values)
    low_priority, high_cpu = (63, 3) if cores == 4 else (59, 2)
    thread_info = ((((high_cpu << 8) | 0) << 6 | 28) << 6 | low_priority) << 4
    # debug flags: ForceDebug moved from bit 18 to bit 19 in Atmosphere 1.8.0 (HOS 19)
    for base, size_at in ((aci0, 0x30), (acid, 0x230)):
        kac_off, kac_size = struct.unpack_from("<II", npdm, base + size_at)
        start = base + kac_off
        kac = bytearray(npdm[start:start + kac_size])
        if not patch_kac(kac, 3, thread_info):
            sys.exit("в NPDM нет ThreadInfo")
        if not old_atmosphere:
            patch_kac(kac, 16, 1 << 19)
        npdm[start:start + kac_size] = kac
    return bytes(npdm)


def patch_nacp(nacp, tid, title):
    for i in range(16):
        entry = i * 0x300
        nacp[entry:entry + 0x300] = bytes(0x300)
        name, author = title.encode(), AUTHOR.encode()
        nacp[entry:entry + len(name)] = name
        nacp[entry + 0x200:entry + 0x200 + len(author)] = author
    struct.pack_into("<I", nacp, 0x302c, (1 << len(LANGUAGES)) - 1)   # supported languages
    nacp[0x3060:0x3070] = VERSION.encode().ljust(0x10, b"\0")
    nacp[0x3025] = 0          # no user selection: the game saves on the SD card
    nacp[0x3026] = 0          # user account switch lock
    nacp[0x3027] = 1          # add-on content registration: on demand
    nacp[0x3034] = 0          # screenshots allowed
    nacp[0x3035] = 2          # video capture: automatic
    nacp[0x3036] = 0          # no data loss confirmation
    nacp[0x3037] = 0          # play log: all
    nacp[0x30f0] = 2          # logo type: Nintendo
    nacp[0x30f1] = 0          # logo handling: automatic
    nacp[0x3210] = 0          # play log query capability
    nacp[0x3213] = 0          # no network service license on launch
    nacp[0x30a8:0x30b0] = b"wine-nx\0"                               # error code category
    struct.pack_into("<Q", nacp, 0x3038, tid)                         # presence group
    struct.pack_into("<QQ", nacp, 0x3070, tid ^ 0x1000, tid)          # add-on base, save owner
    struct.pack_into("<8Q", nacp, 0x30b0, *([tid] * 8))               # local communication
    struct.pack_into("<Q", nacp, 0x30f8, tid)                         # pseudo device id seed
    nacp[0x3080:0x30a0] = bytes(0x20)                                 # no save data
    nacp[0x3148:0x3168] = bytes(0x20)
    return bytes(nacp)


# ---- containers ----

def pfs0(files):
    names = b""
    table = b""
    offset = 0
    for name, data in files:
        table += struct.pack("<QQII", offset, len(data), len(names), 0)
        names += name.encode() + b"\0"
        offset += len(data)
    names = padded(names, 0x20)
    return (struct.pack("<4sIII", b"PFS0", len(files), len(names), 0) + table + names +
            b"".join(data for _, data in files))


def romfs_hash(parent, name):
    h = parent ^ 123456789
    for byte in name:
        h = ((h >> 5) | (h << 27)) & 0xffffffff
        h ^= byte
    return h


def romfs_table_count(n):
    if n < 3:
        return 3
    if n < 19:
        return n | 1
    while any(n % p == 0 for p in (2, 3, 5, 7, 11, 13, 17)):
        n += 1
    return n


def romfs(files):
    """A RomFS with every file in the root directory."""
    data, file_table, offsets = b"", b"", []
    entry = 0
    for name, content in files:
        data = padded(data, 0x10)
        offsets.append((entry, len(data), len(content)))
        data += content
        entry += 0x20 + align(len(name), 4)
    file_hash = [0xffffffff] * romfs_table_count(len(files))
    for i, ((name, _), (entry, offset, size)) in enumerate(zip(files, offsets)):
        raw = name.encode()
        bucket = romfs_hash(0, raw) % len(file_hash)
        sibling = offsets[i + 1][0] if i + 1 < len(files) else 0xffffffff
        file_table += struct.pack("<IIQQII", 0, sibling, offset, size, file_hash[bucket], len(raw))
        file_table += padded(raw, 4)
        file_hash[bucket] = entry
    dir_hash = [0xffffffff] * 3
    dir_hash[romfs_hash(0, b"") % 3] = 0
    dir_table = struct.pack("<6I", 0, 0xffffffff, 0xffffffff, 0 if files else 0xffffffff, 0xffffffff, 0)

    dir_hash_off = align(0x200 + len(data), 4)
    tables = [struct.pack(f"<{len(dir_hash)}I", *dir_hash), dir_table,
              struct.pack(f"<{len(file_hash)}I", *file_hash), file_table]
    header, pos = [0x50], dir_hash_off
    for t in tables:
        header += [pos, len(t)]
        pos += len(t)
    header.append(0x200)
    image = struct.pack("<10Q", *header).ljust(0x200, b"\0") + data
    image = image.ljust(dir_hash_off, b"\0") + b"".join(tables)
    return image


class Nca:
    def __init__(self, content_type, tid):
        self.content_type, self.tid = content_type, tid
        self.body = bytearray(0xc00)
        self.sections = []           # (start, end, fs header)

    def _section(self, data, fs_header):
        start = len(self.body)
        self.body += padded(data, 0x200)
        self.sections.append((start, len(self.body), fs_header))

    def add_pfs0(self, files, block):
        image = pfs0(files)
        hashes = b"".join(sha256(image[i:i + block]) for i in range(0, len(image), block))
        layer = align(len(hashes), 0x200)
        fs = bytearray(0x200)
        struct.pack_into("<HBBB", fs, 0, 2, 1, 2, 1)             # version, PFS0, SHA-256 layers, not encrypted
        fs[8:0x28] = sha256(hashes)
        struct.pack_into("<IIQQQQ", fs, 0x28, block, 2, 0, len(hashes), layer, len(image))
        self._section(padded(hashes, 0x200) + image, fs)

    def add_romfs(self, files):
        image = romfs(files)
        romfs_size = len(image)
        levels = [padded(image, IVFC_BLOCK)]
        for _ in range(5):
            below = levels[0]
            levels.insert(0, padded(b"".join(sha256(below[i:i + IVFC_BLOCK])
                                              for i in range(0, len(below), IVFC_BLOCK)), IVFC_BLOCK))
        fs = bytearray(0x200)
        struct.pack_into("<HBBB", fs, 0, 2, 0, 3, 1)             # version, RomFS, IVFC, not encrypted
        struct.pack_into("<4sIII", fs, 8, b"IVFC", 0x20000, 0x20, 7)
        offset = 0
        for i, level in enumerate(levels):
            size = romfs_size if i == 5 else len(level)
            struct.pack_into("<QQII", fs, 0x18 + 0x18 * i, offset, size, 14, 0)
            offset += size
        fs[0xc8:0xe8] = sha256(levels[0])
        self._section(b"".join(levels), fs)

    def build(self, key):
        header = bytearray(self.body[:0xc00])
        struct.pack_into("<4sBBBBQQII", header, 0x200, b"NCA3", 0, self.content_type, 0, 0,
                         len(self.body), self.tid, 0, 0x000C1100)
        for i, (start, end, fs) in enumerate(self.sections):
            struct.pack_into("<IIB", header, 0x240 + 0x10 * i, start // 0x200, end // 0x200, 1)
            header[0x280 + 0x20 * i:0x2a0 + 0x20 * i] = sha256(fs)
            header[0x400 + 0x200 * i:0x600 + 0x200 * i] = fs
        encrypted = b""
        for sector in range(6):   # AES-128-XTS, 0x200-byte sectors, big-endian sector tweak
            enc = Cipher(algorithms.AES(key), modes.XTS(sector.to_bytes(16, "big"))).encryptor()
            encrypted += enc.update(bytes(header[0x200 * sector:0x200 * (sector + 1)])) + enc.finalize()
        return encrypted + bytes(self.body[0xc00:])


def cnmt(tid, contents):
    data = struct.pack("<QIBBHHHBBBBI4x", tid, 0, 0x80, 0, 0x10, len(contents), 0, 0, 0, 0, 0, 0)
    data += struct.pack("<QII", tid | 0x800, 0, 0)               # patch id, required versions
    for content_type, nca in contents:
        digest = sha256(nca)
        size = len(nca)
        data += digest + digest[:16] + struct.pack("<IBBBB", size & 0xffffffff, size >> 32, 0, content_type, 0)
    return data + bytes(0x20)


def build(app, key, a):
    title, program_name, icon_name, file_name = app
    icon_path = os.path.join(a.icons, icon_name)
    if not os.path.isfile(icon_path):
        sys.exit(f"нет иконки {icon_path}: запустите nsp/make_icon.py")
    icon = open(icon_path, "rb").read()
    if icon[:2] != b"\xff\xd8" or len(icon) > 0x20000:
        sys.exit(f"{icon_path}: иконка должна быть JPEG 256x256 не больше 128 КБ")

    target = GAME_DIR + program_name
    args = f"{RUNTIME} {target}"
    digest = struct.unpack("<Q", sha256(("wine-nx forwarder " + args).encode())[:8])[0]
    tid = 0x0500000000000000 | (digest & 0x00FFFFFFFFFFF000)

    nso, npdm = forwarder_exefs(SPHAIRA)
    program = Nca(CONTENT_PROGRAM, tid)
    program.add_pfs0([("main", nso), ("main.npdm", patch_npdm(npdm, tid, a.cores, a.old_atmosphere))], EXEFS_BLOCK)
    program.add_romfs([("nextArgv", args.encode()), ("nextNroPath", RUNTIME.encode())])
    program = program.build(key)

    control = Nca(CONTENT_CONTROL, tid)
    control.add_romfs([("control.nacp", patch_nacp(winenx_nacp(WINENX_NRO), tid, title))] +
                      [(f"icon_{lang}.dat", icon) for lang in LANGUAGES])
    control = control.build(key)

    meta = Nca(CONTENT_META, tid)
    meta.add_pfs0([(f"Application_{tid:016x}.cnmt",
                    cnmt(tid, [(NCM_PROGRAM, program), (NCM_CONTROL, control)]))], META_BLOCK)
    meta = meta.build(key)

    files = [(sha256(program)[:16].hex() + ".nca", program), (sha256(control)[:16].hex() + ".nca", control),
             (sha256(meta)[:16].hex() + ".cnmt.nca", meta)]
    out = os.path.join(a.out, f"{file_name} [{tid:016X}][v0].nsp")
    with open(out, "wb") as f:
        f.write(pfs0(files))
    print(f"NSP: {out} ({os.path.getsize(out)} байт)")
    print(f"  title id {tid:016X}; запускает {RUNTIME} {target}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--keys", default=os.path.expanduser("~/.switch/prod.keys"))
    ap.add_argument("--icons", default=os.path.join(ROOT, "nsp"), help="папка с icon.jpg и icon-setup.jpg")
    ap.add_argument("--out", default=os.path.join(ROOT, "nsp", "out"))
    ap.add_argument("--cores", type=int, choices=(3, 4), default=3)
    ap.add_argument("--old-atmosphere", action="store_true",
                    help="Atmosphere до 1.8.0: старое место флага ForceDebug в NPDM")
    a = ap.parse_args()

    key = header_key(a.keys)
    os.makedirs(a.out, exist_ok=True)
    for app in APPS:
        build(app, key, a)
    print(f"адресное пространство 32-bit (no alias), ядер {a.cores}, "
          f"ForceDebug {'бит 18 (старый Atmosphere)' if a.old_atmosphere else 'бит 19 (Atmosphere 1.8.0+)'}")


if __name__ == "__main__":
    main()
