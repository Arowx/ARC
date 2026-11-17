#!/usr/bin/env python3
"""Generate the API docs from include/arc.h, so they never drift from the code.
    python3 tools/gen_api_docs.py           writes docs/API.md (everything), docs/QUICK.md (a short
                                            index: every name, one line each, by topic) and
                                            docs/api/<topic>.md (one page per topic: signatures + notes)
    python3 tools/gen_api_docs.py --check   exits 1 if any of those files is out of date (used by build test)
Rules the header follows (keep them when editing arc.h):
  - sections start with a banner:  /* ===...=== */ then /* Section title */
  - a comment block directly above a declaration describes it
  - a trailing /* ... */ on the same line is a short note
"""
import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
src = open(os.path.join(ROOT, 'include', 'arc.h'), encoding='utf-8').read()
lines = src.split('\n')

def strip_comment(c):
    c = c.strip()
    c = re.sub(r'^/\*+', '', c); c = re.sub(r'\*+/$', '', c)
    return '\n'.join(re.sub(r'^\s*\*\s?', '', l) for l in c.split('\n')).strip()

# Graphics is big, so it is split into topic pages at these comments (the first words of a comment).
GFX_TOPICS = [
    ('Colours:', 'graphics-meshes', 'Colours, meshes and materials'),
    ('Textures (RGBA8)', 'graphics-textures', 'Textures and images'),
    ('3D camera', 'graphics-camera', 'Camera and depth'),
    ('Lighting (per pixel)', 'graphics-lighting', 'Lighting, shadows, reflections and post-processing'),
    ('Draw a mesh.', 'graphics-drawing', 'Drawing meshes and mesh styles'),
    ('---- skeletal animation', 'graphics-animation', 'Skeletal animation'),
    ('---- particles', 'graphics-effects', 'Particles, trails, decals and stamps'),
    ('Every option in one call', 'graphics-drawing', None),
    ('Immediate geometry', 'graphics-shapes', 'Immediate geometry, quick shapes and 3D lines'),
    ('2D overlay in screen pixels', 'graphics-2d', '2D drawing: rects, sprites, nine-slice, blend, clip'),
    ('Built-in 8x8 font', 'graphics-text', 'Text: built-in font, baked fonts, rich text'),
    ('---- 2D extras', 'graphics-2d', None),
    ('---- Render targets', 'graphics-targets', 'Render targets, screenshots and frame timing'),
    ('---- Scene speed-ups', 'graphics-scene', 'Scene speed-ups: cells, portals, LOD, static batches'),
]
def slug(t): return re.sub(r'[^a-z0-9]+', '-', t.lower().split('(')[0].split(':')[0]).strip('-')
topics = {}            # slug -> {'title', 'section', 'items'}
topic_order = []
cur_topic = None
def set_topic(sl, title, section):
    global cur_topic
    if sl not in topics: topics[sl] = {'title': title, 'section': section, 'items': []}; topic_order.append(sl)
    cur_topic = sl
