/* ARC Direct3D 11 backend (Windows). Shaders are compiled at startup with the system's
 * d3dcompiler_47.dll (part of Windows 10/11), loaded at runtime — no SDK or extra DLLs to ship. */
#include "arc_internal.h"
#define COBJMACROS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

static const char *HLSL =
    "cbuffer F : register(b0) { column_major float4x4 vp; float4 eye, ld, lc, amb, fc, fog, rim, screen, misc; column_major float4x4 svp; float4 sh; float4 envt, envh, envb; float4 nlt; float4 tm; column_major float4x4 cvp[3]; float4 cp[3]; float4 shc; column_major float4x4 ivp; float4 soft; float4 clipp, rpar; float4 wm0, wm1, cut0, cut1, cut2; };\n"
    "cbuffer M : register(b2) { float4 m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11; float4 pat[4]; float4 pjm[32]; };\n"   /* 0.10.1 paint pattern (see arc__pattern) */   /* 0.9.9 m10.w: colour by layer; m11.x: this mesh is cut away */   /* 0.9.6 m9: splat layer scales; m10: sharpness, layers, on */   /* 0.8.9 m8: tint3 rgb, w = blue on */   /* 0.8.8 m7: paint tint2 rgb, w = on (mask t13) */
    "Texture2D ptex : register(t13); SamplerState smp4 : register(s4);\n"   /* 0.8.7 m6.x: the mesh's reflection amount; 0.8.8 m6.y: colour world scale */
    "Texture2D rtex : register(t12); SamplerState smp3 : register(s3);\n"   /* 0.8.7 the mirror pass's picture (clamped, mips) */   /* material (arc__mesh_rec.mat; m2.w = has one); 0.8.4 m3, m4: as GL's u_m3, u_m4 */
    "Texture2D ntex : register(t5); Texture2D otex : register(t6); Texture2D etex : register(t7); SamplerState smp2 : register(s2);\n"
    "Texture2DArray layers_tex : register(t18);\n"
    "Texture2D wmask : register(t19);\n"   /* 0.9.9 world mask (see arc__pass) */
    "static const float B4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };\n"   /* 0.9.6 splat layers (see ARC__MAT3_*; as GL's splat_tex) */
    "Texture2D dtex : register(t10); Texture2D mtex : register(t11);\n"   /* 0.8.4 detail map */
    "float pow5(float x) { float x2 = x * x; return x2 * x2 * x; }\n"
    "uint dn_h(int3 c) { uint h = ((uint)c.x * 73856093u) ^ ((uint)c.y * 19349663u) ^ ((uint)c.z * 83492791u); h = h ^ (h >> 13); h = h * 0x5bd1e995u; h = h ^ (h >> 15); return h; }\n"
    "float dn_c(int3 c) { return (float)(dn_h(c) & 0xFFFFFFu) / 16777215.0; }\n"
    "float dn_v(float3 p) {\n"
    "  float3 fl = floor(p), f = p - fl; int3 i = (int3)fl; f = f * f * (3.0 - 2.0 * f);\n"
    "  float a = lerp(dn_c(i), dn_c(i + int3(1, 0, 0)), f.x), b = lerp(dn_c(i + int3(0, 1, 0)), dn_c(i + int3(1, 1, 0)), f.x);\n"
    "  float c = lerp(dn_c(i + int3(0, 0, 1)), dn_c(i + int3(1, 0, 1)), f.x), d = lerp(dn_c(i + int3(0, 1, 1)), dn_c(i + int3(1, 1, 1)), f.x);\n"
    "  return lerp(lerp(a, b, f.y), lerp(c, d, f.y), f.z);\n"
    "}\n"
    "float pat_m(float3 p, float3 fn) {\n"   /* 0.10.1 a paint pattern at a bind-pose point (as the software renderer's pattern_at) */
    "  float k = pat[0].x, is = pat[0].y, w = pat[0].z, o = pat[0].w, ca = pat[1].x, sa = pat[1].y;\n"
    "  if (k < 1.5) { float t = (p.x * ca + p.y * sa) * is + o; float m = (t - floor(t)) < w ? 1.0 : 0.0;\n"
    "    if (pat[1].z > 0.5) { float t2 = (p.y * ca - p.x * sa) * is + o; if ((t2 - floor(t2)) < w) m = 1.0; } return m; }\n"
    "  if (k < 3.5) { float3 an = abs(fn); int ax = (an.x >= an.y && an.x >= an.z) ? 0 : (an.y >= an.z ? 1 : 2);\n"
    "    float u = ax == 0 ? p.z : p.x, v = ax == 1 ? p.z : p.y, ru = (u * ca - v * sa) * is + o, rv = (u * sa + v * ca) * is + o;\n"
    "    if (k < 2.5) { float c = floor(ru) + floor(rv); return c - 2.0 * floor(c * 0.5); }\n"
    "    float2 f = float2(ru - floor(ru), rv - floor(rv)) - 0.5; float r = sqrt(w / 3.14159265); return dot(f, f) < r * r ? 1.0 : 0.0; }\n"
    "  if (k < 4.5) return p.x * ca + p.y * sa > o ? 1.0 : 0.0;\n"
    "  return dn_v(float3(p.x * is + pat[3].x + o, p.y * is + pat[3].y, p.z * is + pat[3].z)) < w ? 1.0 : 0.0;\n"
    "}\n"
    "float dissolve_noise(float3 p) { return dn_v(p * 5.0) * 0.65 + dn_v(p * 11.0 + 17.0) * 0.35; }\n"
    "float3 ggx(float3 n, float3 V, float3 L, float NdotL, float NdotV, float rough, float3 F0) {\n"   /* GGX highlight times N.L */
    "  float3 H = normalize(L + V); float NdotH = max(dot(n, H), 0.0), VdotH = max(dot(V, H), 0.0);\n"
    "  float a = rough * rough, a2 = a * a, dd = NdotH * NdotH * (a2 - 1.0) + 1.0, D = a2 / (3.14159265 * dd * dd);\n"
    "  float k = (rough + 1.0) * (rough + 1.0) * 0.125, G = NdotV / (NdotV * (1.0 - k) + k) * NdotL / (NdotL * (1.0 - k) + k);\n"
    "  return D * G / (4.0 * NdotV * NdotL + 1e-4) * 3.14159265 * NdotL * (F0 + (1.0 - F0) * pow5(1.0 - VdotH));\n"
    "}\n"
    "Texture2D<float4> ltex : register(t14); Texture2D<uint> ltile : register(t15); Texture2D<uint> lidx : register(t16);\n"
    "SamplerComparisonState shadow_smp : register(s1); Texture2D lshadow_tex : register(t17);\n"   /* 0.9.5 light shadows (see arc__pass.lsh_n; as GL's lsh_vis, atlas v flipped) */
    "float lsh_vis(float4 a, float4 b, float4 c, float4 s, float3 wp, float3 n) {\n"
    "  float3 d = wp - a.xyz, F, up; float face = 0.0; bool spot = b.w > -0.25;\n"
    "  if (spot) { F = c.xyz; up = abs(F.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0); }\n"
    "  else { float3 ad = abs(d);\n"
    "    if (ad.x >= ad.y && ad.x >= ad.z) { face = d.x >= 0.0 ? 0.0 : 1.0; F = float3(d.x >= 0.0 ? 1.0 : -1.0, 0.0, 0.0); up = float3(0.0, 1.0, 0.0); }\n"
    "    else if (ad.y >= ad.z) { face = d.y >= 0.0 ? 2.0 : 3.0; F = float3(0.0, d.y >= 0.0 ? 1.0 : -1.0, 0.0); up = float3(0.0, 0.0, 1.0); }\n"
    "    else { face = d.z >= 0.0 ? 4.0 : 5.0; F = float3(0.0, 0.0, d.z >= 0.0 ? 1.0 : -1.0); up = float3(0.0, 1.0, 0.0); } }\n"
    "  float3 R = normalize(cross(F, up)), U = cross(R, F); float zn = s.y, t = s.z, tx = s.w, z0 = dot(d, F);\n"
    "  if (z0 <= zn) return 1.0;\n"
    "  float ws = 2.0 * t * z0 * tx; float3 q = d + n * (1.5 * ws); float z = dot(q, F); if (z <= zn) return 1.0;\n"
    "  float u = dot(q, R) / (z * t) * 0.5 + 0.5, v = dot(q, U) / (z * t) * 0.5 + 0.5;\n"
    "  if (spot && (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0)) return 1.0;\n"
    "  float zf = 1.0 / sqrt(a.w), zr = max(z - ws, zn), rf = zf / (zf - zn) * (1.0 - zn / zr);\n"
    "  u = clamp(u, 1.5 * tx, 1.0 - 1.5 * tx); v = clamp(v, 1.5 * tx, 1.0 - 1.5 * tx);\n"
    "  float2 at = float2((face + u) / 6.0, 1.0 - (s.x + v) / 4.0), dt = float2(tx / 6.0, tx / 4.0); float acc = 0.0;\n"
    "  [unroll] for (int j = -1; j <= 1; j++) [unroll] for (int k = -1; k <= 1; k++) acc += lshadow_tex.SampleCmpLevelZero(shadow_smp, at + float2(k, -j) * dt, rf);\n"
    "  return acc / 9.0;\n"
    "}\n"   /* 0.9.5 tiled lights (see arc__pass) */
    "float3 local_lights(float3 wp, float3 n, float3 kd, bool pbr, float3 V, float NdotV, float rough, float3 F0) {\n"
    "  float3 acc = float3(0, 0, 0);\n"
    "  float4 cp = mul(vp, float4(wp, 1.0)); int tx = 0, ty = 0;\n"   /* the point's tile: the lights that can reach it, in rising order */
    "  if (cp.w > 0.0) { tx = clamp((int)floor((cp.x / cp.w * 0.5 + 0.5) * 16.0), 0, 15); ty = clamp((int)floor((cp.y / cp.w * 0.5 + 0.5) * 9.0), 0, 8); }\n"
    "  uint e = ltile.Load(int3(tx, ty, 0)); int first = (int)(e >> 8), cnt = (int)(e & 255u);\n"
    "  [loop] for (int k = 0; k < 64; k++) {\n"
    "    if (k >= cnt) break;\n"
    "    int at = first + k, li = (int)lidx.Load(int3(at & 255, at >> 8, 0));\n"
    "    float4 a = ltex.Load(int3(0, li, 0)), b = ltex.Load(int3(1, li, 0)), c = ltex.Load(int3(2, li, 0));\n"
    "    float3 Lv = a.xyz - wp; float d2 = dot(Lv, Lv), q = saturate(1.0 - d2 * a.w);\n"
    "    if (q <= 0.0) continue;\n"
    "    float3 L = Lv / sqrt(max(d2, 1e-12)); float NdotL = max(dot(n, L), 0.0);\n"
    "    if (NdotL <= 0.0) continue;\n"
    "    float t = saturate((-dot(L, c.xyz) - c.w) / (b.w - c.w));\n"
    "    float w = q * q * t * t * (3.0 - 2.0 * t);\n"
    "    if (nlt.y > 0.5) { float4 s4 = ltex.Load(int3(3, li, 0)); if (s4.x >= 0.0 && s4.x + 0.5 < nlt.y) { w *= lsh_vis(a, b, c, s4, wp, n); if (w <= 0.0) continue; } }\n"
    "    float3 lcol = b.rgb * w;\n"
    "    acc += kd * lcol * NdotL; if (pbr) acc += ggx(n, V, L, NdotL, NdotV, rough, F0) * lcol;\n"
    "  }\n"
    "  return acc;\n"
    "}\n"
    "float3 env_col(float t) { return lerp(envh.rgb, t >= 0.0 ? envt.rgb : envb.rgb, min(abs(t), 1.0)); }\n"
    "cbuffer PP : register(b1) { float4 pa, pb; column_major float4x4 sivp; float4 stop, shor, sbot, pc; float4 pd, pe, pres; column_major float4x4 aivp, avp; float4 ass; };\n"
    "#ifdef ARC_MS\n"
    "Texture2DMS<float> dms : register(t8);\n"
    "#endif\n"
    "Texture2D tex2 : register(t4); Texture2D sdepth : register(t9);\n"   /* t9: 0.7 soft particles, the depth drawn so far (R32F) */
    "Texture2D tex0 : register(t0); SamplerState smp0 : register(s0); Texture2D<float4> bones : register(t1);\n"
    "Texture2D shadow_tex : register(t2); Texture2D tex1 : register(t3);\n"
    /* cascades (atlas in GL coordinates: D3D's v = 1 - v) */
    "float pcf_c(int i, float2 uv, float d) {\n"
    "  float t = sh.w, ref = d - cp[i].x * (1.0 + 4.0 * shc.y), acc = 0.0; float2 a = cp[i].zw + uv * shc.z; a.y = 1.0 - a.y;\n"
    "  if (shc.y <= 0.0) { [unroll] for (int j = -1; j <= 1; j++) [unroll] for (int k = -1; k <= 1; k++) acc += shadow_tex.SampleCmpLevelZero(shadow_smp, a + float2(k, -j) * t, ref); return acc / 9.0; }\n"
    "  float sp = (1.0 + 2.0 * shc.y) * t;\n"
    "  [unroll] for (int j2 = -2; j2 <= 2; j2++) [unroll] for (int k2 = -2; k2 <= 2; k2++) acc += (float)((3 - abs(k2)) * (3 - abs(j2))) * shadow_tex.SampleCmpLevelZero(shadow_smp, a + float2(k2, -j2) * sp, ref);\n"
    "  return acc / 81.0;\n"
    "}\n"
    "bool casc_uv(int i, float3 wp, float3 n, out float3 c, out float e) {\n"
    "  float4 s = mul(cvp[i], float4(wp + n * (cp[i].y * (1.0 + 2.0 * shc.y)), 1.0)); c = s.xyz * 0.5 + 0.5;\n"
    "  float mg = ((shc.y > 0.0 ? 2.0 * (1.0 + 2.0 * shc.y) : 1.0) + 1.5) * sh.w / shc.z;\n"
    "  e = min(min(c.x - mg, 1.0 - mg - c.x), min(c.y - mg, 1.0 - mg - c.y));\n"
    "  return e > 0.0 && c.z < 1.0;\n"
    "}\n"
    "float shadow_vis(float3 wp, float3 n) {\n"
    "  if (sh.x <= 0.0) return 1.0;\n"
    "  if (shc.x > 1.5 || shc.y > 0.0) {\n"
    "    [loop] for (int i = 0; i < 3; i++) {\n"
    "      if ((float)i >= shc.x) break;\n"
    "      float3 c; float e; if (!casc_uv(i, wp, n, c, e)) continue;\n"
    "      float vis = pcf_c(i, c.xy, c.z);\n"
    "      if ((float)(i + 1) < shc.x && e < 0.1) { float3 c2; float e2, v2 = 1.0; if (casc_uv(i + 1, wp, n, c2, e2)) v2 = pcf_c(i + 1, c2.xy, c2.z); vis = v2 + (vis - v2) * (e / 0.1); }\n"
    "      return 1.0 - sh.x * (1.0 - vis);\n"
    "    }\n"
    "    return 1.0;\n"
    "  }\n"
    "  float4 s = mul(svp, float4(wp + n * sh.z, 1.0)); float3 c = s.xyz * 0.5 + 0.5;\n"
    "  if (c.x <= 0.0 || c.x >= 1.0 || c.y <= 0.0 || c.y >= 1.0 || c.z >= 1.0) return 1.0;\n"
    "  float2 uv = float2(c.x, 1.0 - c.y); float acc = 0.0, r = c.z - sh.y;\n"
    "  [unroll] for (int j = -1; j <= 1; j++) [unroll] for (int k = -1; k <= 1; k++) acc += shadow_tex.SampleCmpLevelZero(shadow_smp, uv + float2(k, -j) * sh.w, r);\n"
    "  return 1.0 - sh.x * (1.0 - acc / 9.0);\n"
    "}\n"
    "struct MI { float3 p : POSITION; float3 n : NORMAL; float4 c : COLOR; float2 uv : TEXCOORD0; uint4 j : BLENDINDICES; float4 w : BLENDWEIGHT;\n"
    "            float4 m0 : TEXCOORD1; float4 m1 : TEXCOORD2; float4 m2 : TEXCOORD3; float4 m3 : TEXCOORD4; float4 t : TEXCOORD5; float4 par : TEXCOORD6; float4 ext : TEXCOORD7;\n"
    "            float4 fx0 : TEXCOORD8; float4 fx1 : TEXCOORD9; float4 uvx : TEXCOORD10; };\n"
    "struct VM { float4 pos : SV_Position; float4 c : COLOR; float2 uv : TEXCOORD0; float3 wp : TEXCOORD1; float3 n : TEXCOORD2;\n"
    "            nointerpolation float4 t : TEXCOORD3; nointerpolation float4 par : TEXCOORD4; nointerpolation float4 ext : TEXCOORD5;\n"
    "            float3 op : TEXCOORD6; nointerpolation float4 fx0 : TEXCOORD7; nointerpolation float4 fx1 : TEXCOORD8; float3 bp : TEXCOORD9; float pm : TEXCOORD10; };\n"
    "struct VO { float4 pos : SV_Position; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
    "float4 fix_z(float4 p) { if (misc.y < 0.5) p.z = (p.z + p.w) * 0.5; return p; }\n"   /* 0.7.2 misc.y: reversed-Z, already 0..1 */
    "float3x4 bone(uint b) { uint x = (b % 512) * 3, y = b / 512; return float3x4(bones.Load(int3(x, y, 0)), bones.Load(int3(x + 1, y, 0)), bones.Load(int3(x + 2, y, 0))); }\n"
    "VM vs_mesh(MI i) {\n"
    "  float3 p = i.p, n = i.n;\n"
    "  if (i.ext.y > 0.5) {\n"
    "    uint b0 = (uint)(i.ext.x + 0.5);\n"
    "    float3x4 S = bone(b0 + i.j.x) * i.w.x + bone(b0 + i.j.y) * i.w.y + bone(b0 + i.j.z) * i.w.z + bone(b0 + i.j.w) * i.w.w;\n"
    "    p = mul(S, float4(p, 1.0)); n = mul((float3x3)S, n);\n"
    "  }\n"
    "  float3 wp = i.m0.xyz * p.x + i.m1.xyz * p.y + i.m2.xyz * p.z + i.m3.xyz;\n"
    "  VM o; o.c = i.c; o.uv = float2((1.0 + i.uvx.x) * i.uv.x + i.uvx.y * i.uv.y, i.uvx.z * i.uv.x + (1.0 + i.uvx.w) * i.uv.y) + i.fx1.xy; o.wp = wp; o.n = i.m0.xyz * n.x + i.m1.xyz * n.y + i.m2.xyz * n.z; o.t = i.t; o.par = i.par; o.ext = i.ext;\n"
    "  o.op = p; o.fx0 = i.fx0; o.fx1 = i.fx1;\n"
    "  o.bp = i.p; o.pm = 1.0;\n"   /* 0.10.1 the bind pose; whether the strongest joint's part takes the pattern */
    "  if (pat[0].x > 0.5) { o.pm = pat[3].w;\n"
    "    if (i.ext.y > 0.5) { float bw = i.w.x; uint jj = i.j.x; if (i.w.y > bw) { bw = i.w.y; jj = i.j.y; } if (i.w.z > bw) { bw = i.w.z; jj = i.j.z; } if (i.w.w > bw) jj = i.j.w;\n"
    "      uint ji = jj & 127u; o.pm = pjm[ji >> 2][ji & 3u]; } }\n"
    "  if (i.fx0.w > 0.0) {\n"   /* outline hull: out along the normal bent away from the model's origin */
    "    float3 nn = normalize(o.n), cd = wp - i.m3.xyz; float cl = length(cd); float3 dir = cl > 1e-6 ? nn + cd / cl : nn; float dl = length(dir);\n"
    "    wp += (dl > 1e-4 ? dir / dl : nn) * i.fx0.w; o.wp = wp;\n"
    "  }\n"
    "  o.pos = fix_z(mul(vp, float4(wp, 1.0))); return o;\n"
    "}\n"
    "float fog_f(float d) { float f = 0.0; if (fog.z > 0.0) { if (fog.w > 0.0) { float k = fog.w * d; f = 1.0 - exp(-k * k); } else f = saturate((d - fog.x) * fog.y); } return f; }\n"
    "uint hx_h(int x, int y) { uint h = ((uint)x * 73856093u) ^ ((uint)y * 19349663u); h = h ^ (h >> 13); h = h * 0x5bd1e995u; h = h ^ (h >> 15); return h; }\n"
    "float hx_gain(float x, float k) { float s = 0.5 * pow(2.0 * (x < 0.5 ? x : 1.0 - x), k); return x < 0.5 ? s : 1.0 - s; }\n"
    "float4 hex_cell(Texture2D t, SamplerState sm, float2 st, float2 gx, float2 gy, int2 v, float rot) {\n"   /* one cell's turned, offset sample */
    "  const float S3 = 3.4641016; uint h = hx_h(v.x, v.y), h2 = hx_h(v.x + 31, v.y + 17);\n"
    "  float2 c = float2((float)v.x + (float)v.y * 0.5, (float)v.y * 0.8660254) / S3;\n"
    "  float a = ((float)(h2 & 0xFFFFu) / 65536.0 * 2.0 - 1.0) * 3.14159265 * rot, ca = cos(a), sa = sin(a);\n"
    "  float2 d = st - c, uv = float2(ca * d.x - sa * d.y, sa * d.x + ca * d.y) + c + float2((float)(h & 0xFFFFu), (float)(h >> 16)) / 65536.0;\n"
    "  return t.SampleGrad(sm, uv, float2(ca * gx.x - sa * gx.y, sa * gx.x + ca * gx.y), float2(ca * gy.x - sa * gy.y, sa * gy.x + ca * gy.y));\n"
    "}\n"
    "float hx_w(float4 col, float w) { float w7 = w * w * w; w7 = w7 * w7 * w; return (1.0 + (dot(col.rgb, float3(0.3, 0.59, 0.11)) - 1.0) * 0.6) * w7; }\n"
    "float4 hex_tex(Texture2D t, SamplerState sm, float2 st, float rot, float con) {\n"   /* 0.8.4 hex tiling: see arc_internal.h */
    "  const float S3 = 3.4641016; float2 gx = ddx(st), gy = ddy(st);\n"
    "  float2 p = float2(st.x * S3 - st.y * S3 * 0.57735027, st.y * S3 * 1.15470054), b = floor(p), f = p - b;\n"
    "  float z = 1.0 - f.x - f.y, s = z < 0.0 ? 1.0 : 0.0, s2 = 2.0 * s - 1.0;\n"
    "  float3 w = max(float3(-z * s2, s - f.y * s2, s - f.x * s2), 0.0); int2 ib = (int2)b; int is = (int)s;\n"
    "  float4 c1 = hex_cell(t, sm, st, gx, gy, ib + int2(is, is), rot), c2 = hex_cell(t, sm, st, gx, gy, ib + int2(is, 1 - is), rot), c3 = hex_cell(t, sm, st, gx, gy, ib + int2(1 - is, is), rot);\n"
    "  float3 W = float3(hx_w(c1, w.x), hx_w(c2, w.y), hx_w(c3, w.z));\n"
    "  float sum = W.x + W.y + W.z; W = sum > 0.0 ? W / sum : float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0);\n"
    "  if (con != 0.5) { float k = log(1.0 - con) / log(0.5); W = float3(hx_gain(W.x, k), hx_gain(W.y, k), hx_gain(W.z, k)); W /= W.x + W.y + W.z; }\n"
    "  return W.x * c1 + W.y * c2 + W.z * c3;\n"
    "}\n"
    "float4 ps_mesh(VM i, bool ffc : SV_IsFrontFace) : SV_Target {\n"
    "  float ez = floor(i.ext.z + 0.5), ew = floor(i.ext.w + 0.5);\n"   /* 0.9.9 packed flags (see arc__instance.extra) */
    "  float e_unlit = fmod(ez, 2.0), e_dark = fmod(floor(ez * 0.5), 256.0) / 255.0, e_char = fmod(floor(ez / 512.0), 256.0) / 255.0, e_nomask = floor(ez / 131072.0);\n"
    "  float e_addfog = fmod(ew, 2.0), e_layer = floor(ew * 0.5);\n"
    "  if (dot(clipp.xyz, i.wp) + clipp.w < 0.0) discard;\n"   /* 0.8.7 mirror passes: nothing under the plane */
    "  float wmv = 1.0;\n"   /* 0.9.9 world mask: four texels around the point, softness-shaped weights */
    "  if (wm1.w > 0.5 && e_nomask < 0.5) {\n"
    "    uint mw, mh; wmask.GetDimensions(mw, mh); int2 sz = int2((int)mw, (int)mh); float2 g = (i.wp.xz - wm0.xy) * wm0.z - 0.5, b0 = floor(g), f = g - b0;\n"
    "    f = wm0.w < 0.01 ? step(float2(0.5, 0.5), f) : smoothstep(float2(0.5, 0.5) - 0.5 * wm0.w, float2(0.5, 0.5) + 0.5 * wm0.w, f);\n"
    "    int2 i0 = clamp(int2(b0), int2(0, 0), sz - 1), i1 = clamp(int2(b0) + 1, int2(0, 0), sz - 1);\n"
    "    float m00 = wmask.Load(int3(i0.x, i0.y, 0)).r, m10 = wmask.Load(int3(i1.x, i0.y, 0)).r, m01 = wmask.Load(int3(i0.x, i1.y, 0)).r, m11v = wmask.Load(int3(i1.x, i1.y, 0)).r;\n"
    "    wmv = lerp(lerp(m00, m10, f.x), lerp(m01, m11v, f.x), f.y); }\n"
    "  if (m11.x > 0.5) {\n"   /* 0.9.9 cutaway */
    "    float2 ab = cut1.xz - cut0.xz; float ab2 = dot(ab, ab), t = ab2 > 1e-8 ? saturate(dot(i.wp.xz - cut0.xz, ab) / ab2) : 0.0;\n"
    "    float d = length(i.wp.xz - (cut0.xz + ab * t));\n"
    "    if (d < cut0.w && i.wp.y > cut1.w) {\n"
    "      if (cut2.x < 1.5) discard;\n"
    "      float fd = saturate((i.wp.y - cut1.w) / cut2.y) * saturate((cut0.w - d) / (0.25 * cut0.w));\n"
    "      int2 q = int2(i.pos.xy) & 3;\n"
    "      if ((B4[q.y * 4 + q.x] + 0.5) / 16.0 < fd) discard; }\n"
    "    if (!ffc) { float3 cg = lerp(wm1.rgb, float3(cut2.z, cut2.z, cut2.z), wmv); return float4(lerp(cg, fc.rgb, fog_f(length(i.wp - eye.xyz))), 1.0); } }\n"
    "  float glow = 0.0;\n"
    "  if (i.fx0.x > 0.0) { float nz = dissolve_noise(i.op); if (nz < i.fx0.x) discard; if (i.fx0.y > 0.0 && nz < i.fx0.x + i.fx0.y) glow = i.fx1.w * (1.0 - (nz - i.fx0.x) / i.fx0.y); }\n"
    "  if (i.fx0.w > 0.0) return float4(lerp(i.t.rgb, fc.rgb, fog_f(length(i.wp - eye.xyz))), 1.0);\n"   /* outline hull */
    "  float2 buv = i.uv;\n"
    "  if (m6.y > 0.0) { float3 bn = abs(cross(ddx(i.wp), ddy(i.wp)));\n"   /* 0.8.8 world-mapped colour: the face's plane */
    "    buv = ((bn.y >= bn.x && bn.y >= bn.z) ? i.wp.xz : (bn.x >= bn.z ? i.wp.zy : i.wp.xy)) * m6.y; }\n"
    "  float4 tx = m4.x > 1.5 ? hex_tex(tex0, smp0, buv, m4.y, m4.z) : tex0.Sample(smp0, buv);\n"
    "  if (m10.z > 0.5) {\n"
    "    int n = (int)(m10.y + 0.5); float sh = m10.x, sum = 0.0; float4 cl[4]; float b[4], v[4];\n"
    "    [unroll] for (int k = 0; k < 4; k++) if (k < n) sum += tx[k];\n"
    "    [unroll] for (int k2 = 0; k2 < 4; k2++) { cl[k2] = float4(0, 0, 0, 0); b[k2] = 0.0; if (k2 < n) { b[k2] = sum < 1e-4 ? (k2 == 0 ? 1.0 : 0.0) : tx[k2]; cl[k2] = layers_tex.Sample(smp2, float3(i.wp.xz * m9[k2], (float)k2)); } }\n"
    "    if (sh > 0.0) { float mx = -1e30, d = 1.0 - 0.95 * sh;\n"
    "      [unroll] for (int k3 = 0; k3 < 4; k3++) { v[k3] = b[k3] > 0.0 ? b[k3] + cl[k3].a * sh : -1e30; mx = max(mx, v[k3]); }\n"
    "      [unroll] for (int k4 = 0; k4 < 4; k4++) b[k4] = b[k4] > 0.0 ? max(v[k4] - (mx - d), 0.0) : 0.0; }\n"
    "    float t = 0.0; float3 r = float3(0, 0, 0); [unroll] for (int k5 = 0; k5 < 4; k5++) { t += b[k5]; r += b[k5] * cl[k5].rgb; }\n"
    "    tx = float4(t > 0.0 ? r / t : float3(0, 0, 0), 1.0);\n"
    "  }\n"
    "  if (m10.w > 0.5) tx = layers_tex.Sample(smp2, float3(buv, e_layer));\n"   /* 0.9.9 the colour from this draw's layer of the mesh's array */
    "  if (misc.x > 0.5 && tx.a < 0.5) discard;\n"
    "  float3 c = i.c.rgb * tx.rgb;\n"
    "  if (m3.x > 0.5) {\n"   /* 0.8.4 detail map: colour *= lerp(1, 2 d, strength); world plane from the face normal */
    "    float2 du = i.uv;\n"
    "    if (m3.y > 0.0) { float3 fn = abs(cross(ddx(i.wp), ddy(i.wp)));\n"
    "      du = (fn.y >= fn.x && fn.y >= fn.z) ? i.wp.xz : (fn.x >= fn.z ? i.wp.zy : i.wp.xy); du *= m3.y; }\n"
    "    float4 d = m3.x > 1.5 ? hex_tex(dtex, smp2, du, m3.w, m4.w) : dtex.Sample(smp2, du);\n"
    "    float k = m3.z * d.a;\n"
    "    if (m5.w > 0.5) { float2 mu = du * m5.x;\n"   /* breakup mask: the same mapping at its own scale */
    "      float mk = (m3.x > 1.5 ? hex_tex(mtex, smp2, mu, m3.w, m4.w) : mtex.Sample(smp2, mu)).r;\n"
    "      k *= saturate((mk - m5.y) / m5.z); }\n"
    "    c *= 1.0 + (2.0 * d.rgb - 1.0) * k;\n"
    "  }\n"
    "  float lum = dot(c, float3(0.3, 0.59, 0.11));\n"
    "  float3 base = c * lerp(float3(1, 1, 1), i.t.rgb, i.par.z);\n"
    "  base = lerp(base, i.t.rgb * (0.35 + lum), i.par.x);\n"
    "  if (m7.w > 0.5) { float4 pm = ptex.Sample(smp4, i.uv);\n"   /* 0.8.8 two colours through the mask: R tint, G tint2 */
    "    base = lerp(base, i.t.rgb * (0.35 + lum), pm.r); base = lerp(base, m7.rgb * (0.35 + lum), pm.g);\n"
    "    if (m8.w > 0.5) base = lerp(base, m8.rgb * (0.35 + lum), pm.b); }\n"   /* 0.8.9 blue: tint3 */
    "  float3 pfn = cross(ddx(i.bp), ddy(i.bp));\n"   /* 0.10.1 paint pattern: the face's direction in the bind pose (CHECKER, DOTS) */
    "  if (pat[0].x > 0.5 && i.pm > 0.5) { float pmk = pat_m(i.bp, pfn);\n"
    "    if (pmk > 0.0 && pat[1].w > 0.5) pmk *= m7.w > 0.5 ? ptex.Sample(smp4, i.uv).r : 0.0;\n"
    "    base = lerp(base, pat[2].rgb * (0.35 + lum), pmk); }\n"
    "  if (e_dark > 0.0) base *= 1.0 - 0.85 * e_dark;\n"   /* 0.9.9 darken and char (as the software renderer) */
    "  if (e_char > 0.0) { float n1 = dissolve_noise(i.op * 1.7), soot = smoothstep(0.35, 0.7, n1); float n2 = dissolve_noise(i.op * 7.0 + 31.0), sc = 1.0 - smoothstep(0.0, 0.035, abs(n2 - 0.5));\n"
    "    base = base * (1.0 - 0.8 * e_char) * (1.0 - 0.6 * e_char * soot) + float3(0.55, 0.55, 0.55) * sc * e_char * (1.0 - soot); }\n"
    "  float3 n = normalize(i.n);\n"
    "  if (m0.x > 0.5) {\n"   /* normal map: frame T = dP/du, B = -dP/dv from the triangle (screen derivatives) */
    "    float3 e1 = ddx(i.wp), e2 = ddy(i.wp); float2 t1 = ddx(i.uv), t2 = ddy(i.uv); float det = t1.x * t2.y - t2.x * t1.y;\n"
    "    if (abs(det) > 1e-30) {\n"
    "      float3 T = (t2.y * e1 - t1.y * e2) / det, B = -(t1.x * e2 - t2.x * e1) / det;\n"
    "      T -= n * dot(n, T); float tl = length(T);\n"
    "      if (tl > 1e-12) { T /= tl; float3 Bo = cross(n, T); if (dot(Bo, B) < 0.0) Bo = -Bo;\n"
    "        float3 nm = ntex.Sample(smp2, i.uv).xyz * 2.0 - 1.0; if (m2.w > 1.5) nm.z = sqrt(max(1.0 - dot(nm.xy, nm.xy), 0.0));\n"   /* BC5: Z rebuilt */
    "        n = normalize(T * (nm.x * m0.y) + Bo * (nm.y * m0.y) + n * nm.z); }\n"
    "    }\n"
    "  }\n"
    "  float diff = max(dot(n, ld.xyz), 0.0), d = length(i.wp - eye.xyz), r = 0.0;\n"
    "  float vis = (e_unlit < 0.5 && diff > 0.0) ? shadow_vis(i.wp, n) : 1.0;\n"
    "  if (rim.r + rim.g + rim.b > 0.0) r = pow(1.0 - max(dot(n, normalize(eye.xyz - i.wp)), 0.0), rim.w);\n"
    "  float4 orm = m1.z > 0.5 ? otex.Sample(smp2, i.uv) : float4(1, 1, 1, 1);\n"
    "  float ao = m2.w > 0.5 ? 1.0 - m0.w * (1.0 - orm.r) : 1.0;\n"
    "  float3 lit;\n"
    "  if (e_unlit > 0.5) lit = base * (1.0 + i.t.w);\n"
    "  else if (m0.z > 0.5) {\n"   /* pbr: GGX highlight from the sun, Fresnel-weighted environment */
    "    float metal = saturate(m1.x * orm.b), rough = clamp(m1.y * orm.g, 0.045, 1.0);\n"
    "    float3 V = normalize(eye.xyz - i.wp); float NdotV = max(dot(n, V), 1e-4);\n"
    "    float3 F0 = lerp(float3(0.04, 0.04, 0.04), base, metal), spec = float3(0, 0, 0);\n"
    "    if (diff > 0.0) spec = ggx(n, V, ld.xyz, diff, NdotV, rough, F0) * lc.rgb * vis;\n"
    "    if (nlt.x > 0.5) spec += local_lights(i.wp, n, base * (1.0 - metal), true, V, NdotV, rough, F0);\n"
    "    float3 e = env_col(2.0 * NdotV * n.y - V.y), avg = (envt.rgb + 2.0 * envh.rgb + envb.rgb) * 0.25;\n"
    "    e = e + (avg - e) * rough;\n"
    "    float3 envs = e * (F0 + (max(float3(1.0 - rough, 1.0 - rough, 1.0 - rough), F0) - F0) * pow5(1.0 - NdotV)) * ao * envt.w;\n"
    "    lit = base * (1.0 - metal) * (amb.rgb * ao + lc.rgb * diff * vis) + spec + envs + base * i.t.w + rim.rgb * r;\n"
    "  } else {\n"
    "    lit = base * (amb.rgb * ao + lc.rgb * diff * vis + i.t.w) + rim.rgb * r;\n"
    "    if (nlt.x > 0.5) lit += local_lights(i.wp, n, base, false, n, 1.0, 1.0, base);\n"
    "  }\n"
    "  if (m2.w > 0.5) lit += m2.rgb * (m1.w > 0.5 ? etex.Sample(smp2, i.uv).rgb : float3(1, 1, 1));\n"
    "  if (glow > 0.0) { int e = (int)i.fx1.z; lit += float3(e & 255, (e >> 8) & 255, (e >> 16) & 255) / 255.0 * glow; }\n"
    "  if (rpar.x > 0.0 && m6.x > 0.0) {\n"   /* 0.8.7 planar reflection: the mirror pass's picture at this pixel */
    "    float F = 0.6 + 0.4 * pow5(1.0 - max(dot(n, normalize(eye.xyz - i.wp)), 0.0));\n"
    "    float rr = m0.z > 0.5 ? saturate(m1.y * orm.g) : 0.0;\n"
    "    float3 rc = rtex.SampleLevel(smp3, i.pos.xy * rpar.zw + 0.03 * float2(n.x, n.z), min(6.0 * rr, rpar.y)).rgb;\n"
    "    lit = lerp(lit, rc, rpar.x * m6.x * i.c.a * F);\n"
    "  }\n"
    "  lit = lerp(lit, float3(1, 1, 1), i.par.y);\n"
    "  float f = fog_f(d);\n"
    "  if (wmv < 1.0) lit = lerp(wm1.rgb, lit, wmv);\n"
    "  float4 oc = float4(e_addfog > 0.5 ? lit * (1.0 - f) : lerp(lit, fc.rgb, f), misc.x > 0.5 ? 1.0 : tx.a * i.par.w * i.c.a);\n"
    "  if (i.fx0.z > 0.0) {\n"   /* hologram (additive) */
    "    float fr = 1.0 - abs(dot(n, normalize(eye.xyz - i.wp))); fr *= fr;\n"
    "    float scan = 0.5 + 0.5 * sin(60.0 * i.wp.y - 6.0 * tm.x);\n"   /* not 'line': a reserved word in HLSL */
    "    oc = float4(i.t.rgb * ((0.6 + 0.8 * fr) * (0.7 + 0.3 * scan)) * (1.0 - f), i.fx0.z * saturate(0.25 + 0.75 * fr + 0.3 * scan));\n"
    "  }\n"
    "  if (soft.z > 0.5 && i.fx0.y < 0.0 && i.fx0.x <= 0.0) {\n"   /* soft particles (see arc__pass.soft) */
    "    float sd = sdepth.Load(int3(i.pos.xy, 0)).r;\n"
    "    float4 sp = mul(ivp, float4(i.pos.x / soft.x * 2.0 - 1.0, 1.0 - i.pos.y / soft.y * 2.0, sd * 2.0 - 1.0, 1.0));\n"
    "    oc.a *= saturate(length(sp.xyz / sp.w - i.wp) / -i.fx0.y);\n"
    "  }\n"
    "  return oc;\n"
    "}\n"
    "struct LI { float3 p : POSITION; float4 c : COLOR; };\n"
    "VO vs_line(LI i) { VO o; o.c = i.c; o.uv = float2(0, 0); o.pos = fix_z(mul(vp, float4(i.p, 1.0))); return o; }\n"
    "struct QI { float2 p : POSITION; float2 uv : TEXCOORD0; float4 c : COLOR; };\n"
    "VO vs_quad(QI i) { VO o; o.c = i.c; o.uv = i.uv; o.pos = float4(i.p.x / screen.x * 2.0 - 1.0, 1.0 - i.p.y / screen.y * 2.0, 0.5, 1.0); return o; }\n"
    "float4 ps_color(VO i) : SV_Target { return i.c; }\n"
    "cbuffer Q : register(b3) { float4 q0; };\n"   /* 0.10.1 q0.x: distance-field text */
    "float4 ps_tex(VO i) : SV_Target { float4 t = tex0.Sample(smp0, i.uv);\n"
    "  if (q0.x > 0.0) { float e = saturate((t.a - 0.5) * q0.x + 0.5); t = float4(1, 1, 1, e * e * (3.0 - 2.0 * e)); }\n"
    "  return (i.uv.x < -2048.0 ? float4(1, 1, 1, 1) : t) * i.c; }\n"   /* plain triangles (ARC__UV_PLAIN): white */
    "void ps_shadow(VM i) { clip(tex0.Sample(smp0, i.uv).a - 0.5); if (i.fx0.w > 0.0 || (i.fx0.x > 0.0 && dissolve_noise(i.op) < i.fx0.x)) discard; }\n"
    /* post-processing: full-screen triangle; uv (0,0) = top left */
    "struct VP { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "VP vs_post(uint id : SV_VertexID) { VP o; float2 p = float2((id << 1) & 2, id & 2); o.uv = float2(p.x, 1.0 - p.y); o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0); return o; }\n"
    "float4 ps_sky(VP i) : SV_Target {\n"   /* the same gradient as the soft renderer's sky_fill */
    "  float2 n = float2(i.uv.x * 2.0 - 1.0, 1.0 - i.uv.y * 2.0); float4 a = mul(sivp, float4(n, -1.0, 1.0)), b = mul(sivp, float4(n, 1.0, 1.0));\n"
    "  float3 d = b.xyz / b.w - a.xyz / a.w; float t = d.y / length(d);\n"
    "  return float4(lerp(shor.rgb, t >= 0.0 ? stop.rgb : sbot.rgb, min(abs(t), 1.0)), 1.0);\n"
    "}\n"
    "float4 ps_down(VP i) : SV_Target {\n"   /* pa.xy source texel, pa.z threshold (< 0: none) */
    "  float3 c = (tex0.Sample(smp0, i.uv + float2(-pa.x, -pa.y)).rgb + tex0.Sample(smp0, i.uv + float2(pa.x, -pa.y)).rgb\n"
    "            + tex0.Sample(smp0, i.uv + float2(-pa.x, pa.y)).rgb + tex0.Sample(smp0, i.uv + pa.xy).rgb) * 0.25;\n"
    "  if (pa.z >= 0.0) { float l = dot(c, float3(0.2126, 0.7152, 0.0722)); c = min(c * (l > 1e-4 ? max(l - pa.z, 0.0) / l : 0.0), 64.0); }\n"
    "  return float4(c, 1.0);\n"
    "}\n"
    "float4 ps_up(VP i) : SV_Target {\n"   /* tex0 the smaller up level, tex1 this level's down; pa.xy offset */
    "  float3 c = tex1.Sample(smp0, i.uv).rgb;\n"
    "  [unroll] for (int j = -1; j <= 1; j++) [unroll] for (int k = -1; k <= 1; k++) c += tex0.Sample(smp0, i.uv + float2(k, j) * pa.xy).rgb * ((k == 0 ? 2.0 : 1.0) * (j == 0 ? 2.0 : 1.0) / 16.0);\n"
    "  return float4(c, 1.0);\n"
    "}\n"
    /* ssao (as soft_ssao): depth copied to an R32F target first (t0); ass: w, h, radius, perspective */
    "float4 ps_depthcopy(VP i) : SV_Target { return tex0.Load(int3(i.pos.xy, 0)).rrrr; }\n"
    "#ifdef ARC_MS\n"
    "float4 ps_depthcopy_ms(VP i) : SV_Target { return dms.Load(int2(i.pos.xy), 0).rrrr; }\n"
    "#endif\n"
    "static const float3 SK[16] = { float3(0.0257, 0.0000, 0.1511), float3(-0.0350, 0.0321, 0.1562), float3(0.0059, -0.0673, 0.1667), float3(0.0550, 0.0717, 0.1819), float3(-0.1159, -0.0205, 0.2011), float3(0.1272, -0.0809, 0.2235), float3(-0.0494, 0.1838, 0.2481), float3(-0.1094, -0.2106, 0.2740), float3(0.2746, 0.1003, 0.3000), float3(-0.3293, 0.1359, 0.3247), float3(0.1821, -0.3890, 0.3463), float3(0.1535, 0.4894, 0.3626), float3(-0.5252, -0.3044, 0.3705), float3(0.6962, -0.1530, 0.3650), float3(-0.4782, 0.6801, 0.3369), float3(-0.1241, -0.9578, 0.2591) };\n"
    "static const float BY[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };\n"
    "float dep(int x, int y) { x = clamp(x, 0, (int)ass.x - 1); y = clamp(y, 0, (int)ass.y - 1); return tex0.Load(int3(x, y, 0)).r; }\n"
    "float3 wpos(int x, int y, float d) { float4 p = mul(aivp, float4((x + 0.5) / ass.x * 2.0 - 1.0, 1.0 - (y + 0.5) / ass.y * 2.0, d * 2.0 - 1.0, 1.0)); return p.xyz / p.w; }\n"
    "#ifdef ARC_ZREV\n"   /* 0.7.2 reversed-Z: nothing drawn = 0; orthographic clip z falls with distance */
    "#define EMPTY(d) ((d) <= 0.0)\n#define ZS(v) (-(v))\n"
    "#else\n"
    "#define EMPTY(d) ((d) >= 1.0)\n#define ZS(v) (v)\n"
    "#endif\n"
    "float4 ps_ssao(VP i) : SV_Target {\n"
    "  int x = (int)i.pos.x, y = (int)i.pos.y;\n"
    "  float d = dep(x, y);\n"
    "  if (EMPTY(d)) return float4(0, 0, 0, 0);\n"
    "  float3 P = wpos(x, y, d);\n"
    "  float dl = dep(x - 1, y), dr = dep(x + 1, y), du = dep(x, y - 1), dd = dep(x, y + 1);\n"
    "  float3 ex = abs(dr - d) < abs(d - dl) ? wpos(x + 1, y, dr) - P : P - wpos(x - 1, y, dl);\n"
    "  float3 ey = abs(dd - d) < abs(d - du) ? wpos(x, y + 1, dd) - P : P - wpos(x, y - 1, du);\n"
    "  float3 N = normalize(cross(ey, ex));\n"
    "  float ang = BY[(y & 3) * 4 + (x & 3)] * (6.28318531 / 16.0);\n"
    "  float3 hlp = abs(N.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);\n"
    "  float3 T0 = normalize(cross(hlp, N)), B0 = cross(N, T0), T = T0 * cos(ang) + B0 * sin(ang), B = cross(N, T);\n"
    "  float occ = 0.0, R = ass.z;\n"
    "  [loop] for (int k = 0; k < 16; k++) {\n"
    "    float3 sp = P + (T * SK[k].x + B * SK[k].y + N * SK[k].z) * R;\n"
    "    float4 c = mul(avp, float4(sp, 1.0));\n"
    "    if (c.w <= 1e-6) continue;\n"
    "    float sx = (c.x / c.w * 0.5 + 0.5) * ass.x, sy = (0.5 - c.y / c.w * 0.5) * ass.y;\n"
    "    if (sx < 0.0 || sy < 0.0 || sx >= ass.x || sy >= ass.y) continue;\n"
    "    int ix = (int)sx, iy = (int)sy; float qd = dep(ix, iy);\n"
    "    if (EMPTY(qd)) continue;\n"
    "    float3 Q = wpos(ix, iy, qd); float4 qc = mul(avp, float4(Q, 1.0));\n"
    "    bool hid = ass.w > 0.5 ? qc.w < c.w - 0.025 * R : ZS(qc.z) < ZS(c.z) - 0.025 * R * abs(avp[2][2]);\n"
    "    if (hid) occ += saturate(2.0 - distance(Q, P) / R);\n"
    "  }\n"
    "  return float4(1.0 - occ / 16.0, 1.0, 0.0, 1.0);\n"
    "}\n"
    /* composite: pa bloom, exposure, tonemap, desaturate; pb contrast, vignette, aspect, scanline period; pc tilt, focus, width, 1/levels;
       pd ssao, fxaa, crt, pixelate; pe aberration, scanlines, lut strength (0 = none), lut size; pres w, h, 1/w, 1/h.
       ntex (t5) = the AO buffer, otex (t6) = the LUT here */
    "float3 S(float2 q) { return tex0.SampleLevel(smp0, q, 0).rgb; }\n"   /* no gradients: fxaa samples inside a loop */
    "float luma(float3 c) { return sqrt(dot(saturate(c), float3(0.299, 0.587, 0.114))); }\n"
    "float FL(float2 p) { return luma(S(p)); }\n"
    "float3 fxaa(float2 tc) {\n"
    "  float tx = pres.z, ty = pres.w, up = -pres.w;\n"
    "  float3 c = S(tc); float M = luma(c), N = FL(tc + float2(0.0, up)), So = FL(tc - float2(0.0, up)), E = FL(tc + float2(tx, 0.0)), W = FL(tc - float2(tx, 0.0));\n"
    "  float mn = min(M, min(min(N, So), min(E, W))), mx = max(M, max(max(N, So), max(E, W))), range = mx - mn;\n"
    "  if (range < max(0.0312, 0.125 * mx)) return c;\n"
    "  float NW = FL(tc + float2(-tx, up)), NE = FL(tc + float2(tx, up)), SW = FL(tc + float2(-tx, -up)), SE = FL(tc + float2(tx, -up));\n"
    "  float sub = saturate(abs((2.0 * (N + So + E + W) + NW + NE + SW + SE) / 12.0 - M) / range); sub = sub * sub * (3.0 - 2.0 * sub); sub = sub * sub * 0.75;\n"
    "  float eh = abs(NW + SW - 2.0 * W) + 2.0 * abs(N + So - 2.0 * M) + abs(NE + SE - 2.0 * E);\n"
    "  float ev = abs(NW + NE - 2.0 * N) + 2.0 * abs(W + E - 2.0 * M) + abs(SW + SE - 2.0 * So);\n"
    "  bool horz = eh >= ev;\n"
    "  float2 nrm = horz ? float2(0.0, up) : float2(-tx, 0.0), al = horz ? float2(tx, 0.0) : float2(0.0, ty);\n"
    "  float L1 = horz ? N : W, L2 = horz ? So : E, g1 = abs(L1 - M), g2 = abs(L2 - M);\n"
    "  bool p1 = g1 >= g2; float2 st = p1 ? nrm : -nrm; float lo = ((p1 ? L1 : L2) + M) * 0.5, gs = max(g1, g2) * 0.25;\n"
    "  float2 pb = tc + st * 0.5, pn = pb - al, pp = pb + al;\n"
    "  float en = FL(pn) - lo, ep = FL(pp) - lo; bool dn = abs(en) >= gs, dp = abs(ep) >= gs;\n"
    "  float Q[8] = { 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 4.0 };\n"
    "  [loop] for (int i = 0; i < 8; i++) {\n"
    "    if (dn && dp) break;\n"
    "    if (!dn) { pn -= al * Q[i]; en = FL(pn) - lo; dn = abs(en) >= gs; }\n"
    "    if (!dp) { pp += al * Q[i]; ep = FL(pp) - lo; dp = abs(ep) >= gs; }\n"
    "  }\n"
    "  float aa = dot(al, al), dstn = dot(tc - pn, al) / aa, dstp = dot(pp - tc, al) / aa;\n"
    "  bool mlt = M - lo < 0.0, good = dstn < dstp ? ((en < 0.0) != mlt) : ((ep < 0.0) != mlt);\n"
    "  float off = good ? 0.5 - min(dstn, dstp) / (dstn + dstp) : 0.0; off = max(off, sub);\n"
    "  return S(tc + st * off);\n"
    "}\n"
    "float ao(float2 q) {\n"
    "  float2 c = ntex.Sample(smp0, q).rg; if (c.g < 0.5) return 1.0;\n"
    "  float sr = 0.0, sg = 0.0;\n"
    "  [unroll] for (int j = -1; j <= 1; j++) [unroll] for (int k = -1; k <= 1; k++) { float2 a = ntex.Sample(smp0, q + float2(k, j) * 1.5 * pres.zw).rg; sr += a.r; sg += a.g; }\n"
    "  return sg > 1e-6 ? sr / sg : 1.0;\n"
    "}\n"
    "float3 lut(float3 c) {\n"
    "  float n = pe.w; c = saturate(c); float bl = c.b * (n - 1.0), b0 = floor(bl), b1 = min(b0 + 1.0, n - 1.0), f = bl - b0;\n"
    "  float x = c.r * (n - 1.0) + 0.5, y = (c.g * (n - 1.0) + 0.5) / n;\n"
    "  return lerp(otex.Sample(smp0, float2((b0 * n + x) / (n * n), y)).rgb, otex.Sample(smp0, float2((b1 * n + x) / (n * n), y)).rgb, f);\n"
    "}\n"
    "float4 ps_comp(VP i) : SV_Target {\n"
    "  float2 uv = i.uv, q = uv;\n"
    "  if (pd.z > 0.0) {\n"
    "    float2 d = q - 0.5; q = 0.5 + d * (1.0 + 0.25 * pd.z * dot(2.0 * d, 2.0 * d));\n"
    "    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) return float4(0, 0, 0, 1);\n"
    "  }\n"
    "  float vs = q.y;\n"
    "  if (pd.w > 1.0) q = (floor(q * pres.xy / pd.w) + 0.5) * pd.w * pres.zw;\n"
    "  float3 s = pd.y > 0.5 ? fxaa(q) : S(q);\n"
    "  if (pe.x > 0.0) { float2 o = 0.02 * pe.x * (q - 0.5); s.r = S(q + o).r; s.b = S(q - o).b; }\n"
    "  if (pc.x > 0.0) s = lerp(s, tex2.Sample(smp0, q).rgb * pc.w, pc.x * smoothstep(0.0, 1.0, saturate((abs(q.y - pc.y) - pc.z) / 0.2)));\n"
    "  if (pd.x > 0.0) s *= lerp(1.0, ao(q), pd.x);\n"
    "  float3 c = s * pa.y + tex1.Sample(smp0, q).rgb * pa.x;\n"
    "  if (pa.z > 0.5) { c = max(c, 0.0); c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14); }\n"
    "  float l = dot(c, float3(0.2126, 0.7152, 0.0722)); c = lerp(c, l.xxx, pa.w); c = (c - 0.5) * (1.0 + pb.x) + 0.5;\n"
    "  if (pe.z > 0.0) c = lerp(c, lut(c), pe.z);\n"
    "  if (pb.y > 0.0) { float2 d = (uv - 0.5) * float2(pb.z, 1.0); float k = smoothstep(0.0, 1.0, saturate((length(d) / sqrt(0.25 * pb.z * pb.z + 0.25) - 0.45) / 0.55)); c *= 1.0 - pb.y * k; }\n"
    "  if (pe.y > 0.0) c *= 1.0 - pe.y * (0.5 - 0.5 * cos(6.28318531 * (vs * pres.y - 0.5) / pb.w));\n"
    "  return float4(saturate(c), 1.0);\n"
    "}\n";

