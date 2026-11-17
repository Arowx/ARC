# Camera and depth

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Graphics** · [quick reference](../QUICK.md) · [full API](../API.md)


3D camera for this frame. Call before arc_draw_mesh.

```c
typedef struct { arc_v3 eye, target, up; float fov_y; float ortho_height; float znear, zfar; } arc_camera;
```

0.7.2 reversed-Z (arc_app.depth_mode, chosen at start). STANDARD: the classic depth buffer (near 0, far 1; 24-bit
   on the GPU), which runs out of precision far away: two surfaces a few centimetres apart flicker (z-fight) at a few
   hundred metres with a small znear. REVERSED: near 1, far 0 in a 32-bit float depth buffer: the float's precision
   near 0 cancels the projection's loss with distance, so surfaces 1 cm apart stay apart at 2 km with znear 0.05.
   REVERSED_INFINITE: the same with no far clip plane for perspective cameras (zfar still bounds arc_sphere_visible
   and arc_aabb_visible). The picture is otherwise the same: every depth feature (ARC_DEPTH_* styles, x-ray,
   depth_pull, line depth push, soft particles, ssao, decals, sky, render targets) follows; shadow maps keep their own
   standard depth. Where the GPU can't give a 0..1 clip range (WebGL2 without EXT_clip_control, GL without 4.5 /
   ARB_clip_control, D3D11 under Wine) ARC still draws reversed with float depth, but the gain is smaller (about 10x
   instead of 1000x; ARC logs it). arc_view_proj, arc_camera_view_proj and the screen helpers keep the standard
   matrix in every mode. The software renderer stores float depth in both modes.

```c
enum { ARC_DEPTH_STANDARD = 0, ARC_DEPTH_REVERSED = 1, ARC_DEPTH_REVERSED_INFINITE = 2 };
ARC_API int arc_depth_mode(void);    // the mode in effect (ARC_DEPTH_STANDARD / _REVERSED / _REVERSED_INFINITE)
ARC_API bool arc_depth_precise(void);    // reversed with a float buffer and a 0..1 clip range: the full precision gain
```

Replace the game's screen camera while drawing (NULL = the game's own again); render targets are untouched.
   Used by the `camera` agent command so screenshot scripts can frame any game.

```c
ARC_API void arc_set_camera_override(const arc_camera *c);
```

Hide all 2D drawing on the screen (HUD, text, UI, FPS overlay) - for page art and trailers. The `hud off` / `hud on`
   agent commands and --no-hud do the same. 2D drawn into render targets is kept (it may be part of the 3D world).

```c
ARC_API void arc_set_hud_hidden(bool hidden);
ARC_API bool arc_hud_hidden(void);
ARC_API void arc_set_camera(const arc_camera *c);
ARC_API arc_m4 arc_view_proj(void);
```
