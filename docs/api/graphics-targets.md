# Render targets, screenshots and frame timing

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Graphics** · [quick reference](../QUICK.md) · [full API](../API.md)


---- Render targets: draw into a texture, then draw that texture (UI canvases, minimaps, pixel-art scaling) ----

```c
ARC_API arc_texture arc_target_create(int w, int h, bool smooth);
```

Until arc_target_end, every draw (2D and 3D) goes into `target`. Targets nest. clear: fill with argb first
   (alpha byte 0 = transparent here). Each pass uses the camera and lights set while it is current.

```c
ARC_API void arc_target_begin(arc_texture target, bool clear, uint32_t argb);
ARC_API void arc_target_end(void);
ARC_API int arc_target_width(void);    // size of what is being drawn into now (the screen outside any target)
ARC_API int arc_target_height(void);
```

---- 0.10.2 split-screen: several views of one world in one window ----
   Between arc_viewport_begin and arc_viewport_end everything is drawn into that rectangle of the window (pixels,
   top-left origin; clipped to the window) as if it were the whole screen: set its camera with arc_set_camera, draw
   the world, then its HUD. Inside a viewport arc_width / arc_height / arc_aspect, arc_target_width, 2D, UI,
   arc_safe_area, arc_mouse, arc_world_to_screen and arc_screen_ray all work in the viewport's own pixels (0,0 = its
   top-left), so HUD code doesn't change. Each viewport gets its own sky, light tiles, light shadows, sun shadow
   cascades fitted to its camera, planar reflection, and the post effects of arc_set_post (SSAO, bloom, FXAA, ...).
   A frame with no viewports draws exactly as before. Viewports can't nest, and must start outside render targets
   (targets can be used inside one). Up to ARC_MAX_VIEWPORTS a frame. The `camera` / `cam rig` overrides change only
   the window's own camera. On D3D11 with MSAA the viewports are drawn without it.
   Between frames (in tick) arc_screen_ray(px) with a window pixel picks through the viewport under px (last frame's
   viewports and cameras); arc_viewport_at tells which one that is. Players and pads: see docs/split-screen.md.

```c
#define ARC_MAX_VIEWPORTS 4
ARC_API void arc_viewport_begin(int x, int y, int w, int h);
ARC_API void arc_viewport_end(void);
ARC_API int arc_viewport_current(void);    // index of the viewport being drawn (in begin order), -1 outside
ARC_API int arc_viewport_count(void);    // viewports this frame so far (in draw), else last frame's
ARC_API bool arc_viewport_rect(int index, int *x, int *y, int *w, int *h);    // its window rectangle (as above)
```

the viewport under window pixel px (-1: none); local (may be NULL) = px in that viewport's pixels

```c
ARC_API int arc_viewport_at(arc_v2 px, arc_v2 *local);
```

a world point to window pixels through viewport index's camera (false: behind it)

```c
ARC_API bool arc_viewport_world_to_screen(int index, arc_v3 p, arc_v2 *out);
```

Save the last rendered frame as PNG (works in every backend, including headless).

```c
ARC_API bool arc_screenshot(const char *path);    // .png, or .jpg/.jpeg (quality 90: a 1920x1080 shot is well under 1 MB)
```

Save a screenshot once the current frame has been drawn (from tick or draw; works headless and in
   immediate mode). Prints "ARC ok shot PATH" like the agent command.

```c
ARC_API void arc_screenshot_after_frame(const char *path);
```

0.10.1 contact sheets for art review: register meshes in draw() with arc_sheet_add (it only records while the
   `sheet` command collects, so it costs nothing otherwise); `sheet FILE.png [COLS] [cell WxH] [bg RRGGBB|transparent]
   [margin M]` draws each in its own cell, framed by arc_camera_fit from the front (a little above and to the right),
   under the same light, with its label underneath, and prints "ARC sheet {cells...}". Cells are at most the window's
   size. Posed meshes are framed by their bind-pose box. `shot FILE.png crop [MARGIN]` trims a screenshot to what
   differs from its corner colour.

```c
ARC_API void arc_sheet_add(arc_mesh mesh, arc_m4 model, const arc_mesh_style *style, const char *label);
ARC_API int arc_sheet_count(void);    // items registered while collecting
typedef struct { int draw_calls, instances, triangles, lines, quads, passes; float cpu_ms, frame_ms; float tick_ms; double avg_frame_ms; const char *backend; int dropped; int culled; int lod_instances[4]; int cells_visible; int static_draws; int textures, textures_compressed; long long texture_bytes; int vertices_2d, vertices_2d_max; } arc_gfx_stats;
ARC_API arc_gfx_stats arc_stats(void);
```

The engine's FPS overlay (top left): frame rate, frame-time percentiles and the worst frame of the last 10 s,
   frame/CPU/tick times, draw calls, triangles and a frame-time graph (green 60+, amber 30+, red below; it rescales
   so spikes fit, with a line at the 99th percentile). Also --fps, the `fps` command, or F3 with --dev /
   arc_app.dev_keys.

```c
ARC_API void arc_show_fps(bool on);
ARC_API bool arc_fps_shown(void);
```

Frame times (ms between rendered frames) over the last 10 seconds (600 frames). p99 is the time 1 frame in 100
   is slower than; low1_fps = 1000 / p99 ("1% low"). samples = 0 until a frame has been rendered.

```c
typedef struct { float avg, p50, p95, p99, max, max_1s, low1_fps; int samples; } arc_frame_times;
ARC_API arc_frame_times arc_get_frame_times(void);
```

Frame budget (0.7.1): where the time of a frame goes, against a target frame time. The FPS overlay shows it as bars
   (a part over the whole budget is red; when the frame is over, its biggest part is amber), `stats` prints it and
   `dump` has it as "budget". Parts: tick (game logic, all ticks of the frame), draw (draw(): building the draw queue,
   with the UI), sort (ARC batching the queue), submit (the renderer: on GPUs handing the frame over, in the soft
   renderer all the drawing), present (swap / vsync wait: where the GPU's time shows up), audio (ARC's audio work per
   tick and the mixer), other (the rest of the frame). zones: the game's own ARC_PROF zones (they are inside tick or
   draw), largest first. Values are smoothed over about half a second. Display only: nothing here changes the game.

```c
typedef struct { float budget_ms; float frame_ms; float tick_ms, draw_ms, sort_ms, submit_ms, present_ms, audio_ms, other_ms; int zone_count; const char *zone_name[8]; float zone_ms[8]; bool over; } arc_frame_budget;
ARC_API arc_frame_budget arc_get_frame_budget(void);
ARC_API void arc_set_frame_budget(float ms);    // e.g. 8.33 for 120 Hz; 0 = the default
```
