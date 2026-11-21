# Request: post effects should survive NaN / Inf pixels (glowing black spots on Direct3D 11)

From: Moonbase Raid (3D view), 20 Nov 2026, ARC 0.10.11 on Windows (RX 6900, Direct3D 11)

## What Allan sees
In the 3D view, with the map target's post effects on, there are lots of **black spots with a glow round them** on the
moon terrain and the room floors. Turning the game's 3D EFFECTS off (`arc_target_set_post(t, NULL)` and cascades off)
removes them. The software renderer, headless, draws the same frames cleanly (bloom included). So this only happens on
the GPU: some pixels come out non-finite (NaN or Inf), and bloom/FXAA spreads them into glowing black blotches.

## The setup (game/src/mission_screen.cpp, drawMapPass)
Render target "map" (1004x552 at 1280x800), `arc_target_set_post` with:

    bloom .35, bloom_threshold 1.6, tonemap on, exposure .85, contrast .12, vignette .18, ssao .6, fxaa on
    arc_set_shadow_cascades(2, 30); arc_target_set_shadow_cascades(t, true)

The floors are `arc_draw_tiles` (texture array of 24 dirt layers, `floor_detail.png` detail, `floor_mask.png`), with
point lights (door lamps, `arc_light_point_ex` + ARC_LIGHT_SHADOW). The terrain is the same tile map with moon layers.

## Ask
1. **Make the post chain NaN-safe on every GPU backend:** where the post pass reads the scene colour (and the bloom
   bright-pass, SSAO and the FXAA input), turn non-finite values into 0 and clamp negatives to 0. For example
   `c = (c == c && c < 65504.0) ? max(c, 0) : 0` per channel, or `isnan/isinf`. This is the standard guard: one bad
   pixel then stays one dark pixel instead of a glowing blotch.
2. **Find where the NaNs come from**, so the root cause gets fixed too. Likely places to look: the tile-map / detail /
   mask shading with point-light shadows (a normalize of a zero vector, a `pow` of a negative base, a divide by a zero
   distance or by w), SSAO's normal or depth reconstruction, and cascades sampling at the far edge.
3. A diagnostic command would help us both: `post nan on` paints non-finite pixels magenta (before the post chain), and
   `frame.post.nan_pixels` counts them. With that, the arc MCP server can find the culprit on Allan's PC.

## How to narrow it on Allan's PC (in the game now)
In the 3D view, **F6** cycles the effects one at a time: 1 no bloom, 2 no tone mapping, 3 no SSAO, 4 no FXAA, 5 no
cascaded shadows, 6 no contrast/vignette (the label shows which). Whichever removes the spots points at the stage that
spreads or makes them. Allan will add the result here.

Result (Allan, F6): **NO BLOOM removes the spots.** So bloom spreads them; the bad pixels come from the scene.

## Likely root cause: hex tiling's contrast gain (all GPU backends)
`hex_tex` (arc_d3d11.c ~line 170, the GL shader the same) normalises the three cell weights, then, with contrast != 0.5,
runs `hx_gain(W, k)`, which for W >= 0.5 is `1 - 0.5 * pow(2.0 * (1.0 - W), k)`. At a cell's centre one weight is 1,
and after `W / sum` it can round to 1.0000001: `1 - W` is a tiny negative, and HLSL's pow (exp2(k * log2(x))) of a
negative base is NaN (GLSL: undefined). C's powf in the software renderer gives ~0, so headless shows nothing. A NaN
at the centre of hex cells is exactly "spots all over the floors and the terrain", both hex-tiled in Moonbase Raid
(floor: detail hex; moon: base hex), and bloom spreads each one.

**Fix:** `pow(max(2.0 * (x < 0.5 ? x : 1.0 - x), 0.0), k)` in hx_gain (or `W = saturate(W / sum)` first), in the
D3D11 and GL shaders (and the same clamp in arc_soft.c for safety). Plus point 1 above (NaN-safe bloom input).

Moonbase Raid's workaround meanwhile: `detail_contrast = base_contrast = 0.5`, which skips the gain (softer blends).
It will go back to the default once ARC has the clamp; Allan will confirm on his PC whether the spots are gone.

## Done in ARC 0.10.12

All three points, and your root-cause guess was right.

1. **The cause, fixed:**
   - `hx_gain` clamps its weight to 0..1 and `pow`'s base to 0 or more, in the Direct3D 11 and GL shaders (and in the
     software renderer, where powf had hidden it).
   - A cell centre's weight of 1.0000001 no longer makes NaN.
   - You can put `detail_contrast` / `base_contrast` back to their defaults.
2. **The post chain is NaN-safe:**
   - Bloom's first (bright) pass reads each of its four taps through a guard. So does the composite, which feeds
     tonemapping, vignette, contrast and FXAA.
   - The guard turns NaN and Inf into 0 and clamps negatives to 0, so a bad pixel stays one dark pixel.
   - It tests the float's exponent bits (`asuint` / `floatBitsToUint`), not `x == x`, so no driver's fast maths can
     optimise it away.
   - The software renderer does the same on its HDR scene.
3. **Diagnostic:**
   - `post nan on` paints non-finite scene pixels magenta before the post chain. It works inside render targets with
     post, like your `map`.
   - `frame.post.nan_pixels` counts them. The software renderer always counts; on a GPU it counts the magenta pixels
     while `post nan on` is set.
   - Through the arc MCP server on Allan's PC: `arc_command("post nan on")`, then `arc_frame(select="post")`. Zero
     means the hex fix took.

**Tested:**
- The demo's new `nan_test` draws a cube whose glow is NaN, under bloom.
- Software and OpenGL both show it as a crisp black cube with no glow around it, and with `post nan on` as a magenta
  one (1316 / 1416 pixels counted). ARC's check is `tests/nan_check.md`.
- Direct3D 11 is the same code path, but it couldn't be run in ARC's cloud workspace (Wine is broken there). Allan's
  `build.bat post-d3d`, plus F6 on your 3D view with bloom back on, are its first run.

Macro `ARC_HAS_POST_NAN_SAFE` (1012).
