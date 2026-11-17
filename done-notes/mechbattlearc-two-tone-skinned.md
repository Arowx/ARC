# Request from MechBattleArc (Molten Breach): styled skinned meshes, two-colour paint and outlines

**Why.** Players can't easily tell sides apart (feedback, 12 Nov). The game now uses team-coloured discs and icons,
but the mechs themselves can only be tinted with one colour, because `arc_draw_mesh_posed(m, model, tint, anim,
pose)` takes a single tint. `arc_mesh_style` already has the tools: `recolour`, `outline` / `outline_rgb`,
`xray` / `xray_rgb`, `emissive` and `whiten` for hit flashes. None of them reach skinned meshes.

**Asks.**
1. `ARC_API void arc_draw_mesh_posed_styled(arc_mesh m, arc_m4 model, arc_anim a, const arc_pose *pose,
   const arc_mesh_style *st);`: the posed draw with the whole style, at least `tint`, `emissive`, `recolour`,
   `outline` / `outline_rgb`, `xray` / `xray_rgb`, `transparency` and `depth`.
2. Two-colour paint through a mask: `arc_texture paint_mask` and `uint32_t tint2` in `arc_mesh_style`. The mask uses
   the mesh's UVs:
   - red = how much of the primary colour (`tint`),
   - green = how much of the secondary colour (`tint2`),
   - blue = unused for now; areas with neither (black) keep the texture's own colour (gunmetal joints, glass).
   - Colour = lerp(texture, tint x (0.35 + luminance), R), then lerp(that, tint2 x (0.35 + luminance), G), as
     `recolour` already does for one colour, so the painted shading and wear survive.
   - `paint_mask` {0} = off, as now.

**Game side (ready once these land).** The repaint tool (`tools/repaint_mechs.py`) already splits each mech's
texture into armour panels, trim and gunmetal recesses. It will write `assets/mechs/<frame>[-style].mask.png`
(R panels, G trim) next to each model, and the game will draw every faction in its two colours (e.g. Halcyon gold
over navy, the Drift red over black), with an outline in its team colour. Static meshes would benefit too: pylons
in owner colours, for example.
