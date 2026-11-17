# WebGPU spike (ARC 0.7.2, Phase E2)

**Recommendation: later, not now.** A WebGPU backend for ARC is feasible and the spike shows the core maps over
cleanly, but it would be about 1,900 lines (more than `arc_gl.c`), a fourth copy of every shader formula, and a
second web backend to keep (WebGL2 stays as the fallback for years). Today it would buy ARC's small itch.io games
little that WebGL2 + reversed-Z doesn't already give. Revisit when ARC wants GPU compute (GPU particles, culling,
skinning) or when Firefox (Linux, Android) and Chrome on Linux ship WebGPU by default.

## What was built

`experiments/webgpu/` is a stand-alone page, **not** wired into ARC's renderer selection:

| File | Lines | What |
|---|---|---|
| `spike.c` | 176 | the experiment: about 80 lines of JavaScript (EM_JS: device, pipeline, buffers, one render pass per frame, read-back), 26 lines of WGSL, about 50 lines of C (scene set-up, matrices, batches, main loop) |
| `scene.h` | 48 | the test scene, shared with the reference: 24 shapes on a floor, filled into ARC's 144-byte instance layout exactly as `arc_draw_mesh_styled` fills `arc__instance` |
| `ref.c` | 33 | the reference: ARC itself (software renderer) draws the same scene and writes the meshes it made (`arc_vertex`, 36 bytes, normals worked out by `arc_mesh_create`) to `meshes.bin` |
| `run_spike.js`, `compare.py`, `shell.html`, `build.sh` | 15 + 9 + 7 + 19 | headless Chromium runner, picture comparison, page, build |

The spike draws ARC-style instanced meshes: the mesh vertex buffer at slot 0 (ARC's `arc_vertex`), one instance
buffer at slot 1 (the 144-byte `arc__instance`: 9 `float32x4` attributes, as in `arc_gl.c`), one `drawIndexed` per
mesh batch using WebGPU's `firstInstance` (no per-batch attribute re-pointing as WebGL2 needs). The fragment shader
is `arc_gl.c`'s basic mesh path translated to WGSL: white texture, vertex colour, tint multiply, recolour, whiten,
sun + ambient, emissive, rim, linear/exp fog. Depth is reversed-Z with `depth32float` and `greater`, the same matrix
as ARC 0.7.2's `ARC_DEPTH_REVERSED` (WebGPU's clip range is 0..1, so reversed-Z needs nothing else). Optional 4x MSAA
(`?msaa`). Build and run: `./build.sh run`.

### Why raw `navigator.gpu` through EM_JS, not Emscripten's WebGPU port

This emsdk (Emscripten 4.x, `6.0.13`) no longer has `-sUSE_WEBGPU` (removed in 4.0.18); the replacement is
`--use-port=emdawnwebgpu`, which I tried:
- it downloads a pinned Dawn release from GitHub at build time (`emdawnwebgpu_pkg-v20260423...zip`): an external
  dependency fetched by the build, against ARC's "no external libraries, one compiler call" rule;
- it **requires linking with `em++`** ("emdawnwebgpu requires C++"): ARC and many ARC games are C11;
- `webgpu.h` is a large C API with futures/callbacks for the asynchronous parts (adapter, device, map) that ARC's
  frame loop would have to pump.

EM_JS keeps the web platform as it is (`plat_web.c` already talks to the browser this way), the JavaScript only does
what ARC needs, and the call count is small: with instancing a frame is a few calls (upload instances, uniforms, the
batch list), so a real backend would pass a compact command list once per frame rather than call per draw.

## Did it run?

