#!/usr/bin/env python3
"""Convert models to ARC meshes (.arcm, loaded with arc_mesh_load) and animations (.arca, arc_anim_load).

    python3 tools/mesh2arcm.py model.glb out.arcm [options]
    python3 tools/mesh2arcm.py --batch models/ assets/meshes/ [options]        every .glb/.gltf/.obj in a folder
    python3 tools/mesh2arcm.py rig.glb player.arcm --anim player.arca idle.glb jog.glb punch.glb [options]

Inputs: .glb / .gltf (built-in reader: textures, vertex colours, skins, animations; needs numpy and Pillow),
        .obj (built-in: per-vertex colours or .mtl Kd colours), anything else through the optional 'trimesh'.

Size and placement (applied in this order):
  --yaw DEG          turn the model around +Y first (fix which way it faces)
  --footprint N      largest horizontal size = N, centred on X/Z, feet at y = 0   (game characters and props)
  --height N         height = N, centred on X/Z, feet at y = 0
  --size N           largest dimension = N, centred on X/Z, feet at y = 0          (older option)
  --fit X Y Z        (0.9.8) scale each axis to exactly this size (a door leaf 0.9 2.0 0.05); 0 = that axis takes
                     the smallest of the other scales
  --fit-uniform X Y Z  one scale so the model fits inside X x Y x Z (0 = no limit on that axis)
  --origin bottom|centre|back   where (0, 0, 0) goes (default bottom: centred on X/Z, feet at y = 0; centre: the
                     middle of the box; back: centred on X, feet at y = 0, the back (lowest z) at z = 0). Works with
                     any of the size options, or alone (no scaling).
  The final bounds are printed ("bounds min ... max ... size ...") for tools to read (arc_import --json has them).
Shape:
  --flat             one normal per face (faceted look). glTF keeps its own shared vertices and normals by
                     default; OBJ is faceted by default unless --smooth.
  --smooth           OBJ: shared vertices with averaged normals
Colour:
  --texture-size N   largest texture side kept (default 1024; 0 = as in the file)
                     The texture is stored as JPEG (the file's own when possible; quality --jpeg-quality, default 85)
                     or as PNG when it has transparency or with --png.
  --bake-colors      no texture: bake it into vertex colours (the ARC 0.3 look)
  --color RRGGBB     one colour for everything
  --ignore-color-factor / --keep-color-factor
                     The glTF standard multiplies a texture by its material's baseColorFactor. Some generators (Ludo,
                     for one) have exported a flat grey factor such as 0.4 with the texture, which darkens the model, so
                     by default a flat grey factor (r = g = b < 1) on a textured material is ignored, with a note.
                     --ignore-color-factor ignores any factor's colour on textured materials (alpha still applies);
                     --keep-color-factor applies every factor as the standard says.
Animation (skinned glTF only):
  --anim OUT.arca FILE...   clips from each file (a rig's own animations count too); a clip is named after its
                     file when the file has one animation, else file:animation
  --fps N            sampling rate for clips (default 30)
  --in-place         remove the root's sideways travel (clips that walk away from the origin)
Materials (ARC 0.6; glTF maps are kept by default):
  normal, metallic-roughness, occlusion and emissive maps (and their factors) are stored with the mesh, laid out
  like the colour texture, so ARC lights it with bumps, metal and roughness highlights and glow.
  --no-material      leave them out (the pre-0.6 look)       --no-pbr   keep the maps, plain lighting
  --metallic F / --roughness F               override the factors (0..1); either turns pbr on
  --normal-map IMG   a tangent-space normal map (OpenGL convention: green = up the texture)
  --normal-from-height IMG [--normal-strength S]   make one from an image's brightness (light = raised; S 2)
  --orm-map IMG      packed red occlusion, green roughness, blue metallic
  --emissive-map IMG [--emissive-strength F]       glowing parts (F: brightness, above 1 blooms)
  --glow-mask IMG                                  0.10.15: lamps switched per draw: red, green and blue mark three groups
                                                   (arc_mesh_style.glow[3]); stored in the .arcm, read by arc_mesh_glow_mask
  --normal-png       store the normal map losslessly (default JPEG quality 92)
  --map-size N       shrink the material maps to N pixels on their longest side (e.g. 256 with a 512 texture)
GPU-compressed textures (ARC 0.7.1; needs tools/texcompress.py next to this file):
  --compress         store the texture and maps block-compressed with mip levels (.arcm version 4): 4-8x less GPU
                     memory and no image decoding at load, but bigger files than JPEG (packs zlib them). Images whose
                     sides are not multiples of 4 stay PNG/JPEG. Formats: --color-format auto|bc1|bc3|bc7 (auto: BC1, or
                     BC3 with soft alpha), --normal-format bc5|bc3|bc1 (bc5), --map-format bc1|bc7 (bc1).
                     tools/texcompress.py --arcm converts an existing .arcm the same way.
Other:
  --axm              write the older AXE AXM2 format (unindexed, no texture)

.arcm v2 layout (little endian): "ARCM" u32 2, u32 vcount, u32 icount, u32 flags (1 skinned, 2 double-sided,
4 16-bit indices), u32 texture_bytes, u32 material_bytes (0.6), u32 0; vcount x {f32 pos[3], f32 normal[3], u8 rgba[4], f32 u, v};
skinned: vcount x {u8 joints[4], u8 weights[4] (sum 255)}; indices (padded to 4 bytes); then the texture file
(PNG, QOI or baseline JPEG); then the material block (see src/arc_gfx.c). Version 3: 16-byte vertices (see
src/arc_gfx.c); version 4 (ARC 0.7.1, --compress): version 3 whose texture and maps may be .arct block-compressed
images (see src/arc_texc.c); older ARC refuses version 4 files.
.arca layout: see src/arc_anim.c.
"""
import argparse, io, json, math, os, struct, sys

# ---------------------------------------------------------------- small math (numpy)
def need_np():
    try:
        import numpy as np  # noqa
        return np
    except ImportError:
        sys.exit('glTF conversion needs numpy (pip install numpy pillow)')


def quat_mat(q):
    np = need_np(); x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def trs_mat(t, r, s):
    np = need_np(); m = np.eye(4); m[:3, :3] = quat_mat(r) * np.asarray(s)[None, :]; m[:3, 3] = t
    return m