typedef struct { float vp[16]; float eye[4], ld[4], lc[4], amb[4], fc[4], fog[4], rim[4], screen[4], misc[4]; float svp[16]; float sh[4]; float env[12]; float nlt[4]; float tm[4]; float cvp[48]; float cp[12]; float shc[4]; float ivp[16]; float soft[4]; float clip[4], rpar[4]; float wm0[4], wm1[4], cut0[4], cut1[4], cut2[4]; } d3d_cb;
typedef struct { ID3D11Texture2D *tex; ID3D11RenderTargetView *rtv; ID3D11ShaderResourceView *srv; int w, h; } d3d_rt;
typedef struct { ID3D11Buffer *vb, *ib, *sb; int icount; } d3d_mesh;
typedef struct { ID3D11Texture2D *tex, *depth; ID3D11ShaderResourceView *srv; ID3D11RenderTargetView *rtv; ID3D11DepthStencilView *dsv; int flags; int w, h; ID3D11ShaderResourceView *dsrv; int fmt; } d3d_tex;

static struct {
    HWND hwnd; int w, h;
    ID3D11Device *dev; ID3D11DeviceContext *ctx; IDXGISwapChain *swap;
    ID3D11RenderTargetView *rtv; ID3D11Texture2D *depth_tex; ID3D11DepthStencilView *dsv; ID3D11ShaderResourceView *dsrv, *ms_dsrv;   /* depth SRVs: soft particles */
    d3d_rt softc; float soft[4];   /* 0.7 soft particles: depth copy (R32F), and w, h, on for the frame constants */
    int msaa; ID3D11Texture2D *ms_tex, *ms_depth; ID3D11RenderTargetView *ms_rtv; ID3D11DepthStencilView *ms_dsv;   /* multisampled screen */
    ID3D11VertexShader *vs_mesh, *vs_line, *vs_quad; ID3D11PixelShader *ps_mesh, *ps_color, *ps_tex;
    ID3D11InputLayout *il_mesh, *il_line, *il_quad;
    ID3D11Buffer *cb_mat;   /* material constants (b2), per batch */
    ID3D11Buffer *cb_quad; float quad_sdf;   /* 0.10.1 b3: 2D batch constants (distance-field text) */
    ID3D11Buffer *cb, *inst_vb, *line_vb, *quad_vb, *imm_vb, *ident_vb, *noskin_vb; size_t inst_cap, line_cap, quad_cap, imm_cap;
    ID3D11Texture2D *bone_tex; ID3D11ShaderResourceView *bone_srv;
    ID3D11Texture2D *lt_tex[3]; ID3D11ShaderResourceView *lt_srv[3];   /* 0.9.5 lights: data 4 x 256 RGBA32F, tiles 16 x 9 R32_UINT, lists 256 x 36 R8_UINT */
    const arc__frame_data *cur_frame;
    bool arr[ARC__MAX_TEXTURES]; ID3D11Texture2D *dummy_arr_tex; ID3D11ShaderResourceView *dummy_arr; bool cut_on;   /* 0.9.6 texture arrays; a 1 x 1 white one */
    ID3D11Texture2D *lsh_tex; ID3D11DepthStencilView *lsh_dsv; ID3D11ShaderResourceView *lsh_srv; int lsh_res; bool lsh_ok;   /* 0.9.5 light shadow atlas (6 x 4 faces) */
    ID3D11RasterizerState *rs_cull, *rs_none, *rs_cull_cw;
    ID3D11DepthStencilState *ds_on, *ds_off, *ds_test;
    ID3D11DepthStencilState *ds_shadow;   /* shadow maps: always LESS (0.7.2: they stay standard with reversed-Z) */
    bool zrev; float dclear;              /* 0.7.2 reversed-Z (arc__pass.zrev), fixed at init: D32_FLOAT depth, cleared to 0, compares swapped */
    ID3D11DepthStencilState *ds_mode[8];   /* per batch (0.6.3): ARC__Z_* compare | ARC__Z_NOWRITE */
    ID3D11BlendState *bs[4];   /* indexed by ARC__BLEND_* */
    ID3D11BlendState *bs_mesh_add;
    ID3D11SamplerState *smp[8];   /* smooth | repeat << 1 | mipmaps << 2 */
    d3d_mesh meshes[ARC__MAX_MESHES]; d3d_tex texs[ARC__MAX_TEXTURES];
    /* shadows */
    ID3D11PixelShader *ps_shadow; ID3D11SamplerState *smp_shadow; ID3D11Texture2D *sh_tex; ID3D11DepthStencilView *sh_dsv; ID3D11ShaderResourceView *sh_srv; int sh_res;
    /* post-processing */
    ID3D11VertexShader *vs_post; ID3D11PixelShader *ps_down, *ps_up, *ps_comp, *ps_sky; ID3D11Buffer *cb_post;
    /* the post buffers: ps[0] the screen's, the rest render targets' own (0.8.2, arc_target_set_post; ps_id: the target) */
    struct d3d_pset {
        d3d_rt scene, bl[ARC__BLOOM_LEVELS], bu[ARC__BLOOM_LEVELS], tl[ARC__TILT_LEVELS], tu[ARC__TILT_LEVELS];
        ID3D11Texture2D *scene_depth, *scene_ms, *scene_ms_depth; ID3D11DepthStencilView *scene_dsv, *scene_ms_dsv; ID3D11RenderTargetView *scene_ms_rtv; int scene_samples;
        ID3D11ShaderResourceView *scene_dsrv, *scene_ms_dsrv; d3d_rt depthc, aort;   /* ssao */
    } ps[1 + ARC__PSET_MAX];
    uint32_t ps_id[1 + ARC__PSET_MAX];
    ID3D11PixelShader *ps_ssao, *ps_dcopy, *ps_dcopy_ms;
} D;
typedef struct d3d_pset d3d_pset;
static d3d_pset *PS = &D.ps[0];   /* the post buffers in use */