**Yes, with one sandbox limit.** Headless Chromium 153 (`/home/claude/arcdev/webrun`'s Playwright) with
`--enable-unsafe-webgpu --use-webgpu-adapter=swiftshader --enable-unsafe-swiftshader` gives a WebGPU adapter
(`google swiftshader`, a fallback adapter) and device. Configuring a **WebGPU canvas** in this headless set-up
destroys the device at once ("A valid external Instance reference no longer exists"), even in a plain JavaScript
page with no ARC code, with every flag combination tried (Vulkan/SwiftShader, ANGLE, `--disable-gpu-compositing`).
So the spike can also draw into its own texture (`?offscreen`), which works; a real browser uses the canvas path
(written, not testable here).

Result against ARC's software renderer (320 x 180):

| | mean difference | pixels > 2 apart | pixels > 16 apart |
|---|---|---|---|
| WebGPU, 1 sample | 0.33 / 255 | 0.56% | 0.54% (edges: rasterisation rules) |
| WebGPU, 4x MSAA | 1.02 / 255 | 3.4% | 2.8% (antialiased edges vs the aliased reference) |

The lighting matches ARC's formula away from edges. Output size: `spike.wasm` 43 KB (18 KB of it the embedded
meshes, plus stdio), `spike.js` 67 KB unminified / 19 KB gzipped, almost all Emscripten runtime.

## How big would a full backend be?

ARC today: `arc_gl.c` 1,237 lines (GLSL about 330 of them), `arc_d3d11.c` 1,127 (HLSL about 300). Estimate for an
`arc_webgpu.c` (JavaScript in EM_JS + WGSL + C), by feature, scaled from the spike's measured pieces:

| Feature | JS | WGSL | C | Notes |
|---|---|---|---|---|
| Device, adapter, **asynchronous start**, canvas, resize, device lost | 90 | | 30 | ARC's web start must wait for the adapter before `init` |
| Frame: passes and targets, encoders, a C -> JS command list | 120 | | 80 | one render pass per ARC pass; load/clear/store per pass |
| Meshes (vertex, index, skin buffers), update, destroy | 40 | | 20 | |
| Textures, updates, **mip generation** (no `generateMipmap`: a blit chain), sampler cache | 110 | 15 | 20 | |
| Compressed textures (`texture-compression-bc`) | 30 | | | mobile GPUs lack BC: ARC's CPU decode already covers it |
| Render targets (colour + depth, sampled later) | 50 | | | |
| **Pipeline cache** (blend x depth state x cull/flip x MSAA x format) | 90 | | | pipelines are immutable: every ARC__Z_* / blend combination is a pipeline |
| Instancing and batches (`firstInstance`) | 30 | | | simpler than GL |
| Mesh shader: skinning, dissolve, outline hull, hologram, normal maps, PBR, environment, point/spot lights, fog, soft particles | | 200 | | GLSL MESH_VS + MESH_FS are about 170 lines |
| Skinning (bones in a storage buffer) | 15 | | | simpler than the bone texture |
| Materials (bind group per material, cached) | 60 | | | |
| Lights | 10 | | | |
| Shadows and cascades (atlas, comparison sampler, depth-only pipeline, viewports) | 70 | 45 | | stay standard depth |
| Post chain: bloom and tilt chains, SSAO, composite (FXAA, LUT, CRT, pixelate, aberration, scanlines, vignette) | 160 | 160 | | a bind group per post pass |
| 2D batches (4 blends, scissor, plain quads) | 60 | 15 | | |
| Lines and immediate triangles | 40 | 10 | | |
| Sky | 15 | 12 | | |
| Particles | 0 | | | the mesh path |
| Soft-particle depth copy (copyTextureToTexture; with MSAA a sample-0 shader pass: WebGPU has no depth resolve) | 50 | 10 | | |
| MSAA (colour resolve; depth for SSAO / soft particles) | 40 | | | |
| Reversed-Z | 5 | | | native 0..1 clip range |
| **Screenshots / `read_pixels`** (`mapAsync` is asynchronous) | 50 | | 40 | ARC's `shot` and `compare` read back synchronously today: on WebGPU they would land a frame later |
| Shader-cache equivalent (`createRenderPipelineAsync` warm-up at load) | 30 | | | browsers cache compiled pipelines themselves |
| WebGL2 fallback selection in `plat_web.c` | | | 40 | both web backends ship |
| **Total** | **~1,165** | **~480** | **~230** | **about 1,900 lines (give or take 30%)** |

Plus test infrastructure: the post test needs an offscreen read-back path in headless runs (this sandbox can't
present a WebGPU canvas), about 100 lines.

