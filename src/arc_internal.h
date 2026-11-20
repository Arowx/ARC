/* ARC internal interfaces: shared between core, graphics front end, backends and platforms. */
#ifndef ARC_INTERNAL_H
#define ARC_INTERNAL_H
#include "../include/arc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define ARC__MAX_MESHES    4096
#define ARC__MAX_TEXTURES  1024
#define ARC__TPOST_MAX     8      /* 0.8.2: render targets that can have their own post effects at once */
#define ARC__PSET_MAX      (ARC__TPOST_MAX + ARC_MAX_VIEWPORTS)   /* 0.10.2 backends' post buffer sets: those targets + viewports */
#define ARC__MAX_INSTANCES (1 << 18)   /* 262,144 mesh draws per frame */
#define ARC__MAX_LINES     (1 << 17)
#define ARC__MAX_LTRIS     (1 << 19)   /* 0.10.11 thick-line triangle vertices per frame (allocated at the first thick line) */
#define ARC__MAX_VERTS2D   (1 << 20)   /* 2D triangle vertices per frame */
#define ARC__MAX_PASSES    256
#define ARC__LT_TX 16              /* 0.9.5 tiled lights: tiles across */
#define ARC__LT_TY 9               /* and down */
#define ARC__LT_TILES (ARC__LT_TX * ARC__LT_TY)
#define ARC__LT_CAP 64             /* lights a tile can list */
#define ARC__LT_FLOATS 16          /* floats a light takes */
#define ARC__MAX_FRAME_LIGHTS 256
#define ARC__MAX_SHADOW_LIGHTS 4   /* 0.9.5 lights casting shadows per frame: an atlas of 6 x 4 faces (a point light uses a row, a spot one face) */
#define ARC__MAX_BATCHES   65536         /* 2D batches per frame (a new one at each texture / blend / clip change) */
#define ARC__MAX_IMM       (1 << 19)   /* immediate-geometry vertices per frame */
#define ARC__MAX_BONES     (1 << 16)   /* skinning matrices per frame (3 x vec4 each) */
#define ARC__BONES_PER_ROW 512         /* GPU bone texture: 1536 x 128 RGBA32F texels */

/* ---- frame description handed to a render backend ---------------------- */

typedef struct {            /* 160 bytes, matches the GPU instance layout */
    float model[16];
    float tint[4];          /* r,g,b, emissive */
    float params[4];        /* recolour mix, whiten, multiply (1 = tint multiplies), alpha (1 = opaque) */
    float extra[4];         /* first bone in the frame's bone list, skinned (1/0), unlit, additive fog. 0.9.9 packs more into the
                               last two (whole numbers, exact in floats): extra[2] = unlit (0/1) + 2 darken (0..255) + 512 char
                               (0..255) + 131072 no world mask (0/1); extra[3] = additive fog (0/1) + 2 texture layer (0..63) */
    float fx[8];            /* 0.6 effects: dissolve, edge width, hologram, outline push (> 0: this is an outline hull);
                               uv scroll offset u, v, edge rgb (r + g*256 + b*65536), edge glow */
    float uvx[4];           /* 0.8.4 UV transform's 2x2 part minus the identity, all zero = none:
                               uv' = ((1 + uvx[0]) u + uvx[1] v, uvx[2] u + (1 + uvx[3]) v) + (fx[4], fx[5]);
                               its offset is folded into fx[4..5] with the scroll */
} arc__instance;
/* dissolve noise (all renderers): h(x,y,z) = x*73856093 ^ y*19349663 ^ z*83492791 (uint32), h ^= h >> 13,
   h *= 0x5bd1e995, h ^= h >> 15; value = (h & 0xFFFFFF) / 16777215 at integer corners, smoothstep-trilinear;
   n(p) = 0.65 v(5 p) + 0.35 v(11 p + 17), p = the model's own (skinned) position. Gone where n < dissolve; edge
   glow (rgb * glow * (1 - (n - dissolve) / width)) where n < dissolve + width.
   hologram: fr = (1 - |N.V|)^2, line = 0.5 + 0.5 sin(60 y - 6 t); rgb = tint (0.6 + 0.8 fr)(0.7 + 0.3 line),
   alpha = hologram * clamp(0.25 + 0.75 fr + 0.3 line), additive. outline: the hull pushed out along normalize(N + normalize(P - model origin)) (world),
   front faces culled, flat colour. */

enum { ARC__MB_OPAQUE = 0, ARC__MB_ALPHA = 1, ARC__MB_ADD = 2 };
/* 0.6.3: each batch carries its depth state: the compare (ARC__Z_LESS / ALWAYS / GREATER) plus ARC__Z_NOWRITE.
   Backends set blend (blend), depth and the opaque flag (alpha test, alpha out 1: blend == ARC__MB_OPAQUE) per batch.
   Shadow maps take only batches with blend 0 and depth 0. */
enum { ARC__Z_LESS = 0, ARC__Z_ALWAYS = 1, ARC__Z_GREATER = 2, ARC__Z_FUNC = 3, ARC__Z_NOWRITE = 4 };
typedef struct { uint32_t mesh; int first, count; int flip; int blend; int depth;
                 uint32_t paint_tex, tint2, tint3; int pat; } arc__mesh_batch;   /* 0.10.1 pat: index + 1 in arc__frame_data.pats (0 = none) */
/* 0.10.1 a paint pattern as the shaders take it (see arc_paint_pattern): p[0] kind, 1/scale, width, offset; p[1] cos, sin
   of the angle, cross, armour; p[2] colour, w unused; p[3] noise offset xyz, w: a static mesh is in the part mask.
   jm[j]: joint j's part is in the mask (1) or not (0). */
#define ARC__MAX_PATTERNS 64
typedef struct { float p[4][4]; float jm[128]; } arc__pattern;   /* flip: mirrored instances (wind CW); 0.8.8 paint: arc_mesh_style.paint_mask, tint2, (0.8.9) tint3 (0 = none / blue off) */
typedef struct { float x, y, z; uint32_t color; float gain; } arc__line_vertex;   /* 20 bytes; gain (0.10.11): colour scale, above 1 blooms */
typedef struct { float x, y, u, v; uint32_t color; } arc__vert2d;              /* 20 bytes, color as R,G,B,A bytes */
enum { ARC__BLEND_ALPHA = 0, ARC__BLEND_ADD, ARC__BLEND_MULTIPLY, ARC__BLEND_NONE };
typedef struct {
    uint32_t texture; int first, count;      /* vertices, 3 per triangle */
    int blend; int clip; int cx, cy, cw, ch; /* clip rect in target pixels, top-left origin */
    int plain;                               /* only plain (untextured) triangles so far: it can take any texture */
    float sdf;                               /* 0.10.1: > 0: the texture's alpha is a distance field; screen-pixel distance = (a - 0.5) * sdf */
} arc__batch2d;
/* 0.6.5: untextured 2D triangles (fills, lines, untextured arc_draw_triangles2d) join whatever batch is open; their
   vertices carry u = ARC__UV_PLAIN and the 2D shaders use white instead of the texture there (u < -2048) */
