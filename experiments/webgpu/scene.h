/* WebGPU spike: the test scene, shared by ref.c (drawn by ARC's software renderer, the reference) and spike.c (drawn
   by the WebGPU experiment). Instances are filled exactly as ARC's arc_draw_mesh_styled fills arc__instance (144 bytes,
   the GPU instance layout of arc_gl.c / arc_d3d11.c): model, tint (rgb, emissive), params (recolour, whiten,
   multiply, alpha), extra (bone, skinned, unlit, additive fog), fx[8] (0.6 effects, unused here). */
#ifndef SPIKE_SCENE_H
#define SPIKE_SCENE_H
#include "../../include/arc.h"

typedef struct { float model[16], tint[4], params[4], extra[4], fx[8]; } spike_instance;
_Static_assert(sizeof(spike_instance) == 144, "the GPU instance layout is 144 bytes");

enum { SPIKE_W = 320, SPIKE_H = 180, SPIKE_N = 25 };   /* 24 shapes on a floor */
static const arc_camera SPIKE_CAM = { { 0, 3.2f, 7.5f }, { 0, 0.2f, 0 }, { 0, 1, 0 }, 0.8f, 0, 0.1f, 100 };
#define SPIKE_LIGHT_DIR arc_v3_make(0.4f, 1.0f, 0.3f)
#define SPIKE_LIGHT 0xFFFFFFu
#define SPIKE_AMBIENT 0x404850u
#define SPIKE_RIM 0x203050u
#define SPIKE_RIM_POWER 3.0f
#define SPIKE_FOG 0x101820u
#define SPIKE_FOG_START 7.0f
#define SPIKE_FOG_END 16.0f
#define SPIKE_CLEAR 0x101820u

typedef struct { int mesh; arc_m4 model; uint32_t tint; float recolour, emissive, whiten; } spike_shape;   /* mesh: 0 cube, 1 sphere */
static inline int spike_shapes(spike_shape *s) {
    static const uint32_t pal[6] = { 0xE04040, 0x40C060, 0x4060E0, 0xE0C040, 0xC050C0, 0x40C0C0 };
    int n = 0;
    s[n].mesh = 0; s[n].model = arc_m4_trs_y(arc_v3_make(0, -0.55f, -1.5f), 0, arc_v3_make(9, 0.1f, 7)); s[n].tint = 0x909090;
    s[n].recolour = s[n].emissive = s[n].whiten = 0; n++;
    for (int z = 0; z < 4; z++) for (int x = 0; x < 6; x++) {
        int i = z * 6 + x; float sc = 0.7f;
        s[n].mesh = (x + z) & 1;
        s[n].model = arc_m4_trs_y(arc_v3_make(-3.0f + 1.2f * (float)x, 0, 0.8f - 1.6f * (float)z), 0.35f * (float)i, arc_v3_make(sc, sc, sc));
        s[n].tint = pal[i % 6];
        s[n].recolour = (i % 5 == 2) ? 0.6f : 0; s[n].emissive = (i % 7 == 3) ? 0.4f : 0; s[n].whiten = (i == 11) ? 0.3f : 0;
        n++;
    }
    return n;
}
/* as ARC's draw_mesh_full for a solid arc_draw_mesh_styled (multiply = recolour > 0 ? 0 : 1) */
static inline void spike_fill(const spike_shape *s, spike_instance *o) {
    memset(o, 0, sizeof *o);
    memcpy(o->model, s->model.m, sizeof o->model);
    o->tint[0] = (float)((s->tint >> 16) & 0xFF) / 255.0f; o->tint[1] = (float)((s->tint >> 8) & 0xFF) / 255.0f; o->tint[2] = (float)(s->tint & 0xFF) / 255.0f;
    o->tint[3] = s->emissive;
    o->params[0] = s->recolour; o->params[1] = s->whiten; o->params[2] = s->recolour > 0 ? 0.0f : 1.0f; o->params[3] = 1;
}
#endif
