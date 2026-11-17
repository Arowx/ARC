# Request from MechBattleArc: ground markers (tile highlights, rings, drop zones) as one feature

**Why.** The board in MOLTEN BREACH is covered in markers built from quads and discs by hand: move and attack tile
highlights, team rings under each mech (disc, rim, faction trim, chevron or diamond drawn on top), pulsing drop zones
with a "DROP ZONE" label, exit tiles, intent arrows. Getting them to sit on uneven ground, not z-fight, read over
bright grass, and stay visible through units (ON_TOP) took many rounds of tuning.

**Ask.** `arc_marker` drawn on the ground with one call each, batched:
- shapes: `ARC_MARK_TILE` (square, optional rounded corners), `_RING` (inner / outer radius), `_DISC`, `_ARROW`
  (from - to, width, head), `_CHEVRON`, `_DIAMOND`;
- style: `fill_rgb/alpha`, `edge_rgb`, `edge_width` (screen pixels), `pulse` (Hz) and `pulse_amount`, `dash`;
  `on_top` (draw through units, faint) and `depth_bias` handled by ARC;
- placement: x, z, size, yaw; sits on `arc_terrain` / a height callback, or a flat y;
- `label` text drawn above it facing the camera (optional), sharing the text renderer;
- shows in `frame` as `frame.markers` (shape, pos, rgb) so tests can check them.

**Game side (ready / to do).** `team_ring`, the tile highlights and drop zones in `src/main.c` would become a few
`arc_marker_*` calls; the readability tuning (team colours `0x38D8F0` / `0xFF3828`, faction trim) stays in the game.

## Acceptance

```arc-check
exe build/mechbattlearc_headless
args --no-session
do newbattle 5; autoplay; withdraw; step 30
expect count(frame.markers[*]) >= 3
expect frame.texts[*].text contains "DROP ZONE"
```
Described in words: a ring and a tile on a slope stay on the ground without z-fighting at grazing angles; an `on_top`
marker shows through a cube; the same picture on all renderers.

---

## Done in ARC 0.9.9 (15 Nov 2026)

`arc_draw_marker(&(arc_marker){ ... })` in draw(), one call per marker (feature macro `ARC_HAS_MARKERS`). Consecutive
markers batch into pooled meshes.

- Shapes: `ARC_MARK_TILE` (square; `inner` rounds the corners), `_RING` (`inner` = inner radius share), `_DISC`,
  `_ARROW` (from `x, z` to `x2, z2`, `size` = width, `head`), `_CHEVRON`, `_DIAMOND`.
- Style: `fill_rgb` / `fill_alpha`, `edge_rgb` / `edge_width` (screen pixels) / `edge_alpha`, `pulse` (Hz) and
  `pulse_amount`, `dash`. `on_top` adds a faint copy through units. Depth bias is handled inside ARC.
- Placement: `x, z, size, yaw`; on the ground through `height` (a callback, with `height_ud`), `terrain` (an
  `arc_terrain`), or a flat `y`; `lift` raises it a little.
- `label` ("DROP ZONE") is drawn as text above the marker (`label_rgb`, `label_height`, `label_scale`), so it is in
  `frame.texts`.
- `frame.markers` lists each marker (shape, position, colours), so your arc-check block works as written.

Readability tuning stays in the game, as you said: team colours and faction trim are just `fill_rgb` / `edge_rgb`.
A team ring is a `_RING` plus a `_CHEVRON` or `_DIAMOND` on top.

Acceptance:
- `tests/board_check.md` (in `build test`): 9/9 on `samples/board` (marker count, three rings, a chevron, "DROP ZONE"
  in `frame.texts`, plus board commands).
- Post test on the software renderer, GL and D3D11 (standard and reversed depth): a ring and a tile on a slope seen
  at about 16 degrees are solid, with no z-fighting holes; an `on_top` ring shows through a cube.
- Your own check (`newbattle 5; autoplay; withdraw`) needs the game to switch to the markers first.
