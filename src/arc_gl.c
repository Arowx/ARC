/* ARC OpenGL backend: OpenGL 3.3 core (Linux) and OpenGL ES 3.0 / WebGL2 (web, with ARC_GLES).
 * Instanced mesh drawing, one dynamic buffer per stream, no GL extension libraries. */
#include "arc_internal.h"

#if defined(ARC_GLES)
#include <GLES3/gl3.h>
#include <emscripten/html5.h>   /* WebGL extensions (compressed textures) are switched on by name */
#define GLSL_HEADER "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#define GLF(ret, name, args)
#define ARC_GL_LOAD() true
#else
#include <GL/gl.h>
#include <GL/glext.h>
#define GLSL_HEADER "#version 330 core\n"
/* functions beyond OpenGL 1.1 are loaded at runtime by the platform (glXGetProcAddress / wglGetProcAddress) */
#define ARC_GL_FUNCS(X) \
    X(GLuint, glCreateShader, (GLenum)) X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, glCompileShader, (GLuint)) X(void, glGetShaderiv, (GLuint, GLenum, GLint *)) X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(GLuint, glCreateProgram, (void)) X(void, glAttachShader, (GLuint, GLuint)) X(void, glLinkProgram, (GLuint)) \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint *)) X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, glUseProgram, (GLuint)) X(GLint, glGetUniformLocation, (GLuint, const GLchar *)) X(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *)) \
    X(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) X(void, glUniform4fv, (GLint, GLsizei, const GLfloat *)) \
    X(void, glUniform2f, (GLint, GLfloat, GLfloat)) X(void, glUniform1i, (GLint, GLint)) \
    X(void, glGenBuffers, (GLsizei, GLuint *)) X(void, glBindBuffer, (GLenum, GLuint)) X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *)) X(void, glDeleteBuffers, (GLsizei, const GLuint *)) \
    X(void, glGenVertexArrays, (GLsizei, GLuint *)) X(void, glBindVertexArray, (GLuint)) X(void, glDeleteVertexArrays, (GLsizei, const GLuint *)) \
    X(void, glEnableVertexAttribArray, (GLuint)) X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, glVertexAttribDivisor, (GLuint, GLuint)) X(void, glDrawElementsInstanced, (GLenum, GLsizei, GLenum, const void *, GLsizei)) \
    X(void, glDrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei)) \
    X(void, glActiveTexture, (GLenum)) X(void, glUniform1f, (GLint, GLfloat)) \
    X(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
    X(void, glGenFramebuffers, (GLsizei, GLuint *)) X(void, glBindFramebuffer, (GLenum, GLuint)) X(void, glDeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) X(GLenum, glCheckFramebufferStatus, (GLenum)) \
    X(void, glGenRenderbuffers, (GLsizei, GLuint *)) X(void, glBindRenderbuffer, (GLenum, GLuint)) X(void, glDeleteRenderbuffers, (GLsizei, const GLuint *)) \
    X(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) X(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    X(void, glRenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, glGenerateMipmap, (GLenum)) X(void, glVertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, glDrawBuffers, (GLsizei, const GLenum *)) \
    X(void, glTexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, glTexSubImage3D, (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(const GLubyte *, glGetStringi, (GLenum, GLuint)) X(void, glCompressedTexImage2D, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *))
#define GLDECL(ret, name, args) static ret (APIENTRYP p_##name) args;
ARC_GL_FUNCS(GLDECL)
#define glCreateShader p_glCreateShader
#define glShaderSource p_glShaderSource
#define glCompileShader p_glCompileShader
#define glGetShaderiv p_glGetShaderiv
#define glGetShaderInfoLog p_glGetShaderInfoLog
#define glCreateProgram p_glCreateProgram
#define glAttachShader p_glAttachShader
#define glLinkProgram p_glLinkProgram
#define glGetProgramiv p_glGetProgramiv
#define glGetProgramInfoLog p_glGetProgramInfoLog
#define glUseProgram p_glUseProgram
#define glGetUniformLocation p_glGetUniformLocation
#define glBindAttribLocation p_glBindAttribLocation
#define glUniformMatrix4fv p_glUniformMatrix4fv
#define glUniform4fv p_glUniform4fv
#define glUniform2f p_glUniform2f
#define glUniform1i p_glUniform1i
#define glGenBuffers p_glGenBuffers
#define glBindBuffer p_glBindBuffer
#define glBufferData p_glBufferData
#define glBufferSubData p_glBufferSubData
#define glDeleteBuffers p_glDeleteBuffers
#define glGenVertexArrays p_glGenVertexArrays
#define glBindVertexArray p_glBindVertexArray
#define glDeleteVertexArrays p_glDeleteVertexArrays
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glVertexAttribPointer p_glVertexAttribPointer
#define glVertexAttribDivisor p_glVertexAttribDivisor
#define glDrawElementsInstanced p_glDrawElementsInstanced
#define glDrawArraysInstanced p_glDrawArraysInstanced
#define glActiveTexture p_glActiveTexture
#define glUniform1f p_glUniform1f
#define glBlendFuncSeparate p_glBlendFuncSeparate
#define glGenFramebuffers p_glGenFramebuffers
#define glBindFramebuffer p_glBindFramebuffer
#define glDeleteFramebuffers p_glDeleteFramebuffers
#define glFramebufferTexture2D p_glFramebufferTexture2D
#define glCheckFramebufferStatus p_glCheckFramebufferStatus
#define glGenRenderbuffers p_glGenRenderbuffers
#define glBindRenderbuffer p_glBindRenderbuffer
#define glDeleteRenderbuffers p_glDeleteRenderbuffers
#define glRenderbufferStorage p_glRenderbufferStorage
#define glFramebufferRenderbuffer p_glFramebufferRenderbuffer
#define glRenderbufferStorageMultisample p_glRenderbufferStorageMultisample
#define glBlitFramebuffer p_glBlitFramebuffer
#define glGenerateMipmap p_glGenerateMipmap
#define glVertexAttrib4f p_glVertexAttrib4f
#define glDrawBuffers p_glDrawBuffers
#define glTexImage3D p_glTexImage3D
#define glTexSubImage3D p_glTexSubImage3D
#define glGetStringi p_glGetStringi
#define glCompressedTexImage2D p_glCompressedTexImage2D
void *arc__gl_get_proc(const char *name);    /* provided by the platform */
static bool arc_gl_load(void) {
    bool ok = true;
#define GLLOAD(ret, name, args) p_##name = (ret (APIENTRYP) args)arc__gl_get_proc(#name); if (!p_##name) { fprintf(stderr, "ARC error missing GL function %s\n", #name); ok = false; }
    ARC_GL_FUNCS(GLLOAD)
    return ok;
}
#define ARC_GL_LOAD() arc_gl_load()
#endif

/* Meshes: skinning and the instance transform in the vertex shader; per-pixel lighting, texture, rim and fog in the
   fragment shader. The software renderer (arc_soft.c, shade()) uses the same formula. */
static const char *MESH_VS =
    "layout(location=0) in vec3 a_pos; layout(location=1) in vec3 a_nrm; layout(location=2) in vec4 a_col; layout(location=3) in vec2 a_uv;\n"
    "layout(location=4) in vec4 a_joint; layout(location=5) in vec4 a_weight;\n"
    "layout(location=6) in vec4 i_m0; layout(location=7) in vec4 i_m1; layout(location=8) in vec4 i_m2; layout(location=9) in vec4 i_m3;\n"
    "layout(location=10) in vec4 i_tint; layout(location=11) in vec4 i_par; layout(location=12) in vec4 i_ext;\n"
    "layout(location=13) in vec4 i_fx0; layout(location=14) in vec4 i_fx1; layout(location=15) in vec4 i_uvx;\n"
    "uniform mat4 u_vp; uniform float u_flip; uniform highp sampler2D u_bones;\n"
    "uniform vec4 u_pat[4]; uniform vec4 u_pjm[32];\n"   /* 0.10.1 paint pattern (see arc__pattern) */
    "out vec3 v_wp; out vec3 v_n; out vec4 v_col; out vec2 v_uv; flat out vec4 v_tint; flat out vec4 v_par; flat out vec4 v_ext;\n"
    "out vec3 v_op; flat out vec4 v_fx0; flat out vec4 v_fx1; out vec3 v_bp; out float v_pm;\n"
    "mat4 bone(int b) {\n"
    "  int x = (b % 512) * 3, y = b / 512;\n"
    "  vec4 r0 = texelFetch(u_bones, ivec2(x, y), 0), r1 = texelFetch(u_bones, ivec2(x + 1, y), 0), r2 = texelFetch(u_bones, ivec2(x + 2, y), 0);\n"
    "  return mat4(r0.x, r1.x, r2.x, 0.0, r0.y, r1.y, r2.y, 0.0, r0.z, r1.z, r2.z, 0.0, r0.w, r1.w, r2.w, 1.0);\n"
    "}\n"
    "void main() {\n"
    "  vec4 p = vec4(a_pos, 1.0); vec3 n = a_nrm;\n"
    "  if (i_ext.y > 0.5) {\n"
    "    int b0 = int(i_ext.x + 0.5);\n"
    "    mat4 S = bone(b0 + int(a_joint.x + 0.5)) * a_weight.x + bone(b0 + int(a_joint.y + 0.5)) * a_weight.y\n"
    "           + bone(b0 + int(a_joint.z + 0.5)) * a_weight.z + bone(b0 + int(a_joint.w + 0.5)) * a_weight.w;\n"
    "    p = vec4((S * p).xyz, 1.0); n = mat3(S) * n;\n"
    "  }\n"
    "  mat4 M = mat4(i_m0, i_m1, i_m2, i_m3);\n"
    "  vec4 wp = M * p;\n"
    "  v_wp = wp.xyz; v_n = mat3(M) * n; v_col = a_col; v_uv = vec2((1.0 + i_uvx.x) * a_uv.x + i_uvx.y * a_uv.y, i_uvx.z * a_uv.x + (1.0 + i_uvx.w) * a_uv.y) + i_fx1.xy; v_tint = i_tint; v_par = i_par; v_ext = i_ext;\n"
    "  v_op = p.xyz; v_fx0 = i_fx0; v_fx1 = i_fx1;\n"
    "  v_bp = a_pos; v_pm = 1.0;\n"   /* 0.10.1 the bind pose; whether the strongest joint's part takes the pattern */
    "  if (u_pat[0].x > 0.5) { v_pm = u_pat[3].w;\n"
    "    if (i_ext.y > 0.5) { float bw = a_weight.x, jj = a_joint.x; if (a_weight.y > bw) { bw = a_weight.y; jj = a_joint.y; } if (a_weight.z > bw) { bw = a_weight.z; jj = a_joint.z; } if (a_weight.w > bw) jj = a_joint.w;\n"
    "      int ji = int(jj + 0.5) & 127; v_pm = u_pjm[ji / 4][ji % 4]; } }\n"
    "  if (i_fx0.w > 0.0) {\n"   /* outline hull: out along the normal bent away from the model's origin */
    "    vec3 nn = normalize(v_n), cd = wp.xyz - i_m3.xyz; float cl = length(cd); vec3 dir = cl > 1e-6 ? nn + cd / cl : nn; float dl = length(dir);\n"
    "    wp.xyz += (dl > 1e-4 ? dir / dl : nn) * i_fx0.w; v_wp = wp.xyz;\n"
    "  }\n"
    "  gl_Position = u_vp * wp; gl_Position.y *= u_flip;\n"
    "#ifdef ARC_ZFIX\n"   /* 0.7.2 reversed-Z without clip control: 0..1 clip z into GL's -1..1 */
    "  gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;\n"
    "#endif\n"
    "}\n";
static const char *MESH_FS =
    "in vec3 v_wp; in vec3 v_n; in vec4 v_col; in vec2 v_uv; flat in vec4 v_tint; flat in vec4 v_par; flat in vec4 v_ext;\n"
    "in vec3 v_op; flat in vec4 v_fx0; flat in vec4 v_fx1; uniform float u_time; in vec3 v_bp; in float v_pm; uniform vec4 u_pat[4];\n"
    "uint dn_h(ivec3 c) { uint h = (uint(c.x) * 73856093u) ^ (uint(c.y) * 19349663u) ^ (uint(c.z) * 83492791u); h ^= h >> 13u; h *= 0x5bd1e995u; h ^= h >> 15u; return h; }\n"
    "float dn_c(ivec3 c) { return float(dn_h(c) & 0xFFFFFFu) / 16777215.0; }\n"
    "float dn_v(vec3 p) {\n"
    "  vec3 fl = floor(p), f = p - fl; ivec3 i = ivec3(fl); f = f * f * (3.0 - 2.0 * f);\n"
    "  float a = mix(dn_c(i), dn_c(i + ivec3(1, 0, 0)), f.x), b = mix(dn_c(i + ivec3(0, 1, 0)), dn_c(i + ivec3(1, 1, 0)), f.x);\n"
    "  float c = mix(dn_c(i + ivec3(0, 0, 1)), dn_c(i + ivec3(1, 0, 1)), f.x), d = mix(dn_c(i + ivec3(0, 1, 1)), dn_c(i + ivec3(1, 1, 1)), f.x);\n"
    "  return mix(mix(a, b, f.y), mix(c, d, f.y), f.z);\n"
    "}\n"
    "float pat_m(vec3 p, vec3 fn) {\n"   /* 0.10.1 a paint pattern at a bind-pose point (as the software renderer's pattern_at) */
    "  float k = u_pat[0].x, is = u_pat[0].y, w = u_pat[0].z, o = u_pat[0].w, ca = u_pat[1].x, sa = u_pat[1].y;\n"
    "  if (k < 1.5) { float t = (p.x * ca + p.y * sa) * is + o, m = fract(t) < w ? 1.0 : 0.0;\n"
    "    if (u_pat[1].z > 0.5) { float t2 = (p.y * ca - p.x * sa) * is + o; if (fract(t2) < w) m = 1.0; } return m; }\n"
    "  if (k < 3.5) { vec3 an = abs(fn); int ax = (an.x >= an.y && an.x >= an.z) ? 0 : (an.y >= an.z ? 1 : 2);\n"
    "    float u = ax == 0 ? p.z : p.x, v = ax == 1 ? p.z : p.y, ru = (u * ca - v * sa) * is + o, rv = (u * sa + v * ca) * is + o;\n"
    "    if (k < 2.5) { float c = floor(ru) + floor(rv); return c - 2.0 * floor(c * 0.5); }\n"
    "    vec2 f = vec2(ru - floor(ru), rv - floor(rv)) - 0.5; float r = sqrt(w / 3.14159265); return dot(f, f) < r * r ? 1.0 : 0.0; }\n"
    "  if (k < 4.5) return p.x * ca + p.y * sa > o ? 1.0 : 0.0;\n"
    "  return dn_v(vec3(p.x * is + u_pat[3].x + o, p.y * is + u_pat[3].y, p.z * is + u_pat[3].z)) < w ? 1.0 : 0.0;\n"
    "}\n"
    "float dissolve_noise(vec3 p) { return dn_v(p * 5.0) * 0.65 + dn_v(p * 11.0 + 17.0) * 0.35; }\n"
    "uniform sampler2D u_tex; uniform vec4 u_eye, u_light_dir, u_light_col, u_ambient, u_fog_col, u_fog, u_rim; uniform float u_opaque, u_mflip;\n"
    "uniform mat4 u_svp; uniform vec4 u_sh; uniform highp sampler2DShadow u_shadow;\n"
    "uniform sampler2D u_ntex, u_otex, u_etex; uniform vec4 u_m0, u_m1, u_m2, u_env_top, u_env_hor, u_env_bot;\n"
    "uniform sampler2D u_dtex, u_mtex; uniform vec4 u_m3, u_m4, u_m5;\n"
    "uniform sampler2D u_rtex; uniform vec4 u_clip, u_rpar; uniform float u_ramt;\n"
    "uniform vec4 u_m6;\n"
    "uniform highp sampler2DArray u_layers; uniform vec4 u_spl, u_spl2;\n"
    "uniform highp sampler2D u_wmask; uniform vec4 u_wm0, u_wm1, u_cut0, u_cut1, u_cut2;\n"   /* 0.9.9 world mask (unit 16), cutaway (see arc__pass); cut2.w: this mesh is cut */   /* 0.9.6 splat: layer scales; sharpness, layers, on (see ARC__MAT3_*) */
    "uniform sampler2D u_ptex; uniform vec4 u_paint, u_paint3;\n"   /* 0.8.8 paint mask (unit 10) and the second colour (w: on) */   /* 0.8.8 x: the colour texture's world scale (0 = mesh uv) */   /* 0.8.7 planar reflections (see arc__pass.mirror) */   /* 0.8.4 detail map, arc__mesh_rec.mat2 */
    "uint hx_h(int x, int y) { uint h = (uint(x) * 73856093u) ^ (uint(y) * 19349663u); h ^= h >> 13u; h *= 0x5bd1e995u; h ^= h >> 15u; return h; }\n"
    "float hx_gain(float x, float k) { float s = 0.5 * pow(2.0 * (x < 0.5 ? x : 1.0 - x), k); return x < 0.5 ? s : 1.0 - s; }\n"
    "vec4 hex_tex(sampler2D t, vec2 st, float rot, float con, bool flip) {\n"   /* 0.8.4 hex tiling: see arc_internal.h */
    "  const float S3 = 3.4641016; vec2 gx = dFdx(st), gy = dFdy(st);\n"
    "  vec2 p = vec2(st.x * S3 - st.y * S3 * 0.57735027, st.y * S3 * 1.15470054), b = floor(p), f = p - b;\n"
    "  float z = 1.0 - f.x - f.y, s = z < 0.0 ? 1.0 : 0.0, s2 = 2.0 * s - 1.0;\n"
    "  vec3 w = max(vec3(-z * s2, s - f.y * s2, s - f.x * s2), 0.0); ivec2 ib = ivec2(b); int is = int(s);\n"
    "  ivec2 cv[3] = ivec2[3](ib + ivec2(is, is), ib + ivec2(is, 1 - is), ib + ivec2(1 - is, is));\n"
    "  vec4 col[3]; vec3 W;\n"
    "  for (int i = 0; i < 3; i++) {\n"
    "    uint h = hx_h(cv[i].x, cv[i].y), h2 = hx_h(cv[i].x + 31, cv[i].y + 17);\n"
    "    vec2 c = vec2(float(cv[i].x) + float(cv[i].y) * 0.5, float(cv[i].y) * 0.8660254) / S3;\n"
    "    float a = (float(h2 & 0xFFFFu) / 65536.0 * 2.0 - 1.0) * 3.14159265 * rot, ca = cos(a), sa = sin(a);\n"
    "    vec2 d = st - c, uv = vec2(ca * d.x - sa * d.y, sa * d.x + ca * d.y) + c + vec2(float(h & 0xFFFFu), float(h >> 16u)) / 65536.0;\n"
    "    vec2 rx = vec2(ca * gx.x - sa * gx.y, sa * gx.x + ca * gx.y), ry = vec2(ca * gy.x - sa * gy.y, sa * gy.x + ca * gy.y);\n"
    "    if (flip) { uv.y = 1.0 - uv.y; rx.y = -rx.y; ry.y = -ry.y; }\n"
    "    col[i] = textureGrad(t, uv, rx, ry);\n"
    "    float wi = w[i], w7 = wi * wi * wi; w7 = w7 * w7 * wi;\n"
    "    W[i] = (1.0 + (dot(col[i].rgb, vec3(0.3, 0.59, 0.11)) - 1.0) * 0.6) * w7;\n"
    "  }\n"
    "  float sum = W.x + W.y + W.z; W = sum > 0.0 ? W / sum : vec3(1.0 / 3.0);\n"
    "  if (con != 0.5) { float k = log(1.0 - con) / log(0.5); W = vec3(hx_gain(W.x, k), hx_gain(W.y, k), hx_gain(W.z, k)); W /= W.x + W.y + W.z; }\n"
    "  return W.x * col[0] + W.y * col[1] + W.z * col[2];\n"
    "}\n"   /* material (arc__mesh_rec.mat; u_m2.w = has one), environment */
    "uniform highp sampler2D u_ltex; uniform highp usampler2D u_ltile, u_lidx; uniform int u_nlt; uniform mat4 u_vp;\n"   /* 0.9.5 tiled point / spot lights (see arc__pass) */
    "uniform highp sampler2DShadow u_lshadow; uniform int u_lsh;\n"   /* 0.9.5 light shadows (see arc__pass.lsh_n; as the software renderer's lsh_vis) */
    "float lsh_vis(vec4 a, vec4 b, vec4 c, vec4 s, vec3 wp, vec3 n) {\n"
    "  vec3 d = wp - a.xyz, F, up; float face = 0.0; bool spot = b.w > -0.25;\n"
    "  if (spot) { F = c.xyz; up = abs(F.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0); }\n"
    "  else { vec3 ad = abs(d);\n"
    "    if (ad.x >= ad.y && ad.x >= ad.z) { face = d.x >= 0.0 ? 0.0 : 1.0; F = vec3(d.x >= 0.0 ? 1.0 : -1.0, 0.0, 0.0); up = vec3(0.0, 1.0, 0.0); }\n"
    "    else if (ad.y >= ad.z) { face = d.y >= 0.0 ? 2.0 : 3.0; F = vec3(0.0, d.y >= 0.0 ? 1.0 : -1.0, 0.0); up = vec3(0.0, 0.0, 1.0); }\n"
    "    else { face = d.z >= 0.0 ? 4.0 : 5.0; F = vec3(0.0, 0.0, d.z >= 0.0 ? 1.0 : -1.0); up = vec3(0.0, 1.0, 0.0); } }\n"
    "  vec3 R = normalize(cross(F, up)), U = cross(R, F); float zn = s.y, t = s.z, tx = s.w, z0 = dot(d, F);\n"
    "  if (z0 <= zn) return 1.0;\n"
    "  float ws = 2.0 * t * z0 * tx; vec3 q = d + n * (1.5 * ws); float z = dot(q, F); if (z <= zn) return 1.0;\n"
    "  float u = dot(q, R) / (z * t) * 0.5 + 0.5, v = dot(q, U) / (z * t) * 0.5 + 0.5;\n"
    "  if (spot && (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0)) return 1.0;\n"
    "  float zf = 1.0 / sqrt(a.w), zr = max(z - ws, zn), ref = zf / (zf - zn) * (1.0 - zn / zr);\n"
    "  u = clamp(u, 1.5 * tx, 1.0 - 1.5 * tx); v = clamp(v, 1.5 * tx, 1.0 - 1.5 * tx);\n"
    "  vec2 at = vec2((face + u) / 6.0, (s.x + v) / 4.0), dt = vec2(tx / 6.0, tx / 4.0); float acc = 0.0;\n"
    "  for (int j = -1; j <= 1; j++) for (int k = -1; k <= 1; k++) acc += texture(u_lshadow, vec3(at + vec2(float(k), float(j)) * dt, ref));\n"
    "  return acc / 9.0;\n"
    "}\n"
    "uniform highp sampler2D u_sdepth; uniform mat4 u_ivp; uniform vec4 u_soft;\n"   /* 0.7 soft particles: depth so far, w, h, on */
    "out vec4 o_col;\n"
    "float pow5(float x) { float x2 = x * x; return x2 * x2 * x; }\n"
    "vec3 ggx(vec3 n, vec3 V, vec3 L, float NdotL, float NdotV, float rough, vec3 F0) {\n"   /* GGX highlight times N.L */
    "  vec3 H = normalize(L + V); float NdotH = max(dot(n, H), 0.0), VdotH = max(dot(V, H), 0.0);\n"
    "  float a = rough * rough, a2 = a * a, dd = NdotH * NdotH * (a2 - 1.0) + 1.0, D = a2 / (3.14159265 * dd * dd);\n"
    "  float k = (rough + 1.0) * (rough + 1.0) * 0.125, G = NdotV / (NdotV * (1.0 - k) + k) * NdotL / (NdotL * (1.0 - k) + k);\n"
    "  return D * G / (4.0 * NdotV * NdotL + 1e-4) * 3.14159265 * NdotL * (F0 + (1.0 - F0) * pow5(1.0 - VdotH));\n"
    "}\n"
    "vec3 local_lights(vec3 wp, vec3 n, vec3 kd, bool pbr, vec3 V, float NdotV, float rough, vec3 F0) {\n"
    "  vec3 acc = vec3(0.0);\n"
    "  vec4 cp = u_vp * vec4(wp, 1.0); int tx = 0, ty = 0;\n"   /* the point's tile: the lights that can reach it, in rising order */
    "  if (cp.w > 0.0) { tx = clamp(int(floor((cp.x / cp.w * 0.5 + 0.5) * 16.0)), 0, 15); ty = clamp(int(floor((cp.y / cp.w * 0.5 + 0.5) * 9.0)), 0, 8); }\n"
    "  uint e = texelFetch(u_ltile, ivec2(tx, ty), 0).r; int first = int(e >> 8u), cnt = int(e & 255u);\n"
    "  for (int k = 0; k < 64; k++) {\n"
    "    if (k >= cnt) break;\n"
    "    int at = first + k, li = int(texelFetch(u_lidx, ivec2(at & 255, at >> 8), 0).r);\n"
    "    vec4 a = texelFetch(u_ltex, ivec2(0, li), 0), b = texelFetch(u_ltex, ivec2(1, li), 0), c = texelFetch(u_ltex, ivec2(2, li), 0);\n"
    "    vec3 Lv = a.xyz - wp; float d2 = dot(Lv, Lv), q = clamp(1.0 - d2 * a.w, 0.0, 1.0);\n"
    "    if (q <= 0.0) continue;\n"
    "    vec3 L = Lv / sqrt(max(d2, 1e-12)); float NdotL = max(dot(n, L), 0.0);\n"
    "    if (NdotL <= 0.0) continue;\n"
    "    float t = clamp((-dot(L, c.xyz) - c.w) / (b.w - c.w), 0.0, 1.0);\n"
    "    float w = q * q * t * t * (3.0 - 2.0 * t);\n"
    "    if (u_lsh > 0) { vec4 s4 = texelFetch(u_ltex, ivec2(3, li), 0); if (s4.x >= 0.0 && int(s4.x) < u_lsh) { w *= lsh_vis(a, b, c, s4, wp, n); if (w <= 0.0) continue; } }\n"
    "    vec3 lc = b.rgb * w;\n"
    "    acc += kd * lc * NdotL; if (pbr) acc += ggx(n, V, L, NdotL, NdotV, rough, F0) * lc;\n"
    "  }\n"
    "  return acc;\n"
    "}\n"
    "vec3 env_col(float t) { return mix(u_env_hor.rgb, t >= 0.0 ? u_env_top.rgb : u_env_bot.rgb, min(abs(t), 1.0)); }\n"
    "uniform mat4 u_cvp[3]; uniform vec4 u_cp[3]; uniform vec4 u_shc;\n"   /* cascades: n, softness, atlas scale */
    "float pcf_c(int i, vec2 uv, float d) {\n"
    "  float t = u_sh.w, ref = d - u_cp[i].x * (1.0 + 4.0 * u_shc.y), acc = 0.0; vec2 a = u_cp[i].zw + uv * u_shc.z;\n"
    "  if (u_shc.y <= 0.0) { for (int j = -1; j <= 1; j++) for (int k = -1; k <= 1; k++) acc += texture(u_shadow, vec3(a + vec2(float(k), float(j)) * t, ref)); return acc / 9.0; }\n"
    "  float sp = (1.0 + 2.0 * u_shc.y) * t;\n"
    "  for (int j = -2; j <= 2; j++) for (int k = -2; k <= 2; k++) acc += float((3 - abs(k)) * (3 - abs(j))) * texture(u_shadow, vec3(a + vec2(float(k), float(j)) * sp, ref));\n"
    "  return acc / 81.0;\n"
    "}\n"
    "bool casc_uv(int i, vec3 wp, vec3 n, out vec3 c, out float e) {\n"
    "  vec4 s = u_cvp[i] * vec4(wp + n * (u_cp[i].y * (1.0 + 2.0 * u_shc.y)), 1.0); c = s.xyz * 0.5 + 0.5;\n"
    "  float mg = ((u_shc.y > 0.0 ? 2.0 * (1.0 + 2.0 * u_shc.y) : 1.0) + 1.5) * u_sh.w / u_shc.z;\n"
    "  e = min(min(c.x - mg, 1.0 - mg - c.x), min(c.y - mg, 1.0 - mg - c.y));\n"
    "  return e > 0.0 && c.z < 1.0;\n"
    "}\n"
    "float shadow_vis(vec3 wp, vec3 n) {\n"
    "  if (u_sh.x <= 0.0) return 1.0;\n"
    "  if (u_shc.x > 1.5 || u_shc.y > 0.0) {\n"
    "    for (int i = 0; i < 3; i++) {\n"
    "      if (float(i) >= u_shc.x) break;\n"
    "      vec3 c; float e; if (!casc_uv(i, wp, n, c, e)) continue;\n"
    "      float vis = pcf_c(i, c.xy, c.z);\n"
    "      if (float(i + 1) < u_shc.x && e < 0.1) { vec3 c2; float e2, v2 = 1.0; if (casc_uv(i + 1, wp, n, c2, e2)) v2 = pcf_c(i + 1, c2.xy, c2.z); vis = v2 + (vis - v2) * (e / 0.1); }\n"
    "      return 1.0 - u_sh.x * (1.0 - vis);\n"
    "    }\n"
    "    return 1.0;\n"
    "  }\n"
    "  vec4 s = u_svp * vec4(wp + n * u_sh.z, 1.0); vec3 c = s.xyz * 0.5 + 0.5;\n"
    "  if (c.x <= 0.0 || c.x >= 1.0 || c.y <= 0.0 || c.y >= 1.0 || c.z >= 1.0) return 1.0;\n"
    "  float acc = 0.0, r = c.z - u_sh.y;\n"
    "  for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) acc += texture(u_shadow, vec3(c.xy + vec2(float(i), float(j)) * u_sh.w, r));\n"
    "  return 1.0 - u_sh.x * (1.0 - acc / 9.0);\n"
    "}\n"
    "float fog_f(float d) { float f = 0.0; if (u_fog.z > 0.0) { if (u_fog.w > 0.0) { float k = u_fog.w * d; f = 1.0 - exp(-k * k); } else f = clamp((d - u_fog.x) * u_fog.y, 0.0, 1.0); } return f; }\n"
    "vec4 splat_tex(vec4 w) {\n"   /* every layer sampled (uniform control flow), the unweighted ones then left out */
    "  int n = int(u_spl2.y + 0.5); float sh = u_spl2.x, sum = 0.0; vec4 c[4]; float b[4], v[4];\n"
    "  for (int i = 0; i < 4; i++) if (i < n) sum += w[i];\n"
    "  for (int i = 0; i < 4; i++) { c[i] = vec4(0.0); b[i] = 0.0; if (i < n) { b[i] = sum < 1e-4 ? (i == 0 ? 1.0 : 0.0) : w[i]; c[i] = texture(u_layers, vec3(v_wp.xz * u_spl[i], float(i))); } }\n"
    "  if (sh > 0.0) { float mx = -1e30, d = 1.0 - 0.95 * sh;\n"
    "    for (int i = 0; i < 4; i++) { v[i] = b[i] > 0.0 ? b[i] + c[i].a * sh : -1e30; mx = max(mx, v[i]); }\n"
    "    for (int i = 0; i < 4; i++) b[i] = b[i] > 0.0 ? max(v[i] - (mx - d), 0.0) : 0.0; }\n"
    "  float t = 0.0; vec3 r = vec3(0.0); for (int i = 0; i < 4; i++) { t += b[i]; r += b[i] * c[i].rgb; }\n"
    "  return vec4(t > 0.0 ? r / t : vec3(0.0), 1.0);\n"
    "}\n"
    "void main() {\n"
    "  float ez = floor(v_ext.z + 0.5), ew = floor(v_ext.w + 0.5);\n"   /* 0.9.9 packed flags (see arc__instance.extra) */
    "  float e_unlit = mod(ez, 2.0), e_dark = mod(floor(ez * 0.5), 256.0) / 255.0, e_char = mod(floor(ez / 512.0), 256.0) / 255.0, e_nomask = floor(ez / 131072.0);\n"
    "  float e_addfog = mod(ew, 2.0), e_layer = floor(ew * 0.5);\n"
    "  if (dot(u_clip.xyz, v_wp) + u_clip.w < 0.0) discard;\n"   /* mirror passes: nothing under the plane */
    "  float wmv = 1.0;\n"   /* 0.9.9 world mask: four texels around the point, softness-shaped weights */
    "  if (u_wm1.w > 0.5 && e_nomask < 0.5) {\n"
    "    ivec2 sz = textureSize(u_wmask, 0); vec2 g = (v_wp.xz - u_wm0.xy) * u_wm0.z - 0.5, b0 = floor(g), f = g - b0;\n"
    "    f = u_wm0.w < 0.01 ? step(vec2(0.5), f) : smoothstep(vec2(0.5 - 0.5 * u_wm0.w), vec2(0.5 + 0.5 * u_wm0.w), f);\n"
    "    ivec2 i0 = clamp(ivec2(b0), ivec2(0), sz - 1), i1 = clamp(ivec2(b0) + 1, ivec2(0), sz - 1);\n"
    "    float m00 = texelFetch(u_wmask, ivec2(i0.x, i0.y), 0).r, m10 = texelFetch(u_wmask, ivec2(i1.x, i0.y), 0).r;\n"
    "    float m01 = texelFetch(u_wmask, ivec2(i0.x, i1.y), 0).r, m11 = texelFetch(u_wmask, ivec2(i1.x, i1.y), 0).r;\n"
    "    wmv = mix(mix(m00, m10, f.x), mix(m01, m11, f.x), f.y); }\n"
    "  if (u_cut2.w > 0.5) {\n"   /* 0.9.9 cutaway */
    "    vec2 ab = u_cut1.xz - u_cut0.xz; float ab2 = dot(ab, ab), t = ab2 > 1e-8 ? clamp(dot(v_wp.xz - u_cut0.xz, ab) / ab2, 0.0, 1.0) : 0.0;\n"
    "    float d = length(v_wp.xz - (u_cut0.xz + ab * t));\n"
    "    if (d < u_cut0.w && v_wp.y > u_cut1.w) {\n"
    "      if (u_cut2.x < 1.5) discard;\n"
    "      float fd = clamp((v_wp.y - u_cut1.w) / u_cut2.y, 0.0, 1.0) * clamp((u_cut0.w - d) / (0.25 * u_cut0.w), 0.0, 1.0);\n"
    "      ivec2 q = ivec2(gl_FragCoord.xy) & 3; int bi = q.y * 4 + q.x;\n"
    "      float bt = float(bi == 0 ? 0 : bi == 1 ? 8 : bi == 2 ? 2 : bi == 3 ? 10 : bi == 4 ? 12 : bi == 5 ? 4 : bi == 6 ? 14 : bi == 7 ? 6 : bi == 8 ? 3 : bi == 9 ? 11 : bi == 10 ? 1 : bi == 11 ? 9 : bi == 12 ? 15 : bi == 13 ? 7 : bi == 14 ? 13 : 5);\n"
    "      if ((bt + 0.5) / 16.0 < fd) discard; }\n"
    "    if (!gl_FrontFacing) { vec3 cg = mix(u_wm1.rgb, vec3(u_cut2.z), wmv); o_col = vec4(mix(cg, u_fog_col.rgb, fog_f(length(v_wp - u_eye.xyz))), 1.0); return; } }\n"
    "  float glow = 0.0;\n"
    "  if (v_fx0.x > 0.0) { float nz = dissolve_noise(v_op); if (nz < v_fx0.x) discard; if (v_fx0.y > 0.0 && nz < v_fx0.x + v_fx0.y) glow = v_fx1.w * (1.0 - (nz - v_fx0.x) / v_fx0.y); }\n"
    "  if (v_fx0.w > 0.0) { o_col = vec4(mix(v_tint.rgb, u_fog_col.rgb, fog_f(length(v_wp - u_eye.xyz))), 1.0); return; }\n"   /* outline hull */
    "  vec2 buv = v_uv;\n"
    "  if (u_m6.x > 0.0) { vec3 bn = abs(cross(dFdx(v_wp), dFdy(v_wp)));\n"   /* 0.8.8 world-mapped colour: the face's plane */
    "    buv = ((bn.y >= bn.x && bn.y >= bn.z) ? v_wp.xz : (bn.x >= bn.z ? v_wp.zy : v_wp.xy)) * u_m6.x; }\n"
    "  vec4 tx = u_m4.x > 1.5 ? hex_tex(u_tex, buv, u_m4.y, u_m4.z, u_mflip > 0.5)\n"
    "                         : texture(u_tex, vec2(buv.x, u_mflip > 0.5 ? 1.0 - buv.y : buv.y));\n"   /* render targets are stored bottom-up */
    "  if (u_spl2.z > 0.5) tx = splat_tex(tx);\n"
    "  if (u_spl2.w > 0.5) tx = texture(u_layers, vec3(buv, e_layer));\n"   /* 0.9.9 the colour from this draw's layer of the mesh's array */
    "  if (u_opaque > 0.5 && tx.a < 0.5) discard;\n"
    "  vec3 c = v_col.rgb * tx.rgb;\n"
    "  if (u_m3.x > 0.5) {\n"   /* 0.8.4 detail map: colour *= mix(1, 2 d, strength); world plane from the face normal */
    "    vec2 du = v_uv;\n"
    "    if (u_m3.y > 0.0) { vec3 fn = abs(cross(dFdx(v_wp), dFdy(v_wp)));\n"
    "      du = (fn.y >= fn.x && fn.y >= fn.z) ? v_wp.xz : (fn.x >= fn.z ? v_wp.zy : v_wp.xy); du *= u_m3.y; }\n"
    "    vec4 d = u_m3.x > 1.5 ? hex_tex(u_dtex, du, u_m3.w, u_m4.w, false) : texture(u_dtex, du);\n"
    "    float k = u_m3.z * d.a;\n"
    "    if (u_m5.w > 0.5) { vec2 mu = du * u_m5.x;\n"   /* breakup mask: the same mapping at its own scale */
    "      float mk = (u_m3.x > 1.5 ? hex_tex(u_mtex, mu, u_m3.w, u_m4.w, false) : texture(u_mtex, mu)).r;\n"
    "      k *= clamp((mk - u_m5.y) / u_m5.z, 0.0, 1.0); }\n"
    "    c *= 1.0 + (2.0 * d.rgb - 1.0) * k;\n"
    "  }\n"
    "  float lum = dot(c, vec3(0.3, 0.59, 0.11));\n"
    "  vec3 base = c * mix(vec3(1.0), v_tint.rgb, v_par.z);\n"
    "  base = mix(base, v_tint.rgb * (0.35 + lum), v_par.x);\n"
    "  if (u_paint.w > 0.5) { vec4 pm = texture(u_ptex, v_uv);\n"   /* 0.8.8 two colours through the mask: R tint, G tint2 */
    "    base = mix(base, v_tint.rgb * (0.35 + lum), pm.r); base = mix(base, u_paint.rgb * (0.35 + lum), pm.g);\n"
    "    if (u_paint3.w > 0.5) base = mix(base, u_paint3.rgb * (0.35 + lum), pm.b); }\n"   /* 0.8.9 blue: tint3 */
    "  vec3 pfn = cross(dFdx(v_bp), dFdy(v_bp));\n"   /* 0.10.1 paint pattern: the face's direction in the bind pose (CHECKER, DOTS) */
    "  if (u_pat[0].x > 0.5 && v_pm > 0.5) { float pmk = pat_m(v_bp, pfn);\n"
    "    if (pmk > 0.0 && u_pat[1].w > 0.5) pmk *= u_paint.w > 0.5 ? texture(u_ptex, v_uv).r : 0.0;\n"
    "    base = mix(base, u_pat[2].rgb * (0.35 + lum), pmk); }\n"
    "  if (e_dark > 0.0) base *= 1.0 - 0.85 * e_dark;\n"   /* 0.9.9 darken and char (as the software renderer) */
    "  if (e_char > 0.0) { float n1 = dissolve_noise(v_op * 1.7), soot = smoothstep(0.35, 0.7, n1); float n2 = dissolve_noise(v_op * 7.0 + 31.0), sc = 1.0 - smoothstep(0.0, 0.035, abs(n2 - 0.5));\n"
    "    base = base * (1.0 - 0.8 * e_char) * (1.0 - 0.6 * e_char * soot) + vec3(0.55) * sc * e_char * (1.0 - soot); }\n"
    "  vec3 n = normalize(v_n);\n"
    "  if (u_m0.x > 0.5) {\n"   /* normal map: frame T = dP/du, B = -dP/dv from the triangle (screen derivatives) */
    "    vec3 e1 = dFdx(v_wp), e2 = dFdy(v_wp); vec2 t1 = dFdx(v_uv), t2 = dFdy(v_uv); float det = t1.x * t2.y - t2.x * t1.y;\n"
    "    if (abs(det) > 1e-30) {\n"
    "      vec3 T = (t2.y * e1 - t1.y * e2) / det, B = -(t1.x * e2 - t2.x * e1) / det;\n"
    "      T -= n * dot(n, T); float tl = length(T);\n"
    "      if (tl > 1e-12) { T /= tl; vec3 Bo = cross(n, T); if (dot(Bo, B) < 0.0) Bo = -Bo;\n"
    "        vec3 nm = texture(u_ntex, v_uv).xyz * 2.0 - 1.0; if (u_m2.w > 1.5) nm.z = sqrt(max(1.0 - dot(nm.xy, nm.xy), 0.0));\n"   /* BC5: Z rebuilt */
    "        n = normalize(T * (nm.x * u_m0.y) + Bo * (nm.y * u_m0.y) + n * nm.z); }\n"
    "    }\n"
    "  }\n"
    "  float diff = max(dot(n, u_light_dir.xyz), 0.0), d = length(v_wp - u_eye.xyz), rim = 0.0;\n"
    "  float vis = (e_unlit < 0.5 && diff > 0.0) ? shadow_vis(v_wp, n) : 1.0;\n"
    "  if (u_rim.r + u_rim.g + u_rim.b > 0.0) rim = pow(1.0 - max(dot(n, normalize(u_eye.xyz - v_wp)), 0.0), u_rim.w);\n"
    "  vec4 orm = u_m1.z > 0.5 ? texture(u_otex, v_uv) : vec4(1.0);\n"
    "  float ao = u_m2.w > 0.5 ? 1.0 - u_m0.w * (1.0 - orm.r) : 1.0;\n"
    "  vec3 lit;\n"
    "  if (e_unlit > 0.5) lit = base * (1.0 + v_tint.w);\n"
    "  else if (u_m0.z > 0.5) {\n"   /* pbr: GGX highlight from the sun, Fresnel-weighted environment */
    "    float metal = clamp(u_m1.x * orm.b, 0.0, 1.0), rough = clamp(u_m1.y * orm.g, 0.045, 1.0);\n"
    "    vec3 V = normalize(u_eye.xyz - v_wp); float NdotV = max(dot(n, V), 1e-4);\n"
    "    vec3 F0 = mix(vec3(0.04), base, metal), spec = vec3(0.0);\n"
    "    if (diff > 0.0) spec = ggx(n, V, u_light_dir.xyz, diff, NdotV, rough, F0) * u_light_col.rgb * vis;\n"
    "    if (u_nlt > 0) spec += local_lights(v_wp, n, base * (1.0 - metal), true, V, NdotV, rough, F0);\n"
    "    vec3 e = env_col(2.0 * NdotV * n.y - V.y), avg = (u_env_top.rgb + 2.0 * u_env_hor.rgb + u_env_bot.rgb) * 0.25;\n"
    "    e = e + (avg - e) * rough;\n"
    "    vec3 envs = e * (F0 + (max(vec3(1.0 - rough), F0) - F0) * pow5(1.0 - NdotV)) * ao * u_env_top.w;\n"
    "    lit = base * (1.0 - metal) * (u_ambient.rgb * ao + u_light_col.rgb * diff * vis) + spec + envs + base * v_tint.w + u_rim.rgb * rim;\n"
    "  } else {\n"
    "    lit = base * (u_ambient.rgb * ao + u_light_col.rgb * diff * vis + v_tint.w) + u_rim.rgb * rim;\n"
    "    if (u_nlt > 0) lit += local_lights(v_wp, n, base, false, n, 1.0, 1.0, base);\n"
    "  }\n"
    "  if (u_m2.w > 0.5) lit += u_m2.rgb * (u_m1.w > 0.5 ? texture(u_etex, v_uv).rgb : vec3(1.0));\n"
    "  if (glow > 0.0) { int e = int(v_fx1.z); lit += vec3(float(e & 255), float((e >> 8) & 255), float((e >> 16) & 255)) / 255.0 * glow; }\n"
    "  if (u_rpar.x > 0.0 && u_ramt > 0.0) {\n"   /* the mirror pass's picture at this pixel (rows bottom-up, as gl_FragCoord) */
    "    float F = 0.6 + 0.4 * pow5(1.0 - max(dot(n, normalize(u_eye.xyz - v_wp)), 0.0));\n"
    "    float rr = u_m0.z > 0.5 ? clamp(u_m1.y * orm.g, 0.0, 1.0) : 0.0;\n"
    "    vec3 rc = textureLod(u_rtex, gl_FragCoord.xy * u_rpar.zw + 0.03 * vec2(n.x, -n.z), min(6.0 * rr, u_rpar.y)).rgb;\n"
    "    lit = mix(lit, rc, u_rpar.x * u_ramt * v_col.a * F);\n"
    "  }\n"
    "  lit = mix(lit, vec3(1.0), v_par.y);\n"
    "  float f = fog_f(d);\n"
    "  if (wmv < 1.0) lit = mix(u_wm1.rgb, lit, wmv);\n"
    "  o_col = vec4(e_addfog > 0.5 ? lit * (1.0 - f) : mix(lit, u_fog_col.rgb, f), u_opaque > 0.5 ? 1.0 : tx.a * v_par.w * v_col.a);\n"
    "  if (v_fx0.z > 0.0) {\n"   /* hologram (additive) */
    "    float fr = 1.0 - abs(dot(n, normalize(u_eye.xyz - v_wp))); fr *= fr;\n"
    "    float line = 0.5 + 0.5 * sin(60.0 * v_wp.y - 6.0 * u_time);\n"
    "    o_col = vec4(v_tint.rgb * ((0.6 + 0.8 * fr) * (0.7 + 0.3 * line)) * (1.0 - f), v_fx0.z * clamp(0.25 + 0.75 * fr + 0.3 * line, 0.0, 1.0));\n"
    "  }\n"
    "  if (u_soft.z > 0.5 && v_fx0.y < 0.0 && v_fx0.x <= 0.0) {\n"   /* soft particles (see arc__pass.soft) */
    "    float sd = texelFetch(u_sdepth, ivec2(gl_FragCoord.xy), 0).r;\n"
    "    vec4 sp = u_ivp * vec4(gl_FragCoord.xy / u_soft.xy * 2.0 - 1.0, sd * 2.0 - 1.0, 1.0);\n"
    "    o_col.a *= clamp(length(sp.xyz / sp.w - v_wp) / -v_fx0.y, 0.0, 1.0);\n"
    "  }\n"
    "}\n";
/* shadow map: depth only; textures with cut-out alpha cut out their shadow too */
static const char *SHADOW_FS =
    "in vec2 v_uv; in vec3 v_op; flat in vec4 v_fx0; uniform sampler2D u_tex;\n"
    "uint dn_h(ivec3 c) { uint h = (uint(c.x) * 73856093u) ^ (uint(c.y) * 19349663u) ^ (uint(c.z) * 83492791u); h ^= h >> 13u; h *= 0x5bd1e995u; h ^= h >> 15u; return h; }\n"
    "float dn_c(ivec3 c) { return float(dn_h(c) & 0xFFFFFFu) / 16777215.0; }\n"
    "float dn_v(vec3 p) {\n"
    "  vec3 fl = floor(p), f = p - fl; ivec3 i = ivec3(fl); f = f * f * (3.0 - 2.0 * f);\n"
    "  float a = mix(dn_c(i), dn_c(i + ivec3(1, 0, 0)), f.x), b = mix(dn_c(i + ivec3(0, 1, 0)), dn_c(i + ivec3(1, 1, 0)), f.x);\n"
    "  float c = mix(dn_c(i + ivec3(0, 0, 1)), dn_c(i + ivec3(1, 0, 1)), f.x), d = mix(dn_c(i + ivec3(0, 1, 1)), dn_c(i + ivec3(1, 1, 1)), f.x);\n"
    "  return mix(mix(a, b, f.y), mix(c, d, f.y), f.z);\n"
    "}\n"
    "float dissolve_noise(vec3 p) { return dn_v(p * 5.0) * 0.65 + dn_v(p * 11.0 + 17.0) * 0.35; }\n"
    "void main() { if (v_fx0.w > 0.0 || texture(u_tex, v_uv).a < 0.5 || (v_fx0.x > 0.0 && dissolve_noise(v_op) < v_fx0.x)) discard; }\n";
/* post-processing: a full-screen triangle; bloom down (4 bilinear taps, bright pass on the first), up (tent), composite */
static const char *POST_VS =
    "out vec2 v_uv; void main() { vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); v_uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
static const char *SKY_FS =   /* the same gradient as the soft renderer's sky_fill */
    "in vec2 v_uv; uniform mat4 u_ivp; uniform vec4 u_top, u_hor, u_bot; out vec4 o_col;\n"
    "void main() {\n"
    "  vec2 n = v_uv * 2.0 - 1.0; vec4 a = u_ivp * vec4(n, -1.0, 1.0), b = u_ivp * vec4(n, 1.0, 1.0);\n"
    "  vec3 d = b.xyz / b.w - a.xyz / a.w; float t = d.y / length(d);\n"
    "  o_col = vec4(mix(u_hor.rgb, t >= 0.0 ? u_top.rgb : u_bot.rgb, min(abs(t), 1.0)), 1.0);\n"
    "}\n";
static const char *DOWN_FS =
    "in vec2 v_uv; uniform sampler2D u_src; uniform vec2 u_texel; uniform float u_threshold; out vec4 o_col;\n"
    "void main() {\n"
    "  vec3 c = (texture(u_src, v_uv + vec2(-u_texel.x, -u_texel.y)).rgb + texture(u_src, v_uv + vec2(u_texel.x, -u_texel.y)).rgb\n"
    "          + texture(u_src, v_uv + vec2(-u_texel.x, u_texel.y)).rgb + texture(u_src, v_uv + u_texel).rgb) * 0.25;\n"
    "  if (u_threshold >= 0.0) { float l = dot(c, vec3(0.2126, 0.7152, 0.0722)); c = min(c * (l > 1e-4 ? max(l - u_threshold, 0.0) / l : 0.0), vec3(64.0)); }\n"
    "  o_col = vec4(c, 1.0);\n"
    "}\n";
static const char *UP_FS =
    "in vec2 v_uv; uniform sampler2D u_src, u_down; uniform vec2 u_texel; out vec4 o_col;\n"
    "void main() {\n"
    "  vec3 c = texture(u_down, v_uv).rgb;\n"
    "  for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) c += texture(u_src, v_uv + vec2(float(i), float(j)) * u_texel).rgb * (float((i == 0 ? 2 : 1) * (j == 0 ? 2 : 1)) / 16.0);\n"
    "  o_col = vec4(c, 1.0);\n"
    "}\n";
/* ssao (as soft_ssao): r = ao, g = 1 where something was drawn; rows here are bottom-up, x/y below top-down */
static const char *SSAO_FS =
    "in vec2 v_uv; uniform highp sampler2D u_depth; uniform mat4 u_ivp, u_vp; uniform vec4 u_ss; out vec4 o_col;\n"   /* ss: w, h, radius, perspective */
    "#ifdef ARC_ZREV\n"   /* 0.7.2 reversed-Z: nothing drawn = 0; orthographic clip z falls with distance */
    "#define EMPTY(d) ((d) <= 0.0)\n#define ZS(v) (-(v))\n"
    "#else\n"
    "#define EMPTY(d) ((d) >= 1.0)\n#define ZS(v) (v)\n"
    "#endif\n"
    "const vec3 K[16] = vec3[16](vec3(0.0257, 0.0000, 0.1511), vec3(-0.0350, 0.0321, 0.1562), vec3(0.0059, -0.0673, 0.1667), vec3(0.0550, 0.0717, 0.1819), vec3(-0.1159, -0.0205, 0.2011), vec3(0.1272, -0.0809, 0.2235), vec3(-0.0494, 0.1838, 0.2481), vec3(-0.1094, -0.2106, 0.2740), vec3(0.2746, 0.1003, 0.3000), vec3(-0.3293, 0.1359, 0.3247), vec3(0.1821, -0.3890, 0.3463), vec3(0.1535, 0.4894, 0.3626), vec3(-0.5252, -0.3044, 0.3705), vec3(0.6962, -0.1530, 0.3650), vec3(-0.4782, 0.6801, 0.3369), vec3(-0.1241, -0.9578, 0.2591));\n"
    "const float BY[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);\n"
    "float dep(int x, int y) { int w = int(u_ss.x), h = int(u_ss.y); x = clamp(x, 0, w - 1); y = clamp(y, 0, h - 1); return texelFetch(u_depth, ivec2(x, h - 1 - y), 0).r; }\n"
    "vec3 wpos(int x, int y, float d) { vec4 p = u_ivp * vec4((float(x) + 0.5) / u_ss.x * 2.0 - 1.0, 1.0 - (float(y) + 0.5) / u_ss.y * 2.0, d * 2.0 - 1.0, 1.0); return p.xyz / p.w; }\n"
    "void main() {\n"
    "  int x = int(gl_FragCoord.x), y = int(u_ss.y) - 1 - int(gl_FragCoord.y);\n"
    "  float d = dep(x, y);\n"
    "  if (EMPTY(d)) { o_col = vec4(0.0); return; }\n"
    "  vec3 P = wpos(x, y, d);\n"
    "  float dl = dep(x - 1, y), dr = dep(x + 1, y), du = dep(x, y - 1), dd = dep(x, y + 1);\n"
    "  vec3 ex = abs(dr - d) < abs(d - dl) ? wpos(x + 1, y, dr) - P : P - wpos(x - 1, y, dl);\n"
    "  vec3 ey = abs(dd - d) < abs(d - du) ? wpos(x, y + 1, dd) - P : P - wpos(x, y - 1, du);\n"
    "  vec3 N = normalize(cross(ey, ex));\n"
    "  float ang = BY[(y & 3) * 4 + (x & 3)] * (6.28318531 / 16.0);\n"
    "  vec3 hlp = abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n"
    "  vec3 T0 = normalize(cross(hlp, N)), B0 = cross(N, T0), T = T0 * cos(ang) + B0 * sin(ang), B = cross(N, T);\n"
    "  float occ = 0.0, R = u_ss.z;\n"
    "  for (int k = 0; k < 16; k++) {\n"
    "    vec3 sp = P + (T * K[k].x + B * K[k].y + N * K[k].z) * R;\n"
    "    vec4 c = u_vp * vec4(sp, 1.0);\n"
    "    if (c.w <= 1e-6) continue;\n"
    "    float sx = (c.x / c.w * 0.5 + 0.5) * u_ss.x, sy = (0.5 - c.y / c.w * 0.5) * u_ss.y;\n"
    "    if (sx < 0.0 || sy < 0.0 || sx >= u_ss.x || sy >= u_ss.y) continue;\n"
    "    int ix = int(sx), iy = int(sy); float qd = dep(ix, iy);\n"
    "    if (EMPTY(qd)) continue;\n"
    "    vec3 Q = wpos(ix, iy, qd); vec4 qc = u_vp * vec4(Q, 1.0);\n"
    "    bool hid = u_ss.w > 0.5 ? qc.w < c.w - 0.025 * R : ZS(qc.z) < ZS(c.z) - 0.025 * R * abs(u_vp[2][2]);\n"
    "    if (hid) occ += clamp(2.0 - distance(Q, P) / R, 0.0, 1.0);\n"
    "  }\n"
    "  o_col = vec4(1.0 - occ / 16.0, 1.0, 0.0, 1.0);\n"
    "}\n";
static const char *COMP_FS =
    "in vec2 v_uv; uniform sampler2D u_scene, u_bloom, u_tilt, u_ao, u_lut; uniform vec4 u_p0, u_p1, u_p2, u_p3, u_p4, u_res; out vec4 o_col;\n"
    /* p0: bloom, exposure, tonemap, desaturate; p1: contrast, vignette, aspect, scanline period; p2: tilt, focus, width, 1/levels;
       p3: ssao, fxaa, crt, pixelate; p4: aberration, scanlines, lut strength (0 = none), lut size; res: w, h, 1/w, 1/h.
       q: top-down screen uv (as the soft renderer); tc: texture coordinates (bottom-up) */
    "vec3 S(vec2 tc) { return texture(u_scene, tc).rgb; }\n"
    "float luma(vec3 c) { return sqrt(dot(clamp(c, 0.0, 1.0), vec3(0.299, 0.587, 0.114))); }\n"
    "float FL(vec2 p) { return luma(S(p)); }\n"
    "vec3 fxaa(vec2 tc) {\n"
    "  float tx = u_res.z, ty = u_res.w, up = u_res.w;\n"
    "  vec3 c = S(tc); float M = luma(c), N = FL(tc + vec2(0.0, up)), So = FL(tc - vec2(0.0, up)), E = FL(tc + vec2(tx, 0.0)), W = FL(tc - vec2(tx, 0.0));\n"
    "  float mn = min(M, min(min(N, So), min(E, W))), mx = max(M, max(max(N, So), max(E, W))), range = mx - mn;\n"
    "  if (range < max(0.0312, 0.125 * mx)) return c;\n"
    "  float NW = FL(tc + vec2(-tx, up)), NE = FL(tc + vec2(tx, up)), SW = FL(tc + vec2(-tx, -up)), SE = FL(tc + vec2(tx, -up));\n"
    "  float sub = clamp(abs((2.0 * (N + So + E + W) + NW + NE + SW + SE) / 12.0 - M) / range, 0.0, 1.0); sub = sub * sub * (3.0 - 2.0 * sub); sub = sub * sub * 0.75;\n"
    "  float eh = abs(NW + SW - 2.0 * W) + 2.0 * abs(N + So - 2.0 * M) + abs(NE + SE - 2.0 * E);\n"
    "  float ev = abs(NW + NE - 2.0 * N) + 2.0 * abs(W + E - 2.0 * M) + abs(SW + SE - 2.0 * So);\n"
    "  bool horz = eh >= ev;\n"
    "  vec2 nrm = horz ? vec2(0.0, up) : vec2(-tx, 0.0), al = horz ? vec2(tx, 0.0) : vec2(0.0, ty);\n"
    "  float L1 = horz ? N : W, L2 = horz ? So : E, g1 = abs(L1 - M), g2 = abs(L2 - M);\n"
    "  bool p1 = g1 >= g2; vec2 st = p1 ? nrm : -nrm; float lo = ((p1 ? L1 : L2) + M) * 0.5, gs = max(g1, g2) * 0.25;\n"
    "  vec2 pb = tc + st * 0.5, pn = pb - al, pp = pb + al;\n"
    "  float en = FL(pn) - lo, ep = FL(pp) - lo; bool dn = abs(en) >= gs, dp = abs(ep) >= gs;\n"
    "  float Q[8] = float[8](1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 4.0);\n"
    "  for (int i = 0; i < 8; i++) {\n"
    "    if (dn && dp) break;\n"
    "    if (!dn) { pn -= al * Q[i]; en = FL(pn) - lo; dn = abs(en) >= gs; }\n"
    "    if (!dp) { pp += al * Q[i]; ep = FL(pp) - lo; dp = abs(ep) >= gs; }\n"
    "  }\n"
    "  float aa = dot(al, al), dstn = dot(tc - pn, al) / aa, dstp = dot(pp - tc, al) / aa;\n"
    "  bool mlt = M - lo < 0.0, good = dstn < dstp ? ((en < 0.0) != mlt) : ((ep < 0.0) != mlt);\n"
    "  float off = good ? 0.5 - min(dstn, dstp) / (dstn + dstp) : 0.0; off = max(off, sub);\n"
    "  return S(tc + st * off);\n"
    "}\n"
    "float ao(vec2 tc) {\n"
    "  vec2 c = texture(u_ao, tc).rg; if (c.g < 0.5) return 1.0;\n"
    "  float sr = 0.0, sg = 0.0;\n"
    "  for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) { vec2 a = texture(u_ao, tc + vec2(float(i), float(j)) * 1.5 * u_res.zw).rg; sr += a.r; sg += a.g; }\n"
    "  return sg > 1e-6 ? sr / sg : 1.0;\n"
    "}\n"
    "vec3 lut(vec3 c) {\n"
    "  float n = u_p4.w; c = clamp(c, 0.0, 1.0); float bl = c.b * (n - 1.0), b0 = floor(bl), b1 = min(b0 + 1.0, n - 1.0), f = bl - b0;\n"
    "  float x = c.r * (n - 1.0) + 0.5, y = (c.g * (n - 1.0) + 0.5) / n;\n"
    "  return mix(texture(u_lut, vec2((b0 * n + x) / (n * n), y)).rgb, texture(u_lut, vec2((b1 * n + x) / (n * n), y)).rgb, f);\n"
    "}\n"
    "void main() {\n"
    "  vec2 uv = vec2(v_uv.x, 1.0 - v_uv.y), q = uv, tc = v_uv;\n"
    "  if (u_p3.z > 0.0) {\n"
    "    vec2 d = q - 0.5; q = 0.5 + d * (1.0 + 0.25 * u_p3.z * dot(2.0 * d, 2.0 * d));\n"
    "    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) { o_col = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
    "    tc = vec2(q.x, 1.0 - q.y);\n"
    "  }\n"
    "  float vs = q.y;\n"
    "  if (u_p3.w > 1.0) { q = (floor(q * u_res.xy / u_p3.w) + 0.5) * u_p3.w * u_res.zw; tc = vec2(q.x, 1.0 - q.y); }\n"
    "  vec3 s = u_p3.y > 0.5 ? fxaa(tc) : S(tc);\n"
    "  if (u_p4.x > 0.0) { vec2 o = 0.02 * u_p4.x * (q - 0.5); o.y = -o.y; s.r = S(tc + o).r; s.b = S(tc - o).b; }\n"
    "  if (u_p2.x > 0.0) s = mix(s, texture(u_tilt, tc).rgb * u_p2.w, u_p2.x * smoothstep(0.0, 1.0, clamp((abs(q.y - u_p2.y) - u_p2.z) / 0.2, 0.0, 1.0)));\n"
    "  if (u_p3.x > 0.0) s *= mix(1.0, ao(tc), u_p3.x);\n"
    "  vec3 c = s * u_p0.y + texture(u_bloom, tc).rgb * u_p0.x;\n"
    "  if (u_p0.z > 0.5) { c = max(c, 0.0); c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14); }\n"
    "  float l = dot(c, vec3(0.2126, 0.7152, 0.0722)); c = mix(c, vec3(l), u_p0.w); c = (c - 0.5) * (1.0 + u_p1.x) + 0.5;\n"
    "  if (u_p4.z > 0.0) c = mix(c, lut(c), u_p4.z);\n"
    "  if (u_p1.y > 0.0) { vec2 d = (uv - 0.5) * vec2(u_p1.z, 1.0); float k = smoothstep(0.0, 1.0, clamp((length(d) / sqrt(0.25 * u_p1.z * u_p1.z + 0.25) - 0.45) / 0.55, 0.0, 1.0)); c *= 1.0 - u_p1.y * k; }\n"
    "  if (u_p4.y > 0.0) c *= 1.0 - u_p4.y * (0.5 - 0.5 * cos(6.28318531 * (vs * u_res.y - 0.5) / u_p1.w));\n"
    "  o_col = vec4(clamp(c, 0.0, 1.0), 1.0);\n"
    "}\n";
static const char *LINE_VS =
    "layout(location=0) in vec3 a_pos; layout(location=1) in vec4 a_col; layout(location=2) in float a_gain; uniform mat4 u_vp; uniform float u_flip; out vec4 v_col;\n"
    "void main() { v_col = vec4(a_col.rgb * a_gain, a_col.a); gl_Position = u_vp * vec4(a_pos, 1.0); gl_Position.y *= u_flip;\n"
    "#ifdef ARC_ZFIX\n"
    "  gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;\n"
    "#endif\n"
    "}\n";
static const char *LINE_FS = "in vec4 v_col; out vec4 o_col; void main() { o_col = v_col; }\n";
static const char *QUAD_VS =
    "layout(location=0) in vec2 a_pos; layout(location=1) in vec2 a_uv; layout(location=2) in vec4 a_col; uniform vec2 u_screen; uniform float u_flip, u_tflip;\n"
    "out vec2 v_uv; out vec4 v_col;\n"
    "void main() { v_uv = vec2(a_uv.x, mix(a_uv.y, 1.0 - a_uv.y, u_tflip)); v_col = a_col; gl_Position = vec4(a_pos.x / u_screen.x * 2.0 - 1.0, (1.0 - a_pos.y / u_screen.y * 2.0) * u_flip, 0.0, 1.0); }\n";
static const char *QUAD_FS =
    "in vec2 v_uv; in vec4 v_col; uniform sampler2D u_tex; uniform float u_sdf; out vec4 o_col;\n"
    "void main() { vec4 t = texture(u_tex, v_uv);\n"
    "  if (u_sdf > 0.0) { float e = clamp((t.a - 0.5) * u_sdf + 0.5, 0.0, 1.0); t = vec4(1.0, 1.0, 1.0, e * e * (3.0 - 2.0 * e)); }\n"   /* 0.10.1 distance-field text */
    "  o_col = (v_uv.x < -2048.0 ? vec4(1.0) : t) * v_col; }\n";   /* plain triangles (ARC__UV_PLAIN): white */

typedef struct { GLuint vao, vbo, ibo, sbo; int icount; } gl_mesh;
static struct {
    GLuint mesh_prog, line_prog, quad_prog;
    GLint u_sdepth, u_ivp, u_soft; GLuint soft_tex, soft_fbo; int soft_w, soft_h; GLenum soft_fmt; int soft_win;   /* 0.7 soft particles; soft_win: */
                                                                                                                  /* the window's format (index + 1, -1 = none) */
    GLint u_layers, u_spl, u_spl2, u_wmask, u_wm0, u_wm1, u_cut0, u_cut1, u_cut2; GLuint dummy_arr; bool cut_on; float cut2[4]; GLint u_ntex, u_otex, u_etex, u_m0, u_m1, u_m2, u_m3, u_m4, u_m5, u_dtex, u_mtex, u_rtex, u_clip, u_rpar, u_ramt, u_m6, u_ptex, u_paint, u_paint3, u_pat_fs, u_pat_vs, u_pjm, u_env_top, u_env_hor, u_env_bot, u_ltex, u_ltile, u_lidx, u_nlt, u_time;
    GLuint lt_tex, ltile_tex, lidx_tex; const arc__frame_data *cur_frame;
    GLuint lsh_tex, lsh_fbo; int lsh_res; bool lsh_ok; GLint u_lshadow, u_lsh;   /* 0.9.5 light shadow atlas (6 x 4 faces of lsh_res); lsh_ok: drawn for the pass now */   /* 0.9.5 tiled lights: data (4 x 256 RGBA32F), a pass's tiles (16 x 9 R32UI), its lists (256 x 36 R8UI) */
    GLint u_vp, u_flip, u_eye, u_ld, u_lc, u_amb, u_fc, u_fog, u_rim, u_opaque, u_mflip, u_tex, u_bones, ul_vp, ul_flip, uq_screen, uq_tex, uq_flip, uq_tflip, uq_sdf;
    GLuint inst_vbo, line_vao, line_vbo, ltri_vao, ltri_vbo, quad_vao, quad_vbo, imm_vao, imm_vbo, ident_vbo, bone_tex;
    GLuint fbo[ARC__MAX_TEXTURES], rbo[ARC__MAX_TEXTURES];
    uint8_t tfmt[ARC__MAX_TEXTURES]; int ctex;   /* 0.7.1: a texture's ARC_TEXFMT_* on the GPU; formats the GPU takes (bit 1 << fmt) */
    bool arr[ARC__MAX_TEXTURES];                 /* 0.9.6 the texture is an array (GL_TEXTURE_2D_ARRAY) */
    gl_mesh meshes[ARC__MAX_MESHES];
    GLuint tex[ARC__MAX_TEXTURES];
    int w, h;
    int msaa; GLuint ms_fbo, ms_color, ms_depth; int ms_w, ms_h;   /* multisampled screen, resolved into the window */
    /* shadows */
    GLuint shadow_prog, shadow_tex, shadow_fbo, shadow_dummy; int shadow_res;
    GLint u_svp, u_sh, u_shadow, us_vp, us_flip, us_bones, us_tex, u_cvp, u_cp, u_shc;
    /* post-processing: the screen's 3D in HDR (optionally multisampled, resolved), bloom levels */
    GLuint down_prog, up_prog, comp_prog, post_vao, sky_prog;
    GLint uk_ivp, uk_top, uk_hor, uk_bot;
    GLint ud_src, ud_texel, ud_thr, uu_src, uu_down, uu_texel, uc_scene, uc_bloom, uc_p0, uc_p1, uc_p2, uc_tilt, uc_ao, uc_lut, uc_p3, uc_p4, uc_res;
    GLuint ssao_prog; GLint ua_depth, ua_ivp, ua_vp, ua_ss;
    /* the post buffers: ps[0] the screen's, the rest render targets' own (0.8.2, arc_target_set_post; ps_id: the target) */
    struct gl_pset {
        GLuint scene_tex, scene_depth, scene_fbo, scene_ms_fbo, scene_ms_color, scene_ms_depth; int scene_w, scene_h, scene_samples; GLenum scene_fmt;
        GLuint bl_tex[ARC__BLOOM_LEVELS], bl_fbo[ARC__BLOOM_LEVELS], bu_tex[ARC__BLOOM_LEVELS], bu_fbo[ARC__BLOOM_LEVELS]; int bl_w[ARC__BLOOM_LEVELS], bl_h[ARC__BLOOM_LEVELS];
        GLuint tl_tex[ARC__TILT_LEVELS], tl_fbo[ARC__TILT_LEVELS], tu_tex[ARC__TILT_LEVELS], tu_fbo[ARC__TILT_LEVELS];   /* tilt-shift blur (sizes as bl_w/bl_h) */
        GLuint ao_tex, ao_fbo;
    } ps[1 + ARC__PSET_MAX];
    uint32_t ps_id[1 + ARC__PSET_MAX];
    /* 0.7.2 reversed-Z (arc__pass.zrev), fixed at init: the depth format of every depth buffer ARC makes, the value
       depth is cleared to, the usual compare and its opposite (x-ray, HIDDEN); clipctl: 0..1 clip range available
       (glClipControl / EXT_clip_control), clip01: what it is set to now */
    bool zrev, clipctl, clip01; GLenum dfmt, zless, zgreater; float dclear;
    GLuint rs_fbo, rs_color; int rs_w, rs_h;   /* WebGL2 + reversed-Z + msaa: a plain RGBA8 step between the multisampled screen and the (RGB) canvas */
} GL;
typedef struct gl_pset gl_pset;
static gl_pset *PS = &GL.ps[0];   /* the post buffers in use */
#if defined(ARC_GLES)
#include <emscripten/emscripten.h>
EM_JS(int, arc_web_clip_control_init, (void), {
    var e = GLctx ? GLctx.getExtension('EXT_clip_control') : null; Module.arcClipCtl = e; return e ? 1 : 0;
});
EM_JS(void, arc_web_clip_control, (int zero_one), {
    var e = Module.arcClipCtl; if (e) e.clipControlEXT(e.LOWER_LEFT_EXT, zero_one ? e.ZERO_TO_ONE_EXT : e.NEGATIVE_ONE_TO_ONE_EXT);
});
static void gl_clear_depth(float v) { glClearDepthf(v); }
#else
typedef void (APIENTRYP arc_clip_control_fn)(GLenum, GLenum);
static arc_clip_control_fn p_glClipControl;
static void gl_clear_depth(float v) { glClearDepth((GLdouble)v); }
#endif
#ifndef GL_LOWER_LEFT
#define GL_LOWER_LEFT 0x8CA1
#endif
#ifndef GL_NEGATIVE_ONE_TO_ONE
#define GL_NEGATIVE_ONE_TO_ONE 0x935E
#define GL_ZERO_TO_ONE 0x935F
#endif
/* the clip range for what comes next: 0..1 for reversed-Z passes, GL's own -1..1 for shadow maps */
static void gl_clip01(bool on) {
    if (!GL.clipctl || GL.clip01 == on) return;
    GL.clip01 = on;
#if defined(ARC_GLES)
    arc_web_clip_control(on ? 1 : 0);
#else
    p_glClipControl(GL_LOWER_LEFT, on ? GL_ZERO_TO_ONE : GL_NEGATIVE_ONE_TO_ONE);
#endif
}


static const char *g_gl_defs = "";   /* 0.7.2: #defines put before the next shaders' source */
static GLuint gl_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type); const char *srcs[3] = { GLSL_HEADER, g_gl_defs, src };
    glShaderSource(s, 3, srcs, NULL); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; glGetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "ARC error shader: %s\n", log); }
    return s;
}
static GLuint gl_program(const char *vs, const char *fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, gl_shader(GL_VERTEX_SHADER, vs)); glAttachShader(p, gl_shader(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; glGetProgramInfoLog(p, sizeof log, NULL, log); fprintf(stderr, "ARC error link: %s\n", log); return 0; }
    return p;
}

