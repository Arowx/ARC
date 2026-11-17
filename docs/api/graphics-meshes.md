# Colours, meshes and materials

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Graphics** · [quick reference](../QUICK.md) · [full API](../API.md)


Colours: 0xRRGGBB is opaque. Use arc_rgba() for alpha.
An alpha byte of 0 means opaque (so plain 0xRRGGBB works); arc_rgba never returns alpha 0.

```c
uint32_t arc_rgba(uint32_t rgb, float a);
#define ARC_WHITE 0xFFFFFF
#define ARC_BLACK 0x000000
typedef struct { uint32_t id; } arc_mesh;    // 0 = none
typedef struct { uint32_t id; } arc_texture;    // 0 = none (white)
typedef struct { float px, py, pz; float nx, ny, nz; uint32_t color; float u, v; } arc_vertex;
uint32_t arc_vcol(uint32_t rgb);
```

Upload a mesh to the GPU once; draw it thousands of times per frame (instanced).

```c
ARC_API arc_mesh arc_mesh_create(const arc_vertex *v, int vcount, const uint32_t *idx, int icount);
ARC_API void arc_mesh_destroy(arc_mesh m);    // also destroys a texture the mesh file brought with it
ARC_API int arc_mesh_triangles(arc_mesh m);
ARC_API int arc_mesh_vertex_count(arc_mesh m);
```

Skinned mesh: each vertex follows up to 4 joints of an arc_anim skeleton (weights out of 255, summing to 255).

```c
typedef struct { uint8_t joint[4]; uint8_t weight[4]; } arc_skin_vertex;
ARC_API arc_mesh arc_mesh_create_skinned(const arc_vertex *v, int vcount, const uint32_t *idx, int icount, const arc_skin_vertex *skin);
ARC_API bool arc_mesh_skinned(arc_mesh m);
```

Replace a mesh's vertices (same count; indices stay). For shapes rebuilt on the CPU: water, cloth,
   procedural terrain. Update before drawing the mesh in a frame: a frame draws every instance with the
   vertices the mesh has when the frame ends.

```c
ARC_API void arc_mesh_update(arc_mesh m, const arc_vertex *v, int vcount);
```

A texture drawn on the mesh (with its vertex colours multiplied in). The texture is not owned by the
   mesh unless it came from the mesh file. Pixels with alpha below 0.5 are cut out (leaves, fences).

```c
ARC_API void arc_mesh_set_texture(arc_mesh m, arc_texture t);
ARC_API arc_texture arc_mesh_texture(arc_mesh m);
```

Materials (0.6): extra maps and surface settings for a mesh. .arcm files made by mesh2arcm.py carry them from glTF
   (normal, metallic-roughness, occlusion and emissive maps). A mesh without one is shaded exactly as before.
   - normal: tangent-space normal map (OpenGL convention: green = up the texture), sharing the colour texture's u, v.
     No tangents are stored: they are worked out per pixel from positions and texture coordinates.
   - pbr: lit as metal or non-metal with sharp or broad highlights (GGX) and reflections of the environment
     (arc_set_environment, or the sky). metallic 0..1 and roughness 0..1 multiply the map's blue and green.
   - orm: glTF's packed map: R = ambient occlusion (strength `occlusion`), G = roughness, B = metallic.
   - emissive: glows with emissive_rgb (HDR: above 1 blooms) times the emissive map's colour (no map: everywhere).
   The mesh owns maps loaded with it; maps the game sets stay the game's.

```c
typedef struct { arc_texture normal, orm, emissive; float normal_scale; bool pbr; float metallic, roughness; float occlusion; float emissive_rgb[3]; arc_texture detail; float detail_scale, detail_strength; int detail_tiling, base_tiling; float detail_rotate, base_rotate; float detail_contrast, base_contrast; arc_texture detail_mask; float mask_scale, mask_threshold, mask_softness; float base_scale; arc_texture layers; float layer_scale[4]; float splat_sharpness; } arc_material;
enum { ARC_TILE_REPEAT = 0, ARC_TILE_HEX = 1 };
ARC_API void arc_mesh_set_material(arc_mesh m, const arc_material *mat);    // NULL = none (plain shading)
ARC_API bool arc_mesh_material(arc_mesh m, arc_material *out);    // false if the mesh has none
```