## How the WGSL would be produced

1. **Hand-written (recommended if it is ever done).** About 480 lines, ported from the GLSL with the software
   renderer as the reference and `tests/test_post.c` as the judge (it already catches renderer drift in GL, D3D11
   and WebGL2). The cost is a fourth copy of every formula (C, GLSL, HLSL, WGSL) to keep in step.
2. **Generated from the GLSL at build time** (naga or Tint). ARC's GLSL is GLSL 330 / ES 300 with loose uniforms and
   combined samplers; the translators want Vulkan-style GLSL 450 (uniform blocks, explicit bindings, separate
   textures and samplers). That means restructuring the GL backend's shaders and adding a Rust or C++ tool to every
   build (and to the Windows set-up): against ARC's no-dependency rule.
3. **One shared shader source with macros** (a common subset expanded to GLSL, HLSL and WGSL) would also remove the
   GLSL/HLSL duplication, but WGSL's syntax (`let`, `var<uniform>`, `vec4f`, attributes on struct members) is far
   enough from C-like shading languages that the macro layer gets heavy. Only worth it as a separate project.

## Benefits over WebGL2

- **Compute shaders**: GPU particles, GPU culling and LOD, compute SSAO/bloom, GPU skinning: none possible in WebGL2.
- **Bind groups and immutable pipelines**: state is validated once, not per draw; much less CPU per batch, and no
  GL state machine to keep consistent (ARC's GL backend resets blend/depth/cull state around every pass).
- **firstInstance, storage buffers**: no per-batch attribute re-pointing (WebGL2 has no base instance), bones and
  lights in plain storage buffers instead of textures and uniform arrays.
- **0..1 depth and depth32float everywhere**: reversed-Z's full precision without depending on `EXT_clip_control`
  (about 95% of Chrome and 98% of Safari users have it, but only about 38% of Firefox users, per web3dsurvey).
- **Fewer driver issues**: one specified API on top of D3D12/Metal/Vulkan instead of WebGL's GL-ES-on-ANGLE layers;
  validation errors are deterministic and reported.
- Timestamp queries for real GPU timing in ARC's FPS overlay.

## Risks

- **Browser support (checked November 2026):** Chrome/Edge on Windows, macOS and ChromeOS since 113; Chrome on
  Android 12+ for ARM/Qualcomm/Intel GPUs (121), other GPU families still pending; Chrome on **Linux** only by default
  for Intel Gen12+ (144) and NVIDIA on Wayland (147), otherwise behind flags. Firefox on Windows since 141 and macOS
  (Apple Silicon 145, all Macs 147); **Linux and Android** Nightly only or behind a pref (Mozilla targets 2026).
  Safari 26 on macOS, iOS, iPadOS and visionOS; nothing on iOS 18 and older. So a WebGPU-only ARC web build would
  lose part of the Linux, Android and older-iPhone audience: WebGL2 must stay as the fallback.
- **Two web backends** to test and keep in step, each needing the post test in headless Chromium; and this sandbox's
  headless Chromium can't present a WebGPU canvas (offscreen only), so canvas-specific bugs would only show on real
  machines.
- **Asynchrony**: adapter/device creation and read-back are promises. ARC's synchronous `shot`, `compare` and replay
  screenshots (central to the agent workflow) would need a deferred-by-one-frame design on the web.
- **Size**: the JavaScript side grows by about 1,200 lines (plus the WGSL strings in the binary); still small next to a
  game's assets, but the web build stops being "GL glue only".
- **Toolchain**: Emscripten's own WebGPU support moved to an external, C++-only port in 2025; the raw-JS route avoids
  it but means ARC owns the bindings.

## Sources (browser support)

- [gpuweb implementation status wiki](https://github.com/gpuweb/gpuweb/wiki/Implementation-Status)
- [web.dev: WebGPU is now supported in major browsers (25 Nov 2025)](https://web.dev/blog/webgpu-supported-major-browsers)
- [web3dsurvey: EXT_clip_control](https://web3dsurvey.com/webgl/extensions/EXT_clip_control)