#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#define INST_STRIDE ((GLsizei)sizeof(arc__instance))
/* per-vertex attributes 0-3 from the mesh, 4-5 from its skin buffer (or constants), 6-12 per instance */
static void gl_vertex_attribs(void) {
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(arc_vertex), (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(arc_vertex), (void *)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(arc_vertex), (void *)24);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(arc_vertex), (void *)28);
}
static void gl_instance_attribs(size_t base) {
    for (int a = 0; a < 4; a++) glVertexAttribPointer((GLuint)(6 + a), 4, GL_FLOAT, GL_FALSE, INST_STRIDE, (void *)(base + (size_t)a * 16));
    glVertexAttribPointer(10, 4, GL_FLOAT, GL_FALSE, INST_STRIDE, (void *)(base + 64));
    glVertexAttribPointer(11, 4, GL_FLOAT, GL_FALSE, INST_STRIDE, (void *)(base + 80));
    glVertexAttribPointer(12, 4, GL_FLOAT, GL_FALSE, INST_STRIDE, (void *)(base + 96));
    glVertexAttribPointer(13, 4, GL_FLOAT, GL_FALSE, INST_STRIDE, (void *)(base + 112));
    glVertexAttribPointer(14, 4, GL_FLOAT, GL_FALSE, INST_STRIDE, (void *)(base + 128));
    glVertexAttribPointer(15, 4, GL_FLOAT, GL_FALSE, INST_STRIDE, (void *)(base + 144));
}
#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif
static GLuint gl_depth_tex2(int n, int h) {   /* a comparison-sampled depth texture, cleared to 1 (fully lit) */
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, n, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    GLuint fb; glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t, 0);
    GLenum none = GL_NONE; glDrawBuffers(1, &none);
    if (GL.zrev) gl_clear_depth(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &fb);
    return t;
}
static GLuint gl_depth_tex(int n) { return gl_depth_tex2(n, n); }
/* 0.7.1 compressed texture formats this GL takes (S3TC = BC1/BC3, RGTC = BC4/BC5 (core in desktop GL 3), BPTC = BC7) */
static bool gl_has_ext(const char *name) {
#if defined(ARC_GLES)
    return emscripten_webgl_enable_extension(emscripten_webgl_get_current_context(), name) != 0;   /* also switches it on */
#else
    GLint n = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++) { const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i); if (e && !strcmp(e, name)) return true; }
    return false;
