# Request from HOSTLESS: planar reflections for polished floors (ARC 0.8.4)

## What the game wants
HOSTLESS has a floor-polishing robot. Floor cells it polishes switch to a mirror finish, and the Executive and
Medical floors start fully polished. Allan wants them to look "highly reflective": the guards, the BULWARK walker,
the ghost's glow, desks, neon trim and light cones mirrored in the floor, like a wet or freshly polished office at night.

## What it does today
The floor is two meshes, dull and polished (rebuilt when the polisher flips a cell). Polished cells use
`arc_material { .pbr = true, .metallic = 0.45f, .roughness = 0.05f }`. That gives sharp highlights from the point
and spot lights, and the environment gradient (`arc_set_environment`). It reads as glossy, but nothing in the room
is mirrored. ARC has no way to reflect scene geometry today.

## What would make it easy
A single reflection plane (y = constant): enough for any game with flat floors, water or ice.

```c
/* Mirror the scene in a horizontal plane: meshes marked reflective show it, blurred by their roughness. */
ARC_API void arc_reflection_plane(float y, float strength, float scale);   /* scale: target size vs. the screen, e.g. 0.5; strength 0 = off */
ARC_API void arc_mesh_set_reflective(arc_mesh m, float amount);           /* 0..1; multiplied by vertex alpha, so per-cell masks work */
ARC_API void arc_mesh_set_reflected(arc_mesh m, bool on);                 /* leave a mesh out of the mirror pass (default on) */
```

- **The mirror pass:** the same camera reflected about the plane, with a clip plane at y so nothing below the
  floor shows, drawn into a render target at `scale` × the screen size. Opaque meshes and models (including
  skinned ones), lit as normal. A smaller shadow budget, or none, in this pass is fine.
  - Particles, trails, decals and UI can be skipped at first. Emissive and glowing meshes, and lights, matter most
    for the look.
- **Sampling it:** reflective meshes sample the target in screen space, perturbed by the normal map if there is
  one, and blurred by roughness (a few mip levels or a cheap blur). Fresnel would add more reflection at grazing
  angles. The result is mixed over the PBR result by `amount × vertex alpha`.
- **Per-cell control:** HOSTLESS would put each cell's polish in the vertex alpha. Grimy cells would be 0, and polished ones
  1. Then one floor mesh could replace the two, and a cell could fade in as the robot buffs it.
- **Cost and quality settings:** one extra scene pass at half resolution. A quality setting to turn it off, and web
  (WebGL2) support at least at a lower scale, would let jam games use it safely.

## Test case
A flat reflective floor, a lit cube and an emissive sphere above it. The mirrored image is the right way up, is cut
off at the floor line, and blurs as roughness rises. Nothing from below the plane appears. With
`arc_reflection_plane(..., 0, ...)` the frame matches today's output pixel for pixel.

## In HOSTLESS meanwhile
- Polished cells use the glossy PBR material above.
- A small light under each polisher's pad, so the floor catches a blue glow.
- A faint wet sheen on freshly polished cells.

When planar reflections land, the game would call `arc_reflection_plane(0, 0.6f, 0.5f)`, mark the floor
reflective, and write polish into the vertex alpha.

## Result (10 Nov, HOSTLESS on 0.8.7)
- In use: `arc_reflection_plane(0, 0.6, 0.5)` while any cell is polished (strength 0 otherwise, and when the player turns
  "Floor reflections" off in Options); the polished floor mesh is `arc_mesh_set_reflective(m, 1)` with each cell's
  vertex alpha fading over the last 12 s of its shine; the flat vision-cone fans are `arc_mesh_set_reflected(false)`.
- **WebGL2 checked** (Chrome, ANGLE/SwiftShader, emsdk build of HOSTLESS): reflections render (guards, a skinned drone,
  desks, racks and lamp glows mirrored in the polish), no shader errors or warnings in the console.
- Thank you. HOSTLESS's own stand-in (drawing bodies upside down under a see-through floor) is gone.
