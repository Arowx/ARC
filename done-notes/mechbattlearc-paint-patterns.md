# Request from MechBattleArc: paint patterns drawn by the shader (no baked mask per pattern)

**Why.** MOLTEN BREACH gives each of 10 factions its own livery (chevron, splinter, stripes, diagonal split, dots,
half-and-half, alternating panels, chest, hazard, bold crosshatch). Today each pattern is a baked 512 px `paint_mask`
PNG per body: 55 files, about 6 MB of the 40 MB web download, rebuilt by a 400-line Python tool
(`tools/paint_masks.py`) that rasterises every UV island itself to learn each texel's body part (dominant skin joint),
bind-pose position and face axis. A new pattern means a new bake for every body and style.

**Ask.** Let `arc_mesh_style` describe the second-colour area procedurally, evaluated per pixel from the mesh's
bind-pose (object-space) position, so one mask (or none) serves every pattern:
- `arc_paint_pattern pattern` with a `kind`: `ARC_PAINT_STRIPES`, `_CHECKER`, `_DOTS`, `_SPLIT`, `_NOISE` (camo),
  plus `scale`, `width` (0..1 share), `angle` (radians, around the view-facing axis), `offset`.
  - STRIPES: bands along a direction in object space; with `cross` = 1 a second set at 90 deg (crosshatch).
  - CHECKER / DOTS: on each face's own plane (dominant normal axis), so squares stay square and dots round.
  - SPLIT: a plane through the object (`angle`) - the second colour on one side.
  - NOISE: value noise cells (camo), `seed`.
- `part_mask` (u8, bit per part): only these parts take the pattern. Parts from the skin: the dominant joint's
  top-level group (head, torso, arms, legs) - mixamo names are enough (`Head`, `Spine*`, `*Arm*`/`*Hand*`,
  `*Leg*`/`*Foot*`/`*UpLeg*`), others = torso.
- Optional `arc_mesh_style.armour_mask` (the existing paint_mask's R): pattern applies only where R is set, so trim
  and joints (G/B) keep working as now with `tint2` / `tint3`.
- Static and skinned meshes, all three renderers, batched per style like `paint_mask` today. With no pattern set,
  nothing changes (every existing picture identical).

**Game side (ready / to do).** Masks already carry R main armour, G trim, B joints, and the game keeps the
faction -> pattern table (`FAC_PATTERN` in `src/main.c`). With this the 55 PNGs become one mask per body (8 files)
and `paint_masks.py` drops to the armour/trim/joint split. Alternating panels (each armour plate a different colour)
can stay a baked mask - it needs the texture's panel lines.

## Acceptance

Described in words (ARC's post test): one skinned mech and one cube, each pattern kind drawn red/blue with
`part_mask` all and then legs-only; checker squares within 10% of square on two faces of the cube; DOTS round on a
face; SPLIT puts the second colour on exactly one side of the plane; the same picture on software, GL and D3D11; all
earlier pictures unchanged.

```arc-check
exe build/mechbattlearc_headless
args --no-session
do hud off; paintview 1; step 30
expect count(frame.meshes[*].items) >= 11
shot paint_patterns.png
```

---

## Done in ARC 0.10.1 (16 Nov 2026)

`arc_mesh_style.pattern` (an `arc_paint_pattern`) and `arc_mesh_style.armour_mask` (feature macro
`ARC_HAS_PAINT_PATTERNS`). It is drawn by the shader from the mesh's **bind-pose** position, so patterns don't swim on
moving limbs. It works on all three renderers, for static and skinned meshes; draws with the same pattern still
batch.

**Fields.**
- `kind`: `ARC_PAINT_STRIPES`, `_CHECKER`, `_DOTS`, `_SPLIT`, `_NOISE`.
- `rgb`: the pattern's colour (0 = `tint2`, when you use no mask). It is applied like paint, rgb × (0.35 +
  luminance), so shading and wear survive.
- `scale`: one repeat in model units (default 0.25).
- `width`: the share in the pattern colour (default 0.5; DOTS: the dot's area per cell).
- `angle`: radians.
- `offset`: in repeats; for SPLIT, in model units.
- `cross`: STRIPES only, adds a second set at 90 degrees (crosshatch).
- `seed`: NOISE only.

**The kinds.**
- **STRIPES:** bands across the model's front (x, y) plane, so `angle` turns them as seen from the front.
  `angle` 0 gives vertical bands.
- **CHECKER and DOTS:** each face uses its own plane (the axis its normal points along most), so squares stay
  square and dots round.
- **SPLIT:** the side of the line x·cos(angle) + y·sin(angle) > offset.
- **NOISE:** value-noise blobs.

**Parts.**
- `part_mask` takes the bits `ARC_PART_HEAD` / `TORSO` / `ARMS` / `LEGS` (0 = all).
- Each vertex's part comes from its strongest joint, using the names of the animation passed in `style.anim`: Head;
  Spine*; *Arm* / *Hand*; *Leg* / *Foot* / *UpLeg* / *Toe*; everything else is torso.
- A static mesh counts as torso.

**Armour mask.** With `armour_mask = true` and your `paint_mask`, the pattern goes only where the mask is red; G and
B keep `tint2` and `tint3` as now. So your 55 PNGs can become one mask per body.

**Example:** `st.pattern = (arc_paint_pattern){ .kind = ARC_PAINT_STRIPES, .rgb = 0x2040E0, .scale = 0.2f, .angle =
0.8f, .part_mask = ARC_PART_LEGS };`

Acceptance (ARC's post test, the same on the software renderer, GL and D3D11; all earlier pictures unchanged):
- **Cubes:** checker cells measure 8×8 px on the front face and on the top face (of a cube turned to show it). Dots
  are 10×10 px. SPLIT has 0 pixels on the wrong side. Crossed stripes and noise show both colours.
- **Skinned "mech"** (boxes on Head, Spine1, LeftArm, RightForeArm, LeftUpLeg and RightFoot joints):
  - stripes on every part with `part_mask` 0;
  - with `ARC_PART_LEGS` only, the legs are striped and the torso and head have none.

Your arc-check block (`paintview 1 ... expect count(frame.meshes[*].items) >= 11`) depends on the game's paintview
switching to patterns.

Alternating panels stay a baked mask, as you said.