#endif
}
static void gl_detect_ctex(void) {
    GL.ctex = 0;
#if defined(ARC_GLES)
    if (gl_has_ext("WEBGL_compressed_texture_s3tc")) GL.ctex |= (1 << ARC_TEXFMT_BC1) | (1 << ARC_TEXFMT_BC3);
    if (gl_has_ext("EXT_texture_compression_rgtc")) GL.ctex |= (1 << ARC_TEXFMT_BC4) | (1 << ARC_TEXFMT_BC5);
    if (gl_has_ext("EXT_texture_compression_bptc")) GL.ctex |= 1 << ARC_TEXFMT_BC7;
#else
    if (gl_has_ext("GL_EXT_texture_compression_s3tc")) GL.ctex |= (1 << ARC_TEXFMT_BC1) | (1 << ARC_TEXFMT_BC3);
    GL.ctex |= (1 << ARC_TEXFMT_BC4) | (1 << ARC_TEXFMT_BC5);
    if (gl_has_ext("GL_ARB_texture_compression_bptc")) GL.ctex |= 1 << ARC_TEXFMT_BC7;
    else { GLint major = 0, minor = 0; glGetIntegerv(GL_MAJOR_VERSION, &major); glGetIntegerv(GL_MINOR_VERSION, &minor); if (major > 4 || (major == 4 && minor >= 2)) GL.ctex |= 1 << ARC_TEXFMT_BC7; }
#endif
}
static int gl_compressed_formats(void) { return GL.ctex; }
/* 0.7.2: reversed-Z set-up: float depth everywhere ARC makes depth, and a 0..1 clip range when this GL has one */
static void gl_depth_setup(void) {
    GL.zrev = arc__depth_mode != ARC_DEPTH_STANDARD; GL.clipctl = false; GL.clip01 = false;
    GL.dfmt = GL.zrev ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT24; GL.dclear = GL.zrev ? 0.0f : 1.0f;
    GL.zless = GL.zrev ? GL_GREATER : GL_LESS; GL.zgreater = GL.zrev ? GL_LESS : GL_GREATER;
    if (!GL.zrev) return;
    bool forbid = arc_arg_flag("no-clip-control");   /* measure the fallback */
#if defined(ARC_GLES)
    GL.clipctl = !forbid && arc_web_clip_control_init() != 0;
#else
    GLint major = 0, minor = 0; glGetIntegerv(GL_MAJOR_VERSION, &major); glGetIntegerv(GL_MINOR_VERSION, &minor);
    p_glClipControl = (arc_clip_control_fn)arc__gl_get_proc("glClipControl");
    GL.clipctl = !forbid && p_glClipControl && (major > 4 || (major == 4 && minor >= 5) || gl_has_ext("GL_ARB_clip_control"));
#endif
    arc__depth_precise = GL.clipctl;
}
static bool gl_init(void) {
    if (!ARC_GL_LOAD()) return false;
    gl_detect_ctex();
    gl_depth_setup();
    if (GL.zrev && !GL.clipctl) g_gl_defs = "#define ARC_ZREV\n#define ARC_ZFIX\n"; else if (GL.zrev) g_gl_defs = "#define ARC_ZREV\n";
    GL.mesh_prog = gl_program(MESH_VS, MESH_FS); GL.line_prog = gl_program(LINE_VS, LINE_FS); GL.quad_prog = gl_program(QUAD_VS, QUAD_FS);
    if (!GL.mesh_prog || !GL.line_prog || !GL.quad_prog) return false;
    GL.u_vp = glGetUniformLocation(GL.mesh_prog, "u_vp"); GL.u_flip = glGetUniformLocation(GL.mesh_prog, "u_flip"); GL.u_eye = glGetUniformLocation(GL.mesh_prog, "u_eye");
    GL.u_ld = glGetUniformLocation(GL.mesh_prog, "u_light_dir"); GL.u_lc = glGetUniformLocation(GL.mesh_prog, "u_light_col");
    GL.u_amb = glGetUniformLocation(GL.mesh_prog, "u_ambient"); GL.u_fc = glGetUniformLocation(GL.mesh_prog, "u_fog_col");
    GL.u_fog = glGetUniformLocation(GL.mesh_prog, "u_fog"); GL.u_rim = glGetUniformLocation(GL.mesh_prog, "u_rim");
    GL.u_opaque = glGetUniformLocation(GL.mesh_prog, "u_opaque"); GL.u_mflip = glGetUniformLocation(GL.mesh_prog, "u_mflip"); GL.u_tex = glGetUniformLocation(GL.mesh_prog, "u_tex"); GL.u_bones = glGetUniformLocation(GL.mesh_prog, "u_bones");
    GL.u_ntex = glGetUniformLocation(GL.mesh_prog, "u_ntex"); GL.u_otex = glGetUniformLocation(GL.mesh_prog, "u_otex"); GL.u_etex = glGetUniformLocation(GL.mesh_prog, "u_etex");
    GL.u_m3 = glGetUniformLocation(GL.mesh_prog, "u_m3"); GL.u_m4 = glGetUniformLocation(GL.mesh_prog, "u_m4"); GL.u_dtex = glGetUniformLocation(GL.mesh_prog, "u_dtex");
    GL.u_m5 = glGetUniformLocation(GL.mesh_prog, "u_m5");
    GL.u_rtex = glGetUniformLocation(GL.mesh_prog, "u_rtex"); GL.u_clip = glGetUniformLocation(GL.mesh_prog, "u_clip");
    GL.u_rpar = glGetUniformLocation(GL.mesh_prog, "u_rpar"); GL.u_m6 = glGetUniformLocation(GL.mesh_prog, "u_m6");
    GL.u_layers = glGetUniformLocation(GL.mesh_prog, "u_layers");
    GL.u_wmask = glGetUniformLocation(GL.mesh_prog, "u_wmask"); GL.u_wm0 = glGetUniformLocation(GL.mesh_prog, "u_wm0"); GL.u_wm1 = glGetUniformLocation(GL.mesh_prog, "u_wm1");
    GL.u_cut0 = glGetUniformLocation(GL.mesh_prog, "u_cut0"); GL.u_cut1 = glGetUniformLocation(GL.mesh_prog, "u_cut1"); GL.u_cut2 = glGetUniformLocation(GL.mesh_prog, "u_cut2"); GL.u_spl = glGetUniformLocation(GL.mesh_prog, "u_spl"); GL.u_spl2 = glGetUniformLocation(GL.mesh_prog, "u_spl2");
    {   /* 0.9.6 a 1 x 1 white array bound while no mesh splats (WebGL checks sampler types) */
        static const uint8_t white[4] = { 255, 255, 255, 255 };
        glGenTextures(1, &GL.dummy_arr); glBindTexture(GL_TEXTURE_2D_ARRAY, GL.dummy_arr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    }
    GL.u_ptex = glGetUniformLocation(GL.mesh_prog, "u_ptex"); GL.u_paint = glGetUniformLocation(GL.mesh_prog, "u_paint"); GL.u_paint3 = glGetUniformLocation(GL.mesh_prog, "u_paint3");
    GL.u_pat_fs = glGetUniformLocation(GL.mesh_prog, "u_pat"); GL.u_pat_vs = GL.u_pat_fs; GL.u_pjm = glGetUniformLocation(GL.mesh_prog, "u_pjm");   /* 0.10.1 (one uniform shared by both stages) */ GL.u_ramt = glGetUniformLocation(GL.mesh_prog, "u_ramt"); GL.u_mtex = glGetUniformLocation(GL.mesh_prog, "u_mtex");
    GL.u_m0 = glGetUniformLocation(GL.mesh_prog, "u_m0"); GL.u_m1 = glGetUniformLocation(GL.mesh_prog, "u_m1"); GL.u_m2 = glGetUniformLocation(GL.mesh_prog, "u_m2");
    GL.u_time = glGetUniformLocation(GL.mesh_prog, "u_time");
    GL.u_sdepth = glGetUniformLocation(GL.mesh_prog, "u_sdepth"); GL.u_ivp = glGetUniformLocation(GL.mesh_prog, "u_ivp"); GL.u_soft = glGetUniformLocation(GL.mesh_prog, "u_soft");
    GL.u_ltex = glGetUniformLocation(GL.mesh_prog, "u_ltex"); GL.u_ltile = glGetUniformLocation(GL.mesh_prog, "u_ltile");
    GL.u_lidx = glGetUniformLocation(GL.mesh_prog, "u_lidx"); GL.u_nlt = glGetUniformLocation(GL.mesh_prog, "u_nlt");
    GL.u_lshadow = glGetUniformLocation(GL.mesh_prog, "u_lshadow"); GL.u_lsh = glGetUniformLocation(GL.mesh_prog, "u_lsh");
    {   /* 0.9.5 the light textures (integer and float ones: nearest only, read with texelFetch) */
        static float zf[4 * ARC__MAX_FRAME_LIGHTS * 4]; static uint32_t zt[ARC__LT_TILES]; static uint8_t zi[256 * (ARC__LT_TILES * ARC__LT_CAP / 256)];
        GLuint t[3]; glGenTextures(3, t); GL.lt_tex = t[0]; GL.ltile_tex = t[1]; GL.lidx_tex = t[2];
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (int k = 0; k < 3; k++) {
            glBindTexture(GL_TEXTURE_2D, t[k]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            if (k == 0) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, ARC__MAX_FRAME_LIGHTS, 0, GL_RGBA, GL_FLOAT, zf);
            if (k == 1) glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, ARC__LT_TX, ARC__LT_TY, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, zt);
            if (k == 2) glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 256, ARC__LT_TILES * ARC__LT_CAP / 256, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, zi);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }
    GL.u_env_top = glGetUniformLocation(GL.mesh_prog, "u_env_top"); GL.u_env_hor = glGetUniformLocation(GL.mesh_prog, "u_env_hor"); GL.u_env_bot = glGetUniformLocation(GL.mesh_prog, "u_env_bot");
    GL.ul_vp = glGetUniformLocation(GL.line_prog, "u_vp"); GL.ul_flip = glGetUniformLocation(GL.line_prog, "u_flip");
    GL.uq_screen = glGetUniformLocation(GL.quad_prog, "u_screen"); GL.uq_tex = glGetUniformLocation(GL.quad_prog, "u_tex"); GL.uq_flip = glGetUniformLocation(GL.quad_prog, "u_flip"); GL.uq_tflip = glGetUniformLocation(GL.quad_prog, "u_tflip"); GL.uq_sdf = glGetUniformLocation(GL.quad_prog, "u_sdf");
    glGenBuffers(1, &GL.inst_vbo);
    /* skinning matrices: one row of 512 bones (3 texels each) per texture row */
    glGenTextures(1, &GL.bone_tex); glBindTexture(GL_TEXTURE_2D, GL.bone_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, ARC__BONES_PER_ROW * 3, ARC__MAX_BONES / ARC__BONES_PER_ROW, 0, GL_RGBA, GL_FLOAT, NULL);
    /* immediate geometry: per-vertex stream + one identity instance */
    static const float ident[40] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0 };
    glGenVertexArrays(1, &GL.imm_vao); glGenBuffers(1, &GL.imm_vbo); glGenBuffers(1, &GL.ident_vbo);
    glBindVertexArray(GL.imm_vao);
    glBindBuffer(GL_ARRAY_BUFFER, GL.imm_vbo); gl_vertex_attribs();
    glBindBuffer(GL_ARRAY_BUFFER, GL.ident_vbo); glBufferData(GL_ARRAY_BUFFER, sizeof ident, ident, GL_STATIC_DRAW);
    for (int a = 6; a <= 15; a++) { glEnableVertexAttribArray((GLuint)a); glVertexAttribDivisor((GLuint)a, 1); }
    gl_instance_attribs(0);
    for (int k = 0; k < 2; k++) {   /* lines, and (0.10.11) thick lines' triangles: position, colour, gain */
        GLuint *vao = k ? &GL.ltri_vao : &GL.line_vao, *vbo = k ? &GL.ltri_vbo : &GL.line_vbo;
        glGenVertexArrays(1, vao); glGenBuffers(1, vbo);
        glBindVertexArray(*vao); glBindBuffer(GL_ARRAY_BUFFER, *vbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(arc__line_vertex), (void *)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLsizei)sizeof(arc__line_vertex), (void *)12);
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(arc__line_vertex), (void *)16);
    }
    glGenVertexArrays(1, &GL.quad_vao); glGenBuffers(1, &GL.quad_vbo);
    glBindVertexArray(GL.quad_vao); glBindBuffer(GL_ARRAY_BUFFER, GL.quad_vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 20, (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (void *)8);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 20, (void *)16);
    glBindVertexArray(0);
    /* shadows: depth-only program and a 1x1 'fully lit' map bound while shadows are off (WebGL checks sampler types) */
    { const char *d = g_gl_defs; g_gl_defs = ""; GL.shadow_prog = gl_program(MESH_VS, SHADOW_FS); g_gl_defs = d; }   /* shadow maps stay standard */
    GL.u_cvp = glGetUniformLocation(GL.mesh_prog, "u_cvp"); GL.u_cp = glGetUniformLocation(GL.mesh_prog, "u_cp"); GL.u_shc = glGetUniformLocation(GL.mesh_prog, "u_shc");
    GL.u_svp = glGetUniformLocation(GL.mesh_prog, "u_svp"); GL.u_sh = glGetUniformLocation(GL.mesh_prog, "u_sh"); GL.u_shadow = glGetUniformLocation(GL.mesh_prog, "u_shadow");
    if (GL.shadow_prog) {
        GL.us_vp = glGetUniformLocation(GL.shadow_prog, "u_vp"); GL.us_flip = glGetUniformLocation(GL.shadow_prog, "u_flip");
        GL.us_bones = glGetUniformLocation(GL.shadow_prog, "u_bones"); GL.us_tex = glGetUniformLocation(GL.shadow_prog, "u_tex");
    }
    GL.shadow_dummy = gl_depth_tex(1);
    GL.down_prog = gl_program(POST_VS, DOWN_FS); GL.up_prog = gl_program(POST_VS, UP_FS); GL.comp_prog = gl_program(POST_VS, COMP_FS);
    if (GL.down_prog && GL.up_prog && GL.comp_prog) {
        GL.ud_src = glGetUniformLocation(GL.down_prog, "u_src"); GL.ud_texel = glGetUniformLocation(GL.down_prog, "u_texel"); GL.ud_thr = glGetUniformLocation(GL.down_prog, "u_threshold");
        GL.uu_src = glGetUniformLocation(GL.up_prog, "u_src"); GL.uu_down = glGetUniformLocation(GL.up_prog, "u_down"); GL.uu_texel = glGetUniformLocation(GL.up_prog, "u_texel");
        GL.uc_scene = glGetUniformLocation(GL.comp_prog, "u_scene"); GL.uc_bloom = glGetUniformLocation(GL.comp_prog, "u_bloom");
        GL.uc_p0 = glGetUniformLocation(GL.comp_prog, "u_p0"); GL.uc_p1 = glGetUniformLocation(GL.comp_prog, "u_p1");
        GL.uc_p2 = glGetUniformLocation(GL.comp_prog, "u_p2"); GL.uc_tilt = glGetUniformLocation(GL.comp_prog, "u_tilt");
        GL.uc_ao = glGetUniformLocation(GL.comp_prog, "u_ao"); GL.uc_lut = glGetUniformLocation(GL.comp_prog, "u_lut");
        GL.uc_p3 = glGetUniformLocation(GL.comp_prog, "u_p3"); GL.uc_p4 = glGetUniformLocation(GL.comp_prog, "u_p4"); GL.uc_res = glGetUniformLocation(GL.comp_prog, "u_res");
        GL.ssao_prog = gl_program(POST_VS, SSAO_FS);
        if (GL.ssao_prog) {
            GL.ua_depth = glGetUniformLocation(GL.ssao_prog, "u_depth"); GL.ua_ivp = glGetUniformLocation(GL.ssao_prog, "u_ivp");
            GL.ua_vp = glGetUniformLocation(GL.ssao_prog, "u_vp"); GL.ua_ss = glGetUniformLocation(GL.ssao_prog, "u_ss");
        }
    }
    glGenVertexArrays(1, &GL.post_vao);
    GL.sky_prog = gl_program(POST_VS, SKY_FS);
    if (GL.sky_prog) {
        GL.uk_ivp = glGetUniformLocation(GL.sky_prog, "u_ivp"); GL.uk_top = glGetUniformLocation(GL.sky_prog, "u_top");
        GL.uk_hor = glGetUniformLocation(GL.sky_prog, "u_hor"); GL.uk_bot = glGetUniformLocation(GL.sky_prog, "u_bot");
    }