#define ARC__UV_PLAIN (-4096.0f)
/* One render pass: everything drawn into one target between arc_target_begin/end (target 0 = the screen).
   Order inside a pass: meshes, immediate triangles, lines, then 2D. */
enum { ARC__POST_ON, ARC__POST_BLOOM, ARC__POST_THRESHOLD, ARC__POST_RADIUS, ARC__POST_EXPOSURE, ARC__POST_TONEMAP,
       ARC__POST_DESAT, ARC__POST_CONTRAST, ARC__POST_VIGNETTE, ARC__POST_TILT, ARC__POST_TILT_FOCUS, ARC__POST_TILT_WIDTH,
       ARC__POST_SSAO, ARC__POST_SSAO_RADIUS, ARC__POST_FXAA, ARC__POST_LUT, ARC__POST_LUT_N, ARC__POST_LUT_STRENGTH,
       ARC__POST_SCANLINES, ARC__POST_CRT, ARC__POST_PIXELATE, ARC__POST_ABERRATION, ARC__POST_COUNT };
typedef struct {
    uint32_t target; int width, height;
    bool clear; float clear_col[4];
    arc_m4 view_proj; arc_v3 eye;
    float light_dir[4], light_col[4], ambient[4], fog_col[4], fog[4], rim[4];   /* fog: start, 1/len, on, exp density; rim: rgb, power */
    /* shadows (directional light): light view-projection, { strength (0 = off), depth bias, normal offset (world), 1 / map size } */
    float shadow_vp[16]; float shadow[4]; int shadow_res;
    /* 0.6: cascades (shadow_n 1 = the classic box above, exactly as before) and softness; a 2x2 atlas of
       shadow_res squares when shadow_n > 1. Per cascade: view-projection, { depth bias, normal offset, atlas x, atlas y } */
    int shadow_n; float shadow_soft; float shadow_cvp[3 * 16]; float shadow_cp[3 * 4];
    /* post-processing: ARC__POST_* (screen passes; 0.8.2: also target passes of a target given arc_target_set_post) */
    float post[ARC__POST_COUNT];
    float ivp[16];   /* screen passes: the inverse view-projection (ssao) */
    /* sky gradient (screen passes that clear): top rgb + on, horizon rgb, below rgb; the inverse view-projection it uses */
    float sky[12]; float sky_ivp[16];
    /* environment for pbr reflections: top rgb + intensity, horizon rgb, below rgb */
    float env[12];
    /* 0.9.5 point and spot lights, tiled: the frame's lights (arc__frame_data.lights, ARC__LT_FLOATS each: position +
       1/range^2, rgb*intensity + cos(inner), spot dir + cos(outer), then { shadow slot or -1, 0, 0, 0 }; points: dir 0,
       cos -0.5 / -1, so the cone term is 1) of which this pass sees the first nlights. The screen is split into
       ARC__LT_TX x ARC__LT_TY tiles; a world point p is in tile (tx, ty): c = view_proj * p,
       tx = clamp(floor((c.x / c.w * 0.5 + 0.5) * ARC__LT_TX), 0, ARC__LT_TX - 1), ty the same with c.y (up).
       arc__frame_data.light_tiles[lt_tiles + ty * ARC__LT_TX + tx] = (first << 8) | count: that tile's lights are
       light_index[lt_idx + first .. + count - 1], in rising order (at most ARC__LT_CAP). Every light whose range can
       reach a point in the tile is listed, so summing them in that order gives exactly what summing all the pass's
       lights would (the others add nothing). lt_tiles -1: no lights. */
    int nlights; int lt_tiles, lt_idx, lt_idx_count;
    /* 0.9.5 light shadows: how many of the frame's shadow slots this pass renders and uses (the first lsh_n; 0 =
       none, e.g. mirror passes). A shadowed light's last 4 floats: slot, near, tan of the face's half-angle (a point
       light's 6 faces are 90 degrees plus a margin), 1 / face size. Its face f of slot s sits at (f, s) in the atlas
       (GL texture coordinates: rows from the bottom), seen through frame lsh_vp[(s * 6 + f) * 16]. Points use faces
       +x -x +y -y +z -z, forward F with up (0,1,0) for x and z, (0,0,1) for y; spots one face along dir with up
       (0,1,0), or (1,0,0) when dir is nearly vertical; right R = normalize(cross(F, up)), up' = cross(R, F). */
    int lsh_n;
    float time;   /* seconds (wrapped at 1000): hologram lines */
    /* mesh batches: mb_count drawn before immediate triangles and lines (background layer, solid meshes), then
       tb_count after them (see-through/additive sorted back to front, then the overlay layer) */
    int mb_first, mb_count, tb_count, inst_first, inst_count, imm_first, imm_count, line_first, line_count, b2_first, b2_count;
    /* 0.10.11 thick lines: ltri_count triangle vertices (arc__line_vertex, colour alpha = edge coverage) drawn right after
       the pass's lines: alpha blended, depth tested, no depth write, both faces, colour * gain */
    int ltri_first, ltri_count;
    /* 0.7 soft particles: an instance in the see-through batches has fx[1] < 0 with fx[0] <= 0 (fade distance -fx[1]).
       Then, before those batches, the backend makes the depth drawn so far readable and fades each such pixel's alpha
       by clamp(|S - P| / soft, 0, 1): P = the pixel's world point, S = the depth buffer's point at that pixel through
       ivp (ndc x, y from the pixel centre, z = depth * 2 - 1). See soft_fade in arc_soft.c. */
    int soft;
    /* 0.7.2 reversed-Z (arc_app.depth_mode): zrev 1 = view_proj maps depth to a 0..1 clip range, near 1, far 0 (0 at
       infinity with ARC_DEPTH_REVERSED_INFINITE): clip z / w is the depth buffer value itself (no (z + 1) / 2), so
       GPU backends use a 0..1 clip range (D3D as it is, GL glClipControl ZERO_TO_ONE; without clip control the vertex
       shader writes z * 2 - w), clear depth to 0 and swap LESS and GREATER in every batch's compare (ARC__Z_*).
       ivp then includes the 0..1 step: ivp * (x, y, depth * 2 - 1, 1) still gives the world point, as above. The
       software renderer's depth buffer holds clip z / w (ndc) in both modes: -1..1 standard, 1..0 reversed.
       Shadow maps stay standard (their own matrices, LESS, cleared to 1). view_proj_std: the standard matrix
       (the sky's rays). zrev 0: everything exactly as before. */
    int zrev; arc_m4 view_proj_std;
    /* 0.8.7 planar reflections. mirror 1: a mirror pass (its view_proj includes the reflection, so front faces wind
       the other way: backends swap their front-face winding for the whole pass) and pixels whose world point p has
       dot(clip.xyz, p) + clip.w < 0 are discarded. refl_tex: the mirror pass's target this pass's reflective meshes
       sample at their own pixel (uv = pixel centre / pass size, the target's rows stored like the screen's);
       refl = { strength, deepest mip level, 1 / width, 1 / height }. Per mesh: arc__mesh_rec.reflect. Shading (all
       backends, see shade in arc_soft.c): w = strength * reflect * vertex alpha * (0.6 + 0.4 (1 - N.V)^5);
       lod = min(6 * roughness, deepest) (roughness: pbr materials' metallic-roughness value, else 0; trilinear);
       uv += 0.03 (n.x, n.z) (n.x, -n.z on GL, whose rows run bottom-up); colour = mix(colour, refl, w) before the
       flash and the fog. */
    int mirror; float clip[4]; uint32_t refl_tex; float refl[4];
    /* 0.9.9 world mask (fog of war, arc_set_world_mask): wmask = the texture (0 = off), wm = { origin x, origin z,
       1 / cell, softness, dark r, g, b, on }. Shading (all backends, after lighting, before the fog; not for instances
       with the no-mask flag): g = (p.xz - origin) / cell - 0.5, four texels' red around floor(g) (clamped to the
       texture), weights f = fract(g) shaped by softness s: s < 0.01: step(0.5, f); else smoothstep(0.5 - s/2,
       0.5 + s/2, f); m = bilinear; colour = mix(dark, colour, m).
       Cutaway (arc_set_cutaway): cut = { eye x, y, z, radius, focus x, y, z, keep height, mode, band, cap grey, 0 }.
       For meshes with arc__mesh_rec.cutaway while mode > 0: t = the plan-view (x, z) projection of p onto the
       segment eye -> focus (clamped 0..1), d = plan distance to that point; where d < radius and p.y > keep: mode 1
       discards; mode 2 discards where bayer4(pixel) < clamp((p.y - keep) / band, 0, 1) * clamp((radius - d) /
       (0.25 radius), 0, 1). Those meshes draw both faces; back faces are the cap: grey (unlit), then mask and fog. */
    uint32_t wmask; float wm[8]; float cut[12];
    /* 0.10.2 split-screen: vport = viewport index + 1 when this pass draws into a viewport's own target
       (arc_viewport_begin), else 0. Such a pass is treated exactly like a screen pass of that target's size: a pass
       that clears draws the sky (screen rules), the game's post effects run on it (the target post path), and
       cascades follow its camera. The viewport is copied onto the screen afterwards by a 2D quad (no blend). */
    int vport;
} arc__pass;