#define REL(x) do { if (x) { IUnknown_Release((IUnknown *)(x)); (x) = NULL; } } while (0)

void arc__d3d11_set_window(HWND hwnd, int w, int h) { D.hwnd = hwnd; D.w = w; D.h = h; }

static ID3D11Texture2D *d3d_depth_make(int w, int h, int samples, ID3D11DepthStencilView **dsv, ID3D11ShaderResourceView **srv);
static void make_targets(void) {
    ID3D11Texture2D *bb = NULL;
    IDXGISwapChain_GetBuffer(D.swap, 0, &IID_ID3D11Texture2D, (void **)&bb);
    ID3D11Device_CreateRenderTargetView(D.dev, (ID3D11Resource *)bb, NULL, &D.rtv); REL(bb);
    D3D11_TEXTURE2D_DESC dd = { 0 }; dd.Width = (UINT)D.w; dd.Height = (UINT)D.h; dd.MipLevels = 1; dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.SampleDesc.Count = 1; dd.Usage = D3D11_USAGE_DEFAULT; dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    D.depth_tex = d3d_depth_make(D.w, D.h, 0, &D.dsv, &D.dsrv);   /* readable (0.7 soft particles) */
    if (D.msaa) {   /* draw into a multisampled screen, resolved into the back buffer at the end of the frame */
        D3D11_TEXTURE2D_DESC md = dd; md.Format = DXGI_FORMAT_R8G8B8A8_UNORM; md.SampleDesc.Count = (UINT)D.msaa; md.BindFlags = D3D11_BIND_RENDER_TARGET;
        bool ok = SUCCEEDED(ID3D11Device_CreateTexture2D(D.dev, &md, NULL, &D.ms_tex));
        ok = ok && (D.ms_depth = d3d_depth_make(D.w, D.h, D.msaa, &D.ms_dsv, &D.ms_dsrv)) != NULL;
        ok = ok && SUCCEEDED(ID3D11Device_CreateRenderTargetView(D.dev, (ID3D11Resource *)D.ms_tex, NULL, &D.ms_rtv));
        ok = ok && D.ms_dsv;
        if (!ok) { fprintf(stderr, "ARC log multisampling (%dx) is not available here: drawing without it\n", D.msaa); REL(D.ms_rtv); REL(D.ms_dsv); REL(D.ms_tex); REL(D.ms_depth); REL(D.ms_dsrv); D.msaa = 0; }
    }
}
void arc__d3d11_resize(int w, int h) {
    if (!D.swap || w <= 0 || h <= 0 || (w == D.w && h == D.h)) return;
    D.w = w; D.h = h;
    ID3D11DeviceContext_OMSetRenderTargets(D.ctx, 0, NULL, NULL);
    REL(D.rtv); REL(D.dsv); REL(D.depth_tex); REL(D.ms_rtv); REL(D.ms_dsv); REL(D.ms_tex); REL(D.ms_depth); REL(D.dsrv); REL(D.ms_dsrv);
    IDXGISwapChain_ResizeBuffers(D.swap, 0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
    make_targets();
}
void arc__d3d11_present(bool vsync) { IDXGISwapChain_Present(D.swap, vsync ? 1 : 0, 0); }

typedef HRESULT (WINAPI *pD3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
/* ---- shader cache (0.6.3): compiled shaders kept in the save folder (arc_shaders.d3d11), so later starts skip
   d3dcompiler. Keyed by a hash of the HLSL and ARC's version: any change compiles them all again. --no-shader-cache
   turns it off. File: "ARCSHC1\0", u64 key, u32 count, then per shader u16 name length, name ("entry/target"),
   u32 size, bytes. Cached shaders come back in a small blob object of ARC's own. ---- */
typedef struct { ID3D10Blob base; LONG refs; SIZE_T n; uint8_t *data; } mem_blob;
static HRESULT STDMETHODCALLTYPE mb_qi(ID3D10Blob *b, REFIID r, void **o) { (void)b; (void)r; *o = NULL; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE mb_addref(ID3D10Blob *b) { return (ULONG)InterlockedIncrement(&((mem_blob *)b)->refs); }
static ULONG STDMETHODCALLTYPE mb_release(ID3D10Blob *b) {
    LONG r = InterlockedDecrement(&((mem_blob *)b)->refs);
    if (!r) { free(((mem_blob *)b)->data); free(b); }
    return (ULONG)r;
}
static LPVOID STDMETHODCALLTYPE mb_ptr(ID3D10Blob *b) { return ((mem_blob *)b)->data; }
static SIZE_T STDMETHODCALLTYPE mb_size(ID3D10Blob *b) { return ((mem_blob *)b)->n; }
static ID3D10BlobVtbl mb_vtbl = { mb_qi, mb_addref, mb_release, mb_ptr, mb_size };
static ID3DBlob *mem_blob_make(const void *data, size_t n) {
    mem_blob *b = (mem_blob *)calloc(1, sizeof *b); if (!b) return NULL;
    b->data = (uint8_t *)malloc(n ? n : 1); if (!b->data) { free(b); return NULL; }
    memcpy(b->data, data, n); b->n = n; b->refs = 1; b->base.lpVtbl = &mb_vtbl;
    return (ID3DBlob *)b;
}
#define SC_MAX 32
static struct {
    bool on, dirty; uint64_t key; int n, compiled, loaded;
    struct { char name[48]; uint8_t *data; size_t size; } e[SC_MAX];
} SC;
static const char *sc_path(void) { return arc_fmt("%s/arc_shaders.d3d11", arc__save_dir()); }
static void sc_open(void) {
    memset(&SC, 0, sizeof SC);
    if (arc_arg_flag("no-shader-cache")) return;
    SC.on = true;
    uint64_t h = 1469598103934665603ull;   /* FNV-1a over the source, the version and the compile flags */
    for (const char *p = HLSL; *p; p++) { h ^= (uint8_t)*p; h *= 1099511628211ull; }
    for (const char *p = ARC_VERSION "/O3/defs"; *p; p++) { h ^= (uint8_t)*p; h *= 1099511628211ull; }   /* (0.9.6 "defs": names carry the defines) */
    SC.key = h;
    FILE *fp = fopen(sc_path(), "rb"); if (!fp) return;
    fseek(fp, 0, SEEK_END); long len = ftell(fp); fseek(fp, 0, SEEK_SET);
    uint8_t *f = len > 0 && len < (64 << 20) ? (uint8_t *)malloc((size_t)len) : NULL;
    size_t n = f ? fread(f, 1, (size_t)len, fp) : 0; fclose(fp);
    if (!f) return;
    uint64_t key; uint32_t count;
    if (n >= 20 && !memcmp(f, "ARCSHC1", 8) && (memcpy(&key, f + 8, 8), key == SC.key)) {
        memcpy(&count, f + 16, 4);
        size_t pos = 20;
        for (uint32_t i = 0; i < count && SC.n < SC_MAX; i++) {
            uint16_t l; uint32_t sz;
            if (pos + 2 > n) break;
            memcpy(&l, f + pos, 2); pos += 2;
            if (l >= 48 || pos + l + 4 > n) break;
            memcpy(&sz, f + pos + l, 4);
            if (pos + l + 4 + sz > n) break;
            memcpy(SC.e[SC.n].name, f + pos, l); SC.e[SC.n].name[l] = 0;
            SC.e[SC.n].data = (uint8_t *)malloc(sz ? sz : 1); if (!SC.e[SC.n].data) break;
            memcpy(SC.e[SC.n].data, f + pos + l + 4, sz); SC.e[SC.n].size = sz; SC.n++;
            pos += l + 4 + sz;
        }
    }
    free(f);
}
static void sc_close(void) {   /* write the cache if anything was compiled; free it */
    if (SC.on && SC.dirty) {
        size_t total = 20; for (int i = 0; i < SC.n; i++) total += 2 + strlen(SC.e[i].name) + 4 + SC.e[i].size;
        uint8_t *o = (uint8_t *)malloc(total);
        if (o) {
            uint32_t count = (uint32_t)SC.n; size_t pos = 20;
            memcpy(o, "ARCSHC1", 8); memcpy(o + 8, &SC.key, 8); memcpy(o + 16, &count, 4);
            for (int i = 0; i < SC.n; i++) {
                uint16_t l = (uint16_t)strlen(SC.e[i].name); uint32_t sz = (uint32_t)SC.e[i].size;
                memcpy(o + pos, &l, 2); memcpy(o + pos + 2, SC.e[i].name, l); memcpy(o + pos + 2 + l, &sz, 4);
                memcpy(o + pos + 6 + l, SC.e[i].data, sz); pos += 6 + l + sz;
            }
            const char *path = sc_path(), *tmp = arc_fmt("%s.tmp", path);
            if (arc_file_write(tmp, o, total)) { remove(path); rename(tmp, path); }
            free(o);
        }
    }
    for (int i = 0; i < SC.n; i++) free(SC.e[i].data);
    SC.n = 0;
}
static ID3DBlob *compile_def(pD3DCompile fn, const char *entry, const char *target, const D3D_SHADER_MACRO *defs) {
    /* the cache name carries the defines (0.9.6: ps_ssao is built with or without ARC_ZREV; keyed by entry alone, a
       standard run's copy was handed to a reversed run, and orthographic ssao came out black) */
    char name[48]; int nl = snprintf(name, sizeof name, "%s/%s", entry, target);
    for (const D3D_SHADER_MACRO *m = defs; m && m->Name && nl > 0 && nl < (int)sizeof name; m++) nl += snprintf(name + nl, sizeof name - (size_t)nl, "+%s=%s", m->Name, m->Definition ? m->Definition : "");
    if (nl >= (int)sizeof name) SC.on = false;   /* too long to name safely: no caching from here on */
    if (SC.on) for (int i = 0; i < SC.n; i++) if (!strcmp(SC.e[i].name, name)) { SC.loaded++; return mem_blob_make(SC.e[i].data, SC.e[i].size); }
    if (!fn) { fprintf(stderr, "ARC error d3dcompiler_47.dll not found (needed for shader %s)\n", entry); return NULL; }
    ID3DBlob *code = NULL, *err = NULL;
    HRESULT hr = fn(HLSL, strlen(HLSL), "arc.hlsl", defs, NULL, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr)) { fprintf(stderr, "ARC error HLSL %s: %s\n", entry, err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?"); REL(err); return NULL; }
    REL(err);
    SC.compiled++;
    if (SC.on && SC.n < SC_MAX) {
        size_t sz = ID3D10Blob_GetBufferSize(code); uint8_t *d = (uint8_t *)malloc(sz ? sz : 1);
        if (d) { memcpy(d, ID3D10Blob_GetBufferPointer(code), sz); snprintf(SC.e[SC.n].name, sizeof SC.e[SC.n].name, "%s", name); SC.e[SC.n].data = d; SC.e[SC.n].size = sz; SC.n++; SC.dirty = true; }
    }
    return code;
}
static ID3DBlob *compile(pD3DCompile fn, const char *entry, const char *target) { return compile_def(fn, entry, target, NULL); }
static ID3D11Buffer *make_buffer(UINT bytes, UINT bind, int usage, const void *init) {   /* usage: 0 immutable, 1 dynamic, 2 default (updatable) */
    D3D11_BUFFER_DESC bd = { 0 }; bd.ByteWidth = bytes; bd.BindFlags = bind;
    bd.Usage = usage == 1 ? D3D11_USAGE_DYNAMIC : usage == 2 ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE; bd.CPUAccessFlags = usage == 1 ? D3D11_CPU_ACCESS_WRITE : 0;
    D3D11_SUBRESOURCE_DATA sd = { init, 0, 0 };
    ID3D11Buffer *b = NULL; ID3D11Device_CreateBuffer(D.dev, &bd, init ? &sd : NULL, &b); return b;
}
static void upload(ID3D11Buffer **buf, size_t *cap, UINT bind, const void *data, size_t bytes) {
    if (bytes > *cap) { REL(*buf); *cap = bytes * 3 / 2 + 4096; *buf = make_buffer((UINT)*cap, bind, 1, NULL); }
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ID3D11DeviceContext_Map(D.ctx, (ID3D11Resource *)*buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, data, bytes); ID3D11DeviceContext_Unmap(D.ctx, (ID3D11Resource *)*buf, 0); }
}

static bool d3d_init(void) {
    D.zrev = arc__depth_mode != ARC_DEPTH_STANDARD; D.dclear = D.zrev ? 0.0f : 1.0f;
    arc__depth_precise = D.zrev;   /* D3D's clip range is 0..1 already */
    {   /* ...except under Wine, whose wined3d draws through GL's -1..1 here (measured: the GL fallback's precision) */
        HMODULE nt = GetModuleHandleA("ntdll.dll");
        if (D.zrev && nt && GetProcAddress(nt, "wine_get_version")) arc__depth_precise = false;
    }
    DXGI_SWAP_CHAIN_DESC sc = { 0 };
    sc.BufferDesc.Width = (UINT)D.w; sc.BufferDesc.Height = (UINT)D.h; sc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc.SampleDesc.Count = 1; sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sc.BufferCount = 1;
    sc.OutputWindow = D.hwnd; sc.Windowed = TRUE; sc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    UINT flags = arc_arg_flag("d3d-debug") ? D3D11_CREATE_DEVICE_DEBUG : 0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, fl, 3, D3D11_SDK_VERSION, &sc, &D.swap, &D.dev, NULL, &D.ctx);
    if (FAILED(hr)) hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, fl, 3, D3D11_SDK_VERSION, &sc, &D.swap, &D.dev, NULL, &D.ctx);
    if (FAILED(hr)) { fprintf(stderr, "ARC error D3D11 device creation failed (0x%08lx)\n", (unsigned long)hr); return false; }
    D.msaa = arc__msaa >= 2 ? (arc__msaa >= 8 ? 8 : arc__msaa >= 4 ? 4 : 2) : 0;
    if (D.msaa) { UINT q = 0; if (FAILED(ID3D11Device_CheckMultisampleQualityLevels(D.dev, DXGI_FORMAT_R8G8B8A8_UNORM, (UINT)D.msaa, &q)) || !q) D.msaa = 0; }
    make_targets();

    LARGE_INTEGER qf, q0, q1; QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&q0);
    sc_open();
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll"); if (!dc) dc = LoadLibraryA("d3dcompiler_43.dll");
    pD3DCompile fn = dc ? (pD3DCompile)(void *)GetProcAddress(dc, "D3DCompile") : NULL;
    ID3DBlob *b_vm = compile(fn, "vs_mesh", "vs_4_0"), *b_vl = compile(fn, "vs_line", "vs_4_0"), *b_vq = compile(fn, "vs_quad", "vs_4_0");
    ID3DBlob *b_pm = compile(fn, "ps_mesh", "ps_4_0"), *b_pc = compile(fn, "ps_color", "ps_4_0"), *b_pt = compile(fn, "ps_tex", "ps_4_0");
    if (!b_vm || !b_vl || !b_vq || !b_pm || !b_pc || !b_pt) { sc_close(); return false; }
    {   /* shadows and post-processing (optional: the game still runs without them) */
        ID3DBlob *b_ps = compile(fn, "ps_shadow", "ps_4_0"), *b_vp = compile(fn, "vs_post", "vs_4_0"), *b_pd = compile(fn, "ps_down", "ps_4_0"),
                 *b_pu = compile(fn, "ps_up", "ps_4_0"), *b_pcm = compile(fn, "ps_comp", "ps_4_0"), *b_sk = compile(fn, "ps_sky", "ps_4_0");
        if (b_ps) ID3D11Device_CreatePixelShader(D.dev, ID3D10Blob_GetBufferPointer(b_ps), ID3D10Blob_GetBufferSize(b_ps), NULL, &D.ps_shadow);
        if (b_vp && b_pd && b_pu && b_pcm) {
            ID3D11Device_CreateVertexShader(D.dev, ID3D10Blob_GetBufferPointer(b_vp), ID3D10Blob_GetBufferSize(b_vp), NULL, &D.vs_post);
            ID3D11Device_CreatePixelShader(D.dev, ID3D10Blob_GetBufferPointer(b_pd), ID3D10Blob_GetBufferSize(b_pd), NULL, &D.ps_down);
            ID3D11Device_CreatePixelShader(D.dev, ID3D10Blob_GetBufferPointer(b_pu), ID3D10Blob_GetBufferSize(b_pu), NULL, &D.ps_up);
            ID3D11Device_CreatePixelShader(D.dev, ID3D10Blob_GetBufferPointer(b_pcm), ID3D10Blob_GetBufferSize(b_pcm), NULL, &D.ps_comp);
        }
        if (b_vp && b_sk) ID3D11Device_CreatePixelShader(D.dev, ID3D10Blob_GetBufferPointer(b_sk), ID3D10Blob_GetBufferSize(b_sk), NULL, &D.ps_sky);
        REL(b_ps); REL(b_vp); REL(b_pd); REL(b_pu); REL(b_pcm); REL(b_sk);
        static const D3D_SHADER_MACRO ms_defs[] = { { "ARC_MS", "1" }, { NULL, NULL } };
        static const D3D_SHADER_MACRO rz_defs[] = { { "ARC_ZREV", "1" }, { NULL, NULL } };
        ID3DBlob *b_ao = compile_def(fn, "ps_ssao", "ps_4_0", D.zrev ? rz_defs : NULL), *b_dc = compile(fn, "ps_depthcopy", "ps_4_0"), *b_dm = compile_def(fn, "ps_depthcopy_ms", "ps_4_1", ms_defs);
        if (b_ao && b_dc) {
            ID3D11Device_CreatePixelShader(D.dev, ID3D10Blob_GetBufferPointer(b_ao), ID3D10Blob_GetBufferSize(b_ao), NULL, &D.ps_ssao);
            ID3D11Device_CreatePixelShader(D.dev, ID3D10Blob_GetBufferPointer(b_dc), ID3D10Blob_GetBufferSize(b_dc), NULL, &D.ps_dcopy);
        }
        if (b_dm) ID3D11Device_CreatePixelShader(D.dev, ID3D10Blob_GetBufferPointer(b_dm), ID3D10Blob_GetBufferSize(b_dm), NULL, &D.ps_dcopy_ms);
        REL(b_ao); REL(b_dc); REL(b_dm);
        D3D11_SAMPLER_DESC sd = { 0 }; sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT; sd.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = 0;
        ID3D11Device_CreateSamplerState(D.dev, &sd, &D.smp_shadow);
    }
    sc_close();
    QueryPerformanceCounter(&q1);
    arc_log("shaders: %d from the cache, %d compiled (%.0f ms)", SC.loaded, SC.compiled, (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart);
    ID3D11Device_CreateVertexShader(D.dev, ID3D10Blob_GetBufferPointer(b_vm), ID3D10Blob_GetBufferSize(b_vm), NULL, &D.vs_mesh); ID3D11Device_CreateVertexShader(D.dev, ID3D10Blob_GetBufferPointer(b_vl), ID3D10Blob_GetBufferSize(b_vl), NULL, &D.vs_line);
    ID3D11Device_CreateVertexShader(D.dev, ID3D10Blob_GetBufferPointer(b_vq), ID3D10Blob_GetBufferSize(b_vq), NULL, &D.vs_quad);
    ID3D11Device_CreatePixelShader(D.dev, ID3D10Blob_GetBufferPointer(b_pm), ID3D10Blob_GetBufferSize(b_pm), NULL, &D.ps_mesh);
    ID3D11Device_CreatePixelShader(D.dev, ID3D10Blob_GetBufferPointer(b_pc), ID3D10Blob_GetBufferSize(b_pc), NULL, &D.ps_color); ID3D11Device_CreatePixelShader(D.dev, ID3D10Blob_GetBufferPointer(b_pt), ID3D10Blob_GetBufferSize(b_pt), NULL, &D.ps_tex);
    /* slot 0: vertices, slot 1: skin (a zero-stride default for unskinned meshes), slot 2: instances */
    D3D11_INPUT_ELEMENT_DESC il_mesh[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 4, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 7, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 8, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 112, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 9, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 128, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 10, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 144, D3D11_INPUT_PER_INSTANCE_DATA, 1 } };
    D3D11_INPUT_ELEMENT_DESC il_line[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
    D3D11_INPUT_ELEMENT_DESC il_quad[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
    if (FAILED(ID3D11Device_CreateInputLayout(D.dev, il_mesh, (UINT)(sizeof il_mesh / sizeof il_mesh[0]), ID3D10Blob_GetBufferPointer(b_vm), ID3D10Blob_GetBufferSize(b_vm), &D.il_mesh))) { fprintf(stderr, "ARC error D3D11 mesh input layout\n"); return false; }
    ID3D11Device_CreateInputLayout(D.dev, il_line, 2, ID3D10Blob_GetBufferPointer(b_vl), ID3D10Blob_GetBufferSize(b_vl), &D.il_line);
    ID3D11Device_CreateInputLayout(D.dev, il_quad, 3, ID3D10Blob_GetBufferPointer(b_vq), ID3D10Blob_GetBufferSize(b_vq), &D.il_quad);
    REL(b_vm); REL(b_vl); REL(b_vq); REL(b_pm); REL(b_pc); REL(b_pt);

    D.cb = make_buffer(sizeof(d3d_cb), D3D11_BIND_CONSTANT_BUFFER, 1, NULL);
    D.cb_mat = make_buffer(768, D3D11_BIND_CONSTANT_BUFFER, 1, NULL);   /* (0.9.9: m11; 0.10.1: pat, pjm) */
    D.cb_quad = make_buffer(16, D3D11_BIND_CONSTANT_BUFFER, 1, NULL); D.quad_sdf = -1;
    D.cb_post = make_buffer(352, D3D11_BIND_CONSTANT_BUFFER, 1, NULL);
    D3D11_RASTERIZER_DESC rd = { 0 }; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_BACK; rd.FrontCounterClockwise = TRUE; rd.DepthClipEnable = TRUE; rd.ScissorEnable = TRUE;
    rd.MultisampleEnable = D.msaa ? TRUE : FALSE;
    ID3D11Device_CreateRasterizerState(D.dev, &rd, &D.rs_cull); rd.FrontCounterClockwise = FALSE; ID3D11Device_CreateRasterizerState(D.dev, &rd, &D.rs_cull_cw);
    rd.FrontCounterClockwise = TRUE; rd.CullMode = D3D11_CULL_NONE; ID3D11Device_CreateRasterizerState(D.dev, &rd, &D.rs_none);
    static const float ident[40] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0 };
    D.ident_vb = make_buffer(sizeof ident, D3D11_BIND_VERTEX_BUFFER, 0, ident);
    static const uint8_t noskin[8] = { 0, 0, 0, 0, 255, 0, 0, 0 };
    D.noskin_vb = make_buffer(sizeof noskin, D3D11_BIND_VERTEX_BUFFER, 0, noskin);
    /* skinning matrices: 1536 x 128 RGBA32F, 512 bones per row */
    D3D11_TEXTURE2D_DESC bd2 = { 0 }; bd2.Width = ARC__BONES_PER_ROW * 3; bd2.Height = ARC__MAX_BONES / ARC__BONES_PER_ROW; bd2.MipLevels = 1; bd2.ArraySize = 1;
    bd2.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; bd2.SampleDesc.Count = 1; bd2.Usage = D3D11_USAGE_DYNAMIC; bd2.BindFlags = D3D11_BIND_SHADER_RESOURCE; bd2.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(ID3D11Device_CreateTexture2D(D.dev, &bd2, NULL, &D.bone_tex))) { fprintf(stderr, "ARC error D3D11 bone texture\n"); return false; }
    ID3D11Device_CreateShaderResourceView(D.dev, (ID3D11Resource *)D.bone_tex, NULL, &D.bone_srv);
    {   /* 0.9.6 the white stand-in array */
        static const uint8_t white[4] = { 255, 255, 255, 255 };
        D3D11_TEXTURE2D_DESC td = { 0 }; td.Width = td.Height = 1; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = { white, 4, 0 };
        D3D11_SHADER_RESOURCE_VIEW_DESC sv; memset(&sv, 0, sizeof sv); sv.Format = td.Format; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY; sv.Texture2DArray.MipLevels = 1; sv.Texture2DArray.ArraySize = 1;
        if (FAILED(ID3D11Device_CreateTexture2D(D.dev, &td, &sd, &D.dummy_arr_tex)) || FAILED(ID3D11Device_CreateShaderResourceView(D.dev, (ID3D11Resource *)D.dummy_arr_tex, &sv, &D.dummy_arr))) { fprintf(stderr, "ARC error D3D11 array texture\n"); return false; }
    }
    {   /* 0.9.5 the light textures */
        static const DXGI_FORMAT fm[3] = { DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R8_UINT };
        static const int tw[3] = { 4, ARC__LT_TX, 256 }, th[3] = { ARC__MAX_FRAME_LIGHTS, ARC__LT_TY, ARC__LT_TILES * ARC__LT_CAP / 256 };
        static uint8_t zeros[4 * ARC__MAX_FRAME_LIGHTS * 16];
        for (int k = 0; k < 3; k++) {
            D3D11_TEXTURE2D_DESC td = { 0 }; td.Width = (UINT)tw[k]; td.Height = (UINT)th[k]; td.MipLevels = 1; td.ArraySize = 1; td.Format = fm[k];
            td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DYNAMIC; td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            D3D11_SUBRESOURCE_DATA sd = { zeros, (UINT)(tw[k] * (k == 0 ? 16 : k == 1 ? 4 : 1)), 0 };
            if (FAILED(ID3D11Device_CreateTexture2D(D.dev, &td, &sd, &D.lt_tex[k]))) { fprintf(stderr, "ARC error D3D11 light texture\n"); return false; }
            ID3D11Device_CreateShaderResourceView(D.dev, (ID3D11Resource *)D.lt_tex[k], NULL, &D.lt_srv[k]);
        }
    }
    D3D11_DEPTH_STENCIL_DESC dsd = { 0 }; dsd.DepthEnable = TRUE; dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dsd.DepthFunc = D3D11_COMPARISON_LESS;
    ID3D11Device_CreateDepthStencilState(D.dev, &dsd, &D.ds_shadow);
    D3D11_COMPARISON_FUNC zless = D.zrev ? D3D11_COMPARISON_GREATER : D3D11_COMPARISON_LESS, zgreater = D.zrev ? D3D11_COMPARISON_LESS : D3D11_COMPARISON_GREATER;
    dsd.DepthFunc = zless;
    ID3D11Device_CreateDepthStencilState(D.dev, &dsd, &D.ds_on);
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; ID3D11Device_CreateDepthStencilState(D.dev, &dsd, &D.ds_test);
    dsd.DepthEnable = FALSE; ID3D11Device_CreateDepthStencilState(D.dev, &dsd, &D.ds_off);
    for (int z = 0; z < 8; z++) {
        int zf = z & ARC__Z_FUNC; if (zf == 3) continue;
        dsd.DepthEnable = TRUE; dsd.DepthWriteMask = (z & ARC__Z_NOWRITE) ? D3D11_DEPTH_WRITE_MASK_ZERO : D3D11_DEPTH_WRITE_MASK_ALL;
        dsd.DepthFunc = zf == ARC__Z_ALWAYS ? D3D11_COMPARISON_ALWAYS : zf == ARC__Z_GREATER ? zgreater : zless;   /* (reversed-Z: swapped) */
        ID3D11Device_CreateDepthStencilState(D.dev, &dsd, &D.ds_mode[z]);
    }
    /* blend states give the same results as the software renderer */
    D3D11_BLEND_DESC bd = { 0 }; D3D11_RENDER_TARGET_BLEND_DESC *rt = &bd.RenderTarget[0]; rt->RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11Device_CreateBlendState(D.dev, &bd, &D.bs[ARC__BLEND_NONE]);
    rt->BlendEnable = TRUE; rt->BlendOp = rt->BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt->SrcBlend = D3D11_BLEND_SRC_ALPHA; rt->DestBlend = D3D11_BLEND_INV_SRC_ALPHA; rt->SrcBlendAlpha = D3D11_BLEND_ONE; rt->DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    ID3D11Device_CreateBlendState(D.dev, &bd, &D.bs[ARC__BLEND_ALPHA]);
    rt->SrcBlend = D3D11_BLEND_SRC_ALPHA; rt->DestBlend = D3D11_BLEND_ONE; rt->SrcBlendAlpha = D3D11_BLEND_ONE; rt->DestBlendAlpha = D3D11_BLEND_ONE;
    ID3D11Device_CreateBlendState(D.dev, &bd, &D.bs[ARC__BLEND_ADD]);
    rt->SrcBlendAlpha = D3D11_BLEND_ZERO; ID3D11Device_CreateBlendState(D.dev, &bd, &D.bs_mesh_add);   /* additive meshes keep the target's alpha */
    rt->SrcBlend = D3D11_BLEND_DEST_COLOR; rt->DestBlend = D3D11_BLEND_ZERO; rt->SrcBlendAlpha = D3D11_BLEND_DEST_ALPHA; rt->DestBlendAlpha = D3D11_BLEND_ZERO;
    ID3D11Device_CreateBlendState(D.dev, &bd, &D.bs[ARC__BLEND_MULTIPLY]);
    for (int i = 0; i < 8; i++) {
        bool smooth = i & 1, repeat = (i & 2) != 0, mips = (i & 4) != 0;
        D3D11_SAMPLER_DESC sd = { 0 };
        sd.Filter = smooth ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : (mips ? D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT);
        sd.AddressU = sd.AddressV = sd.AddressW = repeat ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = mips ? D3D11_FLOAT32_MAX : 0;
        ID3D11Device_CreateSamplerState(D.dev, &sd, &D.smp[i]);
    }
    return true;
}
static void d3d_rt_free(d3d_rt *r) { REL(r->srv); REL(r->rtv); REL(r->tex); r->w = r->h = 0; }
static void d3d_post_free(void) {
    d3d_rt_free(&PS->scene); for (int l = 0; l < ARC__BLOOM_LEVELS; l++) { d3d_rt_free(&PS->bl[l]); d3d_rt_free(&PS->bu[l]); }
    for (int l = 0; l < ARC__TILT_LEVELS; l++) { d3d_rt_free(&PS->tl[l]); d3d_rt_free(&PS->tu[l]); }
    REL(PS->scene_dsv); REL(PS->scene_depth); REL(PS->scene_ms_rtv); REL(PS->scene_ms_dsv); REL(PS->scene_ms); REL(PS->scene_ms_depth);
    REL(PS->scene_dsrv); REL(PS->scene_ms_dsrv); d3d_rt_free(&PS->depthc); d3d_rt_free(&PS->aort);
}
static void d3d_shutdown(void) {
    for (int i = 0; i < ARC__MAX_MESHES; i++) { REL(D.meshes[i].vb); REL(D.meshes[i].ib); REL(D.meshes[i].sb); }
    for (int i = 0; i < ARC__MAX_TEXTURES; i++) { d3d_tex *t = &D.texs[i]; REL(t->srv); REL(t->tex); REL(t->rtv); REL(t->dsv); REL(t->depth); }
    REL(D.cb_mat); REL(D.inst_vb); REL(D.imm_vb); REL(D.ident_vb); REL(D.noskin_vb); REL(D.rs_cull_cw); REL(D.line_vb); REL(D.quad_vb); REL(D.cb);
    REL(D.bone_srv); REL(D.bone_tex);
    for (int k = 0; k < 3; k++) { REL(D.lt_srv[k]); REL(D.lt_tex[k]); }
    REL(D.lsh_srv); REL(D.lsh_dsv); REL(D.lsh_tex); REL(D.dummy_arr); REL(D.dummy_arr_tex);
    REL(D.il_mesh); REL(D.il_line); REL(D.il_quad); REL(D.vs_mesh); REL(D.vs_line); REL(D.vs_quad); REL(D.cb_quad); REL(D.ps_mesh); REL(D.ps_color); REL(D.ps_tex);
    REL(D.rs_cull); REL(D.rs_none); REL(D.ds_on); REL(D.ds_shadow); REL(D.ds_off); REL(D.ds_test); for (int i = 0; i < 8; i++) REL(D.ds_mode[i]); for (int i = 0; i < 4; i++) REL(D.bs[i]); REL(D.bs_mesh_add);
    for (int i = 0; i < 8; i++) REL(D.smp[i]);
    REL(D.ms_rtv); REL(D.ms_dsv); REL(D.ms_tex); REL(D.ms_depth);
    REL(D.ps_shadow); REL(D.smp_shadow); REL(D.sh_srv); REL(D.sh_dsv); REL(D.sh_tex);
    for (int i = 0; i <= ARC__PSET_MAX; i++) { PS = &D.ps[i]; d3d_post_free(); D.ps_id[i] = 0; } PS = &D.ps[0];
    REL(D.vs_post); REL(D.ps_down); REL(D.ps_up); REL(D.ps_comp); REL(D.ps_sky); REL(D.cb_post); REL(D.ps_ssao); REL(D.ps_dcopy); REL(D.ps_dcopy_ms);
    d3d_rt_free(&D.softc); REL(D.dsrv); REL(D.ms_dsrv);
    REL(D.rtv); REL(D.dsv); REL(D.depth_tex); REL(D.swap); REL(D.ctx); REL(D.dev);
}
static bool d3d_mesh_create(uint32_t id, const arc_vertex *v, int vc, const uint32_t *idx, int ic, const uint8_t *skin) {
    d3d_mesh *m = &D.meshes[id];
    m->vb = make_buffer((UINT)(sizeof(arc_vertex) * (size_t)vc), D3D11_BIND_VERTEX_BUFFER, 2, v);   /* default usage: arc_mesh_update */
    m->ib = make_buffer((UINT)(sizeof(uint32_t) * (size_t)ic), D3D11_BIND_INDEX_BUFFER, 0, idx);
    if (skin) m->sb = make_buffer((UINT)((size_t)vc * 8), D3D11_BIND_VERTEX_BUFFER, 0, skin);
    m->icount = ic; return m->vb && m->ib && (!skin || m->sb);
}
static void d3d_mesh_update(uint32_t id, const arc_vertex *v, int vc) {
    (void)vc; if (D.meshes[id].vb) ID3D11DeviceContext_UpdateSubresource(D.ctx, (ID3D11Resource *)D.meshes[id].vb, 0, NULL, v, 0, 0);
}
static void d3d_mesh_destroy(uint32_t id) { REL(D.meshes[id].vb); REL(D.meshes[id].ib); REL(D.meshes[id].sb); D.meshes[id].icount = 0; }
static bool d3d_tex_make(uint32_t id, int w, int h, const uint8_t *rgba, int flags, bool target) {
    d3d_tex *t = &D.texs[id]; t->fmt = ARC_TEXFMT_RGBA8;
    bool mips = (flags & ARC_TEX_MIPMAPS) != 0;   /* (targets: only the reflection asks; the GPU makes them after its pass) */
    D3D11_TEXTURE2D_DESC td = { 0 }; td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = mips ? 0 : 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | ((target || mips) ? D3D11_BIND_RENDER_TARGET : 0);
    td.MiscFlags = mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
    D3D11_SUBRESOURCE_DATA sd = { rgba, (UINT)w * 4, 0 };
    if (FAILED(ID3D11Device_CreateTexture2D(D.dev, &td, (rgba && !mips) ? &sd : NULL, &t->tex))) return false;
    ID3D11Device_CreateShaderResourceView(D.dev, (ID3D11Resource *)t->tex, NULL, &t->srv);
    t->flags = flags; t->w = w; t->h = h;
    if (mips && rgba) {   /* level 0 from the pixels, the rest made by the GPU */
        ID3D11DeviceContext_UpdateSubresource(D.ctx, (ID3D11Resource *)t->tex, 0, NULL, rgba, (UINT)w * 4, 0);
        ID3D11DeviceContext_GenerateMips(D.ctx, t->srv);
    }
    if (target) {   /* D3D targets are stored top row first already: no flip needed */
        ID3D11Device_CreateRenderTargetView(D.dev, (ID3D11Resource *)t->tex, NULL, &t->rtv);
        if (!(t->depth = d3d_depth_make(w, h, 0, &t->dsv, &t->dsrv)) || !t->dsv) return false;   /* readable: soft particles */
        float zero[4] = { 0, 0, 0, 0 };
        ID3D11DeviceContext_ClearRenderTargetView(D.ctx, t->rtv, zero);
        ID3D11DeviceContext_ClearDepthStencilView(D.ctx, t->dsv, D3D11_CLEAR_DEPTH, D.dclear, 0);
    }
    return true;
}
static bool d3d_tex_create(uint32_t id, int w, int h, const uint8_t *rgba, int flags) { D.arr[id] = false; return d3d_tex_make(id, w, h, rgba, flags, false); }
/* 0.9.6 a texture array (see arc__backend.tex_create_array): subresource k + i * levels = mip k of layer i */
static bool d3d_tex_create_array(uint32_t id, int w, int h, int n, int levels, const uint8_t *const *mips, const int *mw, const int *mh) {
    D3D11_TEXTURE2D_DESC td = { 0 }; td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = (UINT)levels; td.ArraySize = (UINT)n;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd[ARC__MAX_LAYERS * ARC__MAX_MIPS];
    for (int i = 0; i < n; i++) for (int k = 0; k < levels; k++) { sd[i * levels + k].pSysMem = mips[i * levels + k]; sd[i * levels + k].SysMemPitch = (UINT)(mw[k] * 4); sd[i * levels + k].SysMemSlicePitch = 0; }
    (void)mh;
    d3d_tex *t = &D.texs[id];
    if (FAILED(ID3D11Device_CreateTexture2D(D.dev, &td, sd, &t->tex))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv; memset(&sv, 0, sizeof sv); sv.Format = td.Format; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    sv.Texture2DArray.MipLevels = (UINT)levels; sv.Texture2DArray.ArraySize = (UINT)n;
    if (FAILED(ID3D11Device_CreateShaderResourceView(D.dev, (ID3D11Resource *)t->tex, &sv, &t->srv))) { REL(t->tex); return false; }
    t->w = w; t->h = h; t->flags = ARC_TEX_MESH; t->fmt = ARC_TEXFMT_RGBA8; D.arr[id] = true;
    return true;
}
/* 0.7.1 block-compressed textures: BC1/3/4/5/7 are always there from feature level 10 (asked anyway) */
static DXGI_FORMAT d3d_ctex_format(int fmt) {
    return fmt == ARC_TEXFMT_BC1 ? DXGI_FORMAT_BC1_UNORM : fmt == ARC_TEXFMT_BC3 ? DXGI_FORMAT_BC3_UNORM : fmt == ARC_TEXFMT_BC4 ? DXGI_FORMAT_BC4_UNORM
         : fmt == ARC_TEXFMT_BC5 ? DXGI_FORMAT_BC5_UNORM : fmt == ARC_TEXFMT_BC7 ? DXGI_FORMAT_BC7_UNORM : DXGI_FORMAT_UNKNOWN;
}
static int d3d_ctex_caps = -1;
static int d3d_compressed_formats(void) {
    if (d3d_ctex_caps < 0 && D.dev) {
        static const int fmts[5] = { ARC_TEXFMT_BC1, ARC_TEXFMT_BC3, ARC_TEXFMT_BC4, ARC_TEXFMT_BC5, ARC_TEXFMT_BC7 };
        d3d_ctex_caps = 0;
        for (int i = 0; i < 5; i++) { UINT sup = 0; if (SUCCEEDED(ID3D11Device_CheckFormatSupport(D.dev, d3d_ctex_format(fmts[i]), &sup)) && (sup & D3D11_FORMAT_SUPPORT_TEXTURE2D)) d3d_ctex_caps |= 1 << fmts[i]; }
    }
    return d3d_ctex_caps < 0 ? 0 : d3d_ctex_caps;
}
static bool d3d_tex_create_compressed(uint32_t id, int fmt, int w, int h, int levels, const uint8_t *const *data, const size_t *bytes, int flags) {
    DXGI_FORMAT f = d3d_ctex_format(fmt);
    if (f == DXGI_FORMAT_UNKNOWN || !((d3d_compressed_formats() >> fmt) & 1) || levels < 1 || levels > ARC__MAX_MIPS) return false;
    d3d_tex *t = &D.texs[id];
    D3D11_TEXTURE2D_DESC td = { 0 }; td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = (UINT)levels; td.ArraySize = 1; td.Format = f;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd[ARC__MAX_MIPS];
    for (int i = 0, lw = w; i < levels; i++, lw = lw > 1 ? lw / 2 : 1) {
        sd[i].pSysMem = data[i]; sd[i].SysMemPitch = (UINT)(((lw + 3) / 4) * arc__bc_block_bytes(fmt)); sd[i].SysMemSlicePitch = (UINT)bytes[i];
    }
    if (FAILED(ID3D11Device_CreateTexture2D(D.dev, &td, sd, &t->tex))) { t->tex = NULL; return false; }
    if (FAILED(ID3D11Device_CreateShaderResourceView(D.dev, (ID3D11Resource *)t->tex, NULL, &t->srv))) { REL(t->tex); return false; }
    t->flags = flags; t->w = w; t->h = h; t->fmt = fmt;
    return true;
}
static bool d3d_target_create(uint32_t id, int w, int h, bool smooth) { return d3d_tex_make(id, w, h, NULL, (smooth ? ARC_TEX_SMOOTH : 0) | (arc__textures[id].flags & ARC_TEX_MIPMAPS), true); }   /* (0.8.7 the reflection: mips) */
static void d3d_post_free(void);
static void d3d_tex_destroy(uint32_t id) {
    for (int i = 1; i <= ARC__PSET_MAX; i++) if (D.ps_id[i] == id) { d3d_pset *keep = PS; PS = &D.ps[i]; d3d_post_free(); PS = keep; D.ps_id[i] = 0; }   /* 0.8.2 */
    d3d_tex *t = &D.texs[id]; REL(t->srv); REL(t->tex); REL(t->rtv); REL(t->dsv); REL(t->depth); REL(t->dsrv); t->fmt = ARC_TEXFMT_RGBA8; D.arr[id] = false; }
static void d3d_tex_set_smooth(uint32_t id, bool smooth) { D.texs[id].flags = smooth ? (D.texs[id].flags | ARC_TEX_SMOOTH) : (D.texs[id].flags & ~ARC_TEX_SMOOTH); }
static void d3d_tex_update(uint32_t id, int w, int h, const uint8_t *rgba) {
    (void)h; d3d_tex *t = &D.texs[id];
    ID3D11DeviceContext_UpdateSubresource(D.ctx, (ID3D11Resource *)t->tex, 0, NULL, rgba, (UINT)w * 4, 0);
    if ((t->flags & ARC_TEX_MIPMAPS) && !t->rtv) ID3D11DeviceContext_GenerateMips(D.ctx, t->srv);
}
static ID3D11SamplerState *d3d_sampler(const d3d_tex *t) {
    int f = t->flags; return D.smp[((f & ARC_TEX_SMOOTH) ? 1 : 0) | ((f & ARC_TEX_REPEAT) ? 2 : 0) | ((f & ARC_TEX_MIPMAPS) && !t->rtv ? 4 : 0)];
}

static void d3d_pass_cb(const arc__pass *P, int pw, int ph, bool opaque) {
    d3d_cb cb; memcpy(cb.vp, P->view_proj.m, sizeof cb.vp);
    cb.eye[0] = P->eye.x; cb.eye[1] = P->eye.y; cb.eye[2] = P->eye.z; cb.eye[3] = 0;
    memcpy(cb.ld, P->light_dir, 16); memcpy(cb.lc, P->light_col, 16); memcpy(cb.amb, P->ambient, 16); memcpy(cb.fc, P->fog_col, 16); memcpy(cb.fog, P->fog, 16);
    memcpy(cb.rim, P->rim, 16);
    cb.screen[0] = (float)pw; cb.screen[1] = (float)ph; cb.screen[2] = cb.screen[3] = 0;
    cb.misc[0] = opaque ? 1.0f : 0.0f; cb.misc[1] = cb.misc[2] = cb.misc[3] = 0;
    if (D.zrev && P->zrev) cb.misc[1] = 1;   /* 0.7.2: the matrix is reversed-Z, already 0..1 */
    memcpy(cb.svp, P->shadow_vp, sizeof cb.svp); memcpy(cb.sh, P->shadow, sizeof cb.sh); memcpy(cb.env, P->env, sizeof cb.env);
    bool lon = P->nlights > 0 && P->lt_tiles >= 0 && D.cur_frame && D.cur_frame->light_tiles;
    bool lsh = lon && P->lsh_n > 0 && P->mb_count && D.lsh_ok;   /* light shadows drawn for this pass */
    cb.nlt[0] = lon ? (float)P->nlights : 0; cb.nlt[1] = lsh ? (float)P->lsh_n : 0; cb.nlt[2] = cb.nlt[3] = 0; cb.tm[0] = P->time; cb.tm[1] = cb.tm[2] = cb.tm[3] = 0;
    memcpy(cb.cvp, P->shadow_cvp, sizeof cb.cvp); memcpy(cb.cp, P->shadow_cp, sizeof cb.cp);
    memcpy(cb.ivp, P->ivp, sizeof cb.ivp); memcpy(cb.soft, D.soft, sizeof cb.soft);
    memcpy(cb.clip, P->clip, sizeof cb.clip);   /* 0.8.7 */
    {   /* 0.9.9 world mask (t19) and cutaway */
        bool wm = P->wm[7] > 0.5f && P->wmask && D.texs[P->wmask].srv;
        memcpy(cb.wm0, P->wm, 16); memcpy(cb.wm1, P->wm + 4, 16); cb.wm1[3] = wm ? 1.0f : 0.0f;
        memcpy(cb.cut0, P->cut, 16); memcpy(cb.cut1, P->cut + 4, 16); memcpy(cb.cut2, P->cut + 8, 16);
        D.cut_on = P->cut[8] > 0.5f;
        ID3D11ShaderResourceView *ws = D.texs[wm ? P->wmask : 1].srv; ID3D11DeviceContext_PSSetShaderResources(D.ctx, 19, 1, &ws);
    }
    bool rt = P->refl_tex && P->refl[0] > 0 && D.texs[P->refl_tex].srv;
    cb.rpar[0] = rt ? P->refl[0] : 0; cb.rpar[1] = P->refl[1]; cb.rpar[2] = P->refl[2]; cb.rpar[3] = P->refl[3];
    cb.shc[0] = (float)(P->shadow_n > 0 ? P->shadow_n : 1); cb.shc[1] = P->shadow_soft; cb.shc[2] = P->shadow_n > 1 ? 0.5f : 1.0f; cb.shc[3] = 0;
    if (!(D.sh_srv && D.sh_res == (P->shadow_n > 1 ? P->shadow_res * 2 : P->shadow_res))) cb.sh[0] = 0;
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ID3D11DeviceContext_Map(D.ctx, (ID3D11Resource *)D.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, &cb, sizeof cb); ID3D11DeviceContext_Unmap(D.ctx, (ID3D11Resource *)D.cb, 0); }
    if (lon) {   /* 0.9.5 this pass's light tiles and lists (the light data went up with the frame) */
        if (SUCCEEDED(ID3D11DeviceContext_Map(D.ctx, (ID3D11Resource *)D.lt_tex[1], 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            for (int y = 0; y < ARC__LT_TY; y++) memcpy((uint8_t *)m.pData + (size_t)y * m.RowPitch, D.cur_frame->light_tiles + P->lt_tiles + y * ARC__LT_TX, ARC__LT_TX * 4);
            ID3D11DeviceContext_Unmap(D.ctx, (ID3D11Resource *)D.lt_tex[1], 0);
        }
        if (SUCCEEDED(ID3D11DeviceContext_Map(D.ctx, (ID3D11Resource *)D.lt_tex[2], 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            const uint8_t *src = D.cur_frame->light_index + P->lt_idx; int n = P->lt_idx_count;
            for (int y = 0; y * 256 < n; y++) memcpy((uint8_t *)m.pData + (size_t)y * m.RowPitch, src + y * 256, (size_t)(n - y * 256 < 256 ? n - y * 256 : 256));
            ID3D11DeviceContext_Unmap(D.ctx, (ID3D11Resource *)D.lt_tex[2], 0);
        }
    }
    ID3D11DeviceContext_PSSetShaderResources(D.ctx, 14, 3, D.lt_srv);
    { ID3D11ShaderResourceView *ls = lsh ? D.lsh_srv : NULL; ID3D11DeviceContext_PSSetShaderResources(D.ctx, 17, 1, &ls); }
}
static struct { uint32_t paint_tex, tint2, tint3; const arc__pattern *pat; } g_paint;   /* 0.8.8 set per batch before d3d_material */
/* a mesh's material: constants (b2) and maps (t5-t7, white when missing); NULL = none */
static void d3d_material(const arc__mesh_rec *M) {
    ID3D11DeviceContext *c = D.ctx; bool on = M && M->has_mat; float m[192] = { 0 };
    if (on) { memcpy(m, M->mat, 32); m[8] = M->mat[ARC__MAT_ER]; m[9] = M->mat[ARC__MAT_EG]; m[10] = M->mat[ARC__MAT_EB]; m[11] = 1; }
    if (on && M->mat_tex[0] && D.texs[M->mat_tex[0]].srv && D.texs[M->mat_tex[0]].fmt == ARC_TEXFMT_BC5) m[11] = 2;   /* 0.7.1: BC5 normal map: Z rebuilt */
    uint32_t dt = 0, mt = 0;   /* 0.8.4 m3: detail mode, scale, strength, rotate; m4: base mode, rotate, contrast, detail contrast */
    if (M && (M->mat2[ARC__MAT2_DMODE] > 0 || M->mat2[ARC__MAT2_BMODE] > 0)) {
        const float *q = M->mat2; dt = M->detail_tex && D.texs[M->detail_tex].srv ? M->detail_tex : 0;
        m[12] = dt ? q[ARC__MAT2_DMODE] : 0; m[13] = q[ARC__MAT2_DSCALE]; m[14] = q[ARC__MAT2_DSTR]; m[15] = q[ARC__MAT2_DROT];
        m[16] = q[ARC__MAT2_BMODE]; m[17] = q[ARC__MAT2_BROT]; m[18] = q[ARC__MAT2_BCON]; m[19] = q[ARC__MAT2_DCON];
        mt = dt && M->mask_tex && q[ARC__MAT2_MON] > 0 && D.texs[M->mask_tex].srv ? M->mask_tex : 0;
        if (mt) { float ms = q[ARC__MAT2_MSCALE], ds = q[ARC__MAT2_DSCALE];   /* m5: mask scale relative to the detail's uv, threshold, softness, on */
            m[20] = ds > 0 ? (ms > 0 ? ms / ds : 1.0f) : (ms > 0 ? ms : 1.0f); m[21] = q[ARC__MAT2_MTHR]; m[22] = q[ARC__MAT2_MSOFT]; m[23] = 1; }
    }
    D3D11_MAPPED_SUBRESOURCE mm;
    if (SUCCEEDED(ID3D11DeviceContext_Map(c, (ID3D11Resource *)D.cb_mat, 0, D3D11_MAP_WRITE_DISCARD, 0, &mm))) { m[24] = M ? M->reflect : 0; m[25] = M ? M->mat2[ARC__MAT2_BSCALE] : 0;
    uint32_t pt = g_paint.paint_tex && D.texs[g_paint.paint_tex].srv ? g_paint.paint_tex : 0;   /* 0.8.8 the batch's paint scheme */
    if (pt) { arc__unpack_rgb(g_paint.tint2, m + 28); m[31] = 1; if (g_paint.tint3 >> 24) { arc__unpack_rgb(g_paint.tint3 & 0xFFFFFF, m + 32); m[35] = 1; } }
    {   /* 0.9.6 splat */
        bool sp = M && M->layers_tex && D.texs[M->layers_tex].srv && D.arr[M->layers_tex];
        for (int i = 0; i < 4; i++) m[36 + i] = sp ? M->mat3[ARC__MAT3_S0 + i] : 1;
        if (sp && M->layers_mode == 1) m[43] = 1;   /* 0.9.9 the colour by layer */
        else if (sp) { m[40] = M->mat3[ARC__MAT3_SHARP]; m[41] = M->mat3[ARC__MAT3_N]; m[42] = 1; }
        ID3D11ShaderResourceView *ls = sp ? D.texs[M->layers_tex].srv : D.dummy_arr; ID3D11DeviceContext_PSSetShaderResources(c, 18, 1, &ls);
    }
    m[44] = D.cut_on && M && M->cutaway ? 1.0f : 0.0f;   /* 0.9.9 */
    if (g_paint.pat) { memcpy(m + 48, g_paint.pat->p, sizeof g_paint.pat->p); memcpy(m + 64, g_paint.pat->jm, sizeof g_paint.pat->jm); }   /* 0.10.1 */
    memcpy(mm.pData, m, 768);
    ID3D11ShaderResourceView *psrv = D.texs[pt ? pt : 1].srv; ID3D11DeviceContext_PSSetShaderResources(c, 13, 1, &psrv);
    ID3D11SamplerState *s4 = d3d_sampler(&D.texs[pt ? pt : 1]); ID3D11DeviceContext_PSSetSamplers(c, 4, 1, &s4);   /* the mask's own filtering */ ID3D11DeviceContext_Unmap(c, (ID3D11Resource *)D.cb_mat, 0); }
    ID3D11ShaderResourceView *dsrv = D.texs[dt ? dt : 1].srv; ID3D11DeviceContext_PSSetShaderResources(c, 10, 1, &dsrv);
    ID3D11ShaderResourceView *msrv = D.texs[mt ? mt : 1].srv; ID3D11DeviceContext_PSSetShaderResources(c, 11, 1, &msrv);
    ID3D11DeviceContext_PSSetConstantBuffers(c, 2, 1, &D.cb_mat); ID3D11DeviceContext_VSSetConstantBuffers(c, 2, 1, &D.cb_mat);   /* (0.10.1: the vertex shader reads the pattern's part mask) */
    ID3D11ShaderResourceView *srv[3];
    for (int i = 0; i < 3; i++) { uint32_t t = on && M->mat_tex[i] && D.texs[M->mat_tex[i]].srv ? M->mat_tex[i] : 1; srv[i] = D.texs[t].srv; }
    ID3D11DeviceContext_PSSetShaderResources(c, 5, 3, srv);
    ID3D11SamplerState *s2 = D.smp[7]; ID3D11DeviceContext_PSSetSamplers(c, 2, 1, &s2);
}
static void d3d_scissor(int x0, int y0, int x1, int y1) { D3D11_RECT r = { x0, y0, x1, y1 }; ID3D11DeviceContext_RSSetScissorRects(D.ctx, 1, &r); }
/* each batch sets its own blend, depth state and opaque flag (0.6.3); left as the solid defaults afterwards */
static void d3d_draw_batches(const arc__frame_data *f, int first, int count, const arc__pass *P, int pw, int ph) {
    ID3D11DeviceContext *c = D.ctx; float bf[4] = { 0 }; int state = -1, opaque = -1;
    for (int b = first; b < first + count; b++) {
        const arc__mesh_batch *mb = &f->mesh_batches[b]; d3d_mesh *dm = &D.meshes[mb->mesh];
        if (!dm->vb) continue;
        const arc__mesh_rec *M = &arc__meshes[mb->mesh];
        d3d_tex *t = &D.texs[M->texture && D.texs[M->texture].srv ? M->texture : 1];
        ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &t->srv);
        ID3D11SamplerState *smp = d3d_sampler(t);
        g_paint.paint_tex = mb->paint_tex; g_paint.tint2 = mb->tint2; g_paint.tint3 = mb->tint3; g_paint.pat = mb->pat > 0 && mb->pat <= f->pat_count ? &f->pats[mb->pat - 1] : NULL;
        if (M->mat2[ARC__MAT2_BMODE] > 0.5f) smp = D.smp[((t->flags & ARC_TEX_SMOOTH) ? 1 : 0) | 2 | ((t->flags & ARC_TEX_MIPMAPS) && !t->rtv ? 4 : 0)];   /* 0.8.8: hex / world colour repeats */
        ID3D11DeviceContext_PSSetSamplers(c, 0, 1, &smp);
        d3d_material(M);
        if (state != (mb->blend << 4 | mb->depth)) {
            state = mb->blend << 4 | mb->depth;
            ID3D11DeviceContext_OMSetBlendState(c, mb->blend == ARC__MB_ADD ? D.bs_mesh_add : mb->blend ? D.bs[ARC__BLEND_ALPHA] : D.bs[ARC__BLEND_NONE], bf, 0xFFFFFFFF);
            ID3D11DepthStencilState *ds = D.ds_mode[mb->depth & 7]; ID3D11DeviceContext_OMSetDepthStencilState(c, ds ? ds : D.ds_on, 0);
            int op = mb->blend == ARC__MB_OPAQUE;
            if (op != opaque) { opaque = op; d3d_pass_cb(P, pw, ph, op != 0); }
        }
        ID3D11Buffer *vbs[3] = { dm->vb, dm->sb ? dm->sb : D.noskin_vb, D.inst_vb };
        UINT strides[3] = { sizeof(arc_vertex), dm->sb ? 8u : 0u, sizeof(arc__instance) }, offs[3] = { 0, 0, 0 };
        ID3D11DeviceContext_IASetVertexBuffers(c, 0, 3, vbs, strides, offs);
        ID3D11DeviceContext_IASetIndexBuffer(c, dm->ib, DXGI_FORMAT_R32_UINT, 0);
        ID3D11DeviceContext_RSSetState(c, (M->double_sided || (D.cut_on && M->cutaway)) ? D.rs_none : (mb->flip ^ P->mirror) ? D.rs_cull_cw : D.rs_cull);   /* (0.8.7 mirror passes wind the other way; 0.9.9 cut meshes show their backs: the cap) */
        ID3D11DeviceContext_DrawIndexedInstanced(c, (UINT)dm->icount, (UINT)mb->count, 0, 0, (UINT)mb->first);
    }
    ID3D11DeviceContext_RSSetState(c, D.rs_cull); g_paint.paint_tex = g_paint.tint2 = g_paint.tint3 = 0; g_paint.pat = NULL;
    ID3D11DeviceContext_OMSetBlendState(c, D.bs[ARC__BLEND_NONE], bf, 0xFFFFFFFF); ID3D11DeviceContext_OMSetDepthStencilState(c, D.ds_on, 0);
    if (opaque == 0) d3d_pass_cb(P, pw, ph, true);
}

/* ---- shadow map ---- */
static void d3d_shadow_pass(const arc__frame_data *f, const arc__pass *P) {
    ID3D11DeviceContext *c = D.ctx;
    if (!D.ps_shadow) return;
    int atlas = P->shadow_n > 1 ? P->shadow_res * 2 : P->shadow_res;   /* cascades: a 2x2 atlas */
    if (D.sh_res != atlas) {
        REL(D.sh_srv); REL(D.sh_dsv); REL(D.sh_tex); D.sh_res = 0;
        D3D11_TEXTURE2D_DESC td = { 0 }; td.Width = td.Height = (UINT)atlas; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_TYPELESS;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(ID3D11Device_CreateTexture2D(D.dev, &td, NULL, &D.sh_tex))) return;
        D3D11_DEPTH_STENCIL_VIEW_DESC dv; memset(&dv, 0, sizeof dv); dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv; memset(&sv, 0, sizeof sv); sv.Format = DXGI_FORMAT_R32_FLOAT; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
        if (FAILED(ID3D11Device_CreateDepthStencilView(D.dev, (ID3D11Resource *)D.sh_tex, &dv, &D.sh_dsv)) ||
            FAILED(ID3D11Device_CreateShaderResourceView(D.dev, (ID3D11Resource *)D.sh_tex, &sv, &D.sh_srv))) { REL(D.sh_srv); REL(D.sh_dsv); REL(D.sh_tex); return; }
        D.sh_res = atlas;
    }
    ID3D11ShaderResourceView *nul = NULL; ID3D11DeviceContext_PSSetShaderResources(c, 2, 1, &nul);
    ID3D11DeviceContext_OMSetRenderTargets(c, 0, NULL, D.sh_dsv);
    ID3D11DeviceContext_ClearDepthStencilView(c, D.sh_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    d3d_scissor(0, 0, D.sh_res, D.sh_res);
    float bf[4] = { 0 };
    for (int ci = 0; ci < (P->shadow_n > 1 ? P->shadow_n : 1); ci++) {
    D3D11_VIEWPORT vp = { 0, 0, (float)D.sh_res, (float)D.sh_res, 0, 1 };
    if (P->shadow_n > 1) { vp.TopLeftX = P->shadow_cp[ci * 4 + 2] * (float)atlas; vp.TopLeftY = (0.5f - P->shadow_cp[ci * 4 + 3]) * (float)atlas; vp.Width = vp.Height = (float)P->shadow_res; }
    ID3D11DeviceContext_RSSetViewports(c, 1, &vp);
    arc__pass L = *P; memcpy(L.view_proj.m, P->shadow_n > 1 ? P->shadow_cvp + ci * 16 : P->shadow_vp, sizeof L.view_proj.m); L.zrev = 0;
    d3d_pass_cb(&L, D.sh_res, D.sh_res, true);
    ID3D11DeviceContext_OMSetBlendState(c, D.bs[ARC__BLEND_NONE], bf, 0xFFFFFFFF); ID3D11DeviceContext_OMSetDepthStencilState(c, D.ds_shadow, 0);
    ID3D11DeviceContext_IASetInputLayout(c, D.il_mesh); ID3D11DeviceContext_IASetPrimitiveTopology(c, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(c, D.vs_mesh, NULL, 0); ID3D11DeviceContext_PSSetShader(c, D.ps_shadow, NULL, 0);
    ID3D11DeviceContext_RSSetState(c, D.rs_none);
    for (int b = P->mb_first; b < P->mb_first + P->mb_count; b++) {
        const arc__mesh_batch *mb = &f->mesh_batches[b]; d3d_mesh *dm = &D.meshes[mb->mesh];
        const arc__mesh_rec *M = &arc__meshes[mb->mesh];
        if (!dm->vb || M->no_shadow || mb->blend || mb->depth) continue;
        d3d_tex *t = &D.texs[M->texture && D.texs[M->texture].srv && !M->layers_tex ? M->texture : 1];   /* (0.9.6 a splat map is no cut-out) */
        ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &t->srv);
        ID3D11SamplerState *smp = d3d_sampler(t); ID3D11DeviceContext_PSSetSamplers(c, 0, 1, &smp);
        ID3D11Buffer *vbs[3] = { dm->vb, dm->sb ? dm->sb : D.noskin_vb, D.inst_vb };
        UINT strides[3] = { sizeof(arc_vertex), dm->sb ? 8u : 0u, sizeof(arc__instance) }, offs[3] = { 0, 0, 0 };
        ID3D11DeviceContext_IASetVertexBuffers(c, 0, 3, vbs, strides, offs);
        ID3D11DeviceContext_IASetIndexBuffer(c, dm->ib, DXGI_FORMAT_R32_UINT, 0);
        ID3D11DeviceContext_DrawIndexedInstanced(c, (UINT)dm->icount, (UINT)mb->count, 0, 0, (UINT)mb->first);
    }
    }
    ID3D11DeviceContext_RSSetState(c, D.rs_cull);
}
/* 0.9.5 light shadows: the pass's solid meshes from each shadowed light's faces into the atlas (see arc__pass.lsh_n;
   face (f, s) in GL's rows from the bottom is D3D's row 3 - s from the top) */
static void d3d_lshadow_pass(const arc__frame_data *f, const arc__pass *P) {
    ID3D11DeviceContext *c = D.ctx; D.lsh_ok = false;
    if (!D.ps_shadow) return;
    int r = f->lsh_res;
    if (D.lsh_res != r) {
        REL(D.lsh_srv); REL(D.lsh_dsv); REL(D.lsh_tex); D.lsh_res = -r;
        D3D11_TEXTURE2D_DESC td = { 0 }; td.Width = (UINT)(r * 6); td.Height = (UINT)(r * 4); td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_TYPELESS;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(ID3D11Device_CreateTexture2D(D.dev, &td, NULL, &D.lsh_tex))) return;
        D3D11_DEPTH_STENCIL_VIEW_DESC dv; memset(&dv, 0, sizeof dv); dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv; memset(&sv, 0, sizeof sv); sv.Format = DXGI_FORMAT_R32_FLOAT; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
        if (FAILED(ID3D11Device_CreateDepthStencilView(D.dev, (ID3D11Resource *)D.lsh_tex, &dv, &D.lsh_dsv)) ||
            FAILED(ID3D11Device_CreateShaderResourceView(D.dev, (ID3D11Resource *)D.lsh_tex, &sv, &D.lsh_srv))) { REL(D.lsh_srv); REL(D.lsh_dsv); REL(D.lsh_tex); return; }
        D.lsh_res = r;
    }
    if (D.lsh_res != r) return;
    ID3D11ShaderResourceView *nul = NULL; ID3D11DeviceContext_PSSetShaderResources(c, 17, 1, &nul);
    ID3D11DeviceContext_OMSetRenderTargets(c, 0, NULL, D.lsh_dsv);
    ID3D11DeviceContext_ClearDepthStencilView(c, D.lsh_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    d3d_scissor(0, 0, r * 6, r * 4);
    float bf[4] = { 0 };
    for (int sl = 0; sl < P->lsh_n; sl++) for (int fi = 0; fi < f->lsh_faces[sl]; fi++) {
        D3D11_VIEWPORT vp = { (float)(fi * r), (float)((3 - sl) * r), (float)r, (float)r, 0, 1 };
        ID3D11DeviceContext_RSSetViewports(c, 1, &vp);
        arc__pass L = *P; memcpy(L.view_proj.m, f->lsh_vp + (sl * 6 + fi) * 16, sizeof L.view_proj.m); L.zrev = 0;
        d3d_pass_cb(&L, r * 6, r * 4, true);
        ID3D11DeviceContext_OMSetBlendState(c, D.bs[ARC__BLEND_NONE], bf, 0xFFFFFFFF); ID3D11DeviceContext_OMSetDepthStencilState(c, D.ds_shadow, 0);
        ID3D11DeviceContext_IASetInputLayout(c, D.il_mesh); ID3D11DeviceContext_IASetPrimitiveTopology(c, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_VSSetShader(c, D.vs_mesh, NULL, 0); ID3D11DeviceContext_PSSetShader(c, D.ps_shadow, NULL, 0);
        ID3D11DeviceContext_RSSetState(c, D.rs_none);
        for (int b = P->mb_first; b < P->mb_first + P->mb_count; b++) {
            const arc__mesh_batch *mb = &f->mesh_batches[b]; d3d_mesh *dm = &D.meshes[mb->mesh];
            const arc__mesh_rec *M = &arc__meshes[mb->mesh];
            if (!dm->vb || M->no_shadow || mb->blend || mb->depth) continue;
            d3d_tex *t = &D.texs[M->texture && D.texs[M->texture].srv && !M->layers_tex ? M->texture : 1];   /* (0.9.6 a splat map is no cut-out) */
            ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &t->srv);
            ID3D11SamplerState *smp = d3d_sampler(t); ID3D11DeviceContext_PSSetSamplers(c, 0, 1, &smp);
            ID3D11Buffer *vbs[3] = { dm->vb, dm->sb ? dm->sb : D.noskin_vb, D.inst_vb };
            UINT strides[3] = { sizeof(arc_vertex), dm->sb ? 8u : 0u, sizeof(arc__instance) }, offs[3] = { 0, 0, 0 };
            ID3D11DeviceContext_IASetVertexBuffers(c, 0, 3, vbs, strides, offs);
            ID3D11DeviceContext_IASetIndexBuffer(c, dm->ib, DXGI_FORMAT_R32_UINT, 0);
            ID3D11DeviceContext_DrawIndexedInstanced(c, (UINT)dm->icount, (UINT)mb->count, 0, 0, (UINT)mb->first);
        }
    }
    ID3D11DeviceContext_RSSetState(c, D.rs_cull);
    D.lsh_ok = true;
}
/* ---- post-processing ---- */
static bool d3d_rt_make(d3d_rt *r, int w, int h, DXGI_FORMAT fmt) {
    D3D11_TEXTURE2D_DESC td = { 0 }; td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(ID3D11Device_CreateTexture2D(D.dev, &td, NULL, &r->tex))) return false;
    ID3D11Device_CreateRenderTargetView(D.dev, (ID3D11Resource *)r->tex, NULL, &r->rtv);
    ID3D11Device_CreateShaderResourceView(D.dev, (ID3D11Resource *)r->tex, NULL, &r->srv);
    r->w = w; r->h = h; return r->rtv && r->srv;
}
static ID3D11Texture2D *d3d_depth_make(int w, int h, int samples, ID3D11DepthStencilView **dsv, ID3D11ShaderResourceView **srv) {
    D3D11_TEXTURE2D_DESC dd = { 0 }; dd.Width = (UINT)w; dd.Height = (UINT)h; dd.MipLevels = 1; dd.ArraySize = 1;
    dd.Format = srv ? DXGI_FORMAT_R24G8_TYPELESS : DXGI_FORMAT_D24_UNORM_S8_UINT; dd.SampleDesc.Count = (UINT)(samples ? samples : 1); dd.Usage = D3D11_USAGE_DEFAULT;
    if (D.zrev) dd.Format = srv ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_D32_FLOAT;   /* 0.7.2 reversed-Z: float depth */
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL | (srv ? D3D11_BIND_SHADER_RESOURCE : 0);
    ID3D11Texture2D *t = NULL;
    if (FAILED(ID3D11Device_CreateTexture2D(D.dev, &dd, NULL, &t))) return NULL;
    D3D11_DEPTH_STENCIL_VIEW_DESC dv = { 0 }; dv.Format = D.zrev ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D24_UNORM_S8_UINT; dv.ViewDimension = samples ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
    ID3D11Device_CreateDepthStencilView(D.dev, (ID3D11Resource *)t, &dv, dsv);
    if (srv) {   /* ssao reads the depth */
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = { 0 }; sv.Format = D.zrev ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        sv.ViewDimension = samples ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
        ID3D11Device_CreateShaderResourceView(D.dev, (ID3D11Resource *)t, &sv, srv);
    }
    return t;
}
static bool d3d_post_ready_s(int w, int h, int msaa) {
    if (!D.ps_comp) return false;
    if (PS->scene.tex && PS->scene.w == w && PS->scene.h == h) return true;
    d3d_post_free();
    DXGI_FORMAT fmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (!d3d_rt_make(&PS->scene, w, h, fmt)) { d3d_post_free(); return false; }
    PS->scene_depth = d3d_depth_make(w, h, 0, &PS->scene_dsv, &PS->scene_dsrv);
    PS->scene_samples = 0;
    if (msaa) {
        UINT q = 0;
        if (SUCCEEDED(ID3D11Device_CheckMultisampleQualityLevels(D.dev, fmt, (UINT)msaa, &q)) && q) {
            D3D11_TEXTURE2D_DESC td = { 0 }; td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt;
            td.SampleDesc.Count = (UINT)msaa; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
            if (SUCCEEDED(ID3D11Device_CreateTexture2D(D.dev, &td, NULL, &PS->scene_ms))) {
                ID3D11Device_CreateRenderTargetView(D.dev, (ID3D11Resource *)PS->scene_ms, NULL, &PS->scene_ms_rtv);
                PS->scene_ms_depth = d3d_depth_make(w, h, msaa, &PS->scene_ms_dsv, &PS->scene_ms_dsrv);
                if (PS->scene_ms_rtv && PS->scene_ms_dsv) PS->scene_samples = msaa;
            }
        }
    }
    if (D.ps_ssao && (!d3d_rt_make(&PS->depthc, w, h, DXGI_FORMAT_R32_FLOAT) || !d3d_rt_make(&PS->aort, w, h, fmt))) { d3d_rt_free(&PS->depthc); d3d_rt_free(&PS->aort); }
    for (int l = 0; l < ARC__BLOOM_LEVELS; l++) {
        int lw = w >> (l + 1), lh = h >> (l + 1); if (lw < 1) lw = 1; if (lh < 1) lh = 1;
        if (!d3d_rt_make(&PS->bl[l], lw, lh, fmt) || !d3d_rt_make(&PS->bu[l], lw, lh, fmt)) { d3d_post_free(); return false; }
        if (l < ARC__TILT_LEVELS && (!d3d_rt_make(&PS->tl[l], lw, lh, fmt) || !d3d_rt_make(&PS->tu[l], lw, lh, fmt))) { d3d_post_free(); return false; }
    }
    return PS->scene_dsv != NULL;
}
static bool d3d_post_ready(int w, int h) { return d3d_post_ready_s(w, h, D.msaa); }
/* 0.8.2: the post buffers of render target id (a free set if make); targets are not multisampled, nor is their post */
static d3d_pset *d3d_pset_for(uint32_t id, bool make) {
    for (int i = 1; i <= ARC__PSET_MAX; i++) if (D.ps_id[i] == id) return &D.ps[i];
    if (make) for (int i = 1; i <= ARC__PSET_MAX; i++) if (!D.ps_id[i]) { D.ps_id[i] = id; return &D.ps[i]; }
    return NULL;
}
static float g_post_pc[4];   /* tilt-shift: strength, focus, width */
static float g_post_x[48];   /* the rest of cbuffer PP from pd: pd, pe, pres, aivp, avp, ass */
static void d3d_post_cb(const float a[4], const float b[4]) {
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ID3D11DeviceContext_Map(D.ctx, (ID3D11Resource *)D.cb_post, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memset(m.pData, 0, 160); memcpy(m.pData, a, 16); memcpy((uint8_t *)m.pData + 16, b, 16); memcpy((uint8_t *)m.pData + 144, g_post_pc, 16);
        memcpy((uint8_t *)m.pData + 160, g_post_x, sizeof g_post_x);
        ID3D11DeviceContext_Unmap(D.ctx, (ID3D11Resource *)D.cb_post, 0);
    }
}
static void d3d_fullscreen(ID3D11RenderTargetView *rtv, int w, int h, ID3D11PixelShader *ps, ID3D11ShaderResourceView *t0, ID3D11ShaderResourceView *t1) {
    ID3D11DeviceContext *c = D.ctx;
    ID3D11ShaderResourceView *nul[2] = { NULL, NULL }; ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, nul); ID3D11DeviceContext_PSSetShaderResources(c, 3, 1, nul);
    ID3D11DeviceContext_OMSetRenderTargets(c, 1, &rtv, NULL);
    D3D11_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 }; ID3D11DeviceContext_RSSetViewports(c, 1, &vp); d3d_scissor(0, 0, w, h);
    ID3D11DeviceContext_PSSetShader(c, ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &t0); ID3D11DeviceContext_PSSetShaderResources(c, 3, 1, &t1);
    ID3D11DeviceContext_Draw(c, 3, 0);
}
/* a blur chain (as the soft renderer's soft_chain) */
static void d3d_chain(d3d_rt *down, d3d_rt *up, int levels, float threshold, float r) {
    float z[4] = { 0, 0, 0, 0 };
    for (int l = 0; l < levels; l++) {
        const d3d_rt *src = l ? &down[l - 1] : &PS->scene;
        float a[4] = { 1.0f / (float)src->w, 1.0f / (float)src->h, l ? -1.0f : threshold, 0 };
        d3d_post_cb(a, z);
        d3d_fullscreen(down[l].rtv, down[l].w, down[l].h, D.ps_down, src->srv, NULL);
    }
    for (int l = levels - 1; l >= 0; l--) {
        if (l == levels - 1) { float a[4] = { 0, 0, -1, 0 }; d3d_post_cb(a, z); d3d_fullscreen(up[l].rtv, up[l].w, up[l].h, D.ps_down, down[l].srv, NULL); continue; }
        float a[4] = { r / (float)(down[l + 1].w), r / (float)(down[l + 1].h), 0, 0 }; d3d_post_cb(a, z);
        d3d_fullscreen(up[l].rtv, up[l].w, up[l].h, D.ps_up, up[l + 1].srv, down[l].srv);
    }
}
static void d3d_post(const float *post, const arc__pass *P, ID3D11RenderTargetView *screen, int w, int h) {
    ID3D11DeviceContext *c = D.ctx; float bf[4] = { 0 };
    bool ssao = post[ARC__POST_SSAO] > 0 && P && D.ps_ssao && PS->aort.srv && (PS->scene_samples ? PS->scene_ms_dsrv && D.ps_dcopy_ms : PS->scene_dsrv != NULL);
    uint32_t lut = post[ARC__POST_LUT] > 0 ? (uint32_t)post[ARC__POST_LUT] : 0; if (lut && (!arc__textures[lut].used || !D.texs[lut].srv)) lut = 0;
    memset(g_post_x, 0, sizeof g_post_x);
    g_post_x[0] = ssao ? post[ARC__POST_SSAO] : 0; g_post_x[1] = post[ARC__POST_FXAA]; g_post_x[2] = post[ARC__POST_CRT]; g_post_x[3] = post[ARC__POST_PIXELATE];
    g_post_x[4] = post[ARC__POST_ABERRATION]; g_post_x[5] = post[ARC__POST_SCANLINES]; g_post_x[6] = lut ? post[ARC__POST_LUT_STRENGTH] : 0; g_post_x[7] = post[ARC__POST_LUT_N];
    g_post_x[8] = (float)w; g_post_x[9] = (float)h; g_post_x[10] = 1.0f / (float)w; g_post_x[11] = 1.0f / (float)h;
    if (P) {
        const float *vp = P->view_proj.m;
        memcpy(g_post_x + 12, P->ivp, 64); memcpy(g_post_x + 28, vp, 64);
        g_post_x[44] = (float)w; g_post_x[45] = (float)h; g_post_x[46] = post[ARC__POST_SSAO_RADIUS]; g_post_x[47] = fabsf(vp[3]) + fabsf(vp[7]) + fabsf(vp[11]) > 1e-6f ? 1.0f : 0.0f;
    }
    if (PS->scene_samples) ID3D11DeviceContext_ResolveSubresource(c, (ID3D11Resource *)PS->scene.tex, 0, (ID3D11Resource *)PS->scene_ms, 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
    ID3D11DeviceContext_IASetInputLayout(c, NULL); ID3D11DeviceContext_IASetPrimitiveTopology(c, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(c, D.vs_post, NULL, 0);
    ID3D11DeviceContext_RSSetState(c, D.rs_none); ID3D11DeviceContext_OMSetDepthStencilState(c, D.ds_off, 0);
    ID3D11DeviceContext_OMSetBlendState(c, D.bs[ARC__BLEND_NONE], bf, 0xFFFFFFFF);
    ID3D11DeviceContext_PSSetConstantBuffers(c, 1, 1, &D.cb_post);
    ID3D11SamplerState *lin = D.smp[1]; ID3D11DeviceContext_PSSetSamplers(c, 0, 1, &lin);
    bool bloom = post[ARC__POST_BLOOM] > 0, tilt = post[ARC__POST_TILT] > 0;
    g_post_pc[0] = 0; g_post_pc[1] = g_post_pc[2] = g_post_pc[3] = 0;
    if (bloom) d3d_chain(PS->bl, PS->bu, ARC__BLOOM_LEVELS, post[ARC__POST_THRESHOLD], post[ARC__POST_RADIUS]);
    if (tilt) d3d_chain(PS->tl, PS->tu, ARC__TILT_LEVELS, -1.0f, 1.0f);
    g_post_pc[0] = tilt ? post[ARC__POST_TILT] : 0; g_post_pc[1] = post[ARC__POST_TILT_FOCUS]; g_post_pc[2] = post[ARC__POST_TILT_WIDTH]; g_post_pc[3] = 1.0f / ARC__TILT_LEVELS;
    float a[4] = { bloom ? post[ARC__POST_BLOOM] / ARC__BLOOM_LEVELS : 0, post[ARC__POST_EXPOSURE], post[ARC__POST_TONEMAP], post[ARC__POST_DESAT] };
    float b[4] = { post[ARC__POST_CONTRAST], post[ARC__POST_VIGNETTE], (float)w / (float)h, fmaxf(2.0f, roundf((float)h / 270.0f)) };
    if (ssao) {   /* depth (sample 0 when multisampled) -> R32F, then the AO buffer */
        float z[4] = { 0 }; d3d_post_cb(z, z);
        if (PS->scene_samples) { ID3D11DeviceContext_OMSetRenderTargets(c, 1, &PS->depthc.rtv, NULL); ID3D11DeviceContext_PSSetShaderResources(c, 8, 1, &PS->scene_ms_dsrv); }
        d3d_fullscreen(PS->depthc.rtv, w, h, PS->scene_samples ? D.ps_dcopy_ms : D.ps_dcopy, PS->scene_samples ? NULL : PS->scene_dsrv, NULL);
        ID3D11ShaderResourceView *nul8 = NULL; ID3D11DeviceContext_PSSetShaderResources(c, 8, 1, &nul8);
        d3d_fullscreen(PS->aort.rtv, w, h, D.ps_ssao, PS->depthc.srv, NULL);
    }
    ID3D11DeviceContext_OMSetRenderTargets(c, 1, &screen, NULL);   /* the blur's last target must be unbound before it is read */
    ID3D11ShaderResourceView *t2 = tilt ? PS->tu[0].srv : PS->scene.srv; ID3D11DeviceContext_PSSetShaderResources(c, 4, 1, &t2);
    ID3D11ShaderResourceView *t56[2] = { ssao ? PS->aort.srv : PS->scene.srv, lut ? D.texs[lut].srv : PS->scene.srv }; ID3D11DeviceContext_PSSetShaderResources(c, 5, 2, t56);
    d3d_post_cb(a, b);
    d3d_fullscreen(screen, w, h, D.ps_comp, PS->scene.srv, bloom ? PS->bu[0].srv : PS->scene.srv);
    ID3D11ShaderResourceView *nul = NULL, *nul2[2] = { NULL, NULL };
    ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &nul); ID3D11DeviceContext_PSSetShaderResources(c, 3, 1, &nul); ID3D11DeviceContext_PSSetShaderResources(c, 4, 1, &nul);
    ID3D11DeviceContext_PSSetShaderResources(c, 5, 2, nul2);
}
/* 0.7 soft particles: the depth drawn so far (sample 0 when multisampled) copied into an R32F texture (t9) that
   the mesh shader reads; the pass's targets are bound again afterwards. false: particles stay hard */
static bool d3d_soft_depth(int pw, int ph, ID3D11RenderTargetView *rtv, ID3D11DepthStencilView *dsv, ID3D11ShaderResourceView *dsrv, bool ms) {
    ID3D11DeviceContext *c = D.ctx; float bf[4] = { 0 }; ID3D11ShaderResourceView *nul = NULL;
    if (!dsrv || !D.vs_post || !(ms ? D.ps_dcopy_ms : D.ps_dcopy)) return false;
    if (!D.softc.tex || D.softc.w != pw || D.softc.h != ph) { d3d_rt_free(&D.softc); if (!d3d_rt_make(&D.softc, pw, ph, DXGI_FORMAT_R32_FLOAT)) { d3d_rt_free(&D.softc); return false; } }
    ID3D11DeviceContext_PSSetShaderResources(c, 9, 1, &nul);
    ID3D11DeviceContext_IASetInputLayout(c, NULL); ID3D11DeviceContext_IASetPrimitiveTopology(c, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(c, D.vs_post, NULL, 0);
    ID3D11DeviceContext_RSSetState(c, D.rs_none); ID3D11DeviceContext_OMSetDepthStencilState(c, D.ds_off, 0);
    ID3D11DeviceContext_OMSetBlendState(c, D.bs[ARC__BLEND_NONE], bf, 0xFFFFFFFF);
    if (ms) { ID3D11DeviceContext_OMSetRenderTargets(c, 1, &D.softc.rtv, NULL); ID3D11DeviceContext_PSSetShaderResources(c, 8, 1, &dsrv); }
    d3d_fullscreen(D.softc.rtv, pw, ph, ms ? D.ps_dcopy_ms : D.ps_dcopy, ms ? NULL : dsrv, NULL);
    ID3D11DeviceContext_PSSetShaderResources(c, 8, 1, &nul); ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &nul);
    ID3D11DeviceContext_OMSetRenderTargets(c, 1, &rtv, dsv);
    D3D11_VIEWPORT vp = { 0, 0, (float)pw, (float)ph, 0, 1 }; ID3D11DeviceContext_RSSetViewports(c, 1, &vp); d3d_scissor(0, 0, pw, ph);
    ID3D11DeviceContext_RSSetState(c, D.rs_cull); ID3D11DeviceContext_OMSetDepthStencilState(c, D.ds_on, 0);
    ID3D11DeviceContext_PSSetShaderResources(c, 9, 1, &D.softc.srv);
    D.soft[0] = (float)pw; D.soft[1] = (float)ph; D.soft[2] = 1; D.soft[3] = 0;
    return true;
}
static void d3d_draw_2d(const arc__frame_data *f, const arc__pass *P, int pw, int ph);
static void d3d_pass_cb(const arc__pass *P, int pw, int ph, bool solid);
/* 0.8.2: after a target's last pass of the frame with its own post: post from its scene buffer into the target (ssao
   with the last of its passes that drew 3D), then the 2D of all its passes on top (as soft_target_post) */
static void d3d_target_post(const arc__frame_data *f, int pi) {
    const arc__pass *P = &f->passes[pi]; uint32_t id = P->target; ID3D11DeviceContext *c = D.ctx;
    for (int k = pi + 1; k < f->pass_count; k++) if (f->passes[k].target == id && f->passes[k].post[ARC__POST_ON] > 0) return;
    const arc__pass *src = P;
    for (int k = pi; k >= 0; k--) { const arc__pass *q = &f->passes[k]; if (q->target == id && q->post[ARC__POST_ON] > 0 && (q->mb_count || q->imm_count)) { src = q; break; } }
    d3d_pset *ps = d3d_pset_for(id, false); d3d_tex *tt = &D.texs[id]; if (!ps || !tt->rtv) return;
    ID3D11ShaderResourceView *nul2 = NULL; ID3D11DeviceContext_PSSetShaderResources(c, 2, 1, &nul2);
    PS = ps; d3d_post(P->post, src, tt->rtv, tt->w, tt->h); PS = &D.ps[0];
    ID3D11DeviceContext_OMSetRenderTargets(c, 1, &tt->rtv, NULL);
    D3D11_VIEWPORT vp = { 0, 0, (float)tt->w, (float)tt->h, 0, 1 }; ID3D11DeviceContext_RSSetViewports(c, 1, &vp);
    ID3D11DeviceContext_PSSetConstantBuffers(c, 0, 1, &D.cb);
    for (int k = 0; k <= pi; k++) {
        const arc__pass *q = &f->passes[k];
        if (q->target != id || q->post[ARC__POST_ON] <= 0 || !q->b2_count) continue;
        d3d_pass_cb(q, tt->w, tt->h, true);
        d3d_draw_2d(f, q, tt->w, tt->h);
    }
    ID3D11DeviceContext_PSSetSamplers(c, 1, 1, &D.smp_shadow);
}
static void d3d_render(const arc__frame_data *f) {
    if (f->width != D.w || f->height != D.h) arc__d3d11_resize(f->width, f->height);
    ID3D11DeviceContext *c = D.ctx;
    if (f->instance_count) upload(&D.inst_vb, &D.inst_cap, D3D11_BIND_VERTEX_BUFFER, f->instances, sizeof(arc__instance) * (size_t)f->instance_count);
    if (f->imm_count) upload(&D.imm_vb, &D.imm_cap, D3D11_BIND_VERTEX_BUFFER, f->imm, sizeof(arc_vertex) * (size_t)f->imm_count);
    if (f->line_vertex_count) upload(&D.line_vb, &D.line_cap, D3D11_BIND_VERTEX_BUFFER, f->lines, sizeof(arc__line_vertex) * (size_t)f->line_vertex_count);
    if (f->vert2d_count) upload(&D.quad_vb, &D.quad_cap, D3D11_BIND_VERTEX_BUFFER, f->verts2d, sizeof(arc__vert2d) * (size_t)f->vert2d_count);
    D.cur_frame = f;
    if (f->light_count && f->lights) {   /* 0.9.5 the frame's lights: 4 RGBA32F texels each */
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ID3D11DeviceContext_Map(c, (ID3D11Resource *)D.lt_tex[0], 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            for (int y = 0; y < f->light_count; y++) memcpy((uint8_t *)m.pData + (size_t)y * m.RowPitch, f->lights + (size_t)y * ARC__LT_FLOATS, ARC__LT_FLOATS * sizeof(float));
            ID3D11DeviceContext_Unmap(c, (ID3D11Resource *)D.lt_tex[0], 0);
        }
    }
    if (f->bone_count) {
        int rows = (f->bone_count + ARC__BONES_PER_ROW - 1) / ARC__BONES_PER_ROW;
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ID3D11DeviceContext_Map(c, (ID3D11Resource *)D.bone_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            for (int y = 0; y < rows; y++) memcpy((uint8_t *)m.pData + (size_t)y * m.RowPitch, f->bones + (size_t)y * ARC__BONES_PER_ROW * 12, ARC__BONES_PER_ROW * 12 * sizeof(float));
            ID3D11DeviceContext_Unmap(c, (ID3D11Resource *)D.bone_tex, 0);
        }
    }
    ID3D11DeviceContext_VSSetConstantBuffers(c, 0, 1, &D.cb); ID3D11DeviceContext_PSSetConstantBuffers(c, 0, 1, &D.cb);
    ID3D11DeviceContext_VSSetShaderResources(c, 1, 1, &D.bone_srv);
    float bf[4] = { 0 }; bool screen_drawn = false; const float *post = NULL; const arc__pass *post_pass = NULL;
    ID3D11RenderTargetView *screen_rtv = D.msaa ? D.ms_rtv : D.rtv;
    ID3D11DeviceContext_PSSetSamplers(c, 1, 1, &D.smp_shadow);
    for (int pi = 0; pi < f->pass_count; pi++) {
        const arc__pass *P = &f->passes[pi];
        d3d_tex *tt = P->target ? &D.texs[P->target] : NULL;
        if ((tt && !tt->rtv) || (f->targets_only && !tt) || (f->screen_only && P->target)) continue;
        if (P->shadow[0] > 0 && P->shadow_res > 0 && P->mb_count) d3d_shadow_pass(f, P);
        D.lsh_ok = false; if (P->lsh_n > 0 && P->mb_count) d3d_lshadow_pass(f, P);
        bool to_scene = !tt && P->post[ARC__POST_ON] > 0 && d3d_post_ready(D.w, D.h);
        if (to_scene) { post = P->post; if (!post_pass || P->mb_count || P->imm_count) post_pass = P; }
        d3d_pset *tps = NULL;   /* 0.8.2: a target with its own post draws its 3D into its own scene buffer */
        if (tt && P->post[ARC__POST_ON] > 0 && (tps = d3d_pset_for(P->target, true)) != NULL) {
            PS = tps; if (!d3d_post_ready_s(tt->w, tt->h, 0)) tps = NULL;
            PS = &D.ps[0];
        }
        ID3D11RenderTargetView *rtv = tps ? tps->scene.rtv : tt ? tt->rtv : to_scene ? (PS->scene_samples ? PS->scene_ms_rtv : PS->scene.rtv) : screen_rtv;
        ID3D11DepthStencilView *dsv = tps ? tps->scene_dsv : tt ? tt->dsv : to_scene ? (PS->scene_samples ? PS->scene_ms_dsv : PS->scene_dsv) : (D.msaa ? D.ms_dsv : D.dsv);
        if (!tt) screen_drawn = true;
        int pw = tt ? tt->w : D.w, ph = tt ? tt->h : D.h;
        ID3D11ShaderResourceView *nul = NULL; ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &nul);   /* a target can't be bound as input and output */
        {   /* 0.8.7: the reflection this pass samples (t12, s3); white otherwise, never the pass's own target */
            uint32_t rt = P->refl_tex && P->refl[0] > 0 && D.texs[P->refl_tex].srv && P->refl_tex != P->target ? P->refl_tex : 1;
            ID3D11ShaderResourceView *rsrv = D.texs[rt].srv; ID3D11DeviceContext_PSSetShaderResources(c, 12, 1, &rsrv);
            ID3D11SamplerState *s3 = D.smp[5]; ID3D11DeviceContext_PSSetSamplers(c, 3, 1, &s3);
        }
        ID3D11DeviceContext_OMSetRenderTargets(c, 1, &rtv, dsv);
        ID3D11DeviceContext_PSSetShaderResources(c, 2, 1, P->shadow[0] > 0 && D.sh_res == (P->shadow_n > 1 ? P->shadow_res * 2 : P->shadow_res) ? &D.sh_srv : &nul);
        D3D11_VIEWPORT vp = { 0, 0, (float)pw, (float)ph, 0, 1 }; ID3D11DeviceContext_RSSetViewports(c, 1, &vp);
        d3d_scissor(0, 0, pw, ph);
        if (P->clear) {
            ID3D11DeviceContext_ClearRenderTargetView(c, rtv, P->clear_col);
            ID3D11DeviceContext_ClearDepthStencilView(c, dsv, D3D11_CLEAR_DEPTH, D.dclear, 0);
            if ((!P->target || P->mirror || P->vport) && P->sky[3] > 0 && D.ps_sky && D.vs_post) {   /* (0.10.2 viewports too) */
                D3D11_MAPPED_SUBRESOURCE m; float bf0[4] = { 0 };
                if (SUCCEEDED(ID3D11DeviceContext_Map(c, (ID3D11Resource *)D.cb_post, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                    float *o = (float *)m.pData; memset(o, 0, 160);   /* pa, pb, sivp, stop, shor, sbot, pc */
                    memcpy(o + 8, P->sky_ivp, 64); memcpy(o + 24, P->sky, 12); memcpy(o + 28, P->sky + 4, 12); memcpy(o + 32, P->sky + 8, 12);
                    ID3D11DeviceContext_Unmap(c, (ID3D11Resource *)D.cb_post, 0);
                }
                ID3D11DeviceContext_IASetInputLayout(c, NULL); ID3D11DeviceContext_IASetPrimitiveTopology(c, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                ID3D11DeviceContext_VSSetShader(c, D.vs_post, NULL, 0); ID3D11DeviceContext_PSSetShader(c, D.ps_sky, NULL, 0);
                ID3D11DeviceContext_PSSetConstantBuffers(c, 1, 1, &D.cb_post);
                ID3D11DeviceContext_RSSetState(c, D.rs_none); ID3D11DeviceContext_OMSetDepthStencilState(c, D.ds_off, 0);
                ID3D11DeviceContext_OMSetBlendState(c, D.bs[ARC__BLEND_NONE], bf0, 0xFFFFFFFF);
                ID3D11DeviceContext_Draw(c, 3, 0);
                ID3D11DeviceContext_IASetInputLayout(c, D.il_mesh);
                ID3D11DeviceContext_VSSetShader(c, D.vs_mesh, NULL, 0); ID3D11DeviceContext_PSSetShader(c, D.ps_mesh, NULL, 0);
            }
        }
        d3d_pass_cb(P, pw, ph, true);
        ID3D11DeviceContext_RSSetState(c, D.rs_cull);
        ID3D11DeviceContext_OMSetDepthStencilState(c, D.ds_on, 0);
        ID3D11DeviceContext_OMSetBlendState(c, D.bs[ARC__BLEND_NONE], bf, 0xFFFFFFFF);
        if (P->mb_count || P->imm_count) {
            ID3D11DeviceContext_IASetInputLayout(c, D.il_mesh);
            ID3D11DeviceContext_IASetPrimitiveTopology(c, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D11DeviceContext_VSSetShader(c, D.vs_mesh, NULL, 0); ID3D11DeviceContext_PSSetShader(c, D.ps_mesh, NULL, 0);
        }
        if (P->mb_count) d3d_draw_batches(f, P->mb_first, P->mb_count, P, pw, ph);
        if (P->imm_count) {                     /* immediate triangles: identity instance, double-sided */
            ID3D11Buffer *vbs[3] = { D.imm_vb, D.noskin_vb, D.ident_vb }; UINT strides[3] = { sizeof(arc_vertex), 0, sizeof(arc__instance) }, offs[3] = { 0, 0, 0 };
            ID3D11DeviceContext_IASetVertexBuffers(c, 0, 3, vbs, strides, offs);
            ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &D.texs[1].srv); ID3D11DeviceContext_PSSetSamplers(c, 0, 1, &D.smp[0]);
            d3d_material(NULL);
            ID3D11DeviceContext_RSSetState(c, D.rs_none);
            ID3D11DeviceContext_DrawInstanced(c, (UINT)P->imm_count, 1, (UINT)P->imm_first, 0);
            ID3D11DeviceContext_RSSetState(c, D.rs_cull);
        }
        if (P->line_count) {
            UINT stride = sizeof(arc__line_vertex), off = 0;
            ID3D11DeviceContext_IASetInputLayout(c, D.il_line); ID3D11DeviceContext_IASetPrimitiveTopology(c, D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            ID3D11DeviceContext_IASetVertexBuffers(c, 0, 1, &D.line_vb, &stride, &off);
            ID3D11DeviceContext_VSSetShader(c, D.vs_line, NULL, 0); ID3D11DeviceContext_PSSetShader(c, D.ps_color, NULL, 0);
            ID3D11DeviceContext_Draw(c, (UINT)P->line_count, (UINT)P->line_first);
        }
        if (P->tb_count) {                      /* see-through and additive meshes, already sorted back to front */
            bool soft = false;
            if (P->soft) {
                bool ms = tt ? false : to_scene ? PS->scene_samples != 0 : D.msaa != 0;
                ID3D11ShaderResourceView *ds = tps ? tps->scene_dsrv : tt ? tt->dsrv : to_scene ? (PS->scene_samples ? PS->scene_ms_dsrv : PS->scene_dsrv) : (D.msaa ? D.ms_dsrv : D.dsrv);
                soft = d3d_soft_depth(pw, ph, rtv, dsv, ds, ms);
            }
            d3d_pass_cb(P, pw, ph, false);
            ID3D11DeviceContext_IASetInputLayout(c, D.il_mesh);
            ID3D11DeviceContext_IASetPrimitiveTopology(c, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D11DeviceContext_VSSetShader(c, D.vs_mesh, NULL, 0); ID3D11DeviceContext_PSSetShader(c, D.ps_mesh, NULL, 0);
            d3d_draw_batches(f, P->mb_first + P->mb_count, P->tb_count, P, pw, ph);
            if (soft) {
                D.soft[2] = 0; d3d_pass_cb(P, pw, ph, true);
                ID3D11ShaderResourceView *nul9 = NULL; ID3D11DeviceContext_PSSetShaderResources(c, 9, 1, &nul9);
            }
        }
        if (tps) { d3d_target_post(f, pi); continue; }
        if (P->mirror && tt) {   /* 0.8.7: its mip levels, to blur it by */
            ID3D11DeviceContext_OMSetRenderTargets(c, 0, NULL, NULL); ID3D11DeviceContext_GenerateMips(c, tt->srv); continue;
        }
        if (P->b2_count && !to_scene) d3d_draw_2d(f, P, pw, ph);
    }
    ID3D11ShaderResourceView *nul2 = NULL; ID3D11DeviceContext_PSSetShaderResources(c, 2, 1, &nul2);
    if (post) {                                 /* post-process the screen's 3D, then draw the screen's 2D on top */
        d3d_post(post, post_pass, screen_rtv, D.w, D.h);
        ID3D11DeviceContext_OMSetRenderTargets(c, 1, &screen_rtv, NULL);
        D3D11_VIEWPORT vp = { 0, 0, (float)D.w, (float)D.h, 0, 1 }; ID3D11DeviceContext_RSSetViewports(c, 1, &vp);
        ID3D11DeviceContext_PSSetConstantBuffers(c, 0, 1, &D.cb);
        for (int pi = 0; pi < f->pass_count; pi++) {
            const arc__pass *P = &f->passes[pi];
            if (P->target || f->targets_only || !P->b2_count) continue;
            d3d_pass_cb(P, D.w, D.h, true);
            d3d_draw_2d(f, P, D.w, D.h);
        }
    }
    ID3D11ShaderResourceView *nul = NULL; ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &nul);
    if (D.msaa && screen_drawn) {               /* resolve the multisampled screen into the back buffer */
        ID3D11Texture2D *bb = NULL; IDXGISwapChain_GetBuffer(D.swap, 0, &IID_ID3D11Texture2D, (void **)&bb);
        if (bb) { ID3D11DeviceContext_ResolveSubresource(c, (ID3D11Resource *)bb, 0, (ID3D11Resource *)D.ms_tex, 0, DXGI_FORMAT_R8G8B8A8_UNORM); REL(bb); }
    }
    ID3D11DeviceContext_OMSetRenderTargets(c, 1, &D.rtv, D.dsv);
}
static void d3d_draw_2d(const arc__frame_data *f, const arc__pass *P, int pw, int ph) {
    ID3D11DeviceContext *c = D.ctx; float bf[4] = { 0 };
    UINT stride = sizeof(arc__vert2d), off = 0;
            ID3D11DeviceContext_RSSetState(c, D.rs_none); ID3D11DeviceContext_OMSetDepthStencilState(c, D.ds_off, 0);
            ID3D11DeviceContext_IASetInputLayout(c, D.il_quad); ID3D11DeviceContext_IASetPrimitiveTopology(c, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D11DeviceContext_IASetVertexBuffers(c, 0, 1, &D.quad_vb, &stride, &off);
            ID3D11DeviceContext_VSSetShader(c, D.vs_quad, NULL, 0); ID3D11DeviceContext_PSSetShader(c, D.ps_tex, NULL, 0);
            ID3D11DeviceContext_PSSetConstantBuffers(c, 3, 1, &D.cb_quad);
            for (int b = P->b2_first; b < P->b2_first + P->b2_count; b++) {
                const arc__batch2d *bt = &f->batches2d[b]; d3d_tex *t = &D.texs[bt->texture];
                if (bt->clip) {
                    int x0 = arc_clampi(bt->cx, 0, pw), y0 = arc_clampi(bt->cy, 0, ph), x1 = arc_clampi(bt->cx + bt->cw, 0, pw), y1 = arc_clampi(bt->cy + bt->ch, 0, ph);
                    if (x1 <= x0 || y1 <= y0) continue;
                    d3d_scissor(x0, y0, x1, y1);
                } else d3d_scissor(0, 0, pw, ph);
                ID3D11DeviceContext_OMSetBlendState(c, D.bs[bt->blend & 3], bf, 0xFFFFFFFF);
                ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &t->srv);
                ID3D11SamplerState *smp = d3d_sampler(t); ID3D11DeviceContext_PSSetSamplers(c, 0, 1, &smp);
                if (bt->sdf != D.quad_sdf) {   /* 0.10.1 */
                    D3D11_MAPPED_SUBRESOURCE mq;
                    if (SUCCEEDED(ID3D11DeviceContext_Map(c, (ID3D11Resource *)D.cb_quad, 0, D3D11_MAP_WRITE_DISCARD, 0, &mq))) { float q[4] = { bt->sdf, 0, 0, 0 }; memcpy(mq.pData, q, 16); ID3D11DeviceContext_Unmap(c, (ID3D11Resource *)D.cb_quad, 0); D.quad_sdf = bt->sdf; }
                }
                ID3D11DeviceContext_Draw(c, (UINT)bt->count, (UINT)bt->first);
            }
}
static bool d3d_read(uint8_t *rgba, int w, int h) {
    ID3D11Texture2D *bb = NULL, *st = NULL;
    IDXGISwapChain_GetBuffer(D.swap, 0, &IID_ID3D11Texture2D, (void **)&bb); if (!bb) return false;
    D3D11_TEXTURE2D_DESC td; ID3D11Texture2D_GetDesc(bb, &td);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
    if (FAILED(ID3D11Device_CreateTexture2D(D.dev, &td, NULL, &st))) { REL(bb); return false; }
    ID3D11DeviceContext_CopyResource(D.ctx, (ID3D11Resource *)st, (ID3D11Resource *)bb);
    D3D11_MAPPED_SUBRESOURCE m; bool ok = false;
    if (SUCCEEDED(ID3D11DeviceContext_Map(D.ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &m))) {
        int cw = arc_mini(w, (int)td.Width), ch = arc_mini(h, (int)td.Height);
        for (int y = 0; y < ch; y++) memcpy(rgba + (size_t)y * w * 4, (uint8_t *)m.pData + (size_t)y * m.RowPitch, (size_t)cw * 4);
        ID3D11DeviceContext_Unmap(D.ctx, (ID3D11Resource *)st, 0); ok = true;
    }
    REL(st); REL(bb); return ok;
}
const arc__backend arc__backend_d3d11 = { "d3d11", d3d_init, d3d_shutdown, d3d_mesh_create, d3d_mesh_update, d3d_mesh_destroy, d3d_tex_create,
    d3d_target_create, d3d_tex_destroy, d3d_tex_set_smooth, d3d_tex_update, d3d_render, d3d_read, d3d_compressed_formats, d3d_tex_create_compressed, NULL, d3d_tex_create_array };