#if defined(ARC_GLES)
    GL.msaa = 0;   /* the web asks the browser for an antialiased canvas instead (plat_web.c) */
    if (GL.zrev) GL.msaa = arc__msaa >= 2 ? (arc__msaa >= 4 ? 4 : 2) : 0;   /* reversed-Z: ARC's own screen (float depth), multisampled here */
#else
    GL.msaa = arc__msaa >= 2 ? (arc__msaa >= 8 ? 8 : arc__msaa >= 4 ? 4 : 2) : 0;
#endif
    return true;
}
static void gl_shutdown(void) {}

static bool gl_mesh_create(uint32_t id, const arc_vertex *v, int vc, const uint32_t *idx, int ic, const uint8_t *skin) {
    gl_mesh *m = &GL.meshes[id];
    glGenVertexArrays(1, &m->vao); glGenBuffers(1, &m->vbo); glGenBuffers(1, &m->ibo);
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo); glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(arc_vertex) * (size_t)vc), v, GL_STATIC_DRAW);
    gl_vertex_attribs();
    if (skin) {
        glGenBuffers(1, &m->sbo); glBindBuffer(GL_ARRAY_BUFFER, m->sbo); glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vc * 8), skin, GL_STATIC_DRAW);
        glEnableVertexAttribArray(4); glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_FALSE, 8, (void *)0);
        glEnableVertexAttribArray(5); glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, 8, (void *)4);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(uint32_t) * (size_t)ic), idx, GL_STATIC_DRAW);
    /* instance attributes: pointers are set per draw (no base-instance in GL 3.3 / WebGL2) */
    glBindBuffer(GL_ARRAY_BUFFER, GL.inst_vbo);
    for (int a = 6; a <= 15; a++) { glEnableVertexAttribArray((GLuint)a); glVertexAttribDivisor((GLuint)a, 1); }
    glBindVertexArray(0);
    m->icount = ic;
    return true;
}
static void gl_mesh_update(uint32_t id, const arc_vertex *v, int vc) {
    gl_mesh *m = &GL.meshes[id]; if (!m->vbo) return;
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo); glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(sizeof(arc_vertex) * (size_t)vc), v);
}
static void gl_mesh_destroy(uint32_t id) {
    gl_mesh *m = &GL.meshes[id];
    glDeleteVertexArrays(1, &m->vao); glDeleteBuffers(1, &m->vbo); glDeleteBuffers(1, &m->ibo);
    if (m->sbo) glDeleteBuffers(1, &m->sbo);
    memset(m, 0, sizeof *m);
}
static void gl_tex_filter(GLuint tex, int flags) {
    bool smooth = (flags & ARC_TEX_SMOOTH) != 0, mips = (flags & ARC_TEX_MIPMAPS) != 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? (smooth ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR) : (smooth ? GL_LINEAR : GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
}
/* 0.9.6 a texture array (see arc__backend.tex_create_array): the mips ARC made, trilinear, repeating */
static bool gl_tex_create_array(uint32_t id, int w, int h, int n, int levels, const uint8_t *const *mips, const int *mw, const int *mh) {
    (void)w; (void)h;
    GL.tfmt[id] = ARC_TEXFMT_RGBA8; GL.arr[id] = true;
    glGenTextures(1, &GL.tex[id]); glBindTexture(GL_TEXTURE_2D_ARRAY, GL.tex[id]);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, levels - 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int k = 0; k < levels; k++) {
        glTexImage3D(GL_TEXTURE_2D_ARRAY, k, GL_RGBA8, mw[k], mh[k], n, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        for (int i = 0; i < n; i++) glTexSubImage3D(GL_TEXTURE_2D_ARRAY, k, 0, 0, i, mw[k], mh[k], 1, GL_RGBA, GL_UNSIGNED_BYTE, mips[i * levels + k]);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    return true;
}
static bool gl_tex_create(uint32_t id, int w, int h, const uint8_t *rgba, int flags) {
    GL.tfmt[id] = ARC_TEXFMT_RGBA8; GL.arr[id] = false;
    glGenTextures(1, &GL.tex[id]); gl_tex_filter(GL.tex[id], flags);
    GLint wrap = (flags & ARC_TEX_REPEAT) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (flags & ARC_TEX_MIPMAPS) glGenerateMipmap(GL_TEXTURE_2D);
    return true;
}
/* A render target: colour texture + depth renderbuffer. Like the screen, a target's first row in memory is its
   bottom (so GL breaks ties on pixel edges exactly as it does on the screen, and as raylib does); the quad shader
   flips v when it samples a target (u_tflip), so to the game every texture's v = 0 is its top. */
static bool gl_target_create(uint32_t id, int w, int h, bool smooth) {
    if (!gl_tex_create(id, w, h, NULL, (smooth ? ARC_TEX_SMOOTH : 0) | (arc__textures[id].flags & ARC_TEX_MIPMAPS))) return false;   /* (0.8.7 the reflection: mips) */
    glGenFramebuffers(1, &GL.fbo[id]); glBindFramebuffer(GL_FRAMEBUFFER, GL.fbo[id]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, GL.tex[id], 0);
    glGenRenderbuffers(1, &GL.rbo[id]); glBindRenderbuffer(GL_RENDERBUFFER, GL.rbo[id]);
    glRenderbufferStorage(GL_RENDERBUFFER, GL.dfmt, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, GL.rbo[id]);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "ARC error render target %dx%d incomplete (0x%x)\n", w, h, st); return false; }
    /* start transparent */
    glBindFramebuffer(GL_FRAMEBUFFER, GL.fbo[id]); glClearColor(0, 0, 0, 0); if (GL.zrev) gl_clear_depth(GL.dclear); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}
