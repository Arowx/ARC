#!/usr/bin/env python3
"""Split a game's assets/ for a web build that starts small (ARC 0.8.6, docs/WEB.md).

    python3 tools/web_packs.py assets build/web build/webstage --packs "levels music"
    python3 tools/web_packs.py assets build/web build/webstage --packs "*"        every folder but assets/first/
    python3 tools/web_packs.py --list build/webstage/arc_packs.txt

Each folder of assets/ named in --packs becomes OUT/<folder>.arcpak (next to index.html; its files keep their names,
"assets/levels/l1.arcm"); everything else is copied to STAGE/assets/ for the first package that the page preloads
(emcc --preload-file STAGE@/), with STAGE/arc_packs.txt, the list ARC reads to know which pack holds which file:

    pack levels.arcpak 1234567 9f86d081884c7d65        name, bytes, content hash (the browser cache's key)
    file levels.arcpak 23456 assets/levels/l1.arcm      pack, file bytes, the file's name

A queued load (arc_load_begin) of a file in a pack that is not here yet fetches the pack first; arc_pack_fetch starts one
early. Packs come from the browser's cache on later visits until their content changes. "*" packs every folder except
"first" (keep the title screen's files there). Files at the top of assets/ always go in the first package.
No third-party modules needed (uses tools/arcpak.py).
"""
import argparse, hashlib, os, shutil, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arcpak  # noqa: E402


def split(assets, out_dir, stage, packs, verbose=False):
    assets = os.path.normpath(assets)
    prefix = os.path.basename(assets)                     # "assets": the names the game loads by
    if not os.path.isdir(assets): sys.exit('not a folder: ' + assets)
    folders = sorted(d for d in os.listdir(assets) if os.path.isdir(os.path.join(assets, d)))
    want = folders if packs.strip() == '*' else packs.replace(',', ' ').split()
    if packs.strip() == '*': want = [d for d in want if d != 'first']
    for w in want:
        if w not in folders: sys.exit('no folder %s in %s' % (w, assets))
    os.makedirs(out_dir, exist_ok=True)
    if os.path.isdir(stage): shutil.rmtree(stage)
    os.makedirs(os.path.join(stage, prefix))
    lines = ['# ARC web packs (tools/web_packs.py): pack NAME BYTES HASH / file PACK BYTES PATH']
    first = 0
    for root, dirs, files in os.walk(assets):
        dirs.sort()
        rel = os.path.relpath(root, assets)
        top = rel.split(os.sep)[0] if rel != '.' else ''
        if top in want: continue
        for f in sorted(files):
            src = os.path.join(root, f); dst = os.path.join(stage, prefix, rel, f)
            os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copyfile(src, dst); first += os.path.getsize(src)
    for w in want:
        entries = []
        for root, dirs, files in os.walk(os.path.join(assets, w)):
            dirs.sort()
            for f in sorted(files):
                p = os.path.join(root, f)
                entries.append((prefix + '/' + os.path.relpath(p, assets).replace(os.sep, '/'), p))
        name = w + '.arcpak'; out = os.path.join(out_dir, name)
        arcpak.build(out, entries, verbose=verbose)
        h = hashlib.sha256(open(out, 'rb').read()).hexdigest()[:16]
        lines.append('pack %s %d %s' % (name, os.path.getsize(out), h))
        for n, p in entries: lines.append('file %s %d %s' % (name, os.path.getsize(p), n))
    with open(os.path.join(stage, 'arc_packs.txt'), 'w', newline='\n') as o: o.write('\n'.join(lines) + '\n')
    print('first package: %.2f MB in %s; packs: %s' % (first / 1e6, stage, ', '.join(w + '.arcpak' for w in want) or 'none'))


def show(path):
    for l in open(path):
        if l.startswith('pack '): print(l.rstrip())
    print('%d files in packs' % sum(1 for l in open(path) if l.startswith('file ')))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('assets', nargs='?'); ap.add_argument('out', nargs='?'); ap.add_argument('stage', nargs='?')
    ap.add_argument('--packs', default='*', help='folders of assets/ to pack ("*": all but first/)')
    ap.add_argument('--list', metavar='ARC_PACKS_TXT', help='print the packs of a manifest')
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args()
    if a.list: return show(a.list)
    if not (a.assets and a.out and a.stage): ap.error('assets, out and stage are needed')
    split(a.assets, a.out, a.stage, a.packs, a.verbose)


if __name__ == '__main__':
    main()
