/* WebGPU spike (ARC 0.7.2 Phase E2): NOT part of ARC's renderer selection. A minimal stand-alone page that draws
   ARC-style instanced meshes with WebGPU, to measure what a real backend would take (see REPORT.md):
   - the meshes are ARC's own (meshes.bin from ref.c: arc_vertex, 36 bytes), the instances ARC's 144-byte layout
     (scene.h), uploaded once per frame and drawn with one drawIndexed per mesh batch (WebGPU has firstInstance, so
     no per-draw attribute re-pointing as on WebGL2);
   - the lighting is arc_gl.c's basic mesh shader (texture, tint, recolour, whiten, sun + ambient, emissive, rim, fog)
     in WGSL; no shadows, materials, point lights, skinning or effects;
   - depth: reversed-Z with depth32float, as ARC 0.7.2's ARC_DEPTH_REVERSED (WebGPU's clip range is 0..1 already);
   - after a few frames it copies the picture back (COPY_SRC canvas) for comparison with ARC's software renderer.
   Raw navigator.gpu through EM_JS (no Emscripten WebGPU port): see REPORT.md for why. */
#include "scene.h"
#include <emscripten/emscripten.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- the JavaScript side: device, pipeline, buffers, one render pass per frame ---- */
EM_JS(void, gpu_init, (int w, int h, int msaa), {
  /* offscreen (?offscreen): draw into a texture of our own instead of the canvas (headless Chromium + SwiftShader
     loses the device as soon as a WebGPU canvas is configured) */
  const S = Module.spk = { ready: false, meshes: [], w: w, h: h, msaa: msaa, frames: 0, offscreen: location.search.indexOf('offscreen') >= 0 };
  if (!navigator.gpu) { console.log('SPIKE error no navigator.gpu'); return; }
  (async () => {
    const adapter = S.adapter = await navigator.gpu.requestAdapter();   /* kept: Chrome drops the instance with it */
    if (!adapter) { console.log('SPIKE error no adapter'); return; }
    const dev = S.dev = await adapter.requestDevice();
    dev.addEventListener('uncapturederror', e => console.log('SPIKE error ' + e.error.message));
    dev.lost.then(i => console.log('SPIKE device lost: ' + i.reason + ' ' + i.message));
    console.log('SPIKE adapter ' + (adapter.info ? adapter.info.vendor + ' ' + adapter.info.architecture : '?'));
    if (S.offscreen) { S.fmt = 'rgba8unorm'; S.target = dev.createTexture({ size: [w, h], format: S.fmt, usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC }); }
    else {
      const canvas = Module.canvas; canvas.width = w; canvas.height = h;
      S.ctx = canvas.getContext('webgpu'); S.fmt = navigator.gpu.getPreferredCanvasFormat();
      S.ctx.configure({ device: dev, format: S.fmt, alphaMode: 'opaque', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });
    }
    const code = UTF8ToString(Module._spike_wgsl());
    const mod = dev.createShaderModule({ code });
    const f4 = (loc, off) => ({ shaderLocation: loc, offset: off, format: 'float32x4' });
    S.pipe = dev.createRenderPipeline({
      layout: 'auto',
      vertex: { module: mod, entryPoint: 'vs', buffers: [
        { arrayStride: 36, stepMode: 'vertex', attributes: [ { shaderLocation: 0, offset: 0, format: 'float32x3' }, { shaderLocation: 1, offset: 12, format: 'float32x3' },
                                                             { shaderLocation: 2, offset: 24, format: 'unorm8x4' }, { shaderLocation: 3, offset: 28, format: 'float32x2' } ] },
        { arrayStride: 144, stepMode: 'instance', attributes: [ f4(4, 0), f4(5, 16), f4(6, 32), f4(7, 48), f4(8, 64), f4(9, 80), f4(10, 96), f4(11, 112), f4(12, 128) ] } ] },
      fragment: { module: mod, entryPoint: 'fs', targets: [ { format: S.fmt } ] },
      primitive: { topology: 'triangle-list', frontFace: 'ccw', cullMode: 'back' },
      depthStencil: { format: 'depth32float', depthWriteEnabled: true, depthCompare: 'greater' },   /* reversed-Z */
      multisample: { count: msaa > 1 ? 4 : 1 } });
    S.ubuf = dev.createBuffer({ size: 192, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    S.ibuf = dev.createBuffer({ size: 144 * 4096, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    S.bind = dev.createBindGroup({ layout: S.pipe.getBindGroupLayout(0), entries: [ { binding: 0, resource: { buffer: S.ubuf } } ] });
    S.depth = dev.createTexture({ size: [w, h], format: 'depth32float', sampleCount: msaa > 1 ? 4 : 1, usage: GPUTextureUsage.RENDER_ATTACHMENT });
    if (msaa > 1) S.ms = dev.createTexture({ size: [w, h], format: S.fmt, sampleCount: 4, usage: GPUTextureUsage.RENDER_ATTACHMENT });
    S.ready = true;
  })();
});
EM_JS(int, gpu_ready, (void), { return Module.spk && Module.spk.ready ? 1 : 0; });
EM_JS(void, gpu_mesh, (int id, const void *v, int vbytes, const void *idx, int icount), {
  const S = Module.spk, dev = S.dev;
  const vb = dev.createBuffer({ size: vbytes, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
  dev.queue.writeBuffer(vb, 0, HEAPU8, v, vbytes);
  const ib = dev.createBuffer({ size: icount * 4, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST });
  dev.queue.writeBuffer(ib, 0, HEAPU8, idx, icount * 4);
  S.meshes[id] = { vb: vb, ib: ib, n: icount };
});
/* batches: (mesh, first instance, count) triples */
EM_JS(void, gpu_frame, (const void *inst, int ninst, const float *uni, const int *batches, int nb, const float *clear, int readback), {
  const S = Module.spk, dev = S.dev;
  dev.queue.writeBuffer(S.ibuf, 0, HEAPU8, inst, ninst * 144);
  dev.queue.writeBuffer(S.ubuf, 0, HEAPU8, uni, 192);
  const tex = S.offscreen ? S.target : S.ctx.getCurrentTexture(), enc = dev.createCommandEncoder();
  const c = HEAPF32.subarray(clear >> 2, (clear >> 2) + 4);
  const pass = enc.beginRenderPass({
    colorAttachments: [ { view: S.ms ? S.ms.createView() : tex.createView(), resolveTarget: S.ms ? tex.createView() : undefined,
                          clearValue: { r: c[0], g: c[1], b: c[2], a: 1 }, loadOp: 'clear', storeOp: 'store' } ],
    depthStencilAttachment: { view: S.depth.createView(), depthClearValue: 0, depthLoadOp: 'clear', depthStoreOp: 'discard' } });
  pass.setPipeline(S.pipe); pass.setBindGroup(0, S.bind); pass.setVertexBuffer(1, S.ibuf);
  for (let b = 0; b < nb; b++) {
    const m = S.meshes[HEAP32[(batches >> 2) + b * 3]], first = HEAP32[(batches >> 2) + b * 3 + 1], count = HEAP32[(batches >> 2) + b * 3 + 2];
    pass.setVertexBuffer(0, m.vb); pass.setIndexBuffer(m.ib, 'uint32');
    pass.drawIndexed(m.n, count, 0, 0, first);
  }
  pass.end();
  let rb = null;
  if (readback) {
    rb = dev.createBuffer({ size: S.w * S.h * 4, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });   /* 320 * 4 is a multiple of 256 */
    enc.copyTextureToBuffer({ texture: tex }, { buffer: rb, bytesPerRow: S.w * 4 }, [S.w, S.h]);
  }
  dev.queue.submit([enc.finish()]);
  S.frames++;
  if (rb) dev.queue.onSubmittedWorkDone().then(() => rb.mapAsync(GPUMapMode.READ)).then(() => {
    const px = new Uint8Array(rb.getMappedRange()).slice(); rb.unmap();
    if (S.fmt === 'bgra8unorm') for (let i = 0; i < px.length; i += 4) { const t = px[i]; px[i] = px[i + 2]; px[i + 2] = t; }
    window.spikeRGBA = Array.from(px); console.log('SPIKE done ' + S.w + 'x' + S.h + ' format ' + S.fmt + ' frames ' + S.frames);
  }).catch(e => console.log('SPIKE error readback ' + e));
});

/* ---- WGSL: arc_gl.c's MESH_VS (no skinning, no outline hull) and MESH_FS's basic path ---- */
static const char *WGSL =
    "struct U { vp: mat4x4f, eye: vec4f, light_dir: vec4f, light_col: vec4f, ambient: vec4f, fog_col: vec4f, fog: vec4f, rim: vec4f };\n"
    "@group(0) @binding(0) var<uniform> u: U;\n"
    "struct VI { @location(0) pos: vec3f, @location(1) nrm: vec3f, @location(2) col: vec4f, @location(3) uv: vec2f,\n"
    "  @location(4) m0: vec4f, @location(5) m1: vec4f, @location(6) m2: vec4f, @location(7) m3: vec4f,\n"
    "  @location(8) tint: vec4f, @location(9) par: vec4f, @location(10) ext: vec4f, @location(11) fx0: vec4f, @location(12) fx1: vec4f };\n"
    "struct VO { @builtin(position) pos: vec4f, @location(0) wp: vec3f, @location(1) n: vec3f, @location(2) col: vec4f, @location(3) uv: vec2f,\n"
    "  @location(4) @interpolate(flat) tint: vec4f, @location(5) @interpolate(flat) par: vec4f, @location(6) @interpolate(flat) ext: vec4f };\n"
    "@vertex fn vs(i: VI) -> VO {\n"
    "  let M = mat4x4f(i.m0, i.m1, i.m2, i.m3); let wp = M * vec4f(i.pos, 1.0);\n"
    "  var o: VO; o.pos = u.vp * wp; o.wp = wp.xyz; o.n = mat3x3f(i.m0.xyz, i.m1.xyz, i.m2.xyz) * i.nrm;\n"
    "  o.col = i.col; o.uv = i.uv + i.fx1.xy; o.tint = i.tint; o.par = i.par; o.ext = i.ext; return o;\n"
    "}\n"
    "fn fog_f(d: f32) -> f32 { var f = 0.0; if (u.fog.z > 0.0) { if (u.fog.w > 0.0) { let k = u.fog.w * d; f = 1.0 - exp(-k * k); } else { f = clamp((d - u.fog.x) * u.fog.y, 0.0, 1.0); } } return f; }\n"
    "@fragment fn fs(i: VO) -> @location(0) vec4f {\n"
    "  let tx = vec4f(1.0);\n"   /* white: no texture in the spike */
    "  let c = i.col.rgb * tx.rgb; let lum = dot(c, vec3f(0.3, 0.59, 0.11));\n"
    "  var base = c * mix(vec3f(1.0), i.tint.rgb, i.par.z); base = mix(base, i.tint.rgb * (0.35 + lum), i.par.x);\n"
    "  let n = normalize(i.n); let diff = max(dot(n, u.light_dir.xyz), 0.0); let d = length(i.wp - u.eye.xyz);\n"
    "  var rim = 0.0; if (u.rim.r + u.rim.g + u.rim.b > 0.0) { rim = pow(1.0 - max(dot(n, normalize(u.eye.xyz - i.wp)), 0.0), u.rim.w); }\n"
    "  var lit: vec3f;\n"
    "  if (i.ext.z > 0.5) { lit = base * (1.0 + i.tint.w); } else { lit = base * (u.ambient.rgb + u.light_col.rgb * diff + i.tint.w) + u.rim.rgb * rim; }\n"
    "  lit = mix(lit, vec3f(1.0), i.par.y);\n"
    "  let f = fog_f(d);\n"
    "  return vec4f(select(mix(lit, u.fog_col.rgb, f), lit * (1.0 - f), i.ext.w > 0.5), 1.0);\n"
    "}\n";
EMSCRIPTEN_KEEPALIVE const char *spike_wgsl(void) { return WGSL; }

/* ---- the C side: what an ARC backend's render() would do with arc__frame_data ---- */
static spike_instance g_inst[SPIKE_N]; static int g_batches[2 * 3], g_nb, g_frame, g_msaa;
static float g_uni[48], g_clear[4];
static arc_m4 proj_rev(const arc_camera *c, float aspect) {   /* as ARC 0.7.2's camera_proj_rev */
    arc_m4 r; memset(&r, 0, sizeof r); float f = 1.0f / tanf(c->fov_y * 0.5f), zn = c->znear, zf = c->zfar;
    r.m[0] = f / aspect; r.m[5] = f; r.m[11] = -1.0f; r.m[10] = zn / (zf - zn); r.m[14] = zf * zn / (zf - zn);
    return r;
}
static void rgb(uint32_t c, float *o) { o[0] = (float)((c >> 16) & 0xFF) / 255.0f; o[1] = (float)((c >> 8) & 0xFF) / 255.0f; o[2] = (float)(c & 0xFF) / 255.0f; o[3] = 1; }
static bool load_meshes(void) {
    FILE *f = fopen("meshes.bin", "rb"); if (!f) { printf("SPIKE error no meshes.bin\n"); return false; }
    for (int m = 0; m < 2; m++) {
        int32_t vc = 0, ic = 0; if (fread(&vc, 4, 1, f) != 1 || fread(&ic, 4, 1, f) != 1) { fclose(f); return false; }
        arc_vertex *v = (arc_vertex *)malloc(sizeof(arc_vertex) * (size_t)vc); uint32_t *idx = (uint32_t *)malloc(4 * (size_t)ic);
        size_t a = fread(v, sizeof(arc_vertex), (size_t)vc, f), b = fread(idx, 4, (size_t)ic, f); (void)a; (void)b;
        gpu_mesh(m, v, (int)(sizeof(arc_vertex) * (size_t)vc), idx, ic); free(v); free(idx);
    }
    fclose(f); return true;
}
static void setup(void) {
    /* instances grouped by mesh (ARC's counting sort by mesh does the same), batches of (mesh, first, count) */
    spike_shape s[SPIKE_N]; int n = spike_shapes(s), k = 0;
    g_nb = 0;
    for (int m = 0; m < 2; m++) {
        int first = k;
        for (int i = 0; i < n; i++) if (s[i].mesh == m) spike_fill(&s[i], &g_inst[k++]);
        if (k > first) { g_batches[g_nb * 3] = m; g_batches[g_nb * 3 + 1] = first; g_batches[g_nb * 3 + 2] = k - first; g_nb++; }
    }
    arc_m4 view = arc_m4_look_at(SPIKE_CAM.eye, SPIKE_CAM.target, SPIKE_CAM.up), vp = arc_m4_mul(proj_rev(&SPIKE_CAM, (float)SPIKE_W / (float)SPIKE_H), view);
    memcpy(g_uni, vp.m, 64);
    g_uni[16] = SPIKE_CAM.eye.x; g_uni[17] = SPIKE_CAM.eye.y; g_uni[18] = SPIKE_CAM.eye.z;
    arc_v3 ld = arc_v3_norm(SPIKE_LIGHT_DIR); g_uni[20] = ld.x; g_uni[21] = ld.y; g_uni[22] = ld.z;
    rgb(SPIKE_LIGHT, g_uni + 24); rgb(SPIKE_AMBIENT, g_uni + 28); rgb(SPIKE_FOG, g_uni + 32);
    g_uni[36] = SPIKE_FOG_START; g_uni[37] = 1.0f / (SPIKE_FOG_END - SPIKE_FOG_START); g_uni[38] = 1; g_uni[39] = 0;
    rgb(SPIKE_RIM, g_uni + 40); g_uni[43] = SPIKE_RIM_POWER;
    rgb(SPIKE_CLEAR, g_clear);
}
static void frame(void) {
    static int state = 0;
    if (state == 0) { if (!gpu_ready()) return; if (!load_meshes()) { emscripten_cancel_main_loop(); return; } setup(); state = 1; }
    g_frame++;
    gpu_frame(g_inst, SPIKE_N, g_uni, g_batches, g_nb, g_clear, g_frame == 10);
    if (g_frame == 10) { printf("SPIKE drew %d instances in %d batches, msaa %d\n", SPIKE_N, g_nb, g_msaa); emscripten_cancel_main_loop(); }
}
EM_JS(int, page_wants_msaa, (void), { return location.search.indexOf('msaa') >= 0 ? 4 : 0; });
int main(void) {
    g_msaa = page_wants_msaa();
    gpu_init(SPIKE_W, SPIKE_H, g_msaa);
    emscripten_set_main_loop(frame, 0, 0);
    return 0;
}