/* 0.7.1: blocks uploaded as they are, with their own mip levels */
static bool gl_tex_create_compressed(uint32_t id, int fmt, int w, int h, int levels, const uint8_t *const *data, const size_t *bytes, int flags) {
    GLenum e = fmt == ARC_TEXFMT_BC1 ? 0x83F1 : fmt == ARC_TEXFMT_BC3 ? 0x83F3 : fmt == ARC_TEXFMT_BC4 ? 0x8DBB : fmt == ARC_TEXFMT_BC5 ? 0x8DBD : fmt == ARC_TEXFMT_BC7 ? 0x8E8C : 0;
    if (!e || !((GL.ctex >> fmt) & 1)) return false;   /* (DXT1 RGBA, DXT5, RGTC1, RGTC2, BPTC unorm) */
    while (glGetError() != GL_NO_ERROR) {}
    glGenTextures(1, &GL.tex[id]); gl_tex_filter(GL.tex[id], flags);
    GLint wrap = (flags & ARC_TEX_REPEAT) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);   /* the file's levels are the whole chain we have */
    for (int i = 0, lw = w, lh = h; i < levels; i++, lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1)
        glCompressedTexImage2D(GL_TEXTURE_2D, i, e, lw, lh, 0, (GLsizei)bytes[i], data[i]);
    if (glGetError() != GL_NO_ERROR) { glDeleteTextures(1, &GL.tex[id]); GL.tex[id] = 0; return false; }
    GL.tfmt[id] = (uint8_t)fmt;
    return true;
}
#if defined(ARC_GLES)
/* 0.8.6: a texture straight from an image the browser decoded (arc_webload.c), mips made on the GPU as for RGBA8 */
static bool gl_tex_create_web(uint32_t id, int job, int w, int h, int flags) {
    GL.tfmt[id] = ARC_TEXFMT_RGBA8;
    glGenTextures(1, &GL.tex[id]); gl_tex_filter(GL.tex[id], flags);   /* (binds it) */
    GLint wrap = (flags & ARC_TEX_REPEAT) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    if (!arc__web_tex_upload(job, w, h)) { glDeleteTextures(1, &GL.tex[id]); GL.tex[id] = 0; return false; }
    if (flags & ARC_TEX_MIPMAPS) glGenerateMipmap(GL_TEXTURE_2D);
    return true;
}
#endif
static void gl_post_free(void);
static void gl_tex_destroy(uint32_t id) {
    GL.tfmt[id] = ARC_TEXFMT_RGBA8; GL.arr[id] = false;
    for (int i = 1; i <= ARC__PSET_MAX; i++) if (GL.ps_id[i] == id) { gl_pset *keep = PS; PS = &GL.ps[i]; gl_post_free(); PS = keep; GL.ps_id[i] = 0; }
    if (GL.fbo[id]) { glDeleteFramebuffers(1, &GL.fbo[id]); GL.fbo[id] = 0; }
    if (GL.rbo[id]) { glDeleteRenderbuffers(1, &GL.rbo[id]); GL.rbo[id] = 0; }
    if (GL.tex[id]) { glDeleteTextures(1, &GL.tex[id]); GL.tex[id] = 0; }
}
static void gl_tex_set_smooth(uint32_t id, bool smooth) {
    (void)smooth; if (GL.tex[id]) gl_tex_filter(GL.tex[id], arc__textures[id].flags);
}
static void gl_tex_update(uint32_t id, int w, int h, const uint8_t *rgba) {
    glBindTexture(GL_TEXTURE_2D, GL.tex[id]); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (arc__textures[id].flags & ARC_TEX_MIPMAPS) glGenerateMipmap(GL_TEXTURE_2D);
}

