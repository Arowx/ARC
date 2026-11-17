# Request from Droids: split-screen (several views of one world in one window)

**Why.** Two players on one PC (a couch transfer duel, co-op on a deck) need two cameras drawing the same scene side
by side or one above the other. ARC has render targets (`arc_target`), and Droids could draw each player's view
into a target and composite them. But every pass would then be done twice, by hand: shadows, post, SSAO, the
tiled-light grid, the HUD's screen size. Each game would have to rediscover which state is per view.

**Ask.** Viewports as a first-class part of a frame:

```c
void arc_viewport_begin(int x, int y, int w, int h);   /* pixels of the window; subsequent arc_set_camera / draws go here */
void arc_viewport_end(void);
```

- Each viewport gets its own camera, light-tile grid, SSAO, and post chain (or one post over the whole window,
  `arc_app` option).
- The sun's shadow cascades are fitted per viewport, or shared when the cameras are close.
- `arc_width()` / `arc_height()` / `arc_aspect()` and the 2D / UI calls work inside a viewport in its own pixels,
  so the HUD code doesn't change.
- `arc_world_to_screen` and mouse picking use the viewport under the mouse.
- Gamepads: input already has several pads. A note in `docs/` on mapping pad N to player N.
- `frame` lists each viewport's camera and meshes.
- **Must not change:** a frame with no viewport calls draws exactly as now (pixel-identical).

**Game side (to do).** Droids would offer "2 players (split)" on the title. Each player gets a droid and their own
rig, HUD and minimap. A transfer duel between them draws the cylinder from both ends.

## Acceptance

In words:
- The demo scene in two side-by-side viewports with different cameras: each half matches a full-window render of
  that camera at that size (within the usual tolerance), on all three renderers.
- Shadows and lights are correct in both halves.
- 2D text drawn at (10, 10) inside each viewport lands at each viewport's top-left.
- A frame without viewports is pixel-identical to 0.9.7.

## Done in ARC 0.10.2

`arc_viewport_begin(x, y, w, h)` / `arc_viewport_end()`, as asked. `#if defined(ARC_HAS_VIEWPORTS)`. Full guide:
`docs/split-screen.md`.

**What you get inside a viewport:**
- `arc_width` / `arc_height` / `arc_aspect`, `arc_target_width`, 2D, text, `arc_safe_area`, `arc_mouse`,
  `arc_world_to_screen` and `arc_screen_ray` are in the viewport's own pixels. Text at (10, 10) lands at each view's
  top-left (checked).
- Its own sky, light tiles, sun shadow cascades fitted to its camera, light shadows, planar reflection and the
  `arc_set_post` chain (SSAO, bloom, FXAA, ...). Minimap render targets work inside it.
- Lights added inside a viewport light only that one; lights added before the viewports light all of them.

**Acceptance:**
- One window-sized viewport gives the same picture as the plain screen with sky, cascades, a shadowed point light,
  a reflection, post and a minimap: exact on the software renderer and D3D11 (both depth modes), within 1 on GL.
- Two 320 x 180 viewports side by side each match a full-window 320 x 180 render of their camera exactly
  (`tests/split_check.md`, software renderer).
- Frames without viewports: every 0.10.1 picture is unchanged on all three renderers.

**Other parts of the ask:**
- Picking: in tick, `arc_screen_ray(arc_mouse(), ...)` goes through the viewport under the mouse with its camera.
  `arc_viewport_at`, `arc_viewport_world_to_screen`.
- `frame` lists `viewports` (rect, camera eye/dir, meshes with counts).
- HUDs made with `arc_ui_*` in tick: wrap each player's widgets in `arc_viewport_begin` / `end` there too.
- One `arc_cam_rig` per player; set `rig.aspect` to the view's width / height.

**Not done, or different from the ask:**
- **Gamepads:** the request says input already has several pads; it doesn't. ARC reads one pad (the first
  connected). Today two players means keyboard + pad (or both on the keyboard). Reading pad N for player N needs
  engine work (platforms plus the recorded input format); ask for it as its own request.
- **One post over the whole window** (the `arc_app` option): not added. Post runs per viewport.
- **Shared shadow cascades when the cameras are close:** not added. Each viewport fits its own; the 4 shadowed
  point/spot lights a frame are shared between the views.
- On D3D11 with MSAA on, viewports are drawn without MSAA.

## Update in ARC 0.10.5: several gamepads

The gamepad gap above is closed: ARC reads up to 4 pads (`#if defined(ARC_HAS_PADS)`).
- `arc_pad_down(pad, ARC_PAD_A)`, `arc_pad_pressed`, `arc_pad_released`, `arc_pad_axis_of(pad, ARC_AXIS_LX)`,
  `arc_pad_is_connected(pad)`, `arc_pad_count()`, and `arc_pad_any_pressed(&button)` for "press A to join".
- Pad N keeps its number while connected (Windows: the XInput player light). So "pad N drives player N" is one line
  per player; see `docs/split-screen.md`.
- The old `ARC_PAD_*` keys and `arc_pad_axis` still read the first pad, so existing code is unchanged.
- Agents: `pad 1 press A`, `pad 1 axis LX 0.5`, `pad 1 connect|disconnect`; `dump` lists the connected `pads`.