def mat_quat(r3):
    np = need_np(); t = r3[0, 0] + r3[1, 1] + r3[2, 2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2; w = 0.25 * s; x = (r3[2, 1] - r3[1, 2]) / s; y = (r3[0, 2] - r3[2, 0]) / s; z = (r3[1, 0] - r3[0, 1]) / s
    elif r3[0, 0] > r3[1, 1] and r3[0, 0] > r3[2, 2]:
        s = math.sqrt(1.0 + r3[0, 0] - r3[1, 1] - r3[2, 2]) * 2; w = (r3[2, 1] - r3[1, 2]) / s; x = 0.25 * s; y = (r3[0, 1] + r3[1, 0]) / s; z = (r3[0, 2] + r3[2, 0]) / s
    elif r3[1, 1] > r3[2, 2]:
        s = math.sqrt(1.0 + r3[1, 1] - r3[0, 0] - r3[2, 2]) * 2; w = (r3[0, 2] - r3[2, 0]) / s; x = (r3[0, 1] + r3[1, 0]) / s; y = 0.25 * s; z = (r3[1, 2] + r3[2, 1]) / s
    else:
        s = math.sqrt(1.0 + r3[2, 2] - r3[0, 0] - r3[1, 1]) * 2; w = (r3[1, 0] - r3[0, 1]) / s; x = (r3[0, 2] + r3[2, 0]) / s; y = (r3[1, 2] + r3[2, 1]) / s; z = 0.25 * s
    q = np.array([x, y, z, w]); return q / np.linalg.norm(q)


def node_trs(n):
    np = need_np()
    if 'matrix' in n:
        m = np.array(n['matrix'], dtype=np.float64).reshape(4, 4).T
        t = m[:3, 3].copy(); s = np.linalg.norm(m[:3, :3], axis=0); s[s == 0] = 1
        return t, mat_quat(m[:3, :3] / s[None, :]), s
    return (np.array(n.get('translation', [0, 0, 0]), dtype=np.float64), np.array(n.get('rotation', [0, 0, 0, 1]), dtype=np.float64),
            np.array(n.get('scale', [1, 1, 1]), dtype=np.float64))


def slerp(a, b, t):
    np = need_np(); d = float(np.dot(a, b))
    if d < 0: b = -b; d = -d
    if d > 0.9995: q = a + t * (b - a); return q / np.linalg.norm(q)
    th = math.acos(min(1.0, d)); return (math.sin((1 - t) * th) * a + math.sin(t * th) * b) / math.sin(th)

# ---------------------------------------------------------------- glTF reading
CT = {5120: 'i1', 5121: 'u1', 5122: 'i2', 5123: 'u2', 5125: 'u4', 5126: 'f4'}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


class GLTF:
    def __init__(self, path):
        np = need_np()
        self.path = path; data = open(path, 'rb').read()
        if data[:4] == b'glTF':
            jl = struct.unpack('<I', data[12:16])[0]
            self.j = json.loads(data[20:20 + jl]); off = 20 + jl
            self.bin = data[off + 8:off + 8 + struct.unpack('<I', data[off:off + 4])[0]] if off + 8 <= len(data) else b''
        else:
            self.j = json.loads(data); self.bin = b''
        self.buffers = []
        for i, b in enumerate(self.j.get('buffers', [])):
            uri = b.get('uri')
            if uri is None: self.buffers.append(self.bin)
            elif uri.startswith('data:'):
                import base64; self.buffers.append(base64.b64decode(uri.split(',', 1)[1]))
            else: self.buffers.append(open(os.path.join(os.path.dirname(path), uri), 'rb').read())
        self.nodes = self.j.get('nodes', [])
        self.parent = [-1] * len(self.nodes)
        for i, n in enumerate(self.nodes):
            for c in n.get('children', []): self.parent[c] = i
        self.np = np

    def view_bytes(self, bvi):
        bv = self.j['bufferViews'][bvi]; buf = self.buffers[bv.get('buffer', 0)]; o = bv.get('byteOffset', 0)
        return buf[o:o + bv['byteLength']], bv.get('byteStride', 0)

    def accessor(self, i):
        np = self.np; a = self.j['accessors'][i]
        dt = np.dtype('<' + CT[a['componentType']]); n = NC[a['type']]; cnt = a['count']
        if 'bufferView' not in a: out = np.zeros((cnt, n), dt)
        else:
            raw, stride = self.view_bytes(a['bufferView']); start = a.get('byteOffset', 0); stride = stride or dt.itemsize * n
            buf = np.frombuffer(raw, np.uint8, count=stride * (cnt - 1) + dt.itemsize * n, offset=start)
            out = np.lib.stride_tricks.as_strided(buf, shape=(cnt, dt.itemsize * n), strides=(stride, 1)).copy().view(dt).reshape(cnt, n)
        if 'sparse' in a: sys.exit('%s: sparse accessors are not supported' % self.path)
        if a.get('normalized') and dt.kind in 'iu': out = out.astype(np.float64) / np.iinfo(dt).max
        return out

    def local(self, i): return trs_mat(*node_trs(self.nodes[i]))

    def world(self, i):
        m = self.local(i); p = self.parent[i]
        while p >= 0: m = self.local(p) @ m; p = self.parent[p]
        return m

    def image(self, tex_index):
        from PIL import Image
        t = self.j['textures'][tex_index]; src = t.get('source')
        if src is None:
            for ext in t.get('extensions', {}).values():
                if 'source' in ext: src = ext['source']
        img = self.j['images'][src]
        if 'bufferView' in img: data = self.view_bytes(img['bufferView'])[0]
        elif img.get('uri', '').startswith('data:'):
            import base64; data = base64.b64decode(img['uri'].split(',', 1)[1])
        else: data = open(os.path.join(os.path.dirname(self.path), img['uri']), 'rb').read()
        im = Image.open(io.BytesIO(data)); im.load()
        out = im.convert('RGBA'); out.arc_source = (data, im.format, bool(im.info.get('progressive') or im.info.get('progression')))
        return out

# ---------------------------------------------------------------- mesh building
class Mesh:
    """positions P (n,3), normals N (n,3), colours C (n,4 uint8), uvs UV (n,2), indices I (m,), optional skin J/W"""
    def __init__(self): self.parts = []; self.tex = None; self.double = False; self.skin = None


def gather_gltf(g, a):
    """every primitive of every mesh node in the default scene, in world space (skinned: mesh space)"""
    np = g.np
    scene = g.j.get('scenes', [{}])[g.j.get('scene', 0)] if g.j.get('scenes') else {'nodes': [i for i in range(len(g.nodes)) if g.parent[i] < 0]}
    order, stack = [], list(reversed(scene.get('nodes', [])))
    while stack:
        i = stack.pop(); order.append(i); stack.extend(reversed(g.nodes[i].get('children', [])))
    prims, skin_index, skinned_nodes = [], None, []
    for ni in order:
        n = g.nodes[ni]
        if 'mesh' not in n: continue
        if 'skin' in n:
            if skin_index is None: skin_index = n['skin']
            if n['skin'] != skin_index: print('  note: %s uses several skins; only the first is kept' % g.path)
            skinned_nodes.append(ni)
        W = g.world(ni)
        for pr in g.j['meshes'][n['mesh']]['primitives']:
            if pr.get('mode', 4) != 4: continue
            prims.append((ni, pr, W, 'skin' in n and n['skin'] == skin_index))
    if not prims: sys.exit('%s: no triangle meshes found' % g.path)
    skinned = skin_index is not None
    if skinned: prims = [p for p in prims if p[3]]
    texmap, images = {}, []
    out = []
    for ni, pr, W, sk in prims:
        at = pr['attributes']
        P = g.accessor(at['POSITION']).astype(np.float64)
        N = g.accessor(at['NORMAL']).astype(np.float64) if 'NORMAL' in at else None
        UV = g.accessor(at['TEXCOORD_0']).astype(np.float64) if 'TEXCOORD_0' in at else np.zeros((len(P), 2))
        C = np.ones((len(P), 4))
        if 'COLOR_0' in at:
            c = g.accessor(at['COLOR_0']).astype(np.float64)
            if c.max() > 1.0: c = c / 255.0
            C[:, :c.shape[1]] = c
        I = g.accessor(pr['indices']).reshape(-1).astype(np.int64) if 'indices' in pr else np.arange(len(P))
        mat = g.j['materials'][pr['material']] if 'material' in pr else {}
        pbr = mat.get('pbrMetallicRoughness', {})
        tex = pbr.get('baseColorTexture', {}).get('index')
        factor = np.array(pbr.get('baseColorFactor', [1, 1, 1, 1]), dtype=np.float64)
        grey = factor[0] == factor[1] == factor[2] and factor[0] < 1.0
        if tex is not None and not getattr(a, 'keep_color_factor', False) and (getattr(a, 'ignore_color_factor', False) or grey):
            if factor[0] < 1.0 or factor[1] < 1.0 or factor[2] < 1.0:
                print('  note: material %s: ignored baseColorFactor %s on a textured material (it would darken the texture; '
                      '--keep-color-factor applies it)' % (pr.get('material', 0), [round(float(v), 3) for v in factor[:3]]))
            factor[:3] = 1.0   # textured: trust the texture's colours (keep the factor's alpha)
        C = C * factor[None, :]
        if mat.get('doubleSided'): out_double = True
        else: out_double = False
        J = Wt = None
        if sk:
            J = g.accessor(at['JOINTS_0']).astype(np.int64); Wt = g.accessor(at['WEIGHTS_0']).astype(np.float64)
        else:   # bake the node transform in
            P = (W[:3, :3] @ P.T).T + W[:3, 3]
            if N is not None:
                nm = np.linalg.inv(W[:3, :3]).T; N = (nm @ N.T).T
            if np.linalg.det(W[:3, :3]) < 0: I = I.reshape(-1, 3)[:, ::-1].reshape(-1)
        out.append(dict(P=P, N=N, UV=UV, C=C, I=I, tex=tex, J=J, W=Wt, double=out_double, mat=pr.get('material')))
    return out, (skin_index if skinned else None), skinned_nodes


def build_texture(g, parts, a):
    """one texture per mesh: a single image as is, several placed side by side (UVs moved to match)"""
    np = g.np
    texes = []
    for p in parts:
        if p['tex'] is not None and p['tex'] not in texes: texes.append(p['tex'])
    if not texes or a.bake_colors or a.color: return None
    from PIL import Image
    imgs = [g.image(t) for t in texes]
    g.arc_layout = (texes, [i.size for i in imgs])   # for material maps packed the same way
    if len(imgs) == 1: atlas = imgs[0]; place = {texes[0]: (0, 0, 1, 1)}
    else:
        print('  note: %d textures packed side by side into one (ARC meshes have one texture)' % len(imgs))
        h = max(i.size[1] for i in imgs); imgs = [i.resize((max(1, round(i.size[0] * h / i.size[1])), h), Image.LANCZOS) for i in imgs]
        Wt = sum(i.size[0] for i in imgs); atlas = Image.new('RGBA', (Wt, h)); x = 0; place = {}
        for t, im in zip(texes, imgs): atlas.paste(im, (x, 0)); place[t] = (x / Wt, 0, im.size[0] / Wt, 1); x += im.size[0]
        for p in parts:
            if p['tex'] in place:
                ox, oy, sx, sy = place[p['tex']]
                p['UV_orig'] = p['UV']
                u = p['UV'][:, 0] % 1.0 if p['UV'][:, 0].max() > 1.0001 or p['UV'][:, 0].min() < -0.0001 else p['UV'][:, 0]
                p['UV'] = np.stack([ox + u * sx, p['UV'][:, 1]], 1)
    for p in parts:   # parts without a texture would sample the image: give them a white texel? use the colour only
        if p['tex'] is None: p['notex'] = True
    if a.texture_size and max(atlas.size) > a.texture_size:
        s = a.texture_size / max(atlas.size); atlas = atlas.resize((max(1, round(atlas.size[0] * s)), max(1, round(atlas.size[1] * s))), Image.LANCZOS)
    return atlas


def compressed(img, a, kind):
    """--compress: .arct bytes (block-compressed, with mips) straight from the image, or None to store it as before"""
    if not getattr(a, 'compress', False): return None
    w, h = img.size
    if w % 4 or h % 4 or w < 4 or h < 4: return None
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import texcompress
    fmt = {'color': a.color_format, 'normal': a.normal_format, 'map': a.map_format}[kind]
    data, _ = texcompress.compress(img.convert('RGBA'), fmt, normal=(kind == 'normal' and fmt == 'bc5'))
    return data


def encode_texture(img, a):
    """the bytes stored in the .arcm: the file's own baseline JPEG when it can be kept as is, else JPEG (opaque
    images) or PNG (images with transparency); with --compress, block-compressed (.arct)"""
    np = need_np()
    c = compressed(img, a, 'color')
    if c: return c, 'ARCT ' + {1: 'BC1', 3: 'BC3', 5: 'BC5', 7: 'BC7'}[struct.unpack('<I', c[8:12])[0]]
    src = getattr(img, 'arc_source', None)
    if src and src[1] == 'JPEG' and not src[2] and not a.png: return src[0], 'JPEG (kept)'
    has_alpha = np.asarray(img)[:, :, 3].min() < 255
    b = io.BytesIO()
    if has_alpha or a.png: img.save(b, 'PNG', optimize=True); kind = 'PNG'
    else: img.convert('RGB').save(b, 'JPEG', quality=a.jpeg_quality, subsampling=2, progressive=False, optimize=True); kind = 'JPEG'
    return b.getvalue(), kind


def encode_map(img, a, lossless=False, kind='map'):
    if getattr(a, 'map_size', 0) and max(img.size) > a.map_size:   # smaller material maps than the colour texture
        from PIL import Image
        k = a.map_size / max(img.size); img = img.resize((max(1, round(img.size[0] * k)), max(1, round(img.size[1] * k))), Image.LANCZOS)
    c = compressed(img, a, kind)
    if c: return c
    b = io.BytesIO()
    if a.png or lossless: img.save(b, 'PNG', optimize=True)
    else:
        try: img.convert('RGB').save(b, 'JPEG', quality=92, subsampling=0, progressive=False, optimize=True)
        except OSError: b = io.BytesIO(); img.convert('RGB').save(b, 'JPEG', quality=92, subsampling=0, progressive=False)   # Pillow: optimize can fail on small buffers
    return b.getvalue()


def normal_from_height(img, strength):
    """a tangent-space normal map (OpenGL: green = up the texture) from a picture's brightness as height"""
    np = need_np(); from PIL import Image
    h = np.asarray(img.convert('L'), np.float64) / 255.0
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5 * strength * h.shape[1] / 64.0
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5 * strength * h.shape[0] / 64.0
    n = np.stack([-dx, dy, np.ones_like(h)], -1); n /= np.linalg.norm(n, axis=-1, keepdims=True)
    rgb = np.clip(np.round((n * 0.5 + 0.5) * 255), 0, 255).astype(np.uint8)
    return Image.fromarray(rgb, 'RGB').convert('RGBA')


def build_material(g, parts, atlas, a):
    """normal, orm (occlusion, roughness, metallic) and emissive maps laid out like the colour texture, plus factors.
    Returns None for a plain mesh. glTF factors are baked into the maps when several materials share one mesh."""
    if a.no_material: return None
    np = need_np(); from PIL import Image
    mats = g.j.get('materials', []) if g else []
    layout = getattr(g, 'arc_layout', None) if g else None
    used = []
    for p in parts:
        if p.get('mat') is not None and p['mat'] not in used: used.append(p['mat'])
    def M(i): return mats[i] if i is not None and i < len(mats) else {}
    def tex_of(m, key, sub=None):
        t = (m.get(sub, {}) if sub else m).get(key)
        return t.get('index') if t else None
    any_maps = any(tex_of(M(i), 'normalTexture') is not None or tex_of(M(i), 'occlusionTexture') is not None or tex_of(M(i), 'emissiveTexture') is not None
                   or tex_of(M(i), 'metallicRoughnessTexture', 'pbrMetallicRoughness') is not None for i in used)
    any_emis = any(max(M(i).get('emissiveFactor', [0, 0, 0])) > 0 for i in used)
    any_pbr = any('pbrMetallicRoughness' in M(i) and ('metallicFactor' in M(i)['pbrMetallicRoughness'] or 'roughnessFactor' in M(i)['pbrMetallicRoughness']
                  or 'metallicRoughnessTexture' in M(i)['pbrMetallicRoughness']) for i in used)
    extra = a.normal_map or a.normal_from_height or a.orm_map or a.emissive_map
    if not (any_maps or any_emis or any_pbr or extra or a.metallic is not None or a.roughness is not None): return None
    first = M(used[0]) if used else {}
    pbr_info = first.get('pbrMetallicRoughness', {})
    mat = dict(pbr=not a.no_pbr and (any_pbr or a.metallic is not None or a.roughness is not None or bool(a.orm_map)),
               nscale=float(first.get('normalTexture', {}).get('scale', 1.0)), metallic=float(pbr_info.get('metallicFactor', 1.0)),
               roughness=float(pbr_info.get('roughnessFactor', 1.0)), occlusion=float(first.get('occlusionTexture', {}).get('strength', 1.0)),
               emissive=[float(v) for v in first.get('emissiveFactor', [0, 0, 0])], maps=[None, None, None])
    es = first.get('extensions', {}).get('KHR_materials_emissive_strength', {}).get('emissiveStrength')
    if es: mat['emissive'] = [v * es for v in mat['emissive']]
    size = atlas.size if atlas is not None else None
    # maps from the glTF, one image per colour texture, in the colour texture's place
    def packed(kind):
        if not layout or atlas is None: 
            imgs = []
            for i in used:
                m = M(i)
                t = tex_of(m, 'normalTexture') if kind == 'n' else tex_of(m, 'emissiveTexture') if kind == 'e' else tex_of(m, 'occlusionTexture') if kind == 'o' else tex_of(m, 'metallicRoughnessTexture', 'pbrMetallicRoughness')
                if t is not None: imgs.append(g.image(t))
            return imgs[0] if imgs else None
        texes, sizes = layout; W = sum(sz[0] for sz in sizes); H = max(sz[1] for sz in sizes)
        fill = {'n': (128, 128, 255, 255), 'o': (255, 255, 255, 255), 'r': (255, 255, 255, 255), 'e': (255, 255, 255, 255)}[kind]
        out = Image.new('RGBA', (W, H), fill); found = False; x = 0
        for t, sz in zip(texes, sizes):
            mi = next((p['mat'] for p in parts if p['tex'] == t), None); m = M(mi)
            ti = tex_of(m, 'normalTexture') if kind == 'n' else tex_of(m, 'emissiveTexture') if kind == 'e' else tex_of(m, 'occlusionTexture') if kind == 'o' else tex_of(m, 'metallicRoughnessTexture', 'pbrMetallicRoughness')
            if ti is not None:
                im = g.image(ti).resize(sz, Image.LANCZOS); found = True
                if len(texes) > 1 and kind in 'er':   # several materials: bake each one's factors into its piece
                    arr = np.asarray(im, np.float64)
                    if kind == 'e': f = m.get('emissiveFactor', [0, 0, 0]); arr[..., :3] *= np.array(f) / max(max(mat['emissive']), 1e-6)
                    else:
                        pr = m.get('pbrMetallicRoughness', {}); arr[..., 1] *= pr.get('roughnessFactor', 1.0) / max(mat['roughness'], 1e-6); arr[..., 2] *= pr.get('metallicFactor', 1.0) / max(mat['metallic'], 1e-6)
                    im = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), 'RGBA')
                out.paste(im, (x, 0))
            x += sz[0]
        if not found: return None
        if size and out.size != size: out = out.resize(size, Image.LANCZOS)
        return out
    n_img = packed('n'); o_img = packed('o'); r_img = packed('r'); e_img = packed('e')
    if a.normal_map: n_img = Image.open(a.normal_map).convert('RGBA')
    if a.normal_from_height: n_img = normal_from_height(Image.open(a.normal_from_height), a.normal_strength)
    if a.emissive_map: e_img = Image.open(a.emissive_map).convert('RGBA'); mat['emissive'] = [a.emissive_strength] * 3 if max(mat['emissive']) <= 0 else mat['emissive']
    if a.orm_map: o_img = r_img = None; orm = Image.open(a.orm_map).convert('RGBA')
    elif o_img is not None or r_img is not None:
        ref = r_img or o_img; orm_arr = np.full((ref.size[1], ref.size[0], 4), 255, np.uint8)
        if r_img is not None: orm_arr[..., 1:3] = np.asarray(r_img)[..., 1:3]
        if o_img is not None: orm_arr[..., 0] = np.asarray(o_img.resize(ref.size, Image.LANCZOS))[..., 0]
        orm = Image.fromarray(orm_arr, 'RGBA')
    else: orm = None
    if o_img is None and not a.orm_map: mat['occlusion'] = 0.0   # no occlusion map: nothing to darken with
    if a.metallic is not None: mat['metallic'] = a.metallic
    if a.roughness is not None: mat['roughness'] = a.roughness
    def fit(im):
        if im is None: return None
        if a.texture_size and max(im.size) > a.texture_size:
            s = a.texture_size / max(im.size); im = im.resize((max(1, round(im.size[0] * s)), max(1, round(im.size[1] * s))), Image.LANCZOS)
        return im
    n_img, orm, e_img = fit(n_img), fit(orm), fit(e_img)
    if e_img is not None and max(mat['emissive']) <= 0: e_img = None   # an emissive map with factor 0 adds nothing
    mat['maps'] = [encode_map(n_img, a, lossless=a.normal_png, kind='normal') if n_img is not None else None,
                   encode_map(orm, a) if orm is not None else None, encode_map(e_img, a) if e_img is not None else None]
    if not any(mat['maps']) and not mat['pbr'] and max(mat['emissive']) <= 0: return None
    return mat