static void gl_upload(GLuint vbo, const void *data, size_t bytes) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, NULL, GL_STREAM_DRAW);    /* orphan */
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, data);
}
static void gl_blend(int mode) {
    switch (mode) {   /* same results as the software renderer */
    case ARC__BLEND_ADD: glEnable(GL_BLEND); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE); break;
    case ARC__BLEND_MULTIPLY: glEnable(GL_BLEND); glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_DST_ALPHA, GL_ZERO); break;
    case ARC__BLEND_NONE: glDisable(GL_BLEND); break;
    default: glEnable(GL_BLEND); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
    }
}
/* a mesh's material: its maps on units 3-5 (white when missing) and factors; NULL = none */
static void gl_material(const arc__mesh_rec *M) {
    static const float zero[4] = { 0, 0, 0, 0 };
    bool on = M && M->has_mat;
    glUniform4fv(GL.u_m0, 1, on ? M->mat : zero); glUniform4fv(GL.u_m1, 1, on ? M->mat + 4 : zero);
    float m2[4] = { on ? M->mat[ARC__MAT_ER] : 0, on ? M->mat[ARC__MAT_EG] : 0, on ? M->mat[ARC__MAT_EB] : 0, on ? 1.0f : 0.0f };
    if (on && M->mat_tex[0] && GL.tfmt[M->mat_tex[0]] == ARC_TEXFMT_BC5) m2[3] = 2.0f;   /* 0.7.1: a BC5 normal map: Z rebuilt */
    glUniform4fv(GL.u_m2, 1, m2);
    bool m2on = M && (M->mat2[ARC__MAT2_DMODE] > 0 || M->mat2[ARC__MAT2_BMODE] > 0);   /* 0.8.4 detail map, hex tiling */
    float m3[4] = { 0, 0, 0, 0 }, m4[4] = { 0, 0, 0, 0 };
    if (m2on) {   /* u_m3: detail mode, scale, strength, rotate; u_m4: base mode, rotate, contrast, detail contrast */
        const float *q = M->mat2;
        m3[0] = q[ARC__MAT2_DMODE]; m3[1] = q[ARC__MAT2_DSCALE]; m3[2] = q[ARC__MAT2_DSTR]; m3[3] = q[ARC__MAT2_DROT];
        m4[0] = q[ARC__MAT2_BMODE]; m4[1] = q[ARC__MAT2_BROT]; m4[2] = q[ARC__MAT2_BCON]; m4[3] = q[ARC__MAT2_DCON];
    }
    uint32_t dt = m2on && M->detail_tex && GL.tex[M->detail_tex] ? M->detail_tex : 0;
    if (!dt) m3[0] = 0;
    uint32_t mt = dt && M->mask_tex && M->mat2[ARC__MAT2_MON] > 0 && GL.tex[M->mask_tex] ? M->mask_tex : 0;
    float m5[4] = { 1, 0, 1, 0 };   /* mask: scale relative to the detail's uv, threshold, softness, on */
    if (mt) { float ms = M->mat2[ARC__MAT2_MSCALE], ds = M->mat2[ARC__MAT2_DSCALE];
        m5[0] = ds > 0 ? (ms > 0 ? ms / ds : 1.0f) : (ms > 0 ? ms : 1.0f); m5[1] = M->mat2[ARC__MAT2_MTHR]; m5[2] = M->mat2[ARC__MAT2_MSOFT]; m5[3] = 1; }
    glUniform4fv(GL.u_m3, 1, m3); glUniform4fv(GL.u_m4, 1, m4); glUniform4fv(GL.u_m5, 1, m5);
    glUniform1f(GL.u_ramt, M ? M->reflect : 0.0f);
    float m6[4] = { M ? M->mat2[ARC__MAT2_BSCALE] : 0, 0, 0, 0 }; glUniform4fv(GL.u_m6, 1, m6);
    {   /* 0.9.6 splat layers (unit 15) */
        bool sp = M && M->layers_tex && GL.tex[M->layers_tex] && GL.arr[M->layers_tex];
        float s1[4] = { 1, 1, 1, 1 }, s2[4] = { 0, 0, 0, 0 };
        if (sp && M->layers_mode == 1) s2[3] = 1;   /* 0.9.9 the colour by layer */
        else if (sp) { for (int i = 0; i < 4; i++) s1[i] = M->mat3[ARC__MAT3_S0 + i]; s2[0] = M->mat3[ARC__MAT3_SHARP]; s2[1] = M->mat3[ARC__MAT3_N]; s2[2] = 1; }
        glUniform4fv(GL.u_spl, 1, s1); glUniform4fv(GL.u_spl2, 1, s2); glUniform1i(GL.u_layers, 15);
        float c2[4] = { GL.cut2[0], GL.cut2[1], GL.cut2[2], GL.cut_on && M && M->cutaway ? 1.0f : 0.0f }; glUniform4fv(GL.u_cut2, 1, c2);   /* 0.9.9 */
        glActiveTexture(GL_TEXTURE15); glBindTexture(GL_TEXTURE_2D_ARRAY, sp ? GL.tex[M->layers_tex] : GL.dummy_arr);
    }
    glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D, GL.tex[dt ? dt : 1]);
    glActiveTexture(GL_TEXTURE8); glBindTexture(GL_TEXTURE_2D, GL.tex[mt ? mt : 1]);
    for (int i = 0; i < 3; i++) {
        uint32_t t = on && M->mat_tex[i] && GL.tex[M->mat_tex[i]] ? M->mat_tex[i] : 1;
        glActiveTexture(GL_TEXTURE3 + (GLenum)i); glBindTexture(GL_TEXTURE_2D, GL.tex[t]);
    }
    glActiveTexture(GL_TEXTURE0);
}
static void gl_mesh_uniforms(const arc__pass *P, float flip, bool opaque) {
    glUseProgram(GL.mesh_prog);
    glUniformMatrix4fv(GL.u_vp, 1, GL_FALSE, P->view_proj.m); glUniform1f(GL.u_flip, flip);
    float eye[4] = { P->eye.x, P->eye.y, P->eye.z, 0 };
    glUniform4fv(GL.u_eye, 1, eye); glUniform4fv(GL.u_ld, 1, P->light_dir); glUniform4fv(GL.u_lc, 1, P->light_col);
    glUniform4fv(GL.u_amb, 1, P->ambient); glUniform4fv(GL.u_fc, 1, P->fog_col); glUniform4fv(GL.u_fog, 1, P->fog); glUniform4fv(GL.u_rim, 1, P->rim);
    glUniform1f(GL.u_opaque, opaque ? 1.0f : 0.0f);
    glUniform1i(GL.u_tex, 0); glUniform1i(GL.u_bones, 1); glUniform1i(GL.u_shadow, 2);
    glUniform1i(GL.u_ntex, 3); glUniform1i(GL.u_otex, 4); glUniform1i(GL.u_etex, 5); glUniform1i(GL.u_dtex, 7); glUniform1i(GL.u_mtex, 8);
    glUniform1f(GL.u_time, P->time);
    {   /* 0.9.9 world mask (unit 16) and cutaway */
        bool wm = P->wm[7] > 0.5f && P->wmask && GL.tex[P->wmask];
        float w0[4] = { P->wm[0], P->wm[1], P->wm[2], P->wm[3] }, w1[4] = { P->wm[4], P->wm[5], P->wm[6], wm ? 1.0f : 0.0f };
        glUniform4fv(GL.u_wm0, 1, w0); glUniform4fv(GL.u_wm1, 1, w1); glUniform1i(GL.u_wmask, 16);
        glActiveTexture(GL_TEXTURE0 + 16); glBindTexture(GL_TEXTURE_2D, GL.tex[wm ? P->wmask : 1]);
        float c0[4] = { P->cut[0], P->cut[1], P->cut[2], P->cut[3] }, c1[4] = { P->cut[4], P->cut[5], P->cut[6], P->cut[7] }, c2[4] = { P->cut[8], P->cut[9], P->cut[10], 0 };
        glUniform4fv(GL.u_cut0, 1, c0); glUniform4fv(GL.u_cut1, 1, c1); glUniform4fv(GL.u_cut2, 1, c2);
        GL.cut_on = P->cut[8] > 0.5f; memcpy(GL.cut2, c2, sizeof c2);
    }
    {   /* 0.8.7: the clip plane (mirror passes) and the reflection this pass's reflective meshes sample */
        uint32_t rt = P->refl_tex && P->refl[0] > 0 && GL.tex[P->refl_tex] ? P->refl_tex : 0;
        float rp[4] = { rt ? P->refl[0] : 0, P->refl[1], P->refl[2], P->refl[3] };
        glUniform4fv(GL.u_clip, 1, P->clip); glUniform4fv(GL.u_rpar, 1, rp); glUniform1i(GL.u_rtex, 9);
        static const float nopaint[4] = { 0, 0, 0, 0 }; glUniform4fv(GL.u_paint, 1, nopaint); glUniform1i(GL.u_ptex, 10);
        static const float nopat0[16] = { 0 }; glUniform4fv(GL.u_pat_fs, 4, nopat0);   /* 0.10.1 */
        glActiveTexture(GL_TEXTURE9); glBindTexture(GL_TEXTURE_2D, GL.tex[rt ? rt : 1]);
    }
    static const float soft_off[4] = { 1, 1, 0, 0 };
    glUniform4fv(GL.u_soft, 1, soft_off); glUniform1i(GL.u_sdepth, 6);
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, GL.tex[1]);
    {   /* 0.9.5 this pass's light tiles and lists (the light data went up with the frame) */
        bool on = P->nlights > 0 && P->lt_tiles >= 0 && GL.cur_frame && GL.cur_frame->light_tiles;
        glUniform1i(GL.u_nlt, on ? P->nlights : 0); glUniform1i(GL.u_ltex, 11); glUniform1i(GL.u_ltile, 12); glUniform1i(GL.u_lidx, 13);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glActiveTexture(GL_TEXTURE12); glBindTexture(GL_TEXTURE_2D, GL.ltile_tex);
        if (on) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ARC__LT_TX, ARC__LT_TY, GL_RED_INTEGER, GL_UNSIGNED_INT, GL.cur_frame->light_tiles + P->lt_tiles);
        glActiveTexture(GL_TEXTURE13); glBindTexture(GL_TEXTURE_2D, GL.lidx_tex);
        if (on && P->lt_idx_count) {
            int full = P->lt_idx_count / 256, rest = P->lt_idx_count % 256; const uint8_t *src = GL.cur_frame->light_index + P->lt_idx;
            if (full) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, full, GL_RED_INTEGER, GL_UNSIGNED_BYTE, src);
            if (rest) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, full, rest, 1, GL_RED_INTEGER, GL_UNSIGNED_BYTE, src + full * 256);
        }
        glActiveTexture(GL_TEXTURE11); glBindTexture(GL_TEXTURE_2D, GL.lt_tex);
        bool lsh = on && P->lsh_n > 0 && P->mb_count && GL.lsh_ok;   /* light shadows drawn for this pass */
        glUniform1i(GL.u_lsh, lsh ? P->lsh_n : 0); glUniform1i(GL.u_lshadow, 14);
        glActiveTexture(GL_TEXTURE14); glBindTexture(GL_TEXTURE_2D, lsh ? GL.lsh_tex : GL.shadow_dummy);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4); glActiveTexture(GL_TEXTURE0);
    }
    glUniform4fv(GL.u_env_top, 1, P->env); glUniform4fv(GL.u_env_hor, 1, P->env + 4); glUniform4fv(GL.u_env_bot, 1, P->env + 8);
    gl_material(NULL);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, GL.bone_tex);
    int atlas = P->shadow_n > 1 ? P->shadow_res * 2 : P->shadow_res;
    bool sh = P->shadow[0] > 0 && GL.shadow_tex && GL.shadow_res == atlas;
    float shp[4] = { sh ? P->shadow[0] : 0, P->shadow[1], P->shadow[2], P->shadow[3] };
    glUniformMatrix4fv(GL.u_svp, 1, GL_FALSE, P->shadow_vp); glUniform4fv(GL.u_sh, 1, shp);
    float shc[4] = { (float)(P->shadow_n > 0 ? P->shadow_n : 1), P->shadow_soft, P->shadow_n > 1 ? 0.5f : 1.0f, 0 };
    glUniformMatrix4fv(GL.u_cvp, 3, GL_FALSE, P->shadow_cvp); glUniform4fv(GL.u_cp, 3, P->shadow_cp); glUniform4fv(GL.u_shc, 1, shc);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, sh ? GL.shadow_tex : GL.shadow_dummy);
    glActiveTexture(GL_TEXTURE0);
}
/* the light's view of the pass's solid meshes into the shadow map (both faces; cut-out alpha respected) */
static void gl_shadow_pass(const arc__frame_data *f, const arc__pass *P) {
    if (!GL.shadow_prog) return;
    int atlas = P->shadow_n > 1 ? P->shadow_res * 2 : P->shadow_res;   /* cascades: a 2x2 atlas */
    if (GL.shadow_res != atlas) {
        if (GL.shadow_tex) { glDeleteTextures(1, &GL.shadow_tex); glDeleteFramebuffers(1, &GL.shadow_fbo); }
        GL.shadow_tex = gl_depth_tex(atlas); GL.shadow_res = atlas;
        glGenFramebuffers(1, &GL.shadow_fbo); glBindFramebuffer(GL_FRAMEBUFFER, GL.shadow_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, GL.shadow_tex, 0);
        GLenum none = GL_NONE; glDrawBuffers(1, &none);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "ARC log shadow map not available here\n"); GL.shadow_res = -1; }
    }
    if (GL.shadow_res != atlas) return;
    glBindFramebuffer(GL_FRAMEBUFFER, GL.shadow_fbo);
    glViewport(0, 0, atlas, atlas);
    if (GL.zrev) { gl_clip01(false); gl_clear_depth(1.0f); }   /* shadow maps stay standard (-1..1, LESS, cleared to 1) */
    glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE); glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glUseProgram(GL.shadow_prog);
    glUniform1f(GL.us_flip, 1.0f); glUniform1i(GL.us_tex, 0); glUniform1i(GL.us_bones, 1);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, GL.bone_tex); glActiveTexture(GL_TEXTURE0);
    for (int ci = 0; ci < (P->shadow_n > 1 ? P->shadow_n : 1); ci++) {
    if (P->shadow_n > 1) glViewport((GLint)(P->shadow_cp[ci * 4 + 2] * (float)atlas + 0.5f), (GLint)(P->shadow_cp[ci * 4 + 3] * (float)atlas + 0.5f), P->shadow_res, P->shadow_res);
    glUniformMatrix4fv(GL.us_vp, 1, GL_FALSE, P->shadow_n > 1 ? P->shadow_cvp + ci * 16 : P->shadow_vp);
    for (int b = P->mb_first; b < P->mb_first + P->mb_count; b++) {
        const arc__mesh_batch *mb = &f->mesh_batches[b]; gl_mesh *m = &GL.meshes[mb->mesh];
        const arc__mesh_rec *M = &arc__meshes[mb->mesh];
        if (!m->vao || M->no_shadow || mb->blend || mb->depth) continue;
        uint32_t tex = M->texture && GL.tex[M->texture] && !M->layers_tex ? M->texture : 1;   /* (0.9.6 a splat map is no cut-out) */
        glBindTexture(GL_TEXTURE_2D, GL.tex[tex]);
        glBindVertexArray(m->vao); glBindBuffer(GL_ARRAY_BUFFER, GL.inst_vbo);
        if (!m->sbo) { glVertexAttrib4f(4, 0, 0, 0, 0); glVertexAttrib4f(5, 1, 0, 0, 0); }
        gl_instance_attribs(sizeof(arc__instance) * (size_t)mb->first);
        glDrawElementsInstanced(GL_TRIANGLES, m->icount, GL_UNSIGNED_INT, 0, mb->count);
    }
    }
    glEnable(GL_CULL_FACE);
    if (GL.zrev) { gl_clip01(true); gl_clear_depth(GL.dclear); }
}
/* 0.9.5 light shadows: the pass's solid meshes from each shadowed light's faces into the atlas (see arc__pass.lsh_n) */
static void gl_lshadow_pass(const arc__frame_data *f, const arc__pass *P) {
    GL.lsh_ok = false;
    if (!GL.shadow_prog) return;
    int r = f->lsh_res;
    if (GL.lsh_res != r) {
        if (GL.lsh_tex) { glDeleteTextures(1, &GL.lsh_tex); glDeleteFramebuffers(1, &GL.lsh_fbo); GL.lsh_tex = 0; }
        GLint mx = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mx); GL.lsh_res = r;
        if (mx < r * 6) { fprintf(stderr, "ARC log light shadows: %d x %d is past this GPU's %d (arc_set_light_shadow_resolution lower)\n", r * 6, r * 4, mx); GL.lsh_res = -r; return; }
        GL.lsh_tex = gl_depth_tex2(r * 6, r * 4);
        glGenFramebuffers(1, &GL.lsh_fbo); glBindFramebuffer(GL_FRAMEBUFFER, GL.lsh_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, GL.lsh_tex, 0);
        GLenum none = GL_NONE; glDrawBuffers(1, &none);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "ARC log light shadows not available here\n"); GL.lsh_res = -r; }
    }
    if (GL.lsh_res != r) return;
    glBindFramebuffer(GL_FRAMEBUFFER, GL.lsh_fbo);
    glViewport(0, 0, r * 6, r * 4);
    if (GL.zrev) { gl_clip01(false); gl_clear_depth(1.0f); }   /* standard depth, as the sun's shadow map */
    glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE); glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glUseProgram(GL.shadow_prog);
    glUniform1f(GL.us_flip, 1.0f); glUniform1i(GL.us_tex, 0); glUniform1i(GL.us_bones, 1);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, GL.bone_tex); glActiveTexture(GL_TEXTURE0);
    for (int sl = 0; sl < P->lsh_n; sl++) for (int fi = 0; fi < f->lsh_faces[sl]; fi++) {
        glViewport(fi * r, sl * r, r, r);
        glUniformMatrix4fv(GL.us_vp, 1, GL_FALSE, f->lsh_vp + (sl * 6 + fi) * 16);
        for (int b = P->mb_first; b < P->mb_first + P->mb_count; b++) {
            const arc__mesh_batch *mb = &f->mesh_batches[b]; gl_mesh *m = &GL.meshes[mb->mesh];
            const arc__mesh_rec *M = &arc__meshes[mb->mesh];
            if (!m->vao || M->no_shadow || mb->blend || mb->depth) continue;
            uint32_t tex = M->texture && GL.tex[M->texture] && !M->layers_tex ? M->texture : 1;   /* (0.9.6 a splat map is no cut-out) */
            glBindTexture(GL_TEXTURE_2D, GL.tex[tex]);
            glBindVertexArray(m->vao); glBindBuffer(GL_ARRAY_BUFFER, GL.inst_vbo);
            if (!m->sbo) { glVertexAttrib4f(4, 0, 0, 0, 0); glVertexAttrib4f(5, 1, 0, 0, 0); }
            gl_instance_attribs(sizeof(arc__instance) * (size_t)mb->first);
            glDrawElementsInstanced(GL_TRIANGLES, m->icount, GL_UNSIGNED_INT, 0, mb->count);
        }
    }
    glEnable(GL_CULL_FACE);
    if (GL.zrev) { gl_clip01(true); gl_clear_depth(GL.dclear); }
    GL.lsh_ok = true;
}
/* ---- post-processing buffers ---- */
static GLuint gl_color_tex(int w, int h, GLenum fmt) {
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    if (fmt == GL_RGBA16F) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, NULL);
    else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}