/* post[]: on, bloom strength, bloom threshold, bloom radius, exposure, tonemap, desaturate, contrast, vignette
   Composite (all renderers): c = scene * exposure + bloom * strength; tonemap (ACES fit); desaturate; contrast;
   vignette; clamp. With post on, the screen's 3D renders into an HDR buffer, then post, then the screen's 2D. */
/* Tilt-shift: a second blur chain like bloom's (4 levels, no bright pass, radius 1); in the composite, before
   exposure: scene = mix(scene, blur / ARC__TILT_LEVELS (the chain sums its levels), strength * smoothstep(clamp((|y - focus| - width) / 0.2, 0, 1))), y from the top. */
/* 0.6 post, in the composite (all renderers; see soft_post for the reference):
   q = screen uv (top-down); crt: q = 0.5 + d (1 + 0.25 crt |2d|^2), d = q - 0.5, black outside 0..1; pixelate: q snaps
   to the centre of its block; scene = fxaa(q) (see soft_fxaa) or scene(q); aberration: red from q + o, blue from q - o, o = 0.02 a (q - 0.5);
   tilt; ssao: scene *= mix(1, ao, ssao), ao = 3x3 bilinear taps (offsets -1.5, 0, 1.5 texels) of the AO buffer
   (r = ao * geometry, g = geometry; sky keeps 1); bloom, exposure, tonemap, desaturate, contrast; lut; vignette;
   scanlines: c *= 1 - s (0.5 - 0.5 cos(2 pi (q.y h - 0.5) / period)), q before pixelate, period = max(2, round(h / 270)).
   The AO buffer: see soft_ssao. */
#define ARC__BLOOM_LEVELS 5
#define ARC__TILT_LEVELS 4
typedef struct {
    int width, height;                       /* the screen */
    const arc__pass *passes; int pass_count;
    const arc__instance *instances; int instance_count;
    const arc__mesh_batch *mesh_batches; int mesh_batch_count;
    const arc_vertex *imm; int imm_count;                   /* immediate triangles, double-sided */
    const arc__line_vertex *lines; int line_vertex_count;
    const arc__line_vertex *ltris; int ltri_vertex_count;   /* 0.10.11 (see arc__pass.ltri_first) */
    const arc__vert2d *verts2d; int vert2d_count;
    const arc__batch2d *batches2d; int batch2d_count;
    const float *bones; int bone_count;      /* skinning matrices: 3 rows of 4 floats each (row-major 3x4) */
    const float *lights; int light_count;    /* 0.9.5 the frame's point/spot lights, ARC__LT_FLOATS each (see arc__pass) */
    const uint32_t *light_tiles; const uint8_t *light_index;   /* per-pass tile tables and light lists (see arc__pass) */
    const float *lsh_vp; const int *lsh_faces; int lsh_count, lsh_res;   /* 0.9.5 light shadow faces (see arc__pass.lsh_n) */
    const arc__pattern *pats; int pat_count; /* 0.10.1 paint patterns (arc__mesh_batch.pat) */
    bool targets_only;                       /* run passes into render targets, leave the screen alone */
    bool screen_only;                        /* the reverse: targets already hold this frame's drawing */
} arc__frame_data;

arc_v3 arc__view_eye(void);   /* 0.9.6 the camera's eye as last set (scatter culling) */
/* 0.9.7 for culling: the direction toward the sun, whether sun shadows are on, whether a reflection plane is (its
   mirrored view sees what the camera doesn't) */