sections, cur, pending = [], None, []
i = 0
intro = []
# file intro = first comment block
m = re.match(r'\s*/\*(.*?)\*/', src, re.S)
if m: intro = strip_comment('/*' + m.group(1) + '*/')
while i < len(lines):
    l = lines[i]; s = l.strip()
    if s.startswith('/* ====') and i + 1 < len(lines):
        title = strip_comment(lines[i + 1]); cur = {'title': title, 'notes': [], 'items': []}; sections.append(cur)
        set_topic(slug(title), title, title)
        i += 3 if lines[i + 2].strip().startswith('/* ====') else 2; pending = []; continue
    if cur is None or not s:
        if not s: pending = [] if not pending or pending[-1] != '' else pending
        i += 1; continue
    if s.startswith('/*'):
        block = [l]
        while '*/' not in lines[i] and i + 1 < len(lines): i += 1; block.append(lines[i])
        pending.append(strip_comment('\n'.join(block)))
        if cur['title'] == 'Graphics':
            for mk, sl, tt in GFX_TOPICS:
                if pending[-1].startswith(mk): set_topic(sl, tt or topics[sl]['title'], 'Graphics'); break
        i += 1; continue
    if s.startswith('#ifdef') or s.startswith('#ifndef') or s.startswith('#else') or s.startswith('#endif') or s.startswith('#if') or s.startswith('#include') or s.startswith('}') and 'extern' in s or s.startswith('extern "C"'):
        i += 1; continue
    # gather a declaration (until ; or { ... } balanced for inline bodies, or a #define line)
    decl = [l]
    if s.startswith('#define'):
        while decl[-1].rstrip().endswith('\\') and i + 1 < len(lines): i += 1; decl.append(lines[i])
    else:
        depth = l.count('{') - l.count('}')
        while (depth > 0 or (';' not in lines[i] and '{' not in lines[i])) and i + 1 < len(lines):
            i += 1; decl.append(lines[i]); depth += lines[i].count('{') - lines[i].count('}')
            if depth <= 0 and ('}' in lines[i] or ';' in lines[i]): break
    text = '\n'.join(decl)
    note = ''
    tm = re.search(r'/\*(.*?)\*/\s*$', decl[0])
    if tm and not decl[0].strip().startswith('/*'): note = tm.group(1).strip()
    # signature: drop inline bodies
    sig = re.sub(r'\s*/\*.*?\*/', '', text, flags=re.S)
    if sig.lstrip().startswith('static inline'):
        sig = sig.split('{')[0].replace('static inline ', '').strip() + ';'
    elif sig.lstrip().startswith('#define'):
        sig = sig.strip().split('\n')[0].rstrip('\\').strip()
        if len(sig) > 110: sig = sig[:sig.find(')') + 1] if ')' in sig else sig[:110]
    else:
        sig = ' '.join(sig.split())
    desc = '\n'.join(p for p in pending if p).strip()
    it = {'sig': sig.strip(), 'note': note, 'desc': desc}
    cur['items'].append(it); topics[cur_topic]['items'].append(it)
    pending = []; i += 1

out = ['# ARC API reference', '',
       '_Generated from `include/arc.h` by `tools/gen_api_docs.py`. Do not edit by hand: change the header, then rerun the tool._', '',
       '```text', intro, '```', '', '## Sections', '']
for s in sections: out.append('- [%s](#%s)' % (s['title'], re.sub(r'[^a-z0-9 -]', '', s['title'].lower()).replace(' ', '-')))
seen = set()
for s in sections:
    out += ['', '## ' + s['title'], '']
    block = []
    def flush():
        if block: out.append('```c\n' + '\n'.join(block) + '\n```'); block.clear()
    for it in s['items']:
        key = re.sub(r'\s+', ' ', it['sig'].split('(')[0])
        if it['sig'].startswith('#define') and key in seen: continue   # C/C++ variants of one macro
        seen.add(key)
        if it['desc']: flush(); out += ['', it['desc'], '']
        sig = it['sig'] if '\n' in it['sig'] else ' '.join(it['sig'].split())
        block.append(sig + (('    // ' + it['note']) if it['note'] else ''))
    flush()
files = {os.path.join('docs', 'API.md'): '\n'.join(out) + '\n'}

def name_of(sig):
    s = sig.strip()
    if s.startswith('#define'): return s.split()[1].split('(')[0]
    if s.startswith('typedef'):
        m = re.search(r'}\s*(\w+)\s*;\s*$', s, re.S) or re.search(r'(\w+)\s*;\s*$', s) or re.search(r'\(\s*\*\s*(\w+)\)', s)
        return m.group(1) if m else None
    if s.startswith('enum') or s.startswith('struct'): return None
    m = re.search(r'(\w+)\s*\(', s)
    if m: return m.group(1)
    m = re.search(r'(\w+)\s*(\[[^\]]*\])?\s*;\s*$', s)
    return m.group(1) if m else None

def first_sentence(t, n=90):
    t = ' '.join(t.split())
    m = re.match(r'(.+?[.!?])(\s|$)', t)
    t = m.group(1) if m else t
    return t if len(t) <= n else t[:n - 1].rstrip() + '…'

