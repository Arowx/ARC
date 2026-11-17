# Split-screen (ARC 0.10.2)

Two or more players on one PC, each with their own view of the same world. Check for it with
`#if defined(ARC_HAS_VIEWPORTS)`.

## Drawing

```c
static void draw(float alpha) {
    int w = arc_width(), h = arc_height();
    for (int p = 0; p < 2; p++) {
        arc_viewport_begin(p * w / 2, 0, w / 2, h);   /* left half, then right half */
        arc_camera cam = arc_cam_rig_camera(&player[p].rig);
        arc_set_camera(&cam);
        draw_world();          /* the same code as single-player */
        draw_hud(p);           /* arc_width() / arc_height() are the half's size here; (0, 0) is its top-left */
        arc_viewport_end();
    }
    arc_draw_rect((float)(w / 2) - 1, 0, 2, (float)h, 0x000000);   /* a divider, on the window */
}
```

- Inside a viewport everything works in its own pixels: `arc_width`, `arc_height`, `arc_aspect`,
  `arc_target_width`, 2D, text, `arc_safe_area`, `arc_mouse`, `arc_world_to_screen` and `arc_screen_ray`.
- Each viewport gets its own sky, light tiles, sun shadow cascades fitted to its camera, light shadows, planar
  reflection and the post effects set with `arc_set_post` (SSAO, bloom, FXAA, vignette and the rest).
- **Lights** added with `arc_light_point` / `arc_light_spot` inside a viewport light only that viewport. Lights added
  before the first viewport light every viewport (and the window). The frame's 4 shadowed lights are shared between
  the viewports, so a shadowed light added in both views uses 2 of them.
- Render targets (a minimap, say) can be drawn into inside a viewport. Viewports can't nest, and must start outside
  render targets.
- Anything drawn after `arc_viewport_end` goes on the window as usual, over the viewports: dividers, a shared score,
  a pause menu.
- A frame with no viewports draws exactly as before.
- Up to `ARC_MAX_VIEWPORTS` (4) a frame. 2 side by side, 2 above each other, or 4 in a grid all work; the
  rectangles may also overlap (picture-in-picture: the last drawn is on top).
- On D3D11 with MSAA on, viewports are drawn without it.

## Camera rigs

Keep one `arc_cam_rig` per player. Set `rig.aspect` to the viewport's width / height (for a TOPDOWN rig with
`bounds`, which otherwise uses the window's shape), update each rig in `tick`, and set its camera inside its
viewport in `draw`.

## UI built in tick

`arc_ui_*` widgets are made in `tick`. Wrap each player's widgets in `arc_viewport_begin(x, y, w, h)` /
`arc_viewport_end()` there too: outside a frame this draws nothing by itself, but `arc_width`, `arc_mouse`,
`arc_safe_area` and the anchored panels (`arc_ui_begin_anchored`) then work in that viewport's pixels, and the
widgets are drawn in it.

## Picking

In `tick`, `arc_screen_ray(arc_mouse(), &o, &d)` goes through whichever viewport of the last frame is under the
mouse, with that viewport's camera. `arc_viewport_at(px, &local)` says which one (or -1), and
`arc_viewport_world_to_screen(i, p, &px)` places a world point in window pixels through viewport `i`'s camera.

## Players and gamepads

ARC 0.10.5 reads up to `ARC_MAX_PADS` (4) gamepads (`#if defined(ARC_HAS_PADS)`). Pad N keeps its number while it
stays connected: on Windows it is the XInput player number (the light on the pad), on Linux the order pads were found
(a new pad takes the lowest free number), on the web the browser's gamepad index.

```c
/* "pad N drives player N": each player's buttons and stick from their own pad */
for (int p = 0; p < 2; p++) {
    arc_v2 move = { arc_pad_axis_of(p, ARC_AXIS_LX), arc_pad_axis_of(p, ARC_AXIS_LY) };
    if (arc_pad_pressed(p, ARC_PAD_A)) jump(p);
    if (!arc_pad_is_connected(p)) show_hint(p, "connect a pad (or use the keyboard)");
}
/* or "press A to join": hand out player slots in the order pads ask */
arc_key b; int pad = arc_pad_any_pressed(&b);
if (pad >= 0 && b == ARC_PAD_A && !pad_has_player(pad)) add_player(pad);
```

- The `ARC_PAD_*` keys with `arc_key_down` and `arc_pad_axis` still read the **first** connected pad, so a one-player
  game is unchanged. A keyboard player and pad players mix freely: keep each player's binding (keyboard, or pad N) in
  one place.
- Replays record every pad (replay format 3; format 2 replays still play, their pad as pad 0).
- Agents: `pad N connect`, `pad N press A`, `pad N release A`, `pad N axis LX 0.5`, `pad N disconnect`; `dump` lists
  the connected `pads` with the buttons held and the left stick.
- Online rollback matches send one input per machine (`arc_app.net_input`); several local players on one machine in an
  online match is a game-side packing choice (put each local pad's input into that machine's input).

## Agents

- `frame` adds a `viewports` list: each one's `rect` (x, y, w, h in window pixels), its camera's `eye` and `dir`,
  and its meshes by name with `count` and `on_screen`. Text drawn in a viewport is listed at its window position.
- The `camera` and `cam rig` overrides change only the window's own camera, not the viewports'.