void arc__cull_info(arc_v3 *to_sun, bool *sun_shadows, bool *mirror);
int arc__anim_parent(arc_anim a, int joint);   /* 0.9.8: -1 for a root (or a bad joint) */
void arc__markers_flush(void);                 /* 0.9.9 ground markers: queue the pass's batch */
void arc__markers_json(arc_json *j);
int arc__gfx_dropped_total(void);   /* draws dropped at per-frame limits since the start */
/* 0.10.2 viewports: the one being drawn (false outside): its window rect */
bool arc__viewport_cur(int *x, int *y, int *w, int *h);
void arc__window_size(int *w, int *h);
void arc__viewport_close(void);
void arc__review_state(bool on);   /* 0.10.6 sheets: ARC's own clean state while on, then the game's back */
void arc__view_effects_off(bool off);   /* 0.10.3 (nesting) the world mask and cutaway off, then back */   /* a viewport left open after draw (or tick) */   /* the whole window, even inside a viewport */
const char *arc__save_dir(void);   /* where arc_save writes (created on first use) */
typedef struct {
    const char *name;
    bool (*init)(void);
    void (*shutdown)(void);
    /* skin: NULL, or 8 bytes per vertex (4 joint indices, 4 weights out of 255) */
    bool (*mesh_create)(uint32_t id, const arc_vertex *v, int vcount, const uint32_t *idx, int icount, const uint8_t *skin);
    void (*mesh_update)(uint32_t id, const arc_vertex *v, int vcount);
    void (*mesh_destroy)(uint32_t id);
    bool (*tex_create)(uint32_t id, int w, int h, const uint8_t *rgba, int flags);   /* ARC_TEX_* */
    bool (*target_create)(uint32_t id, int w, int h, bool smooth);   /* a texture that can be drawn into */
    void (*tex_destroy)(uint32_t id);
    void (*tex_set_smooth)(uint32_t id, bool smooth);
    void (*tex_update)(uint32_t id, int w, int h, const uint8_t *rgba);
    void (*render)(const arc__frame_data *f);
    /* copy the last rendered frame, top row first, RGBA8 */
    bool (*read_pixels)(uint8_t *rgba, int w, int h);
    /* 0.7.1 block-compressed textures (optional; NULL = ARC decodes them to RGBA8 on the CPU): the ARC_TEXFMT_* the GPU
       takes (bit 1 << fmt), and an upload of `levels` mip levels as they are (level i: data[i], bytes[i]; the first
       level's sides are multiples of 4) */
    int  (*compressed_formats)(void);
    bool (*tex_create_compressed)(uint32_t id, int fmt, int w, int h, int levels, const uint8_t *const *data, const size_t *bytes, int flags);
    /* 0.8.6 (optional, WebGL2): a texture straight from a browser-decoded image (arc__imgdec job), mips made on the GPU */
    bool (*tex_create_web)(uint32_t id, int job, int w, int h, int flags);
    /* 0.9.6 (optional; NULL = the software renderer, which samples the layers' CPU copies) a texture array: n layers
       of w x h RGBA8, `levels` mip levels each; level k of layer i is mips[i * levels + k] (sizes mw[k] x mh[k]) */
    bool (*tex_create_array)(uint32_t id, int w, int h, int n, int levels, const uint8_t *const *mips, const int *mw, const int *mh);
} arc__backend;

/* ---- CPU-side resource records (kept for the software renderer, bounds, stats) ---- */
typedef struct {
    bool used, double_sided, owns_texture, no_shadow;
    arc_vertex *v; uint32_t *idx; int vcount, icount;
    uint8_t *skin;                         /* NULL or 8 bytes per vertex */
    int joints;                            /* skinned: highest joint index used + 1 */
    uint32_t texture;                      /* 0 = none (white) */
    arc_v3 lo, hi;
    /* material (0.6): extra maps (normal, orm, emissive; 0 = none) and ARC__MAT_* factors; has_mat false = plain */
    bool has_mat; uint8_t mat_owns;        /* bit i: the mesh owns mat_tex[i] */
    uint32_t mat_tex[3];
    float mat[12];
    uint32_t detail_tex, mask_tex; float mat2[16];   /* 0.8.4: detail map and its breakup mask (never owned), ARC__MAT2_* (all zero = none) */
    uint32_t layers_tex; float mat3[8];   /* 0.9.6 splat: a texture array (never owned) and ARC__MAT3_* (layers_tex 0 = off) */
    uint8_t layers_mode;                  /* 0.9.9: 0 splat; 1 the colour from layers_tex at each instance's layer (arc_mesh_set_texture_array) */
    /* 0.7.1 background loading: pending = the loader's serial while the mesh is queued (vcount 0: draws nothing);
       pend_set: settings the game made meanwhile (1 texture, 2 material, 4 double-sided, 8 shadow), kept at the end */
    uint32_t pending; uint8_t pend_set;   /* (0.8.7: 16 reflection settings) */
    float reflect; bool unreflected;       /* 0.8.7 planar reflections: amount 0..1; left out of the mirror pass */
    bool cutaway;                          /* 0.9.9 cut by arc_set_cutaway */
} arc__mesh_rec;
/* mat[]: has normal map, normal scale, pbr, occlusion | metallic, roughness, has orm, has emissive map | emissive rgb, 0.
   Shading (all renderers, see arc_soft.c shade()): with a normal map the normal is bent by it in a frame (T, B, N) with
   T = dP/du, B = -dP/dv (green points up the texture) worked out from the triangle, made orthonormal to N.
   pbr: F0 = mix(0.04, base, metallic); diffuse base * (1 - metallic) * (ambient * ao + light * NdotL * shadow);
   GGX D, Smith-Schlick G (k = (r+1)^2/8), Schlick F, times pi (ARC's lights carry no 1/pi); environment = sky
   gradient at the reflected ray, blurred toward its average by roughness, times Schlick-roughness Fresnel * ao. */
enum { ARC__MAT_HAS_N, ARC__MAT_NSCALE, ARC__MAT_PBR, ARC__MAT_OCC, ARC__MAT_METAL, ARC__MAT_ROUGH, ARC__MAT_HAS_O, ARC__MAT_HAS_E,
       ARC__MAT_ER, ARC__MAT_EG, ARC__MAT_EB };
/* 0.8.4 mat2[]: detail mode (0 none, 1 repeat, 2 hex), detail scale (0 = mesh UVs), strength, rotate | contrast,
   base mode (0 plain, 2 hex), base rotate, base contrast. Shading (all renderers, see arc_soft.c):
   - detail uv: scale > 0: world position on the plane the triangle faces most (|N.y| largest: (x, z); |N.x|: (z, y);
     else (x, y)) times scale, the triangle's face normal deciding; else the (transformed) mesh uv.
     colour *= mix(1, 2 d.rgb, strength k), on the base colour before tint and light, with k = d.a, times
     clamp((mask.r - threshold) / softness, 0, 1) when there is a mask (MON). The mask is sampled like the detail
     (same plane or mesh uv, same tiling mode and rotate) at its own scale MSCALE.
   - hex (Mikkelsen 2022 hex2colTex): st' = st * 2 sqrt 3, skewed by (1, -1/sqrt3; 0, 2/sqrt3), base = floor, f = fract,
     z = 1 - f.x - f.y, s = z < 0; weights w = (-z s2, s - f.y s2, s - f.x s2) with s2 = 2s - 1, cells v1 = base + (s, s),
     v2 = base + (s, 1 - s), v3 = base + (1 - s, s); cell centre = (v.x + v.y / 2, v.y sqrt3 / 2) / (2 sqrt 3).
     Each cell: h = arc hash (as dissolve, z = 0) of (v.x, v.y): offset = ((h & 0xFFFF), (h >> 16)) / 65536;
     h2 = hash(v.x + 31, v.y + 17): angle = ((h2 & 0xFFFF) / 65536 * 2 - 1) pi rotate; uv_i = R (st - c) + c + offset,
     sampled with the untouched st's mip level (gradients turned with R on the GPU).
     Weights: W_i = mix(1, lum(c_i), 0.6) w_i^7, normalised, then the gain of contrast r (k = log(1 - r) / log 0.5)
     unless r == 0.5, normalised again; result = sum W_i c_i. */