Procedural meshes — handy for prototyping with no art. Unit sized, centred on the origin.

```c
ARC_API arc_mesh arc_mesh_cube(uint32_t rgb);    // 1x1x1
ARC_API arc_mesh arc_mesh_sphere(uint32_t rgb, int segments);    // diameter 1
ARC_API arc_mesh arc_mesh_cylinder(uint32_t rgb, int segments);    // diameter 1, height 1
ARC_API arc_mesh arc_mesh_cone(uint32_t rgb, int segments);    // base diameter 1, height 1, tip +Y
ARC_API arc_mesh arc_mesh_plane(uint32_t rgb, int tiles, uint32_t rgb2);    // 1x1 in XZ, checkerboard of tiles x tiles
```

Mesh builder: procedural shapes with the triangle winding and normals worked out for you.
    arc_mesh_builder *b = arc_mb_begin();
    arc_mb_color(b, 0xC8A060); arc_mb_box(b, lo, hi);
    arc_v2 vase[] = { {0.3f,0}, {0.5f,0.4f}, {0.2f,1} }; arc_mb_lathe(b, vase, 3, 24, true);
    arc_mesh m = arc_mb_finish(b, 0, 0);      // flat shading; 60 = smooth across edges gentler than 60 degrees
Triangles and quads are counter-clockwise seen from their front. arc_mb_transform places what follows (a mirroring
transform keeps faces pointing out). Lathe profiles are (radius, height) going upward, spun round +Y; extrude
outlines are (x, z) in either direction. Texture coordinates come along (lathe: u around, v down the profile).

```c
typedef struct arc_mesh_builder arc_mesh_builder;
enum { ARC_MB_OUTWARD = 1 };    // finish: turn every triangle to face away from the shape's centre (closed solids)
ARC_API arc_mesh_builder *arc_mb_begin(void);
ARC_API void arc_mb_color(arc_mesh_builder *b, uint32_t rgb);    // for the triangles that follow
ARC_API void arc_mb_transform(arc_mesh_builder *b, arc_m4 m);
ARC_API void arc_mb_reset_transform(arc_mesh_builder *b);
ARC_API void arc_mb_tri(arc_mesh_builder *b, arc_v3 a, arc_v3 c, arc_v3 d);
ARC_API void arc_mb_quad(arc_mesh_builder *b, arc_v3 a, arc_v3 c, arc_v3 d, arc_v3 e);
ARC_API void arc_mb_box(arc_mesh_builder *b, arc_v3 lo, arc_v3 hi);
ARC_API void arc_mb_lathe(arc_mesh_builder *b, const arc_v2 *profile, int count, int segments, bool caps);
ARC_API void arc_mb_extrude(arc_mesh_builder *b, const arc_v2 *outline, int count, float y0, float y1, bool caps);
ARC_API int arc_mb_vertex_count(const arc_mesh_builder *b);
ARC_API arc_mesh arc_mb_finish(arc_mesh_builder *b, float smooth_degrees, int flags);    // frees the builder
```

Load an .arcm file (see tools/mesh2arcm.py, which converts GLB/glTF/OBJ). Returns {0} on failure.
   Version 2 files can carry a texture (loaded smooth, mipmapped and repeating) and a skin.

```c
ARC_API arc_mesh arc_mesh_load(const char *path);    // also reads AXE .axm (AXM2) files
ARC_API arc_mesh arc_mesh_from_memory(const uint8_t *data, size_t size);
```

Draw both sides of every triangle (for models with mixed winding, open shells, foliage).
   .axm files load double-sided, matching how AXE drew them.

```c
ARC_API void arc_mesh_set_double_sided(arc_mesh m, bool on);
```

bounding box of a mesh in model space

```c
ARC_API void arc_mesh_bounds(arc_mesh m, arc_v3 *lo, arc_v3 *hi);
```
