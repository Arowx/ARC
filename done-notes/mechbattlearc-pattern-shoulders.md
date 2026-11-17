# Request from MechBattleArc: paint-pattern parts - count *Shoulder* joints as arms (or let the game map joints)

**Why.** Foundry's hazard livery is `ARC_PAINT_STRIPES` with `part_mask = ARC_PART_ARMS | ARC_PART_LEGS`. On our mixamo
rigs the big shoulder pads are skinned to `mixamorig:LeftShoulder` / `RightShoulder`, which the name rules put in the
torso, so the pads lost their stripes compared with the baked mask (where the game counted shoulders as arms).
Compare: `paintview 1` in MechBattleArc, Foundry (9th mech).

**Ask.** Either:
1. `*Shoulder*` / `*Clavicle*` count as `ARC_PART_ARMS`; or
2. `arc_paint_parts_rule(const char *pattern, int part)` (or a per-anim table) so a game can map joint names itself.
Nothing else should change for meshes without such joints.

**Game side.** `src/look.c` SHADER_PAT[PAT_HAZARD] already asks for arms + legs; no game change for option 1.

## Acceptance

Described in words: the skinned test mech with a box on a `LeftShoulder` joint gets stripes with `ARC_PART_ARMS`.

## Done in ARC 0.10.6

Both options:

1. Joints whose names contain `Shoulder` or `Clavicle` (case ignored) now count as `ARC_PART_ARMS`, so
   `mixamorig:LeftShoulder` / `RightShoulder` pads get Foundry's hazard stripes again with no game change.
2. `arc_paint_parts_rule(const char *pattern, int part)` maps joint names yourself: `*` matches any run, `?` one
   character, case ignored, e.g. `arc_paint_parts_rule("*Pauldron*", ARC_PART_ARMS)` once in init. Rules are tried
   before the built-in names, the latest first (up to 32). Part 0 removes a rule; `arc_paint_parts_rule(NULL, 0)` removes
   them all. `#ifdef ARC_HAS_PART_RULES` tells you it's there.

Meshes without such joints are unchanged; ARC's earlier pattern pictures are identical on soft, GL and D3D11.

Checked by a new picture look in `test_post` (look 92): ARC's skinned test mech with `part_mask = ARC_PART_ARMS`
(stripes on the arms, none on the torso), above three pads on joints named `mixamorig:LeftShoulder`, `clavicle_r` and
`Pauldron_L` (the last through a rule). All three pads get stripes on all three renderers. The self-test also covers
the name rules.