/* 0.9.6 splat (all renderers, see arc_soft.c splat()): the mesh's texture, sampled at the mesh uv, gives the weights
   w (rgba, the first N used); sum < 1e-4: layer 0 alone. Layer i is sampled from the array at world (x, z) x SCALE_i,
   repeating, trilinear. SHARP 0: b_i = w_i; else (height blend, layer alpha = height h_i) v_i = w_i + h_i SHARP for
   w_i > 0, d = 1 - 0.95 SHARP, b_i = max(v_i - (max v - d), 0) where w_i > 0. Colour = sum b_i layer_i.rgb / sum b_i,
   alpha 1; then vertex colour, detail, tint and light as usual. */
enum { ARC__MAT3_S0, ARC__MAT3_S1, ARC__MAT3_S2, ARC__MAT3_S3, ARC__MAT3_SHARP, ARC__MAT3_N };
enum { ARC__MAT2_DMODE, ARC__MAT2_DSCALE, ARC__MAT2_DSTR, ARC__MAT2_DROT, ARC__MAT2_DCON, ARC__MAT2_BMODE, ARC__MAT2_BROT, ARC__MAT2_BCON,
       ARC__MAT2_MSCALE, ARC__MAT2_MTHR, ARC__MAT2_MSOFT, ARC__MAT2_MON, ARC__MAT2_BSCALE };
/* 0.8.8 BMODE 1: the colour texture plain but world-mapped; BSCALE > 0: the colour texture's uv = the face's world
   plane (as the detail's) x BSCALE. Hex or world-mapped colour sampling always repeats the texture (the cells and
   planes run past 0..1 whatever the texture's own addressing: GL sets GL_REPEAT for the batch, D3D11 picks the
   repeating sampler, the software renderer wraps). */
#define ARC__MAX_MIPS 16
#define ARC__MAX_LAYERS 64   /* 0.9.9: texture arrays up to 64 layers (splat uses the first 4) */
typedef struct {
    bool used, smooth, target; int w, h; uint8_t *rgba;   /* target: render target (soft renderer draws into rgba) */
    int flags;                                            /* ARC_TEX_* */
    int levels; uint8_t *mip[ARC__MAX_MIPS]; int mw[ARC__MAX_MIPS], mh[ARC__MAX_MIPS];   /* mipmaps (level 0 = rgba) */
    /* 0.7.1: fmt = ARC_TEXFMT_* as stored on the GPU (BC*: uploaded compressed, rgba is NULL); gpu_bytes = its size;
       pending = the loader's serial while queued (a 1x1 transparent stand-in; arc_texture_size says 0 x 0) */
    int fmt; size_t gpu_bytes; uint32_t pending;
    /* 0.9.6 texture arrays (arc_texture_array): layers > 0, rgba NULL; layer_id: hidden CPU-only copies of the layers
       (rgba and mips, no GPU texture of their own; the software renderer samples them), freed with the array */
    int layers; uint32_t layer_id[ARC__MAX_LAYERS]; bool cpu_only;
    int vw, vh;   /* 0.9.8 --no-assets: the file's size (the pixels are a 1 x 1 stand-in); 0 = w, h */
} arc__tex_rec;
extern bool arc__no_assets;
/* 0.9.9 the packed instance flags (see arc__instance.extra) */
static inline int arc__ext_unlit(const float *e) { return (int)(e[2] + 0.5f) & 1; }
static inline float arc__ext_darken(const float *e) { return (float)(((int)(e[2] + 0.5f) >> 1) & 255) / 255.0f; }
static inline float arc__ext_char(const float *e) { return (float)(((int)(e[2] + 0.5f) >> 9) & 255) / 255.0f; }
static inline int arc__ext_nomask(const float *e) { return ((int)(e[2] + 0.5f) >> 17) & 1; }
static inline int arc__ext_addfog(const float *e) { return (int)(e[3] + 0.5f) & 1; }
static inline int arc__ext_layer(const float *e) { return (int)(e[3] + 0.5f) >> 1; }   /* 0.9.8 --no-assets (headless): loaded images keep their size, not their pixels */
void arc__tex_build_mips(arc__tex_rec *t);   /* CPU mip chain (box filter): the soft renderer samples it */

/* 0.7.1 block-compressed textures (arc_texc.c): .arct parsing and CPU decoders */
typedef struct {
    int fmt, w, h, levels, flags;            /* flags: 1 normal map, 2 zlib payload */
    const uint8_t *data; uint8_t *owned;     /* the blocks (owned: inflated here, else inside the file's bytes) */
    size_t bytes, off[ARC__MAX_MIPS]; int lw[ARC__MAX_MIPS], lh[ARC__MAX_MIPS];
} arc__ctex;
bool     arc__ctex_open(const uint8_t *d, size_t n, arc__ctex *t);
void     arc__ctex_free(arc__ctex *t);
size_t   arc__ctex_level_bytes(const arc__ctex *t, int level);
uint8_t *arc__ctex_decode(const arc__ctex *t, int level);           /* malloc'd RGBA of one level */
int      arc__bc_block_bytes(int fmt);
size_t   arc__bc_level_bytes(int fmt, int w, int h);
bool     arc__bc_decode(int fmt, const uint8_t *data, size_t size, int w, int h, uint8_t *rgba);
void     arc__bc1_block(const uint8_t *b, uint8_t *out, bool four);
void     arc__bc4_block(const uint8_t *b, uint8_t *out, int stride);
void     arc__bc5_block(const uint8_t *b, uint8_t *out);
void     arc__bc7_block(const uint8_t *b, uint8_t *out);
/* a decoded image on its way to the GPU (made on any thread, uploaded on the main one) */
typedef struct {
    uint8_t *rgba; int w, h;                 /* RGBA8 (level 0) */
    bool comp; arc__ctex c;                  /* or blocks going to the GPU as they are */
    int nmips; uint8_t *mips[ARC__MAX_MIPS]; /* the software renderer: decoded levels 1.. of a compressed texture */
    /* 0.8.6 browser decoding (web, arc_app.browser_decode): src/src_n are the encoded JPEG/PNG bytes (not owned: the
       loader keeps the file until the image is finished; ARC's own decoder falls back to them), web the platform's
       decode job while it runs (web_done: decoded, the GPU takes it straight from the browser's image) */
    const uint8_t *src; size_t src_n; int web; bool web_done;
} arc__img;
bool arc__img_decode(const uint8_t *d, size_t n, int flags, arc__img *o);   /* thread-safe; d must outlive o if o->comp */
/* 0.8.6: as arc__img_decode, but a JPEG/PNG the browser can decode is only sniffed (o->src set, w/h from its header) */
bool arc__img_decode_or_defer(const uint8_t *d, size_t n, int flags, arc__img *o, bool defer);
/* 0.8.6: 1 JPEG / 2 PNG that the browser may decode exactly enough (no EXIF turn, 8-bit PNG, no colour profile...),
   else 0; *alpha: may be see-through (PNG with alpha or tRNS); w/h from the header. Any thread. */
