# Textures and images

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Graphics** · [quick reference](../QUICK.md) · [full API](../API.md)


Textures (RGBA8). Used by 2D sprites, text and meshes.

```c
ARC_API arc_texture arc_texture_create(int w, int h, const uint8_t *rgba, bool smooth);
ARC_API void arc_texture_update(arc_texture t, const uint8_t *rgba);
ARC_API arc_texture arc_texture_load(const char *path);    // .png (non-interlaced 8-bit), .qoi or baseline .jpg; sharp pixels
```

Options for textures, mostly for meshes: SMOOTH = bilinear filtering; REPEAT = texture coordinates outside
   0..1 tile (ground, walls); MIPMAPS = smaller copies for distant or slanted surfaces (no shimmer).

```c
enum { ARC_TEX_SMOOTH = 1, ARC_TEX_REPEAT = 2, ARC_TEX_MIPMAPS = 4, ARC_TEX_MESH = 7 };
```

0.8.2: the largest side of a mipmapped texture (every mesh texture and material map, and ARC_TEX_MIPMAPS loads)
   when loaded from now on; 0 = no cap (default). Trades 3D texture detail for start time, upload and GPU memory without
   re-converting assets. Compressed .arct files skip their bigger levels (free); PNG/JPEG are halved after decoding (the
   decode itself costs the same). 2D textures, fonts and render targets are never capped. Also --texture-max-size N.

```c
ARC_API void arc_set_texture_max_size(int px);
ARC_API int arc_texture_max_size(void);
ARC_API arc_texture arc_texture_create_ex(int w, int h, const uint8_t *rgba, int flags);
```

0.9.6 a texture array for splatting (arc_material.layers): textures of the same size (RGBA, not compressed or
   still loading), copied in (the originals can be destroyed). Mipmapped, smooth and repeating. Not drawable as a
   texture of its own. {0} on a bad set (logged). arc_texture_destroy frees it. 0.9.9: up to 64 layers (splatting
   uses the first 4), and a mesh can take its colour from one per draw (arc_mesh_set_texture_array): one mesh and one
   draw call for floor tiles in 30 different looks. Each layer uses a texture slot of its own (1024 in all).

```c
#define ARC_TEXTURE_ARRAY_MAX 64
ARC_API arc_texture arc_texture_array(const arc_texture *layers, int n);
ARC_API arc_texture arc_texture_load_ex(const char *path, int flags);
```

Images on the CPU (RGBA8, top row first). Results are malloc'd: free() them.

```c
ARC_API uint8_t *arc_image_load(const char *path, int *w, int *h);
ARC_API uint8_t *arc_image_decode(const uint8_t *data, size_t size, int *w, int *h);    // PNG, QOI or baseline JPEG bytes (or .arct: its first level)
ARC_API bool arc_image_save_png(const char *path, const uint8_t *rgba, int w, int h);    // RGBA if any pixel has alpha < 255, else RGB
ARC_API bool arc_image_save_jpeg(const char *path, const uint8_t *rgba, int w, int h, int quality);    // baseline 4:2:0; quality 1..100 (0 = 90)
ARC_API void arc_texture_size(arc_texture t, int *w, int *h);
```

