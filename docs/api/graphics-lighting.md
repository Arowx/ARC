# Lighting, shadows, reflections and post-processing

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Graphics** · [quick reference](../QUICK.md) · [full API](../API.md)


Lighting (per pixel): one directional light + ambient, optional rim light, linear or exponential fog.

```c
ARC_API void arc_set_light(arc_v3 dir_to_light, uint32_t light_rgb, uint32_t ambient_rgb);
ARC_API void arc_set_fog(uint32_t rgb, float start, float end);    // linear; end <= start disables
ARC_API void arc_set_fog_exp(uint32_t rgb, float density);    // exponential-squared: 1 - exp(-(density*d)^2); 0 disables
```

sky: the screen's background becomes a gradient by view direction, from horizon (level) to top (straight up)
   and to below (straight down). Linear in the view ray's height. Use the fog colour as the horizon so distant
   things fade into the sky. Replaces the clear colour for the screen (not render targets).

```c
ARC_API void arc_set_sky(uint32_t top_rgb, uint32_t horizon_rgb);    // below the horizon = horizon
ARC_API void arc_set_sky_ex(uint32_t top_rgb, uint32_t horizon_rgb, uint32_t below_rgb);
ARC_API void arc_sky_off(void);
```

billboards: camera-facing pictures and text placed in the 3D world. They are depth tested (walls in front
   hide them), unlit and see-through, sorted with particles. Sizes are world units, or screen pixels with
   ARC_BB_PIXELS (same size at any distance). pos is the centre, or the bottom middle with ARC_BB_BOTTOM.
   ARC_BB_UPRIGHT turns only around the vertical (trees, signs, people); ARC_BB_ADDITIVE adds light (glows).

```c
enum { ARC_BB_PIXELS = 1, ARC_BB_BOTTOM = 2, ARC_BB_UPRIGHT = 4, ARC_BB_ADDITIVE = 8 };
ARC_API void arc_draw_billboard(arc_texture tex, arc_v3 pos, float w, float h, uint32_t tint, int flags);    // tex {0} = a soft round dot
ARC_API void arc_draw_billboard_ex(arc_texture tex, arc_v3 pos, float w, float h, uint32_t tint, float alpha, float glow, int flags);
```

text: height = one line (world units, or pixels with ARC_BB_PIXELS). bg_argb: 0 = none, else a box behind it
   (0xE0FFFFFF = a white speech bubble). Lines split at '\n'. Each different text becomes a small cached texture.

```c
ARC_API void arc_draw_billboard_text(arc_v3 pos, float height, uint32_t rgb, uint32_t bg_argb, int flags, const char *text);
```

Rim light: brightens edges that face away from the camera, so models stand out from the ground.
   power shapes the rim (2 = wide, 4 = thin); rgb 0 turns it off.

```c
ARC_API void arc_set_rim(uint32_t rgb, float power);
```

What shiny (pbr) materials reflect: a gradient from straight up (top) through the horizon to straight down.
   Default: the sky when it is on, else a dim version of the ambient and light colours. intensity 0 turns
   reflections off; arc_set_environment(0, 0, 0, -1) goes back to the default.

```c
ARC_API void arc_set_environment(uint32_t top_rgb, uint32_t horizon_rgb, uint32_t below_rgb, float intensity);
```

Point and spot lights (0.6). Added each frame like drawing: call them from draw() (they light the pass being
   drawn and every pass after it this frame). 0.9.5: up to 256 a frame (tiled: each pixel only pays for the lights
   that reach it; up to 64 may overlap one 16th x 9th of the screen, the strongest near the camera winning there),
   and up to 4 of them may cast shadows (the _ex forms with ARC_LIGHT_SHADOW). intensity multiplies rgb (above 1 for bright lights with post bloom); the light fades
   smoothly to nothing at `range`. Spots point along `dir`, full strength inside `inner` and none outside `outer`
   (half-angles in radians, e.g. 0.3 and 0.5). Unlit meshes ignore them.

```c
#define ARC_MAX_LIGHTS 256    // per frame (0.9.5; was 16 per pass)
#define ARC_MAX_SHADOW_LIGHTS 4    // lights casting shadows per frame
ARC_API void arc_light_point(arc_v3 pos, uint32_t rgb, float intensity, float range);
ARC_API void arc_light_spot(arc_v3 pos, arc_v3 dir, uint32_t rgb, float intensity, float range, float inner, float outer);
ARC_API int arc_light_count(void);    // lights added so far this frame
```

0.9.5: the same with flags. ARC_LIGHT_SHADOW: solid meshes (those that cast sun shadows) cast shadows from this
   light, in every pass after it except mirror reflections. The first 4 a frame get them (the rest light without
   shadows; one log line). A point light renders the scene 6 times per pass, a spot once: keep shadowed lights few,
   and ranges short. Spot shadows reach at most 72 degrees from the spot's direction.

```c
#define ARC_LIGHT_SHADOW 1
ARC_API void arc_light_point_ex(arc_v3 pos, uint32_t rgb, float intensity, float range, int flags);
ARC_API void arc_light_spot_ex(arc_v3 pos, arc_v3 dir, uint32_t rgb, float intensity, float range, float inner, float outer, int flags);
ARC_API void arc_set_light_shadow_resolution(int res);    // each face's size: 128, 256, 512 (default) or 1024
ARC_API int arc_light_shadow_count(void);    // shadowed lights so far this frame
```

Shadows from the directional light: solid meshes cast them onto everything lit, inside a box of `radius`
   around `center` (usually what the camera looks at; the box follows it without shimmering). resolution: the
   shadow map's size (2048 default; 0 = default). radius 0 turns shadows off. strength: how much of the direct
   light a shadow takes away (1 = all; 0.6 keeps some). Meshes can opt out of casting (foliage, effects).