int  arc__img_browser_type(const uint8_t *d, size_t n, bool *alpha, int *w, int *h);
/* 0.8.6 the platform's asynchronous image decoder (the browser on the web; NULL elsewhere). Main thread only. */
typedef struct {
    int  (*start)(const uint8_t *d, size_t n, int type, bool alpha);   /* type 1 JPEG / 2 PNG: a job > 0, or 0 (not taken) */
    int  (*poll)(int job, int *w, int *h);                             /* 0 decoding, 1 decoded, -1 failed */
    bool (*pixels)(int job, uint8_t *rgba, int w, int h);              /* the decoded RGBA8, top row first */
    void (*release)(int job);
    const char *name;                                                  /* "createImageBitmap", ... (logs) */
} arc__imgdec;
extern const arc__imgdec *arc__imgdec_hook;    /* tests: a stand-in decoder (NULL: the platform's) */
const arc__imgdec *arc__imgdec_get(void);      /* the decoder in use for queued loads, or NULL (ARC decodes) */
extern int arc__webimg_stats[4];               /* decoded by the browser, uploaded straight to the GPU, fell back to ARC, read back as bytes */
void arc__img_free(arc__img *o);
extern bool arc__no_ctex;                     /* --no-compressed-textures: always decode on the CPU */
/* a parsed .arcm / .axm (thread-safe), built into a mesh on the main thread */
typedef struct {
    bool axm; uint32_t flags; int vc, ic;
    arc_vertex *v; uint32_t *idx; uint8_t *skin;
    bool has_tex, tex_bad; arc__img tex;
    bool has_mat; arc_material mat; bool map_bad[3]; arc__img maps[3];
    char err[360];
} arc__mesh_data;
bool     arc__mesh_parse(const uint8_t *d, size_t n, const char *name, arc__mesh_data *o);
bool     arc__mesh_parse_ex(const uint8_t *d, size_t n, const char *name, arc__mesh_data *o, bool defer);   /* 0.8.6: defer = leave JPEG/PNG maps to the browser (o->tex.src ...) */
void     arc__mesh_data_free(arc__mesh_data *o);
/* the loader's side of the graphics and audio records (main thread) */
uint32_t arc__tex_reserve(int flags, uint32_t serial);
bool     arc__tex_finish(uint32_t id, uint32_t serial, arc__img *img);   /* false: dropped (destroyed meanwhile) or failed */
void     arc__tex_fail(uint32_t id, uint32_t serial);
uint32_t arc__mesh_reserve(uint32_t serial);
bool     arc__mesh_finish(uint32_t id, uint32_t serial, arc__mesh_data *md, const char *name);
void     arc__mesh_fail(uint32_t id, uint32_t serial);
uint32_t arc__sound_reserve(void);
const char *arc__sound_decode(const uint8_t *d, size_t n, int16_t **pcm, int *frames, int *channels, int *rate);   /* NULL = ok, else why not */
void     arc__sound_finish(uint32_t id, int16_t *pcm, int frames, int channels, int rate);
uint32_t arc__music_reserve(void);
bool     arc__music_finish(uint32_t id, const uint8_t *data, size_t n);
void     arc__music_fail(uint32_t id);
void     arc__decoders_warm(void);           /* fill the decoders' lazy tables before worker threads start */
/* the load queue (arc_load.c) */
enum { ARC__LOAD_TEXTURE = 1, ARC__LOAD_MESH, ARC__LOAD_SOUND, ARC__LOAD_MUSIC, ARC__LOAD_PACK /* 0.8.6 */ };
bool     arc__load_queueing(void);                                    /* inside arc_load_begin / end */
uint32_t arc__load_queue(int kind, const char *path, int flags);       /* the reserved id, 0 if the file is missing */
bool     arc__load_frame(void);                                       /* each frame: upload; true while ticks wait */
void     arc__load_shutdown(void);
uint32_t arc__load_queue_pack(int pack);                              /* 0.8.6: a fetched pack as an item of the open group */
uint32_t arc__load_group_open(void);                                  /* 0.8.6: the group being queued, or 0 */
extern bool arc__webimg_bytes;                /* 0.8.6 --browser-decode=bytes */
/* 0.8.6 packs fetched on demand (arc_webload.c). A fetcher downloads a pack (the browser's fetch + Cache API on the
   web; tests use a stand-in; NULL elsewhere: packs are read from disk at once) */
typedef struct {
    int  (*start)(const char *url, const char *cache_key);            /* a job > 0, or 0; cache_key NULL: no cache */
    int  (*poll)(int job, long long *got, long long *total);          /* 0 downloading, 1 done, -1 failed */
    uint8_t *(*take)(int job, size_t *size);                          /* done: the bytes (malloc'd, the caller frees) */
    void (*release)(int job);
    int  (*from_cache)(int job);                                      /* 1: the bytes came from the cache */
} arc__fetcher;
extern const arc__fetcher *arc__fetcher_hook;  /* tests: a stand-in (NULL: the platform's) */
int  arc__pack_find(const char *name);                                /* the pack table's index, or -1 */
int  arc__pack_for_file(const char *path, long long *size);           /* manifest: the pack (index) holding a file not mounted yet, or -1 */
int  arc__pack_poll(int pack, long long *got);                        /* ARC_PACK_*; *got: bytes so far */
bool arc__pack_finish(int pack);                                      /* mount a downloaded pack (main thread) */
long long arc__pack_bytes(int pack);                                  /* its size from the manifest (0: unknown) */
const char *arc__pack_name(int pack);
bool arc__pack_cache_key(const char *name, const char *hash, char *out, size_t cap);   /* "<name>?arc=<hash>"; false: no hash, no caching */
bool arc__pack_manifest_text(const char *text);                       /* tests: use this text as arc_packs.txt */
bool arc__pack_mount_owned(uint8_t *data, size_t size, const char *name);   /* takes the malloc'd bytes */
void arc__webload_reset(void);                                        /* tests: forget packs fetched and the manifest */
const struct arc__threads *arc__load_threads(void);                   /* the platform's, or NULL */
void     arc__draw_loading(float progress);
int  arc__gfx_push_bones(const float *rows, int count);   /* this frame's skinning matrices; first index or -1 */

extern arc__mesh_rec arc__meshes[ARC__MAX_MESHES];
extern arc__tex_rec  arc__textures[ARC__MAX_TEXTURES];