def material_block(mat):
    if not mat: return b''
    maps = [m or b'' for m in mat['maps']]
    hdr = b'MATL' + struct.pack('<2I7f3I', 1, 1 if mat['pbr'] else 0, mat['nscale'], mat['metallic'], mat['roughness'], mat['occlusion'],
                                *mat['emissive'], *[len(m) for m in maps])
    return hdr + b''.join(maps)


def bake_vertex_colours(img, UV, C):
    np = need_np()
    if img is None: return C
    px = np.asarray(img.convert('RGBA')).astype(np.float64) / 255.0; h, w = px.shape[:2]
    u = np.clip(((UV[:, 0] % 1.0) * w).astype(int), 0, w - 1); v = np.clip(((UV[:, 1] % 1.0) * h).astype(int), 0, h - 1)
    return C * px[v, u]


def smooth_normals(P, I):
    np = need_np(); N = np.zeros_like(P); t = I.reshape(-1, 3)
    fn = np.cross(P[t[:, 1]] - P[t[:, 0]], P[t[:, 2]] - P[t[:, 0]])
    for k in range(3): np.add.at(N, t[:, k], fn)
    l = np.linalg.norm(N, axis=1, keepdims=True); l[l == 0] = 1; return N / l


def fit_params(lo, hi, a):
    """(0.9.8) per-axis scale and offset from the size options and --origin: p' = p * scale + offset; None = leave it"""
    ext = [hi[i] - lo[i] for i in range(3)]
    sc = None
    if getattr(a, 'fit', None):
        given = [a.fit[i] / max(ext[i], 1e-9) for i in range(3) if a.fit[i] > 0]
        if not given: sys.exit('--fit needs at least one size above 0')
        sc = [a.fit[i] / max(ext[i], 1e-9) if a.fit[i] > 0 else min(given) for i in range(3)]
    elif getattr(a, 'fit_uniform', None):
        given = [a.fit_uniform[i] / max(ext[i], 1e-9) for i in range(3) if a.fit_uniform[i] > 0]
        if not given: sys.exit('--fit-uniform needs at least one size above 0')
        sc = [min(given)] * 3
    elif a.footprint: sc = [a.footprint / max(ext[0], ext[2], 1e-9)] * 3
    elif a.height: sc = [a.height / max(ext[1], 1e-9)] * 3
    elif a.size: sc = [a.size / max(max(ext), 1e-9)] * 3
    origin = getattr(a, 'origin', None)
    if sc is None and not origin: return None
    if sc is None: sc = [1.0, 1.0, 1.0]
    o = origin or 'bottom'
    c = [(lo[i] + hi[i]) / 2 for i in range(3)]
    anchor = [c[0], c[1] if o == 'centre' else lo[1], lo[2] if o == 'back' else c[2]]
    return sc, [-anchor[i] * sc[i] for i in range(3)]


