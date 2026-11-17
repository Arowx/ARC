#!/usr/bin/env python3
"""Pack asset files into one .arcpak for ARC (arc_pack_mount; assets.arcpak next to the game is mounted at startup).

    python3 tools/arcpak.py assets.arcpak assets            (every file under assets/, stored as "assets/...")
    python3 tools/arcpak.py out.arcpak assets music --exclude "*.psd" --list
    python3 tools/arcpak.py --info assets.arcpak

Files are zlib-compressed when that saves at least 3% (meshes, fonts, WAV and raw data shrink; JPEG, PNG, MP3 and
QOA are stored as they are). Names are the paths as given, with forward slashes: load them in the game by the same
relative path (arc_mesh_load("assets/fauna/thorn1.arcm")). No third-party modules needed.

Format (little endian): "ARCP" u32 1, u32 count, u32 dir_bytes; per file u32 offset, u32 stored_size, u32 size,
u8 method (0 stored, 1 zlib), u8 name_len, name; then the data.
"""
import argparse, fnmatch, os, struct, sys, zlib


def gather(paths, excludes):
    out = []
    for p in paths:
        if os.path.isdir(p):
            for root, dirs, files in os.walk(p):
                dirs.sort()
                for f in sorted(files):
                    out.append(os.path.join(root, f))
        elif os.path.isfile(p):
            out.append(p)
        else:
            sys.exit('not found: ' + p)
    names = []
    for f in out:
        n = os.path.normpath(f).replace('\\', '/')
        while n.startswith('./'): n = n[2:]
        if any(fnmatch.fnmatch(n, e) or fnmatch.fnmatch(os.path.basename(n), e) for e in excludes): continue
        if len(n.encode()) > 255: sys.exit('name too long (255 bytes max): ' + n)
        names.append((n, f))
    seen = set()
    for n, _ in names:
        if n in seen: sys.exit('duplicate name: ' + n)
        seen.add(n)
    return names


def build(out_path, files, level=9, verbose=False):
    entries, blobs = [], []
    for name, path in files:
        raw = open(path, 'rb').read()
        z = zlib.compress(raw, level)
        if len(z) < len(raw) * 0.97: data, method = z, 1
        else: data, method = raw, 0
        entries.append((name, len(data), len(raw), method)); blobs.append(data)
        if verbose: print('%-48s %9d -> %9d %s' % (name, len(raw), len(data), 'zlib' if method else 'stored'))
    dir_bytes = sum(14 + len(n.encode()) for n, _, _, _ in entries)
    off = 16 + dir_bytes
    with open(out_path, 'wb') as o:
        o.write(b'ARCP' + struct.pack('<3I', 1, len(entries), dir_bytes))
        for (name, st, sz, m), d in zip(entries, blobs):
            nb = name.encode()
            o.write(struct.pack('<3IBB', off, st, sz, m, len(nb)) + nb)
            off += st
        for d in blobs: o.write(d)
    raw_total = sum(e[2] for e in entries); st_total = sum(e[1] for e in entries)
    print('%s: %d files, %.2f MB -> %.2f MB' % (out_path, len(entries), raw_total / 1e6, os.path.getsize(out_path) / 1e6))


def info(path):
    d = open(path, 'rb').read()
    if d[:4] != b'ARCP': sys.exit('not an .arcpak file')
    ver, count, dir_bytes = struct.unpack_from('<3I', d, 4)
    at = 16
    for _ in range(count):
        off, st, sz, m, nl = struct.unpack_from('<3IBB', d, at)
        name = d[at + 14:at + 14 + nl].decode()
        print('%-48s %9d %9d %s' % (name, sz, st, 'zlib' if m else 'stored'))
        at += 14 + nl
    print('%d files, version %d' % (count, ver))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', nargs='?'); ap.add_argument('inputs', nargs='*')
    ap.add_argument('--exclude', action='append', default=[], help='glob of files to leave out (repeatable)')
    ap.add_argument('--list', action='store_true', help='print every file')
    ap.add_argument('--info', metavar='PACK', help='list the contents of a pack')
    a = ap.parse_args()
    if a.info: info(a.info); return
    if not a.out or not a.inputs: ap.error('give OUT.arcpak and files or folders')
    build(a.out, gather(a.inputs, a.exclude), verbose=a.list)


if __name__ == '__main__':
    main()