/* ---- raw input written by platforms ---- */
typedef struct {
    uint8_t keys[ARC_KEY_COUNT / 8];   /* bitset */
    float mx, my, wheel;
    float axes[ARC_AXIS_COUNT];
    uint8_t flags;                     /* ARC__IN_* */
    char text[31];                     /* UTF-8 typed this tick, zero-terminated */
    /* 0.10.5 every pad: connected slots, buttons (bit k - ARC_PAD_A), axes. keys/axes/flags above mirror the first one */
    uint32_t padmask;
    uint32_t pbtn[ARC_MAX_PADS];
    float pax[ARC_MAX_PADS][ARC_AXIS_COUNT];
    /* 0.10.11 (replay format 4) the fingers down this tick, in the order they went down: small id, window pixels now
       and where it started (whole pixels). Pressed / released come from comparing two ticks' lists. */
    uint8_t tcount, tpad[3];
    struct { uint8_t id, pad; int16_t x, y, sx, sy; } touch[ARC_TOUCH_MAX];
} arc__input_snap;
enum { ARC__IN_TOUCH = 1, ARC__IN_PAD = 2 };
void arc__input_key(arc_key k, bool down);       /* platform -> raw state */
void arc__input_char(uint32_t codepoint);
void arc__input_axis(arc_axis a, float v);
void arc__input_pad(bool connected);
/* 0.10.5 platforms: pad slots (0..ARC_MAX_PADS-1) */
void arc__input_pad_slot(int slot, bool connected);
void arc__input_pad_button(int slot, arc_key button, bool down);
void arc__input_pad_axis(int slot, arc_axis a, float v);
int arc__joint_part(const char *name);
/* 0.10.8 levels (arc_level.c) and the editor's use of them */
bool arc__level_hot_reload(uint32_t id, const char *path);
size_t arc__level_object_line(const arc_level *l, int layer, int i, char *out, size_t cap);
int arc__level_object_insert_line(arc_level *l, int layer, int at, const char *line);
void arc__editor_level_reloaded(arc_level *l);
int arc__hint_count(const char *key);   /* 0.10.9: times a hint was asked for (printed once) */
void arc__frame_keep(void);   /* 0.10.9: the recording kept (immediate mode), not added to */
uint32_t arc__frame_target(void);   /* 0.10.7: the render target 2D and markers go into now, for `frame` (0 = screen or viewport) */
void arc__frame_target_name(uint32_t id, char *out, size_t n);   /* its name, or "#id" */   /* 0.10.6: the ARC_PART_* a joint name paints as */
uint8_t *arc__replay_widen(const uint8_t *d, size_t size, size_t *out);   /* 0.10.5 replay format 2 (0.10.11: or 3) -> current (tests) */
void arc__input_touch_mode(bool on);
void arc__input_touch(long long finger, int phase, float x, float y);   /* 0.10.11 platforms: phase 0 down, 1 move, 2 up / cancel; finger = the platform's id */

void arc__input_mouse(float x, float y);
void arc__input_mouse_rel(float dx, float dy);   /* captured mouse: movement added to the (unbounded) position */
void arc__input_wheel(float steps);
void arc__input_clear(void);                     /* focus lost */

/* ---- core <-> platform ---- */
typedef struct {
    const char *name;
    bool (*init)(const arc_app *app, int w, int h);       /* create window + backend */
    void (*poll)(void);                                   /* pump events into arc__input_* */
    void (*present)(void);
    void (*shutdown)(void);
    double (*time)(void);                                 /* seconds, monotonic */
    void (*sleep)(double seconds);
    /* optional (NULL = not supported / use the core's default) */
    void (*set_icon)(const uint8_t *rgba, int w, int h);
    void (*toggle_fullscreen)(void);
    bool (*fullscreen)(void);
    bool (*save)(const char *key, const void *data, size_t size);          /* default: files in the data folder */
    uint8_t *(*load)(const char *key, arc_arena *a, size_t *size_out);
    bool (*save_remove)(const char *key);
    /* 0.6.7 mouse look: capture (hide and hold the pointer; report movement with arc__input_mouse_rel) or let go.
       The platform drops it while the window is out of focus and takes it back on return. captured: in effect now */
    void (*mouse_capture)(bool on);
    bool (*mouse_captured)(void);
    /* 0.7.1 threads for background loading (optional: NULL = load on the main thread, a slice each frame) */
    const struct arc__threads *threads;
} arc__platform;
typedef struct arc__threads {
    void *(*start)(void (*fn)(void *), void *arg);   /* NULL on failure */
    void  (*join)(void *thread);
    void *(*lock_create)(void);
    void  (*lock)(void *l);
    void  (*unlock)(void *l);
    void  (*lock_destroy)(void *l);
    void *(*cond_create)(void);
    void  (*wait)(void *cond, void *l);              /* l locked by the caller */
    void  (*wake_all)(void *cond);
    void  (*cond_destroy)(void *cond);
    int   (*cpus)(void);
} arc__threads;

extern const arc__backend *arc__gfx;     /* active render backend */
extern int arc__msaa;                    /* screen samples asked for (arc_app.msaa or --msaa N), read by GPU backends */
extern int arc__depth_mode;              /* 0.7.2: ARC_DEPTH_* (arc_app.depth_mode or --depth-mode), set before the backend starts */
extern bool arc__depth_precise;          /* set by the backend: reversed with float depth and a 0..1 clip range */
void arc__depth_mode_set(int mode);      /* tests: switch the mode from the next pass on (the software renderer only:
                                            GPU backends make their depth buffers for the mode at start) */
extern float arc__dpi;                   /* web hidpi: pixels per CSS pixel */
extern bool arc__quit_requested;

/* core */
int   arc__startup(int argc, char **argv, const arc__platform *plat);  /* parse args, call init */
bool  arc__frame(void);              /* run one frame (ticks + draw); false when it is time to exit */
void  arc__shutdown(void);
void  arc__agent_push(const char *text);   /* queue agent commands ('a; b; c') */
void  arc__set_size(int w, int h);
int   arc__run(int argc, char **argv, const arc__platform *plat);      /* full loop for native platforms */
double arc__plat_time(void);

/* particles (arc_fx.c) */
void arc__fx_tick(float dt);
size_t arc__fx_state(arc_writer *w); bool arc__fx_state_read(arc_reader *r, bool commit);
size_t arc__decal_state(arc_writer *w); bool arc__decal_state_read(arc_reader *r, bool commit);   /* 0.7, after the UI's */
void arc__fx_reset(void);
void arc__scene_after_draw(void);                       /* arc_scene.c: the `cells draw` overlay */
bool arc__scene_command(int argc, const char **argv);   /* `cells`, `lod` */
extern volatile float arc__audio_mix_ms;                /* arc_core.c: frame budget, the mixer's share */
extern int arc__draw_cell;           /* 0.7.1: the cell meshes are tagged with (arc_set_draw_cell); 0 = none */
void arc__fx_shutdown(void);
void arc__bill_shutdown(void);
extern float arc__frame_alpha;

/* UI (arc_ui.c) */
void arc__ui_tick_begin(void);
void arc__ui_tick_end(void);
void arc__ui_render(void);
void arc__ui_print(void);
/* 0.9.0 hints: print `ARC hint <text>` once per key (a likely mistake and its fix) */
enum { ARC__PHASE_INIT = 1, ARC__PHASE_TICK = 2, ARC__PHASE_DRAW = 3 };
extern int arc__phase;
void arc__hint(const char *key, const char *fmt, ...);
void arc__ui_json(arc_json *j, const char *key);
/* 0.9.0 the frame description (`frame` command): record 2D text, sprites and rects while on; then describe the last
   rendered frame as JSON (meshes on screen with names and boxes, texts, sprites, rects, lights, camera, UI) */
void arc__frame_record(bool on);
void arc__frame_json(arc_json *j);
bool arc__ui_find(const char *label, float *cx, float *cy);
/* 0.7.2 developer tools: the tile editor (arc_editor.c) and what it needs from the core */
bool arc__editor_tick(void);                    /* in the tick, before the game's: true = skip the game's tick */
void arc__editor_render(void);
size_t arc__editor_state(arc_writer *w);
bool arc__editor_state_read(arc_reader *r, bool commit);
void arc__editor_command(int argc, const char **argv);
void arc__editor_dump_json(arc_json *j);
bool arc__dev_keys(void);                       /* dev keys on (arc_app.dev_keys or --dev) */
bool arc__sim_replaying(void);                  /* a replay or a silent re-simulation is running: no files written */