static GLuint gl_fbo_for(GLuint tex) {
    GLuint fb; glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    return fb;
}
static void gl_post_free(void) {
    if (PS->scene_fbo) { glDeleteFramebuffers(1, &PS->scene_fbo); glDeleteTextures(1, &PS->scene_tex); glDeleteTextures(1, &PS->scene_depth); }
    if (PS->ao_fbo) { glDeleteFramebuffers(1, &PS->ao_fbo); glDeleteTextures(1, &PS->ao_tex); PS->ao_fbo = 0; }
    if (PS->scene_ms_fbo) { glDeleteFramebuffers(1, &PS->scene_ms_fbo); glDeleteRenderbuffers(1, &PS->scene_ms_color); glDeleteRenderbuffers(1, &PS->scene_ms_depth); }
    for (int l = 0; l < ARC__BLOOM_LEVELS; l++) if (PS->bl_fbo[l]) {
        glDeleteFramebuffers(1, &PS->bl_fbo[l]); glDeleteTextures(1, &PS->bl_tex[l]); glDeleteFramebuffers(1, &PS->bu_fbo[l]); glDeleteTextures(1, &PS->bu_tex[l]); PS->bl_fbo[l] = 0;
    }
    for (int l = 0; l < ARC__TILT_LEVELS; l++) if (PS->tl_fbo[l]) {
        glDeleteFramebuffers(1, &PS->tl_fbo[l]); glDeleteTextures(1, &PS->tl_tex[l]); glDeleteFramebuffers(1, &PS->tu_fbo[l]); glDeleteTextures(1, &PS->tu_tex[l]); PS->tl_fbo[l] = 0;
    }
    PS->scene_fbo = PS->scene_ms_fbo = 0; PS->scene_w = PS->scene_h = 0;
}
static bool gl_post_make(int w, int h, GLenum fmt, int samples) {
    gl_post_free();
    PS->scene_tex = gl_color_tex(w, h, fmt); PS->scene_fbo = gl_fbo_for(PS->scene_tex);
    glGenTextures(1, &PS->scene_depth); glBindTexture(GL_TEXTURE_2D, PS->scene_depth);   /* a texture: ssao reads it */
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)GL.dfmt, w, h, 0, GL_DEPTH_COMPONENT, GL.zrev ? GL_FLOAT : GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, PS->scene_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, PS->scene_depth, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { glBindFramebuffer(GL_FRAMEBUFFER, 0); gl_post_free(); return false; }
    if (samples >= 2) {   /* draw multisampled, resolve into scene_tex */
        glGenFramebuffers(1, &PS->scene_ms_fbo); glBindFramebuffer(GL_FRAMEBUFFER, PS->scene_ms_fbo);
        glGenRenderbuffers(1, &PS->scene_ms_color); glBindRenderbuffer(GL_RENDERBUFFER, PS->scene_ms_color);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, fmt == GL_RGBA16F ? GL_RGBA16F : GL_RGBA8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, PS->scene_ms_color);
        glGenRenderbuffers(1, &PS->scene_ms_depth); glBindRenderbuffer(GL_RENDERBUFFER, PS->scene_ms_depth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL.dfmt, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, PS->scene_ms_depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteFramebuffers(1, &PS->scene_ms_fbo); glDeleteRenderbuffers(1, &PS->scene_ms_color); glDeleteRenderbuffers(1, &PS->scene_ms_depth); PS->scene_ms_fbo = 0;
        }
    }
    for (int l = 0; l < ARC__BLOOM_LEVELS; l++) {
        int lw = w >> (l + 1), lh = h >> (l + 1); if (lw < 1) lw = 1; if (lh < 1) lh = 1;
        PS->bl_w[l] = lw; PS->bl_h[l] = lh;
        PS->bl_tex[l] = gl_color_tex(lw, lh, fmt); PS->bl_fbo[l] = gl_fbo_for(PS->bl_tex[l]);
        PS->bu_tex[l] = gl_color_tex(lw, lh, fmt); PS->bu_fbo[l] = gl_fbo_for(PS->bu_tex[l]);
        if (l < ARC__TILT_LEVELS) {
            PS->tl_tex[l] = gl_color_tex(lw, lh, fmt); PS->tl_fbo[l] = gl_fbo_for(PS->tl_tex[l]);
            PS->tu_tex[l] = gl_color_tex(lw, lh, fmt); PS->tu_fbo[l] = gl_fbo_for(PS->tu_tex[l]);
        }
    }
    PS->ao_tex = gl_color_tex(w, h, fmt); PS->ao_fbo = gl_fbo_for(PS->ao_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    PS->scene_w = w; PS->scene_h = h; PS->scene_fmt = fmt; PS->scene_samples = samples;
    return true;
}
/* the HDR scene buffer for this frame (created or resized on demand); 0 if post-processing can't run here */
static GLuint gl_scene_fbo_s(int w, int h, int samples) {
    if (!GL.comp_prog) return 0;
    if (PS->scene_w != w || PS->scene_h != h || PS->scene_samples != samples || !PS->scene_fbo) {
        if (!gl_post_make(w, h, GL_RGBA16F, samples) && !gl_post_make(w, h, GL_RGBA8, samples)) return 0;   /* RGBA8: no HDR, still post */
    }
    return PS->scene_ms_fbo ? PS->scene_ms_fbo : PS->scene_fbo;
}
static GLuint gl_scene_fbo(int w, int h) { return gl_scene_fbo_s(w, h, arc__msaa >= 2 ? (arc__msaa >= 8 ? 8 : arc__msaa >= 4 ? 4 : 2) : 0); }
/* 0.8.2: the post buffers of render target id (a free set if make); targets are not multisampled, nor is their post */
static gl_pset *gl_pset_for(uint32_t id, bool make) {
    for (int i = 1; i <= ARC__PSET_MAX; i++) if (GL.ps_id[i] == id) return &GL.ps[i];
    if (make) for (int i = 1; i <= ARC__PSET_MAX; i++) if (!GL.ps_id[i]) { GL.ps_id[i] = id; return &GL.ps[i]; }
    return NULL;
}
static void gl_fullscreen(void) { glBindVertexArray(GL.post_vao); glDrawArrays(GL_TRIANGLES, 0, 3); }
/* a blur chain (as the soft renderer's soft_chain) */
static void gl_chain(const GLuint *dfbo, const GLuint *dtex, const GLuint *ufbo, const GLuint *utex, int levels, float threshold, float r, int w, int h) {
    glUseProgram(GL.down_prog); glUniform1i(GL.ud_src, 0);
    for (int l = 0; l < levels; l++) {
        int sw = l ? PS->bl_w[l - 1] : w, sh = l ? PS->bl_h[l - 1] : h;
        glBindFramebuffer(GL_FRAMEBUFFER, dfbo[l]); glViewport(0, 0, PS->bl_w[l], PS->bl_h[l]);
        glBindTexture(GL_TEXTURE_2D, l ? dtex[l - 1] : PS->scene_tex);
        glUniform2f(GL.ud_texel, 1.0f / (float)sw, 1.0f / (float)sh); glUniform1f(GL.ud_thr, l ? -1.0f : threshold);
        gl_fullscreen();
    }
    glUseProgram(GL.up_prog); glUniform1i(GL.uu_src, 0); glUniform1i(GL.uu_down, 1);
    for (int l = levels - 1; l >= 0; l--) {
        glBindFramebuffer(GL_FRAMEBUFFER, ufbo[l]); glViewport(0, 0, PS->bl_w[l], PS->bl_h[l]);
        if (l == levels - 1) {   /* the smallest level is copied as it is (the down shader with no offset) */
            glUseProgram(GL.down_prog); glUniform1i(GL.ud_src, 0); glUniform2f(GL.ud_texel, 0, 0); glUniform1f(GL.ud_thr, -1.0f);
            glBindTexture(GL_TEXTURE_2D, dtex[l]);
            gl_fullscreen();
            glUseProgram(GL.up_prog);
            continue;
        }
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, dtex[l]); glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, utex[l + 1]);
        glUniform2f(GL.uu_texel, r / (float)PS->bl_w[l + 1], r / (float)PS->bl_h[l + 1]);
        gl_fullscreen();
    }
}
static void gl_post(const float *post, const arc__pass *P, GLuint screen, int w, int h) {
    bool ssao = post[ARC__POST_SSAO] > 0 && GL.ssao_prog && P;
    if (PS->scene_ms_fbo) {   /* resolve the multisampled scene (and its depth for ssao: sample 0's) */
        glBindFramebuffer(GL_READ_FRAMEBUFFER, PS->scene_ms_fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, PS->scene_fbo);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT | (ssao ? GL_DEPTH_BUFFER_BIT : 0), GL_NEAREST);
    }
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDepthMask(GL_FALSE);
    glActiveTexture(GL_TEXTURE0);
    bool bloom = post[ARC__POST_BLOOM] > 0, tilt = post[ARC__POST_TILT] > 0;
    if (bloom) gl_chain(PS->bl_fbo, PS->bl_tex, PS->bu_fbo, PS->bu_tex, ARC__BLOOM_LEVELS, post[ARC__POST_THRESHOLD], post[ARC__POST_RADIUS], w, h);
    if (tilt) gl_chain(PS->tl_fbo, PS->tl_tex, PS->tu_fbo, PS->tu_tex, ARC__TILT_LEVELS, -1.0f, 1.0f, w, h);
    if (ssao) {
        const float *vp = P->view_proj.m;
        glBindFramebuffer(GL_FRAMEBUFFER, PS->ao_fbo); glViewport(0, 0, w, h);
        glUseProgram(GL.ssao_prog); glUniform1i(GL.ua_depth, 0);
        glUniformMatrix4fv(GL.ua_ivp, 1, GL_FALSE, P->ivp); glUniformMatrix4fv(GL.ua_vp, 1, GL_FALSE, vp);
        float ss[4] = { (float)w, (float)h, post[ARC__POST_SSAO_RADIUS], fabsf(vp[3]) + fabsf(vp[7]) + fabsf(vp[11]) > 1e-6f ? 1.0f : 0.0f }; glUniform4fv(GL.ua_ss, 1, ss);
        glBindTexture(GL_TEXTURE_2D, PS->scene_depth);
        gl_fullscreen();
    }
    uint32_t lut = post[ARC__POST_LUT] > 0 ? (uint32_t)post[ARC__POST_LUT] : 0; if (lut && (!arc__textures[lut].used || !GL.tex[lut])) lut = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, screen); glViewport(0, 0, w, h);
    glUseProgram(GL.comp_prog); glUniform1i(GL.uc_scene, 0); glUniform1i(GL.uc_bloom, 1); glUniform1i(GL.uc_ao, 3); glUniform1i(GL.uc_lut, 4);
    float p0[4] = { bloom ? post[ARC__POST_BLOOM] / ARC__BLOOM_LEVELS : 0, post[ARC__POST_EXPOSURE], post[ARC__POST_TONEMAP], post[ARC__POST_DESAT] };
    float p1[4] = { post[ARC__POST_CONTRAST], post[ARC__POST_VIGNETTE], (float)w / (float)h, fmaxf(2.0f, roundf((float)h / 270.0f)) };
    float p3[4] = { ssao ? post[ARC__POST_SSAO] : 0, post[ARC__POST_FXAA], post[ARC__POST_CRT], post[ARC__POST_PIXELATE] };
    float p4[4] = { post[ARC__POST_ABERRATION], post[ARC__POST_SCANLINES], lut ? post[ARC__POST_LUT_STRENGTH] : 0, post[ARC__POST_LUT_N] };
    float res[4] = { (float)w, (float)h, 1.0f / (float)w, 1.0f / (float)h };
    glUniform4fv(GL.uc_p3, 1, p3); glUniform4fv(GL.uc_p4, 1, p4); glUniform4fv(GL.uc_res, 1, res);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, ssao ? PS->ao_tex : PS->scene_tex);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, lut ? GL.tex[lut] : PS->scene_tex);
    float p2[4] = { tilt ? post[ARC__POST_TILT] : 0, post[ARC__POST_TILT_FOCUS], post[ARC__POST_TILT_WIDTH], 1.0f / ARC__TILT_LEVELS };
    glUniform4fv(GL.uc_p0, 1, p0); glUniform4fv(GL.uc_p1, 1, p1); glUniform4fv(GL.uc_p2, 1, p2); glUniform1i(GL.uc_tilt, 2);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, tilt ? PS->tu_tex[0] : PS->scene_tex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, bloom ? PS->bu_tex[0] : PS->scene_tex); glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, PS->scene_tex);
    gl_fullscreen();
    glDepthMask(GL_TRUE);
}
/* each batch sets its own blend, depth compare and write, and the opaque flag (0.6.3); left as the solid
   defaults afterwards (no blend, LESS, depth writes on) */