def placement(P, a):
    """the normalising matrix N (4x4) from --yaw / --footprint / --height / --size / --fit / --origin"""
    np = need_np(); M = np.eye(4)
    if a.yaw:
        r = math.radians(a.yaw); c, s = math.cos(r), math.sin(r)
        M[:3, :3] = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    Q = (M[:3, :3] @ P.T).T; lo, hi = Q.min(0), Q.max(0)
    fp = fit_params(list(lo), list(hi), a)
    if fp is None: return M
    T = np.eye(4); T[0, 0], T[1, 1], T[2, 2] = fp[0]; T[:3, 3] = fp[1]
    return T @ M


def print_bounds(P):
    """(0.9.8) the final bounds, one line tools can read"""
    lo = [min(p[i] for p in P) for i in range(3)] if not hasattr(P, 'min') else list(P.min(0))
    hi = [max(p[i] for p in P) for i in range(3)] if not hasattr(P, 'max') else list(P.max(0))
    print('bounds min %.6g %.6g %.6g max %.6g %.6g %.6g size %.6g %.6g %.6g' % (*lo, *hi, *(hi[i] - lo[i] for i in range(3))))


COMPACT = True   # .arcm v3 (16-byte vertices); --v2 writes the 36-byte version 2 (for ARC 0.4)


