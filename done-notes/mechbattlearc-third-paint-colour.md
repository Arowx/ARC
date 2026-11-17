# Request from MechBattleArc (Molten Breach): a third paint colour (`tint3` for the mask's blue channel)

**Why.** Allan wants each mech in three colours: two chassis colours and a joint colour, so each faction can put
complementary colours on different parts. 0.8.8's two-colour paint works well (thanks). The mask's blue channel is
"unused for now".

**Ask.** `uint32_t tint3` in `arc_mesh_style`. With a `paint_mask`, apply blue the same way as red and green, after
them: colour = mix(that, tint3 x (0.35 + luminance), B). `tint3` 0 = white, or "blue ignored when tint3 is 0", either
is fine. It should work on static and skinned meshes and in every renderer, like `tint2`.

**Game side (ready).** `tools/paint_masks.py` now writes blue = joints (the dark unsaturated gunmetal: hips, knees,
elbows, hydraulics) into every `assets/mechs/<body>.mask.png`. When `tint3` lands, the game adds a joint colour per
faction (e.g. Meridian blue / white / orange joints, Charter white / red / gold, Sable violet / silver / gold) through
`look_set_paint`.