static void gl_draw_batches(const arc__frame_data *f, int first, int count, GLenum front, GLenum back_front) {
    int state = -1;
    for (int b = first; b < first + count; b++) {
        const arc__mesh_batch *mb = &f->mesh_batches[b]; gl_mesh *m = &GL.meshes[mb->mesh];
        if (!m->vao) continue;
        const arc__mesh_rec *M = &arc__meshes[mb->mesh];
        uint32_t tex = M->texture && GL.tex[M->texture] ? M->texture : 1;   /* 1 = white */
        gl_material(M);
        glBindTexture(GL_TEXTURE_2D, GL.tex[tex]); glUniform1f(GL.u_mflip, arc__textures[tex].target ? 1.0f : 0.0f);
        {   /* 0.8.8 the batch's paint scheme */
            uint32_t pt = mb->paint_tex && GL.tex[mb->paint_tex] ? mb->paint_tex : 0; float pp[4]; arc__unpack_rgb(mb->tint2, pp); pp[3] = pt ? 1.0f : 0.0f;
            glUniform4fv(GL.u_paint, 1, pp);
            float p3[4]; arc__unpack_rgb(mb->tint3 & 0xFFFFFF, p3); p3[3] = pt && (mb->tint3 >> 24) ? 1.0f : 0.0f; glUniform4fv(GL.u_paint3, 1, p3);
            static const float nopat[16] = { 0 };   /* 0.10.1 the batch's paint pattern */
            const arc__pattern *pa = mb->pat > 0 && mb->pat <= f->pat_count ? &f->pats[mb->pat - 1] : NULL;
            glUniform4fv(GL.u_pat_fs, 4, pa ? &pa->p[0][0] : nopat);
            if (pa) glUniform4fv(GL.u_pjm, 32, pa->jm);
            if (pt) { glActiveTexture(GL_TEXTURE10); glBindTexture(GL_TEXTURE_2D, GL.tex[pt]); glActiveTexture(GL_TEXTURE0); }
        }
        bool wrap = M->mat2[ARC__MAT2_BMODE] > 0.5f && tex != 1 && !(arc__textures[tex].flags & ARC_TEX_REPEAT);   /* 0.8.8: hex / world colour repeats */
        if (wrap) { glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT); }
        if (state != (mb->blend << 4 | mb->depth)) {
            state = mb->blend << 4 | mb->depth;
            if (mb->blend == ARC__MB_ADD) { glEnable(GL_BLEND); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE); }
            else if (mb->blend == ARC__MB_ALPHA) { glEnable(GL_BLEND); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); }
            else glDisable(GL_BLEND);
            glUniform1f(GL.u_opaque, mb->blend == ARC__MB_OPAQUE ? 1.0f : 0.0f);
            int zf = mb->depth & ARC__Z_FUNC;
            glDepthFunc(zf == ARC__Z_ALWAYS ? GL_ALWAYS : zf == ARC__Z_GREATER ? GL.zgreater : GL.zless);   /* (reversed-Z: swapped) */
            glDepthMask((mb->depth & ARC__Z_NOWRITE) ? GL_FALSE : GL_TRUE);
        }
        glBindVertexArray(m->vao); glBindBuffer(GL_ARRAY_BUFFER, GL.inst_vbo);
        if (!m->sbo) { glVertexAttrib4f(4, 0, 0, 0, 0); glVertexAttrib4f(5, 1, 0, 0, 0); }
        gl_instance_attribs(sizeof(arc__instance) * (size_t)mb->first);
        glFrontFace(mb->flip ? back_front : front);
        if (M->double_sided || (GL.cut_on && M->cutaway)) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);   /* (0.9.9 cut meshes show their backs: the cap) */
        glDrawElementsInstanced(GL_TRIANGLES, m->icount, GL_UNSIGNED_INT, 0, mb->count);
        if (wrap) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, GL.tex[tex]); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); }
    }
    glFrontFace(front); glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND); glDepthFunc(GL.zless); glDepthMask(GL_TRUE);
    static const float nopaint[4] = { 0, 0, 0, 0 }; glUniform4fv(GL.u_paint, 1, nopaint);
    static const float nopat1[16] = { 0 }; glUniform4fv(GL.u_pat_fs, 4, nopat1);   /* 0.10.1 */
}
/* the multisampled screen: drawn into, then resolved into the window at the end of the frame */
static GLuint gl_screen_fbo(int w, int h) {
    if (!GL.msaa && !GL.zrev) return 0;   /* reversed-Z: always ARC's own screen (float depth; the window's is 24-bit) */
    if (GL.ms_fbo && GL.ms_w == w && GL.ms_h == h) return GL.ms_fbo;
    if (GL.ms_fbo) { glDeleteFramebuffers(1, &GL.ms_fbo); glDeleteRenderbuffers(1, &GL.ms_color); glDeleteRenderbuffers(1, &GL.ms_depth); GL.ms_fbo = 0; }
    glGenFramebuffers(1, &GL.ms_fbo); glBindFramebuffer(GL_FRAMEBUFFER, GL.ms_fbo);
    glGenRenderbuffers(1, &GL.ms_color); glBindRenderbuffer(GL_RENDERBUFFER, GL.ms_color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, GL.msaa, GL_RGBA8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, GL.ms_color);
    glGenRenderbuffers(1, &GL.ms_depth); glBindRenderbuffer(GL_RENDERBUFFER, GL.ms_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, GL.msaa, GL.dfmt, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, GL.ms_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &GL.ms_fbo); glDeleteRenderbuffers(1, &GL.ms_color); glDeleteRenderbuffers(1, &GL.ms_depth);
        GL.ms_fbo = 0;
        if (GL.msaa) { fprintf(stderr, "ARC log multisampling (%dx) is not available here: drawing without it\n", GL.msaa); GL.msaa = 0; if (GL.zrev) return gl_screen_fbo(w, h); }
        return 0;
    }
    GL.ms_w = w; GL.ms_h = h;
    return GL.ms_fbo;
}

/* 0.7 soft particles: copy the depth drawn so far (the bound framebuffer's) into a texture the mesh shader reads.
   A blit needs the same depth format on both sides: the framebuffers ARC makes are DEPTH_COMPONENT24; the window's
   is asked for. Returns false (particles stay hard) if it can't be done here. */
static bool gl_soft_alloc(int pw, int ph, GLenum fmt) {
    if (GL.soft_fbo && GL.soft_w == pw && GL.soft_h == ph && GL.soft_fmt == fmt) return true;
    if (GL.soft_fbo) { glDeleteFramebuffers(1, &GL.soft_fbo); glDeleteTextures(1, &GL.soft_tex); GL.soft_fbo = 0; }
    bool ds = fmt == GL_DEPTH24_STENCIL8 || fmt == GL_DEPTH32F_STENCIL8;
    GLenum type = fmt == GL_DEPTH24_STENCIL8 ? GL_UNSIGNED_INT_24_8 : fmt == GL_DEPTH32F_STENCIL8 ? GL_FLOAT_32_UNSIGNED_INT_24_8_REV
                : fmt == GL_DEPTH_COMPONENT32F ? GL_FLOAT : fmt == GL_DEPTH_COMPONENT16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    glGenTextures(1, &GL.soft_tex); glBindTexture(GL_TEXTURE_2D, GL.soft_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, pw, ph, 0, ds ? GL_DEPTH_STENCIL : GL_DEPTH_COMPONENT, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &GL.soft_fbo); glBindFramebuffer(GL_FRAMEBUFFER, GL.soft_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, ds ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, GL.soft_tex, 0);
    { GLenum none = GL_NONE; glDrawBuffers(1, &none); glReadBuffer(GL_NONE); }
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    GL.soft_w = pw; GL.soft_h = ph; GL.soft_fmt = fmt;
    if (!ok) { glDeleteFramebuffers(1, &GL.soft_fbo); glDeleteTextures(1, &GL.soft_tex); GL.soft_fbo = 0; }
    return ok;
}
static bool gl_soft_blit(GLuint from, int pw, int ph, GLenum fmt) {
    while (glGetError() != GL_NO_ERROR) {}
    if (!gl_soft_alloc(pw, ph, fmt)) { glBindFramebuffer(GL_FRAMEBUFFER, from); return false; }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, from); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GL.soft_fbo);
    glBlitFramebuffer(0, 0, pw, ph, 0, 0, pw, ph, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, from);
    return glGetError() == GL_NO_ERROR;
}
/* 0.7 soft particles: copy the depth drawn so far (the bound framebuffer's) into a texture the mesh shader reads.
   A blit needs the same depth format on both sides: the framebuffers ARC makes are DEPTH_COMPONENT24; for the
   window's own the formats are tried in turn once and the one that works is kept. false: particles stay hard */
static bool gl_soft_depth(const arc__pass *P, int pw, int ph) {
    static const GLenum FMTS[5] = { GL_DEPTH_COMPONENT24, GL_DEPTH24_STENCIL8, GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT32F, GL_DEPTH32F_STENCIL8 };
    if (GL.u_soft < 0) return false;
    GLint cur = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cur);
    bool ok = false;
    if (cur != 0) ok = gl_soft_blit((GLuint)cur, pw, ph, GL.dfmt);
    else if (GL.soft_win > 0) ok = gl_soft_blit(0, pw, ph, FMTS[GL.soft_win - 1]);
    else if (GL.soft_win == 0) {
        for (int i = 0; i < 5 && !ok; i++) if ((ok = gl_soft_blit(0, pw, ph, FMTS[i]))) GL.soft_win = i + 1;
        if (!ok) { GL.soft_win = -1; fprintf(stderr, "ARC log soft particles: the window's depth can't be copied here: drawn hard (post-processing on makes them work)\n"); }
    }
    while (glGetError() != GL_NO_ERROR) {}
    if (!ok) return false;
    float sw[4] = { (float)pw, (float)ph, 1, 0 };
    glUniform4fv(GL.u_soft, 1, sw); glUniformMatrix4fv(GL.u_ivp, 1, GL_FALSE, P->ivp);
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, GL.soft_tex); glActiveTexture(GL_TEXTURE0);
    return true;
}
static void gl_draw_2d(const arc__frame_data *f, const arc__pass *P, int pw, int ph, float flip);
/* 0.8.2: after a target's last pass of the frame with its own post: post from its scene buffer into the target (ssao
   with the last of its passes that drew 3D), then the 2D of all its passes on top (as soft_target_post) */
static void gl_target_post(const arc__frame_data *f, int pi) {
    const arc__pass *P = &f->passes[pi]; uint32_t id = P->target;
    for (int k = pi + 1; k < f->pass_count; k++) if (f->passes[k].target == id && f->passes[k].post[ARC__POST_ON] > 0) return;
    const arc__pass *src = P;
    for (int k = pi; k >= 0; k--) { const arc__pass *q = &f->passes[k]; if (q->target == id && q->post[ARC__POST_ON] > 0 && (q->mb_count || q->imm_count)) { src = q; break; } }
    gl_pset *ps = gl_pset_for(id, false); if (!ps) return;
    PS = ps; gl_post(P->post, src, GL.fbo[id], P->width, P->height); PS = &GL.ps[0];
    for (int k = 0; k <= pi; k++) {
        const arc__pass *q = &f->passes[k];
        if (q->target != id || q->post[ARC__POST_ON] <= 0 || !q->b2_count) continue;
        glBindFramebuffer(GL_FRAMEBUFFER, GL.fbo[id]); glViewport(0, 0, q->width, q->height);
        gl_draw_2d(f, q, q->width, q->height, 1.0f);
    }
}
static void gl_render(const arc__frame_data *f) {
    GL.w = f->width; GL.h = f->height; GL.cur_frame = f;
    if (f->light_count && f->lights) {   /* 0.9.5 the frame's lights: 4 RGBA32F texels each */
        glBindTexture(GL_TEXTURE_2D, GL.lt_tex); glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, f->light_count, GL_RGBA, GL_FLOAT, f->lights);
    }
    /* upload every stream once; passes draw ranges of them */
    if (f->instance_count) gl_upload(GL.inst_vbo, f->instances, sizeof(arc__instance) * (size_t)f->instance_count);
    if (f->imm_count) gl_upload(GL.imm_vbo, f->imm, sizeof(arc_vertex) * (size_t)f->imm_count);
    if (f->line_vertex_count) gl_upload(GL.line_vbo, f->lines, sizeof(arc__line_vertex) * (size_t)f->line_vertex_count);
    if (f->ltri_vertex_count) gl_upload(GL.ltri_vbo, f->ltris, sizeof(arc__line_vertex) * (size_t)f->ltri_vertex_count);
    if (f->vert2d_count) gl_upload(GL.quad_vbo, f->verts2d, sizeof(arc__vert2d) * (size_t)f->vert2d_count);
    if (f->bone_count) {
        int rows = (f->bone_count + ARC__BONES_PER_ROW - 1) / ARC__BONES_PER_ROW;
        glBindTexture(GL_TEXTURE_2D, GL.bone_tex); glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ARC__BONES_PER_ROW * 3, rows, GL_RGBA, GL_FLOAT, f->bones);
    }
    GLuint screen = gl_screen_fbo(f->width, f->height); bool screen_drawn = false;
    const float *post = NULL; const arc__pass *post_pass = NULL; GLuint scene = 0;
    for (int pi = 0; pi < f->pass_count; pi++) {
        const arc__pass *P = &f->passes[pi];
        bool tgt = P->target != 0 && GL.fbo[P->target];
        if ((P->target && !tgt) || (f->targets_only && !tgt) || (f->screen_only && P->target)) continue;
        int pw = tgt ? P->width : f->width, ph = tgt ? P->height : f->height;
        float flip = 1.0f;
        GLenum front = P->mirror ? GL_CW : GL_CCW, back_front = P->mirror ? GL_CCW : GL_CW;   /* (0.8.7 mirror passes wind the other way) */
        if (P->shadow[0] > 0 && P->shadow_res > 0 && P->mb_count) gl_shadow_pass(f, P);
        GL.lsh_ok = false; if (P->lsh_n > 0 && P->mb_count) gl_lshadow_pass(f, P);
        bool to_scene = !tgt && P->post[ARC__POST_ON] > 0 && (scene || (scene = gl_scene_fbo(f->width, f->height)) != 0);
        if (to_scene) { post = P->post; if (!post_pass || P->mb_count || P->imm_count) post_pass = P; }
        GLuint tscene = 0;   /* 0.8.2: a target with its own post draws its 3D into its own scene buffer */
        if (tgt && P->post[ARC__POST_ON] > 0) {
            gl_pset *ps = gl_pset_for(P->target, true);
            if (ps) { PS = ps; tscene = gl_scene_fbo_s(pw, ph, 0); PS = &GL.ps[0]; }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, tscene ? tscene : tgt ? GL.fbo[P->target] : to_scene ? scene : screen);
        if (!tgt) screen_drawn = true;
        if (GL.zrev) { gl_clip01(true); gl_clear_depth(GL.dclear); }
        glViewport(0, 0, pw, ph);
        glDisable(GL_SCISSOR_TEST);
        if (P->clear) {
            glClearColor(P->clear_col[0], P->clear_col[1], P->clear_col[2], P->clear_col[3]);
            glDepthMask(GL_TRUE); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if ((!P->target || P->mirror || P->vport) && P->sky[3] > 0 && GL.sky_prog) {   /* (0.10.2 viewports too) */
                glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
                glUseProgram(GL.sky_prog); glUniformMatrix4fv(GL.uk_ivp, 1, GL_FALSE, P->sky_ivp);
                glUniform4fv(GL.uk_top, 1, P->sky); glUniform4fv(GL.uk_hor, 1, P->sky + 4); glUniform4fv(GL.uk_bot, 1, P->sky + 8);
                gl_fullscreen();
            }
        }
        glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glDepthFunc(GL.zless);
        glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(front);
        glDisable(GL_BLEND);
        if (P->mb_count) { gl_mesh_uniforms(P, flip, true); gl_draw_batches(f, P->mb_first, P->mb_count, front, back_front); }
        if (P->imm_count) {                     /* immediate triangles: identity instance, double-sided */
            gl_mesh_uniforms(P, flip, true);
            glDisable(GL_CULL_FACE);
            glBindTexture(GL_TEXTURE_2D, GL.tex[1]); glUniform1f(GL.u_mflip, 0);
            glBindVertexArray(GL.imm_vao);
            glVertexAttrib4f(4, 0, 0, 0, 0); glVertexAttrib4f(5, 1, 0, 0, 0);
            glDrawArraysInstanced(GL_TRIANGLES, P->imm_first, P->imm_count, 1);
            glEnable(GL_CULL_FACE);
        }
        if (P->line_count) {
            glUseProgram(GL.line_prog); glUniformMatrix4fv(GL.ul_vp, 1, GL_FALSE, P->view_proj.m); glUniform1f(GL.ul_flip, flip);
            glBindVertexArray(GL.line_vao);
            glDrawArrays(GL_LINES, P->line_first, P->line_count);
        }
        if (P->ltri_count) {                    /* 0.10.11 thick lines: blended, depth tested, no depth write, both faces */
            glUseProgram(GL.line_prog); glUniformMatrix4fv(GL.ul_vp, 1, GL_FALSE, P->view_proj.m); glUniform1f(GL.ul_flip, flip);
            glEnable(GL_BLEND); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE); glEnable(GL_DEPTH_TEST); glDepthFunc(GL.zless); glDisable(GL_CULL_FACE);
            glBindVertexArray(GL.ltri_vao);
            glDrawArrays(GL_TRIANGLES, P->ltri_first, P->ltri_count);
            glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_CULL_FACE);
        }
        if (P->tb_count) {                      /* see-through and additive meshes, already sorted back to front */
            gl_mesh_uniforms(P, flip, false);
            if (P->soft) gl_soft_depth(P, pw, ph);
            gl_draw_batches(f, P->mb_first + P->mb_count, P->tb_count, front, back_front);
        }
        if (tscene) { gl_target_post(f, pi); continue; }
        if (P->mirror && tgt) { glBindTexture(GL_TEXTURE_2D, GL.tex[P->target]); glGenerateMipmap(GL_TEXTURE_2D); continue; }   /* 0.8.7: mips to blur it by */
        if (P->b2_count && !to_scene) gl_draw_2d(f, P, pw, ph, flip);
    }
    if (post) {                                 /* post-process the screen's 3D, then draw the screen's 2D on top */
        gl_post(post, post_pass, screen, f->width, f->height);
        for (int pi = 0; pi < f->pass_count; pi++) {
            const arc__pass *P = &f->passes[pi];
            if (P->target || f->targets_only || !P->b2_count) continue;
            glBindFramebuffer(GL_FRAMEBUFFER, screen); glViewport(0, 0, f->width, f->height);
            gl_draw_2d(f, P, f->width, f->height, 1.0f);
        }
    }
    if (screen && screen_drawn) {               /* resolve the multisampled screen into the window */
#if defined(ARC_GLES)
        if (GL.msaa) {   /* WebGL2 resolves only into the same format: RGBA8 first, then into the canvas */
            if (!GL.rs_fbo || GL.rs_w != f->width || GL.rs_h != f->height) {
                if (GL.rs_fbo) { glDeleteFramebuffers(1, &GL.rs_fbo); glDeleteRenderbuffers(1, &GL.rs_color); }
                glGenFramebuffers(1, &GL.rs_fbo); glBindFramebuffer(GL_FRAMEBUFFER, GL.rs_fbo);
                glGenRenderbuffers(1, &GL.rs_color); glBindRenderbuffer(GL_RENDERBUFFER, GL.rs_color);
                glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, f->width, f->height);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, GL.rs_color);
                GL.rs_w = f->width; GL.rs_h = f->height;
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, screen); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GL.rs_fbo);
            glBlitFramebuffer(0, 0, f->width, f->height, 0, 0, f->width, f->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            screen = GL.rs_fbo;
        }
#endif
        glBindFramebuffer(GL_READ_FRAMEBUFFER, screen); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, f->width, f->height, 0, 0, f->width, f->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
}
static void gl_draw_2d(const arc__frame_data *f, const arc__pass *P, int pw, int ph, float flip) {
        {
            glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDepthMask(GL_FALSE);
            glUseProgram(GL.quad_prog); glUniform2f(GL.uq_screen, (float)pw, (float)ph); glUniform1i(GL.uq_tex, 0); glUniform1f(GL.uq_flip, flip);
            glActiveTexture(GL_TEXTURE0);
            glBindVertexArray(GL.quad_vao);
            for (int b = P->b2_first; b < P->b2_first + P->b2_count; b++) {
                const arc__batch2d *bt = &f->batches2d[b];
                if (bt->clip) {
                    int x0 = arc_clampi(bt->cx, 0, pw), y0 = arc_clampi(bt->cy, 0, ph);
                    int x1 = arc_clampi(bt->cx + bt->cw, 0, pw), y1 = arc_clampi(bt->cy + bt->ch, 0, ph);
                    if (x1 <= x0 || y1 <= y0) continue;
                    glEnable(GL_SCISSOR_TEST);
                    glScissor(x0, ph - y1, x1 - x0, y1 - y0);   /* GL scissor is bottom-up */
                } else glDisable(GL_SCISSOR_TEST);
                gl_blend(bt->blend);
                glBindTexture(GL_TEXTURE_2D, GL.tex[bt->texture]);
                glUniform1f(GL.uq_tflip, arc__textures[bt->texture].target ? 1.0f : 0.0f); glUniform1f(GL.uq_sdf, bt->sdf);
                glDrawArrays(GL_TRIANGLES, bt->first, bt->count);
            }
            glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDepthMask(GL_TRUE);
        }
}
static bool gl_read(uint8_t *rgba, int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glPixelStorei(GL_PACK_ALIGNMENT, 1);
    uint8_t *tmp = (uint8_t *)arc__xalloc((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    for (int y = 0; y < h; y++) memcpy(rgba + (size_t)y * w * 4, tmp + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
    free(tmp); return true;
}
const arc__backend arc__backend_gl = {
#if defined(ARC_GLES)
    "webgl2",
#else
    "gl33",
#endif
    gl_init, gl_shutdown, gl_mesh_create, gl_mesh_update, gl_mesh_destroy, gl_tex_create, gl_target_create, gl_tex_destroy, gl_tex_set_smooth,
    gl_tex_update, gl_render, gl_read, gl_compressed_formats, gl_tex_create_compressed,
#if defined(ARC_GLES)
    gl_tex_create_web,
#else
    NULL,
#endif
    gl_tex_create_array };