Grime overlays (0.8.4): turn a dirt photo into a see-through overlay whose alpha says where the dirt is, in ragged
   islands that fade out at the edges, for arc_draw_stamps (or arc_material.detail). Per pixel of the output:
     - a crop of the source (a seeded square, `crop` of its shorter side: the dirt's scale), resized to size x size;
     - dirt amount = how far the pixel's brightness is from the crop's mean, over 2 standard deviations (0..1);
       darker than the mean is grime (colour times `darken`), lighter is a scratch (kept pale);
     - breakup mask = ramp((fbm - threshold) / softness) of value noise `mask_cells` across, OR-ed (max) with
       `second` times a second, unrelated mask;
     - edge fade = 1 inside, smoothstep to 0 at the circle that touches the edges, over `edge` of the radius.
   alpha = amount x mask x fade. tools/grime.py makes the same files offline (same numbers, give or take a level).

```c
typedef struct { int size; float crop; float threshold; float softness; float mask_cells; int octaves; float second; float edge; float darken; uint32_t seed; } arc_breakup;
ARC_API uint8_t *arc_image_breakup(const uint8_t *rgba, int w, int h, const arc_breakup *b);    // size x size RGBA, free() it; NULL b = defaults
ARC_API arc_texture arc_texture_breakup(const char *path, const arc_breakup *b);    // loads the image, makes the overlay, uploads it (mipmapped, smooth, repeating)
```

0.9.9 image operations, in place on RGBA pixels (from arc_image_load or arc_image_decode), to make variants at load time
   instead of offline (then arc_texture_create). Alpha is kept. Deterministic: the same pixels on every platform.
   - multiply: colour = mix(colour, colour x rgb, amount) (amount 0..1; rgb 0x808080 at 1 halves it).
   - overlay: `src` (sw x sh, its alpha x opacity is how much shows) blended onto the image with `mode`, stretched
     over the whole image (bilinear), or with `tile` repeated at its own size from the top-left.
   - noise: brightness x (1 + amount x (2n - 1)), n = fbm value noise with `cells` features across (whole numbers
     wrap, so a texture that tiles still tiles), `octaves` (0 = 4).
   - char: burnt, as arc_mesh_style.charred but baked: darkened, soot mottling and a few bright scratches (amount
     0..1); what tools/char_glb.py-style scripts did offline.

```c
enum { ARC_IMAGE_NORMAL = 0, ARC_IMAGE_MULTIPLY, ARC_IMAGE_SCREEN, ARC_IMAGE_OVERLAY, ARC_IMAGE_ADD };
ARC_API void arc_image_multiply(uint8_t *rgba, int w, int h, uint32_t rgb, float amount);
ARC_API void arc_image_overlay(uint8_t *rgba, int w, int h, const uint8_t *src, int sw, int sh, int mode, float opacity, bool tile);
ARC_API void arc_image_noise(uint8_t *rgba, int w, int h, float cells, int octaves, float amount, uint32_t seed);
ARC_API void arc_image_char(uint8_t *rgba, int w, int h, float amount, uint32_t seed);
```

Compressed textures (0.7.1). .arct files made by tools/texcompress.py (and the textures inside .arcm meshes made with
   mesh2arcm.py --compress, or recompressed with texcompress.py --arcm) hold GPU block formats with their mip levels:
   BC1 (RGB, or RGB with cut-out alpha; 4 bits a texel), BC3 (RGBA; 8 bits), BC5 (two channels, for normal maps: Z is
   rebuilt; 8 bits), BC7 (RGBA, best quality; 8 bits). The usual loaders take them (arc_texture_load(_ex), arc_mesh_load,
   packs, hot reload) and send them to the GPU as they are: 4-8 times less GPU memory than RGBA8 and no image decoding at
   load. Where the GPU cannot take a format (and always in the software renderer, or with --no-compressed-textures)
   ARC decodes them to RGBA8 at load instead, so they draw the same everywhere. A texture's mip levels are used only
   with ARC_TEX_MIPMAPS (mesh textures have it). arc_texture_update on a compressed texture turns it into RGBA8.

```c
enum { ARC_TEXFMT_RGBA8 = 0, ARC_TEXFMT_BC1 = 1, ARC_TEXFMT_BC3 = 3, ARC_TEXFMT_BC4 = 4, ARC_TEXFMT_BC5 = 5, ARC_TEXFMT_BC7 = 7 };
ARC_API arc_texture arc_texture_from_memory(const uint8_t *data, size_t size, int flags);    // PNG, QOI, JPEG or .arct bytes
ARC_API int arc_texture_format(arc_texture t);    // as stored on the GPU: ARC_TEXFMT_BC* if uploaded compressed
ARC_API bool arc_texture_format_supported(int fmt);    // this GPU takes fmt as it is (never in the software renderer)
```
