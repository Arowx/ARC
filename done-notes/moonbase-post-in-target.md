# Request from Moonbase Raid: post effects (and shadow cascades) for 3D drawn into a render target

**What happened.** Moonbase Raid's new 3D view draws the mission into the same render target as the 2D map
(`MissionScreen::drawMapPass`, a VW x VH target that the screen then places inside its frame next to the squad cards and
buttons). `arc_set_post` only applies to the screen pass (`arc_gfx.c`: a target pass gets `post` zeroed), and shadow
cascades follow the screen camera only. So the 3D view has no bloom, SSAO, FXAA or tonemapping, and only the single
shadow box.

**Why it matters.** The look Allan chose is modern clean 3D. Glowing door lights, droid eyes, reach edges and
room lights need bloom; SSAO would ground the furniture and units; FXAA smooths the edges on the web build.

**What would help (either):**
1. `arc_target_set_post(arc_texture target, const arc_post *p)`: post effects for one render target, run when that
   target's pass ends (NULL = none, as now). Cascades for a target pass that has a perspective camera.
2. Or a 3D viewport on the screen: `arc_set_viewport(x, y, w, h)` so the 3D picture (with the screen's post) fills only
   a rectangle of the screen and 2D draws on top as usual. The game would then skip the render target in 3D mode.

**Workaround meanwhile.** None needed: the view works without post effects.

**Test case.** Draw an emissive cube (`arc_draw_mesh_ex`, emissive 1) into a 640x360 target with
`arc_post{.bloom = 1}` set for it, draw the target to the screen, and check pixels just outside the cube are brighter
than with post off.