```c
ARC_API void arc_set_shadows(arc_v3 center, float radius, int resolution);
ARC_API void arc_set_shadow_strength(float strength);
```

0.6: cascades (2 or 3; 1 = the one box above): the shadow area follows the screen camera in slices, sharp near it
   and reaching `distance` away (0 = 4 x radius); arc_set_shadows' center is then not used and resolution is per
   slice (1024 is plenty; they share a 2x2 atlas). Perspective cameras only. softness 0..1 widens shadow edges
   (0 = the classic 3x3 filter).

```c
ARC_API void arc_set_shadow_cascades(int count, float distance);
ARC_API void arc_set_shadow_softness(float softness);
ARC_API void arc_mesh_set_cast_shadow(arc_mesh m, bool on);
```

Planar reflections (0.8.7): polished floors, water, ice. The scene is drawn a second time from the camera mirrored
   in the plane y = `y` (opaque and see-through meshes and models, skinned ones too, lit as usual, no shadows; nothing
   below y + 0.005), into a render target `scale` x the screen (0.1..1, 0 = 0.5) with mip levels. Meshes marked
   reflective sample it at their pixel on the screen, so the screen pass must be the one drawing them:
     colour = mix(colour, reflection, strength x amount x vertex alpha x F),  F = 0.6 + 0.4 (1 - N.V)^5 (more at
     grazing angles); the reflection blurred by the material's roughness (mip level 6 x roughness; plain materials
     are sharp) and nudged by its normal map. Vertex alpha is the per-cell mask (opaque meshes stay opaque).
   strength 0 turns it off: no extra pass, the frame exactly as without it. One plane, for the first screen pass
   that draws a reflective mesh. Reflective meshes and those set not reflected are left out of the mirror pass;
   immediate triangles, lines, 2D, decals' and stamps' quads near the floor are not mirrored (they are below
   y + 0.005 or not meshes). Static batches are culled to the main camera, so things just off screen may be
   missing from the reflection. Costs one more scene pass at `scale`; WebGL2 too.

```c
ARC_API void arc_reflection_plane(float y, float strength, float scale);
ARC_API void arc_mesh_set_reflective(arc_mesh m, float amount);    // 0..1 (0 = not reflective)
ARC_API void arc_mesh_set_reflected(arc_mesh m, bool on);    // false: left out of the mirror pass (default true)
```

Post-processing of the 3D picture (2D drawing stays untouched on top). Zero means "off/default" everywhere:
   bloom makes bright parts glow (emissive, glowing particles, light above bloom_threshold); tonemap rolls bright
   colours off smoothly instead of clipping them. NULL turns it all off. Kept until changed.

```c
typedef struct { float bloom; float bloom_threshold; float bloom_radius; float exposure; bool tonemap; float desaturate; float contrast; float vignette; float tilt_shift; float tilt_focus; float tilt_width; float ssao; float ssao_radius; bool fxaa; arc_texture lut; float lut_strength; float scanlines; float crt_curve; float pixelate; float aberration; } arc_post;
```

A colour-grading table for arc_post.lut: fn maps each colour (0..1, in place) to its graded colour, sampled on a
   size^3 grid (16 or 32; 0 = 16). Or load one from a PNG made by tools/make_lut.py (it has presets).

```c
ARC_API arc_texture arc_lut_build(void (*fn)(float rgb[3], void *user), void *user, int size);
ARC_API void arc_set_post(const arc_post *p);
```

0.8.2: post effects for one render target, for 3D drawn into a target that is then placed on the screen (a view
   inside a frame of 2D). They run when the target's last pass of the frame has drawn its 3D; the 2D drawn into the
   target goes on top afterwards, untouched, as on the screen. NULL (or all zero) = none, as before. A target with
   post comes out opaque. Up to 8 targets at once (false and an ARC error line past that). Kept until changed or the
   target is destroyed. Vignette, scanlines, CRT and the rest follow the target's own size.

```c
ARC_API bool arc_target_set_post(arc_texture target, const arc_post *p);
```

0.8.2: shadow cascades (arc_set_shadow_cascades) for passes into this target, following the camera set while
   drawing into it (perspective only). Off by default: target passes use the single arc_set_shadows box.

```c
ARC_API void arc_target_set_shadow_cascades(arc_texture target, bool on);
```

world point -> screen pixels; returns false if behind the camera

```c
ARC_API bool arc_world_to_screen(arc_v3 p, arc_v2 *out);
```

screen pixel -> world ray

```c
ARC_API void arc_screen_ray(arc_v2 px, arc_v3 *origin, arc_v3 *dir);
```

The same two, for any camera and view size, with no drawing state: use these in tick() (headless runs never
   call draw(), so the two above would see an old camera there and replays would aim differently).

```c
ARC_API bool arc_camera_to_screen(const arc_camera *c, float view_w, float view_h, arc_v3 p, arc_v2 *out);
ARC_API void arc_camera_ray(const arc_camera *c, float view_w, float view_h, arc_v2 px, arc_v3 *origin, arc_v3 *dir);
ARC_API arc_m4 arc_camera_view_proj(const arc_camera *c, float view_w, float view_h);
```

0.10.1: the camera moved so the box fills the view: keeps base's view direction, up and field of view (or
   orthographic), centres the box and leaves `margin` (a share of the frame, e.g. 0.05) on each side of the tighter
   axis. aspect = view width / height. For art reviews, previews and line-ups.

```c
ARC_API arc_camera arc_camera_fit(arc_camera base, arc_v3 box_min, arc_v3 box_max, float aspect, float margin);
```