typedef struct { FILE *f; int w, h; double delay_acc; double delay_cs; } arc__gif;   /* arc_gif.c: animated GIFs for agents */
bool arc__gif_begin(arc__gif *g, const char *path, int w, int h, double delay_cs);
void arc__gif_frame(arc__gif *g, const uint8_t *rgba);
bool arc__gif_end(arc__gif *g);

/* hot reload of assets (arc_core.c watches, the loaders reload in place) */
enum { ARC__HOT_TEXTURE = 1, ARC__HOT_MESH, ARC__HOT_ANIM, ARC__HOT_SOUND, ARC__HOT_LEVEL };
void arc__hot_watch(int kind, uint32_t id, const char *path);
void arc__hot_watch_ex(int kind, uint32_t id, const char *path, bool from_pack);   /* 0.7.1: a file the loader read */
/* 0.7.1: thread-safe reads for the loader (packs first, like arc_file_read): malloc'd, NUL-terminated */
uint8_t  *arc__file_read_alloc(const char *path, size_t *size_out, bool *from_pack);
long long arc__file_size(const char *path, bool *from_pack);          /* -1 if missing */
bool arc__mesh_reload(arc_mesh old, const char *path);
bool arc__texture_reload(arc_texture t, const char *path);
bool arc__anim_replace(arc_anim old, arc_anim fresh);
bool arc__sound_reload(arc_sound old, const char *path);

/* graphics front end */
bool  arc__gfx_init(const arc__backend *backend, int w, int h);
void  arc__gfx_begin(void);
void  arc__gfx_end(bool render);     /* builds frame data and calls backend->render */
void  arc__gfx_end_offscreen(void);
bool  arc__gfx_render_last(void);    /* draw the last built frame again (to the screen too) */  /* immediate mode, a frame nobody sees: only render-target passes run */
void  arc__gfx_shutdown(void);
bool  arc__gfx_has_frame(void);
uint8_t *arc__gfx_read_frame(int *w, int *h);   /* last rendered frame, malloc'd RGBA */
void  arc__gfx_count_tick_ms(float ms);
void  arc__gfx_set_clear(uint32_t rgb);
void  arc__stats_frame_ms(float ms, float cpu_ms);
bool  arc__png_write(const char *path, const uint8_t *rgba, int w, int h);
bool  arc__image_write(const char *path, const uint8_t *rgba, int w, int h);   /* .jpg/.jpeg -> JPEG, else PNG */
uint8_t *arc__png_read(const uint8_t *data, size_t size, int *w, int *h, arc_arena *a);
uint8_t *arc__qoi_read(const uint8_t *data, size_t size, int *w, int *h, arc_arena *a);
uint8_t *arc__inflate(const uint8_t *src, size_t n, size_t expect, size_t *out_n);   /* zlib stream -> malloc'd bytes */
uint8_t *arc__jpeg_read(const uint8_t *data, size_t size, int *w, int *h);   /* malloc'd RGBA, NULL if not a baseline JPEG */
uint8_t *arc__jpeg_read_max(const uint8_t *data, size_t size, int *w, int *h, int max_side);   /* 0.8.3: reduced while still >= max_side */
extern const uint8_t arc__font8x8[96][8];

/* audio */
void  arc__audio_init(int rate);
void  arc__audio_mix(float *stereo_out, int frames);    /* called by platform audio thread/callback */
int   arc__audio_rate(void);
void  arc__audio_tick(void);
extern bool arc__audio_silent;                         /* re-simulating after a seek: no new sounds */                            /* after every tick: positional voices follow the listener */

/* backends */
/* MP3 decoder (arc_mp3.c) */
typedef struct arc__mp3 arc__mp3;
size_t  arc__mp3_state_size(void);
bool    arc__mp3_open(arc__mp3 *m, const uint8_t *data, size_t size);   /* data must outlive the decoder */
void    arc__mp3_rewind(arc__mp3 *m);
int     arc__mp3_read(arc__mp3 *m, int16_t *out);   /* next frame, gapless-trimmed: samples per channel, -1 at end */
int     arc__mp3_channels(const arc__mp3 *m);
int     arc__mp3_rate(const arc__mp3 *m);
int64_t arc__mp3_length(const arc__mp3 *m);           /* samples per channel, -1 if unknown */
void    arc__mp3_set_native_channels(arc__mp3 *m, bool on);   /* tests: frames keep their own channel count */
int     arc__mp3_frame_channels(const arc__mp3 *m);
int16_t *arc__qoa_decode_all(const uint8_t *d, size_t n, int *channels, int *rate, int *frames);   /* free() the result */
extern const arc__backend arc__backend_soft;
#if defined(ARC_GL)
extern const arc__backend arc__backend_gl;
#endif
#if defined(ARC_D3D11)
extern const arc__backend arc__backend_d3d11;
#endif
extern const arc__platform arc__platform_headless;

/* misc */
#if defined(_MSC_VER)
#include <intrin.h>
#define ARC__BARRIER() _ReadWriteBarrier()
#else
#define ARC__BARRIER() __sync_synchronize()
#endif
void *arc__xalloc(size_t size);       /* malloc that aborts with a message */
#define ARC__COUNTOF(a) ((int)(sizeof(a) / sizeof((a)[0])))

static inline void arc__unpack_rgb(uint32_t c, float out[4]) {
    out[0] = ((c >> 16) & 0xFF) / 255.0f; out[1] = ((c >> 8) & 0xFF) / 255.0f; out[2] = (c & 0xFF) / 255.0f;
    uint32_t a = c >> 24; out[3] = a ? (float)a / 255.0f : 1.0f;
}
/* 0xAARRGGBB (alpha 0 = opaque) -> bytes R,G,B,A in memory */
static inline uint32_t arc__to_abgr(uint32_t c) {
    uint32_t a = c >> 24; if (!a) a = 255;
    return (a << 24) | ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
}
/* 0.10.0 action modules (arc_brain.c, arc_senses.c, arc_shots.c, arc_rig.c) */
void arc__brain_dump_json(arc_json *j); void arc__brain_command(int argc, const char **argv);
void arc__senses_dump_json(arc_json *j);
void arc__shots_json(arc_json *j);
void arc__rig_json(arc_json *j); void arc__rig_command(int argc, const char **argv, const arc_camera *game_cam);
const arc_camera *arc__rig_override(const arc_camera *game); void arc__rig_tick(void);
bool arc__ui_fit_start(int argc, const char **argv); int arc__ui_fit_ticks(void); void arc__ui_fit_before_tick(void); void arc__ui_fit_after_tick(void);   /* 0.10.1 */
extern float arc__safe_insets[4];
size_t arc__shots_state(arc_writer *w); bool arc__shots_state_read(arc_reader *r, bool commit);
#endif