HDR = '_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._'
q = ['# ARC quick reference', '', HDR, '',
     'Every public name, one line each, grouped by topic. Open the topic page for signatures and notes;',
     '`docs/API.md` has everything on one page. Names are C functions unless marked _(type)_ or _(macro)_.', '',
     '## A whole game (tick, draw, dump)', '', '```c',
     '#include "arc.h"',
     'static arc_mesh cube; static float px; static int score;',
     'static void init(void) { cube = arc_mesh_cube(0x40A0FF); arc_mesh_set_name(cube, "player"); }',
     'static void tick(void) {                  /* fixed 60 Hz: all game logic and UI here */',
     '    px += ((arc_key_down(ARC_KEY_D) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_A) ? 1.0f : 0.0f)) * 4.0f * arc_dt();',
     '    if (px > 5) { score++; px = 0; }',
     '}',
     'static void draw(float alpha) {           /* read-only: draw what tick decided */',
     '    (void)alpha;',
     '    arc_camera cam = { { 0, 4, 10 }, { 0, 0, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 100 };',
     '    arc_set_camera(&cam);',
     '    arc_draw_mesh(cube, arc_m4_translate(arc_v3_make(px, 0.5f, 0)), 0xFFFFFF);',
     '    arc_draw_textf(10, 10, 2, 0xFFFFFF, "SCORE %d", score);',
     '}',
     'static void dump(arc_json *j) { arc_json_num_k(j, "x", px); arc_json_num_k(j, "score", score); }',
     'arc_app arc_main(int argc, char **argv) {',
     '    (void)argc; (void)argv;',
     '    return (arc_app){ .title = "Demo", .width = 960, .height = 540,',
     '                      .init = init, .tick = tick, .draw = draw, .dump = dump };',
     '}', '```', '', '## Topics', '']
for sl in topic_order:
    t = topics[sl]
    if t['items']: q.append('- [%s](api/%s.md) - %d names' % (t['title'], sl, len(t['items'])))
nnames = 0
for sl in topic_order:
    t = topics[sl]
    if not t['items']: continue
    q += ['', '## [%s](api/%s.md)' % (t['title'], sl), '']
    seen_n = set()
    for it in t['items']:
        n = name_of(it['sig'])
        if not n or n in seen_n: continue
        seen_n.add(n); nnames += 1
        kind = ' _(macro)_' if it['sig'].startswith('#define') else ' _(type)_' if it['sig'].startswith('typedef') else ''
        what = it['note'] or it['desc']
        q.append('- `%s`%s%s' % (n, kind, (' - ' + first_sentence(what)) if what else ''))
    # topic page
    pg = ['# %s' % t['title'], '', HDR, '', 'Part of **%s** · [quick reference](../QUICK.md) · [full API](../API.md)' % t['section'], '']
    block = []
    def flush2():
        if block: pg.append('```c\n' + '\n'.join(block) + '\n```'); block.clear()
    seen_k = set()
    for it in t['items']:
        key = re.sub(r'\s+', ' ', it['sig'].split('(')[0])
        if it['sig'].startswith('#define') and key in seen_k: continue
        seen_k.add(key)
        if it['desc']: flush2(); pg += ['', it['desc'], '']
        sig = it['sig'] if '\n' in it['sig'] else ' '.join(it['sig'].split())
        block.append(sig + (('    // ' + it['note']) if it['note'] else ''))
    flush2()
    files[os.path.join('docs', 'api', sl + '.md')] = '\n'.join(pg) + '\n'
files[os.path.join('docs', 'QUICK.md')] = '\n'.join(q) + '\n'

if '--check' in sys.argv:
    stale = [p for p, txt in files.items()
             if not os.path.exists(os.path.join(ROOT, p)) or open(os.path.join(ROOT, p), encoding='utf-8').read() != txt]
    apidir = os.path.join(ROOT, 'docs', 'api')
    extra = [os.path.join('docs', 'api', f) for f in (os.listdir(apidir) if os.path.isdir(apidir) else [])
             if f.endswith('.md') and os.path.join('docs', 'api', f) not in files]
    if stale or extra:
        print('ARC error api docs out of date (run python3 tools/gen_api_docs.py):', ', '.join(stale + extra)); sys.exit(1)
    print('ARC ok api docs (%d files)' % len(files)); sys.exit(0)
os.makedirs(os.path.join(ROOT, 'docs', 'api'), exist_ok=True)
for p, txt in files.items():
    open(os.path.join(ROOT, p), 'w', encoding='utf-8', newline='\n').write(txt)
print('wrote docs/API.md, docs/QUICK.md and %d topic pages: %d declarations, %d names'
      % (len(files) - 2, sum(len(s['items']) for s in sections), nnames))