def oct_encode(N):
    """unit normals -> 2 x int8 (octahedral)"""
    np = need_np()
    n = N / np.maximum(np.abs(N).sum(1, keepdims=True), 1e-12)
    x, y, z = n[:, 0].copy(), n[:, 1].copy(), n[:, 2]
    neg = z < 0
    sx = np.where(x >= 0, 1.0, -1.0); sy = np.where(y >= 0, 1.0, -1.0)
    ox = x[neg].copy()
    x[neg] = (1 - np.abs(y[neg])) * sx[neg]; y[neg] = (1 - np.abs(ox)) * sy[neg]
    return np.clip(np.round(np.stack([x, y], 1) * 127), -127, 127).astype(np.int8)


def glow_mask_png(a):
    """0.10.15 --glow-mask IMG: the mask as lossless PNG (red, green and blue groups; see arc_mesh_style.emissive_mask)"""
    if not getattr(a, 'glow_mask', None): return None
    try:
        from PIL import Image
    except ImportError:
        sys.exit('--glow-mask needs Pillow (pip install pillow)')
    import io
    im = Image.open(a.glow_mask).convert('RGB')
    if getattr(a, 'texture_size', 0) and max(im.size) > a.texture_size:   # the same limit as the colour texture, kept crisp
        k = a.texture_size / max(im.size); im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.NEAREST)
    b = io.BytesIO(); im.save(b, 'PNG', optimize=True)
    print('  glow mask %s: %dx%d, %d bytes' % (os.path.basename(a.glow_mask), im.width, im.height, len(b.getvalue())))
    return b.getvalue()


def write_arcm(path, P, N, C, UV, I, skin=None, png=None, double=False, compact=None, material=None, glow=None):
    np = need_np(); n = len(P)
    if n: print_bounds(np.asarray(P, np.float64))
    compact = COMPACT if compact is None else compact
    flags = (1 if skin is not None else 0) | (2 if double else 0) | (4 if n <= 65535 else 0)
    mblock = material_block(material)
    with open(path, 'wb') as o:
        arct = (png or b'')[:4] == b'ARCT' or (material and any((m or b'')[:4] == b'ARCT' for m in material['maps']))
        ver = (4 if arct else 3) if compact else 2      # 4: version 3 with block-compressed (.arct) images (ARC 0.7.1)
        if arct and not compact: sys.exit('--compress needs the compact vertices (not --v2)')
        o.write(b'ARCM' + struct.pack('<6I', ver, n, len(I), flags, len(png) if png else 0, len(mblock)) + struct.pack('<I', 0))
        col = np.clip(np.round(C * 255), 0, 255).astype(np.uint8)
        if compact:
            P = np.asarray(P, np.float64); lo = P.min(0); hi = P.max(0)
            o.write(struct.pack('<6f', *lo, *hi))
            ext = np.where(hi - lo > 0, hi - lo, 1)
            # the loader computes lo + (hi - lo) * u / 65535 in float32: round to the nearest representable step
            q = np.clip(np.round((P - lo) / ext * 65535), 0, 65535).astype('<u2')
            rec = np.zeros(n, dtype=[('p', '<u2', 3), ('n', 'i1', 2), ('c', 'u1', 4), ('uv', '<f2', 2)])
            Nn = np.asarray(N, np.float64); ln = np.linalg.norm(Nn, axis=1, keepdims=True); Nn = np.where(ln > 0, Nn / np.maximum(ln, 1e-12), [0, 1, 0])
            rec['p'] = q; rec['n'] = oct_encode(Nn); rec['c'] = col
            uv = np.asarray(UV, np.float64)
            if np.abs(uv).max(initial=0) > 60000: print('  warning: texture coordinates too large for half floats; use --v2')
            rec['uv'] = uv.astype('<f2')
        else:
            rec = np.zeros(n, dtype=[('p', '<f4', 3), ('n', '<f4', 3), ('c', 'u1', 4), ('uv', '<f4', 2)])
            rec['p'] = P; rec['n'] = N; rec['c'] = col; rec['uv'] = UV
        o.write(rec.tobytes())
        if skin is not None: o.write(skin.astype(np.uint8).tobytes())
        ib = I.astype('<u2' if flags & 4 else '<u4').tobytes(); o.write(ib + b'\0' * ((4 - len(ib) % 4) % 4))
        if png: o.write(png)
        if mblock: o.write(mblock)
        if glow: o.write(b'GLOW' + struct.pack('<I', len(glow)) + glow)   # 0.10.15: the glow mask, after everything (older ARC ignores it)


def skin_weights(J, Wt):
    """top 4 influences, weights out of 255 summing to 255"""
    np = need_np()
    order = np.argsort(-Wt, axis=1)[:, :4]
    J4 = np.take_along_axis(J, order, 1); W4 = np.take_along_axis(Wt, order, 1)
    s = W4.sum(1, keepdims=True); s[s == 0] = 1; W4 = W4 / s
    q = np.floor(W4 * 255).astype(np.int64)
    rest = 255 - q.sum(1)
    for i in np.nonzero(rest)[0]: q[i, int(np.argmax(W4[i] * 255 - q[i]))] += rest[i]
    J4[q == 0] = 0
    return np.concatenate([J4, q], 1).astype(np.uint8)


