#!/usr/bin/env python3
"""Neutralise GLIBC version requirements in every ELF under an AppDir.

For each undefined symbol whose required version is GLIBC_2.X with
X > --max-minor, clear the symbol's .gnu.version entry to VER_NDX_GLOBAL=1
so the dynamic linker accepts whatever default version the target
provides. Renaming the version tag instead doesn't work because different
symbols have different version sets (e.g. atan2f at 2.2.5 + 2.43 but not
2.41), so any single rename target is wrong for some symbol.

Symbols that don't exist at any version on the target (free_aligned_sized,
added in 2.43, etc.) still need an LD_PRELOAD shim.

Usage:
  patch_glibc.py [--max-minor 41] <appdir>
"""
import argparse, re, struct, sys
from pathlib import Path

SHT_GNU_verneed = 0x6FFFFFFE
SHT_GNU_versym  = 0x6FFFFFFF


def patch_elf(path: Path, max_minor: int):
    data = bytearray(path.read_bytes())
    if data[:4] != b'\x7fELF':
        return 0, set()
    if data[4] != 2 or data[5] != 1:           # 64-bit LE only
        return 0, set()

    e_shoff     = struct.unpack_from('<Q', data, 40)[0]
    e_shentsize = struct.unpack_from('<H', data, 58)[0]
    e_shnum     = struct.unpack_from('<H', data, 60)[0]

    def shdr(i):
        b = e_shoff + i * e_shentsize
        return {
            'type':   struct.unpack_from('<I', data, b + 4)[0],
            'offset': struct.unpack_from('<Q', data, b + 24)[0],
            'size':   struct.unpack_from('<Q', data, b + 32)[0],
            'link':   struct.unpack_from('<I', data, b + 40)[0],
        }

    sections = [shdr(i) for i in range(e_shnum)]
    verneed = next((s for s in sections if s['type'] == SHT_GNU_verneed), None)
    versym  = next((s for s in sections if s['type'] == SHT_GNU_versym),  None)
    if not verneed or not versym:
        return 0, set()

    strtab_off = sections[verneed['link']]['offset']

    # Walk Verneed: collect vna_other indices for too-new GLIBC versions
    # and mark them VER_FLG_WEAK so glibc's dl-version.c skips the
    # version-existence check (it walks every Verneed entry, not just the
    # ones a symbol actually uses, so the weak flag is required even when
    # no symbol references the tag).
    VER_FLG_WEAK = 0x2
    too_new = {}
    weak_patched = 0
    vn_off = verneed['offset']
    end_off = vn_off + verneed['size']
    while vn_off < end_off:
        vn_cnt  = struct.unpack_from('<H', data, vn_off + 2)[0]
        vn_aux  = struct.unpack_from('<I', data, vn_off + 8)[0]
        vn_next = struct.unpack_from('<I', data, vn_off + 12)[0]

        ax_off = vn_off + vn_aux
        for _ in range(vn_cnt):
            vna_flags = struct.unpack_from('<H', data, ax_off + 4)[0]
            vna_other = struct.unpack_from('<H', data, ax_off + 6)[0]
            vna_name  = struct.unpack_from('<I', data, ax_off + 8)[0]
            vna_next  = struct.unpack_from('<I', data, ax_off + 12)[0]

            s_off = strtab_off + vna_name
            s_end = data.index(b'\x00', s_off)
            name  = data[s_off:s_end].decode(errors='replace')

            m = re.match(r'^GLIBC_2\.(\d+)$', name)
            if m and int(m.group(1)) > max_minor:
                too_new[vna_other] = name
                if not (vna_flags & VER_FLG_WEAK):
                    struct.pack_into('<H', data, ax_off + 4,
                                     vna_flags | VER_FLG_WEAK)
                    weak_patched += 1

            if not vna_next:
                break
            ax_off += vna_next

        if not vn_next:
            break
        vn_off += vn_next

    if not too_new:
        return 0, set()

    # Walk .gnu.version (one 16-bit entry per .dynsym slot); clear matching
    # entries to VER_NDX_GLOBAL=1, preserving the high "hidden" bit.
    cleared = 0
    vs_off = versym['offset']
    vs_count = versym['size'] // 2
    for i in range(vs_count):
        pos = vs_off + i * 2
        idx_full = struct.unpack_from('<H', data, pos)[0]
        idx_low  = idx_full & 0x7FFF
        if idx_low in too_new:
            new_val = (idx_full & 0x8000) | 1
            struct.pack_into('<H', data, pos, new_val)
            cleared += 1

    if cleared or weak_patched:
        path.write_bytes(data)

    return cleared, set(too_new.values())


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('appdir', type=Path)
    ap.add_argument('--max-minor', type=int, default=41,
                    help='Clear version refs for GLIBC_2.X where X > this (default: 41)')
    args = ap.parse_args()

    total = 0
    for path in sorted(args.appdir.rglob('*')):
        if not path.is_file() or path.is_symlink():
            continue
        try:
            n, versions = patch_elf(path, args.max_minor)
        except Exception as e:
            print(f"  SKIP {path.name}: {e}", file=sys.stderr)
            continue
        if n:
            total += n
            vs = ', '.join(sorted(versions))
            print(f"  {path.name}: cleared {n} symbol ref(s) [{vs}]")

    print(f"Done - {total} symbol version reference(s) cleared.")


if __name__ == '__main__':
    main()
