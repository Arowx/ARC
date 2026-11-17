# Request from MechBattleArc: `sheet` comes out blank in our game (no meshes, no labels)

**Why.** `paintview 1` calls `arc_sheet_add(mesh, model, &style, label)` for its 11 mechs (skinned meshes,
`arc_draw_mesh_posed_styled` with paint mask, pattern, outline). `sheet out.png 4 cell 300x340 bg 50647A` prints
11 cells, all `on_screen: true`, but the PNG is the flat background only (plus a white block in the empty 12th
cell): no meshes and no labels. Same with `style = NULL`, with a plain style, and with a plain style + anim + pose.
ARC's own review check (primitives) passes, so it is probably something our draw sets: post-processing (bloom,
exposure, contrast), a sky, our own camera and light set in draw, `arc_set_post`, or skinned meshes.

Repro (headless, software renderer):
```
mechbattlearc_headless --headless --no-session --size 1600x1000 --cmd "hud off; paintview 1; step 20; sheet s.png 4 cell 300x340 bg 50647A; quit"
```

**Ask.** Sheets draw with ARC's own clean state (no game post, its own camera, light and sky, depth cleared per cell)
whatever the game set this frame, and skinned meshes draw posed (or in bind pose) in their cell; labels always show.

## Acceptance

```arc-check
exe build/mechbattlearc_headless
args --no-session --size 1600x1000
do hud off; paintview 1; step 20
sheet review_sheet.png 4 cell 300x340
```
Described in words: each cell has more than 10% non-background pixels and its label text below.

## Done in ARC 0.10.6

Cause: `hud off`. With the HUD hidden, ARC drops every 2D draw on the screen, and a sheet puts each cell on the screen
as 2D (the cell's picture and its label) before reading it back. So you got the background (from the sky or clear)
and nothing else. ARC's own review check never hid the HUD. I reproduced it in ARC's review fixture with
`hud off; sheet ...`.

- Sheet frames now draw with ARC's own state whatever the game set this frame: HUD shown, and no post, fog, rim light,
  environment, sun shadows, reflection plane, camera override, world mask or cutaway. Everything comes back afterwards,
  so `hud off` is still off after the sheet. Each cell already had its own camera, light and cleared depth.
- Skinned meshes draw with the pose in the style you pass, as before.
- Empty cells (your 12th) are the background colour now, not transparent (that was the white block).
- `sheet` reports each cell's `filled` and `label_ink` (the share of the picture and of the label row unlike the
  background). `arc_check` reads them, and has new `min(...)` / `max(...)`, so your acceptance can say:

```arc-check
exe build/mechbattlearc_headless
args --no-session --size 1600x1000
do hud off; paintview 1; step 20
sheet review_sheet.png 4 cell 300x340
expect min(sheet.cells[*].filled) > 0.1
expect min(sheet.cells[*].label_ink) > 0.01
```

`tests/review_check.md` now makes a sheet with the HUD on and one with it off, and checks every cell of both.