def convert_gltf(src, dst, a):
    np = need_np()
    g = GLTF(src)
    parts, skin_index, skinned_nodes = gather_gltf(g, a)
    atlas = build_texture(g, parts, a)
    P = np.concatenate([p['P'] for p in parts]); UV = np.concatenate([p['UV'] for p in parts]); C = np.concatenate([p['C'] for p in parts])
    base = 0; Is = []
    for p in parts: Is.append(p['I'] + base); base += len(p['P'])
    I = np.concatenate(Is)
    has_n = all(p['N'] is not None for p in parts)
    N = np.concatenate([p['N'] for p in parts]) if has_n else smooth_normals(P, I)
    if atlas is not None:   # vertices of untextured parts must not pick up the image: point them at a corner pixel? keep colour only
        notex = np.concatenate([np.full(len(p['P']), p.get('notex', False)) for p in parts])
        if notex.any():
            print('  note: untextured parts get their texture colour from the image\'s top-left pixel')
            UV[notex] = 0
    if a.bake_colors or atlas is None and any(p['tex'] is not None for p in parts):
        imgs = {}
        for p in parts:
            if p['tex'] is not None and not a.color:
                if p['tex'] not in imgs: imgs[p['tex']] = g.image(p['tex'])
                p['C'] = bake_vertex_colours(imgs[p['tex']], p['UV'], p['C'])
        C = np.concatenate([p['C'] for p in parts])
    if a.color:
        rgb = [int(a.color[i:i + 2], 16) / 255 for i in (0, 2, 4)]; C = np.ones((len(P), 4)); C[:, :3] = rgb
    double = any(p['double'] for p in parts)
    skin = None; arca = None
    if skin_index is not None:
        J = np.concatenate([p['J'] for p in parts]); Wt = np.concatenate([p['W'] for p in parts])
        skin_rec = g.j['skins'][skin_index]; joints = list(skin_rec['joints'])
        IBM = g.accessor(skin_rec['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1).astype(np.float64) if 'inverseBindMatrices' in skin_rec else np.stack([np.eye(4)] * len(joints))
        # parents before children: reorder joints (and remap the vertex joint indices)
        jset = set(joints); depth = {}
        for jn in joints:
            d, p = 0, g.parent[jn]
            while p >= 0:
                if p in jset: d += 1
                p = g.parent[p]
            depth[jn] = d
        new_order = sorted(range(len(joints)), key=lambda k: (depth[joints[k]], k))
        remap = np.zeros(len(joints), np.int64)
        for newi, oldi in enumerate(new_order): remap[oldi] = newi
        joints = [joints[k] for k in new_order]; IBM = IBM[new_order]
        J = remap[np.clip(J, 0, len(remap) - 1)]
        if len(joints) > 128: sys.exit('%s: %d joints (ARC allows 128)' % (src, len(joints)))
        sw = skin_weights(J, Wt)
        # bind pose in world space: the skin at rest (B = the main joint's rest world * its inverse bind)
        Wrest = [g.world(jn) for jn in joints]
        main = int(np.bincount(sw[:, 0], weights=sw[:, 4].astype(float), minlength=len(joints)).argmax())
        B = Wrest[main] @ IBM[main]
        Pw = (B[:3, :3] @ P.T).T + B[:3, 3]; Nw = (np.linalg.inv(B[:3, :3]).T @ N.T).T
        Mn = placement(Pw, a)
        P = (Mn[:3, :3] @ Pw.T).T + Mn[:3, 3]; N = (np.linalg.inv(Mn[:3, :3]).T @ Nw.T).T
        # ARC skin: joint_world' = root * chain, inverse bind' = IBM * B^-1 * Mn^-1 (exact for any rest pose)
        fix = np.linalg.inv(Mn @ B)
        IBMp = np.stack([ib @ fix for ib in IBM])
        jindex = {jn: k for k, jn in enumerate(joints)}
        parents = []
        roots_above = set()
        for jn in joints:
            p = g.parent[jn]
            while p >= 0 and p not in jindex: p = g.parent[p]
            parents.append(jindex[p] if p >= 0 else -1)
            if p < 0: roots_above.add(g.parent[jn])
        if len(roots_above) > 1:
            ws = [g.world(r) if r >= 0 else np.eye(4) for r in roots_above]
            if any(np.abs(w - ws[0]).max() > 1e-5 for w in ws): sys.exit('%s: the skeleton has roots under different transforms' % src)
        above = next(iter(roots_above)); Wabove = g.world(above) if above >= 0 else np.eye(4)
        # joints whose parent is not a joint but some other node in between: fold that node into their local rest
        root = Mn @ Wabove
        names = [g.nodes[jn].get('name', 'joint%d' % k) for k, jn in enumerate(joints)]
        rest = [node_trs(g.nodes[jn]) for jn in joints]
        for k, jn in enumerate(joints):
            if parents[k] < 0 and g.parent[jn] != above:
                rel = np.linalg.inv(Wabove) @ g.world(g.parent[jn]) @ g.local(jn)
                t = rel[:3, 3]; s = np.linalg.norm(rel[:3, :3], axis=0); rest[k] = (t, mat_quat(rel[:3, :3] / s), s)
        skin = sw
        arca = dict(names=names, parents=parents, ibm=IBMp, rest=rest, root=root, joints=joints, g=g)
    else:
        Mn = placement(P, a)
        P = (Mn[:3, :3] @ P.T).T + Mn[:3, 3]; N = (np.linalg.inv(Mn[:3, :3]).T @ N.T).T
    l = np.linalg.norm(N, axis=1, keepdims=True); l[l == 0] = 1; N = N / l
    if a.flat:
        t = I.reshape(-1, 3); fn = np.cross(P[t[:, 1]] - P[t[:, 0]], P[t[:, 2]] - P[t[:, 0]])
        fl = np.linalg.norm(fn, axis=1, keepdims=True); fl[fl == 0] = 1; fn /= fl
        P, UV, C = P[I], UV[I], C[I]; N = np.repeat(fn, 3, 0)
        if skin is not None: skin = skin[I]
        I = np.arange(len(P))
    png = None; kind = ''
    if atlas is not None and not a.bake_colors and not a.color: png, kind = encode_texture(atlas, a)
    if a.axm: return write_axm(dst, P[I], N[I], C[I])
    mat = build_material(g, parts, atlas, a) if not a.bake_colors and not a.color else None
    write_arcm(dst, P, N, C, UV, I, skin, png, double, material=mat, glow=glow_mask_png(a))
    lo, hi = P.min(0), P.max(0)
    if png or mat: texture_report(png, mat, float((hi - lo).max()), a)
    mdesc = ''
    if mat:
        kinds = [k for k, m in zip(('normal', 'orm', 'emissive'), mat['maps']) if m]
        mdesc = 'material %s%s %d KB  ' % ('pbr ' if mat['pbr'] else '', '+'.join(kinds) or 'factors', sum(len(m) for m in mat['maps'] if m) // 1024)
    print('%-24s %6d verts %6d tris  %s%s%s size %.2f x %.2f x %.2f' % (os.path.basename(dst), len(P), len(I) // 3,
          'texture %dx%d %s %d KB  ' % (atlas.size[0], atlas.size[1], kind, len(png) // 1024) if png else '', mdesc,
          '%d joints  ' % len(arca['names']) if arca else '', *(hi - lo)))
    return arca


# 0.8.2: what each texture costs to load. Rates are ARC 0.8.3's own decoders on one core (measured 8 Nov 2026 on a
# cloud x86 machine; a desktop CPU is usually faster): JPEG ~70, PNG ~38 megapixels a second; .arct blocks go to the
# GPU as they are (the cost is inflating the file: ~90 MB a second).
DECODE_MPS = {'jpeg': 70.0, 'png': 38.0}
def image_info(b):
    if b[:4] == b'ARCT':
        w, h = struct.unpack('<II', b[12:20]); return w, h, 'arct'
    from PIL import Image
    im = Image.open(io.BytesIO(b)); return im.size[0], im.size[1], ('jpeg' if im.format == 'JPEG' else 'png')
def texture_report(png, mat, extent, a):
    items = []
    if png: items.append(('colour', png))
    if mat:
        for name, m in zip(('normal', 'orm', 'emissive'), mat['maps']):
            if m: items.append((name, m))
    total = 0.0; big = 0
    for name, b in items:
        w, h, kind = image_info(b)
        ms = len(b) / 90e6 * 1e3 if kind == 'arct' else w * h / 1e6 / DECODE_MPS[kind] * 1e3
        total += ms; big = max(big, w, h)
        print('    %-8s %4d x %-4d %-4s %5d KB  ~%3.0f ms to decode' % (name, w, h, kind, len(b) // 1024, ms))
    if len(items) > 1: print('    textures ~%.0f ms to decode in all' % total)
    if extent > 0 and big / extent > 768:   # more than 512 px a unit, rounded: worth a smaller --texture-size
        print('  note: %d px textures on a model %.2f units across (%d px a unit); --texture-size %d would load ~%.0fx faster'
              % (big, extent, big / extent, max(64, 1 << int(math.log2(max(1, extent * 512)))), (big / max(64, 1 << int(math.log2(max(1, extent * 512))))) ** 2))

def write_axm(dst, P, N, C):
    lo, hi = P.min(0), P.max(0)
    with open(dst, 'wb') as o:
        o.write(b'AXM2' + struct.pack('<I6f', len(P) // 3, *lo, *hi))
        for p, n, c in zip(P, N, C): o.write(struct.pack('<6f4B', *p, *n, *[int(round(x * 255)) & 255 for x in c[:3]], 255))
    print('wrote %s: %d triangles (AXM2)' % (dst, len(P) // 3))

# ---------------------------------------------------------------- animation
def sample_channel(times, vals, t, path):
    np = need_np()
    if len(times) == 1 or t <= times[0]: return vals[0]
    if t >= times[-1]: return vals[-1]
    k = int(np.searchsorted(times, t)) - 1; u = (t - times[k]) / max(1e-9, times[k + 1] - times[k])
    if path == 'rotation': return slerp(vals[k], vals[k + 1], u)
    return vals[k] * (1 - u) + vals[k + 1] * u


def write_arca(path, rig, files, a):
    np = need_np()
    names, parents, rest = rig['names'], rig['parents'], rig['rest']
    nj = len(names); name_index = {n: k for k, n in enumerate(names)}
    clips = []
    for f in files:
        g = GLTF(f); anims = g.j.get('animations', [])
        if not anims: print('  note: %s has no animations' % f); continue
        stem = os.path.splitext(os.path.basename(f))[0]
        for ai, an in enumerate(anims):
            cname = stem if len(anims) == 1 else '%s:%s' % (stem, an.get('name', str(ai)))
            chans, dur = {}, 0.0
            for ch in an['channels']:
                tn = ch['target'].get('node')
                if tn is None: continue
                nm = g.nodes[tn].get('name')
                if nm not in name_index or ch['target']['path'] not in ('translation', 'rotation', 'scale'): continue
                s = an['samplers'][ch['sampler']]
                t = g.accessor(s['input']).reshape(-1).astype(np.float64); v = g.accessor(s['output']).astype(np.float64)
                if s.get('interpolation') == 'CUBICSPLINE': v = v[1::3]
                chans[(name_index[nm], ch['target']['path'])] = (t, v); dur = max(dur, float(t[-1]))
            frames = max(2, int(round(dur * a.fps)) + 1)
            keys = np.zeros((frames, nj, 10), np.float32)
            for fi in range(frames):
                t = dur * fi / (frames - 1)
                for k in range(nj):
                    tt, rr, ss = rest[k]
                    if (k, 'translation') in chans: tt = sample_channel(*chans[(k, 'translation')], t, 'translation')
                    if (k, 'rotation') in chans: rr = sample_channel(*chans[(k, 'rotation')], t, 'rotation')
                    if (k, 'scale') in chans: ss = sample_channel(*chans[(k, 'scale')], t, 'scale')
                    rr = np.asarray(rr, np.float64); rr = rr / (np.linalg.norm(rr) or 1)
                    keys[fi, k, 0:3] = tt; keys[fi, k, 3:7] = rr; keys[fi, k, 7:10] = ss
            if a.in_place:   # the topmost joint with travel keeps its height but not its sideways motion
                for k in range(nj):
                    if (k, 'translation') in chans:
                        keys[:, k, 0] = keys[0, k, 0]; keys[:, k, 2] = keys[0, k, 2]; break
            for fi in range(1, frames):   # same quaternion hemisphere as the previous frame (smooth lerps)
                d = (keys[fi, :, 3:7] * keys[fi - 1, :, 3:7]).sum(1); keys[fi, d < 0, 3:7] *= -1
            loop = 1 if np.abs(keys[0] - keys[-1]).max() < 1e-3 else 0
            clips.append((cname, frames, loop, keys))
            print('  clip %-16s %3d frames  %.2f s  %d channels%s' % (cname, frames, dur, len(chans), '  (loops)' if loop else ''))
    with open(path, 'wb') as o:
        o.write(b'ARCA' + struct.pack('<III', 1, nj, len(clips)))
        o.write(np.asarray(rig['root'], np.float32).T.reshape(-1).tobytes())
        for k in range(nj):
            t, r, s = rest[k]; r = np.asarray(r, np.float64); r = r / (np.linalg.norm(r) or 1)
            o.write(names[k].encode()[:31].ljust(32, b'\0') + struct.pack('<i', parents[k]))
            o.write(np.asarray(rig['ibm'][k], np.float32).T.reshape(-1).tobytes())
            o.write(struct.pack('<10f', *t, *r, *s))
        for cname, frames, loop, keys in clips:
            o.write(cname.encode()[:31].ljust(32, b'\0') + struct.pack('<fII', float(a.fps), frames, loop))
            o.write(keys.astype('<f4').tobytes())
    print('wrote %s: %d joints, %d clips' % (path, nj, len(clips)))

# ---------------------------------------------------------------- OBJ / trimesh (no textures)
def parse_obj(path):
    verts, cols, faces, fcol = [], [], [], []
    mtl_kd, cur = {}, (200, 200, 200)
    base = os.path.dirname(path)
    for line in open(path, encoding='utf-8', errors='ignore'):
        p = line.split()
        if not p: continue
        if p[0] == 'mtllib':
            mp = os.path.join(base, ' '.join(p[1:]))
            if os.path.exists(mp):
                name = None
                for l2 in open(mp, encoding='utf-8', errors='ignore'):
                    q = l2.split()
                    if not q: continue
                    if q[0] == 'newmtl': name = ' '.join(q[1:])
                    elif q[0] == 'Kd' and name: mtl_kd[name] = tuple(int(float(x) * 255) for x in q[1:4])
        elif p[0] == 'usemtl': cur = mtl_kd.get(' '.join(p[1:]), cur)
        elif p[0] == 'v':
            verts.append(tuple(float(x) for x in p[1:4]))
            cols.append(tuple(int(float(x) * 255) if float(x) <= 1.0 else int(float(x)) for x in p[4:7]) if len(p) >= 7 else None)
        elif p[0] == 'f':
            idx = [int(t.split('/')[0]) for t in p[1:]]
            idx = [i - 1 if i > 0 else len(verts) + i for i in idx]
            for k in range(1, len(idx) - 1):
                faces.append((idx[0], idx[k], idx[k + 1])); fcol.append(cur)
    return verts, cols, faces, fcol


def convert_simple(src, dst, a):
    """OBJ (and other formats through trimesh): vertex colours only, pure Python unless trimesh is needed"""
    if src.lower().endswith('.obj'): verts, cols, faces, fcol = parse_obj(src)
    else:
        try:
            import trimesh, numpy as np
        except ImportError: sys.exit('install trimesh (pip install trimesh) for %s files' % os.path.splitext(src)[1])
        m = trimesh.load(src, force='mesh')
        try: vc = m.visual.to_color().vertex_colors[:, :3]
        except Exception: vc = np.full((len(m.vertices), 3), 200)
        verts = [tuple(v) for v in m.vertices]; cols = [tuple(int(c) for c in v) for v in vc]
        faces = [tuple(int(i) for i in f) for f in m.faces]; fcol = [None] * len(faces)
    if not faces: sys.exit('%s: no faces found' % src)
    if a.yaw:
        r = math.radians(a.yaw); c, s = math.cos(r), math.sin(r)
        verts = [(c * v[0] + s * v[2], v[1], -s * v[0] + c * v[2]) for v in verts]
    lo = [min(v[i] for v in verts) for i in range(3)]; hi = [max(v[i] for v in verts) for i in range(3)]
    fp = fit_params(lo, hi, a)
    if fp:
        (sx, sy, sz), (ox, oy, oz) = fp
        verts = [(v[0] * sx + ox, v[1] * sy + oy, v[2] * sz + oz) for v in verts]
    print_bounds(verts)
    override = tuple(int(a.color[i:i + 2], 16) for i in (0, 2, 4)) if a.color else None
    def colour(vi, fi):
        if override: return override
        if cols[vi] is not None: return cols[vi]
        return fcol[fi] or (200, 200, 200)
    def sub(p, q): return (p[0] - q[0], p[1] - q[1], p[2] - q[2])
    def cross(p, q): return (p[1] * q[2] - p[2] * q[1], p[2] * q[0] - p[0] * q[2], p[0] * q[1] - p[1] * q[0])
    def norm(p):
        l = math.sqrt(p[0] ** 2 + p[1] ** 2 + p[2] ** 2) or 1.0
        return (p[0] / l, p[1] / l, p[2] / l)
    out_v, out_i = [], []
    if a.smooth and not a.flat:
        acc = [[0.0, 0.0, 0.0] for _ in verts]
        for f in faces:
            n = cross(sub(verts[f[1]], verts[f[0]]), sub(verts[f[2]], verts[f[0]]))
            for i in f:
                for k in range(3): acc[i][k] += n[k]
        vcol = {}
        for fi, f in enumerate(faces):
            for i in f: vcol.setdefault(i, colour(i, fi))
        for i, v in enumerate(verts): out_v.append((v, norm(acc[i]), vcol.get(i, (200, 200, 200))))
        out_i = [i for f in faces for i in f]
    else:
        for fi, f in enumerate(faces):
            n = norm(cross(sub(verts[f[1]], verts[f[0]]), sub(verts[f[2]], verts[f[0]])))
            for i in f: out_i.append(len(out_v)); out_v.append((verts[i], n, colour(i, fi)))
    if a.axm:
        tris = [out_v[i] for i in out_i]
        lo = [min(v[0][k] for v in tris) for k in range(3)]; hi = [max(v[0][k] for v in tris) for k in range(3)]
        with open(dst, 'wb') as o:
            o.write(b'AXM2' + struct.pack('<I6f', len(tris) // 3, *lo, *hi))
            for p, n, c in tris: o.write(struct.pack('<6f4B', *p, *n, *(int(x) & 255 for x in c), 255))
        print('wrote %s: %d triangles (AXM2)' % (dst, len(tris) // 3)); return
    n = len(out_v); flags = 4 if n <= 65535 else 0
    mblock = b''   # no texture coordinates here, so only a factors material (metal / roughness)
    if not a.no_material and (a.metallic is not None or a.roughness is not None):
        mblock = material_block(dict(pbr=not a.no_pbr, nscale=1.0, metallic=a.metallic if a.metallic is not None else 0.0,
                                     roughness=a.roughness if a.roughness is not None else 1.0, occlusion=0.0, emissive=[0, 0, 0], maps=[None, None, None]))
    with open(dst, 'wb') as o:
        o.write(b'ARCM' + struct.pack('<6I', 2, n, len(out_i), flags, 0, len(mblock)) + struct.pack('<I', 0))
        for p, nn, c in out_v: o.write(struct.pack('<6f4B2f', *p, *nn, *(int(x) & 255 for x in c), 255, 0.0, 0.0))
        ib = struct.pack('<%d%s' % (len(out_i), 'H' if flags & 4 else 'I'), *out_i)
        o.write(ib + b'\0' * ((4 - len(ib) % 4) % 4))
        o.write(mblock)
        glow = glow_mask_png(a)   # 0.10.15
        if glow:
            print('  warning: this format carries no texture coordinates here, so the glow mask lights the whole model as one group (use glTF)')
            o.write(b'GLOW' + struct.pack('<I', len(glow)) + glow)
    print('wrote %s: %d vertices, %d triangles%s' % (dst, n, len(out_i) // 3, '  material (factors)' if mblock else ''))


def convert(src, dst, a):
    if src.lower().endswith(('.glb', '.gltf')): return convert_gltf(src, dst, a)
    convert_simple(src, dst, a); return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src', nargs='?'); ap.add_argument('dst', nargs='?')
    ap.add_argument('--batch', nargs=2, metavar=('SRC_DIR', 'DST_DIR'))
    ap.add_argument('--yaw', type=float, default=0); ap.add_argument('--footprint', type=float, default=0)
    ap.add_argument('--height', type=float, default=0); ap.add_argument('--size', type=float, default=0)
    ap.add_argument('--fit', type=float, nargs=3, metavar=('X', 'Y', 'Z')); ap.add_argument('--fit-uniform', type=float, nargs=3, metavar=('X', 'Y', 'Z'))
    ap.add_argument('--origin', choices=['bottom', 'centre', 'center', 'back'])
    ap.add_argument('--flat', action='store_true'); ap.add_argument('--smooth', action='store_true')
    ap.add_argument('--texture-size', type=int, default=1024); ap.add_argument('--bake-colors', action='store_true')
    ap.add_argument('--png', action='store_true'); ap.add_argument('--jpeg-quality', type=int, default=85)
    ap.add_argument('--color', default=None)
    ap.add_argument('--ignore-color-factor', action='store_true', help='textured materials: ignore any baseColorFactor RGB')
    ap.add_argument('--keep-color-factor', action='store_true', help='apply baseColorFactor as the glTF standard says (no grey-factor fix)')
    ap.add_argument('--anim', nargs='+', metavar='OUT.arca FILE', default=None)
    ap.add_argument('--fps', type=float, default=30); ap.add_argument('--in-place', action='store_true')
    ap.add_argument('--axm', action='store_true')
    ap.add_argument('--v2', action='store_true', help='write .arcm version 2 (36-byte vertices) for ARC 0.4')
    ap.add_argument('--no-material', action='store_true', help='leave out normal/metal-roughness/emissive maps (the pre-0.6 look)')
    ap.add_argument('--no-pbr', action='store_true', help='keep maps but light the mesh the plain way (no metal highlights/reflections)')
    ap.add_argument('--metallic', type=float, default=None); ap.add_argument('--roughness', type=float, default=None)
    ap.add_argument('--normal-map', default=None, help='a tangent-space normal map image (OpenGL convention: green up)')
    ap.add_argument('--normal-from-height', default=None, help='make the normal map from this image\'s brightness (light = raised)')
    ap.add_argument('--normal-strength', type=float, default=2.0, help='bump strength for --normal-from-height (2)')
    ap.add_argument('--normal-png', action='store_true', help='store the normal map as PNG (lossless) instead of JPEG')
    ap.add_argument('--map-size', type=int, default=0, help='largest side of the material maps (0 = as the colour texture)')
    ap.add_argument('--orm-map', default=None, help='a packed map: red occlusion, green roughness, blue metallic')
    ap.add_argument('--glow-mask', default=None, help='0.10.15: an image whose red, green and blue mark glowing parts (arc_mesh_style.emissive_mask); stored in the .arcm')
    ap.add_argument('--emissive-map', default=None); ap.add_argument('--emissive-strength', type=float, default=1.0)
    ap.add_argument('--compress', action='store_true', help='block-compressed textures with mips (.arcm version 4, ARC 0.7.1)')
    ap.add_argument('--color-format', default='auto', choices=['auto', 'bc1', 'bc3', 'bc7'])
    ap.add_argument('--normal-format', default='bc5', choices=['bc5', 'bc3', 'bc1'])
    ap.add_argument('--map-format', default='bc1', choices=['bc1', 'bc7'])
    a = ap.parse_args()
    if a.origin == 'center': a.origin = 'centre'
    global COMPACT
    if a.v2: COMPACT = False
    if a.batch:
        sd, dd = a.batch; os.makedirs(dd, exist_ok=True)
        for f in sorted(os.listdir(sd)):
            if f.lower().endswith(('.glb', '.gltf', '.obj')): convert(os.path.join(sd, f), os.path.join(dd, os.path.splitext(f)[0] + ('.axm' if a.axm else '.arcm')), a)
        return
    if not a.src or not a.dst: ap.error('give SRC and DST (or --batch SRC_DIR DST_DIR)')
    rig = convert(a.src, a.dst, a)
    if a.anim:
        if rig is None: sys.exit('--anim needs a skinned glTF model')
        out, files = a.anim[0], a.anim[1:] or [a.src]
        write_arca(out, rig, files, a)


if __name__ == '__main__':
    main()
