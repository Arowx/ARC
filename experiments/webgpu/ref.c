/* WebGPU spike, the reference: ARC itself draws scene.h (software renderer, headless) and writes the meshes it made
   (vertices after arc_mesh_create worked out the normals, and indices) to meshes.bin for the spike to draw.
   cc -std=c11 -O2 -ffp-contract=off -D_GNU_SOURCE -DARC_HEADLESS_ONLY ref.c ../../src/arc.c -o build/ref -lm
   build/ref --headless --size 320x180 --cmd "step 2; shot build/ref.png; quit" */
#include "scene.h"
#include "../../src/arc_internal.h"   /* to read the meshes' vertex data back */
static arc_mesh meshes[2];
static void dump_meshes(void) {
    FILE *f = fopen("build/meshes.bin", "wb"); if (!f) { arc_log("cannot write build/meshes.bin"); return; }
    for (int m = 0; m < 2; m++) {
        const arc__mesh_rec *M = &arc__meshes[meshes[m].id];
        int32_t vc = M->vcount, ic = M->idx ? M->icount : M->vcount;
        fwrite(&vc, 4, 1, f); fwrite(&ic, 4, 1, f); fwrite(M->v, sizeof(arc_vertex), (size_t)vc, f);
        for (int i = 0; i < ic; i++) { uint32_t k = M->idx ? M->idx[i] : (uint32_t)i; fwrite(&k, 4, 1, f); }
    }
    fclose(f);
}
static void init(void) { meshes[0] = arc_mesh_cube(0xFFFFFF); meshes[1] = arc_mesh_sphere(0xFFFFFF, 16); dump_meshes(); }
static void draw(float alpha) {
    (void)alpha;
    arc_set_camera(&SPIKE_CAM); arc_set_light(SPIKE_LIGHT_DIR, SPIKE_LIGHT, SPIKE_AMBIENT);
    arc_set_rim(SPIKE_RIM, SPIKE_RIM_POWER); arc_set_fog(SPIKE_FOG, SPIKE_FOG_START, SPIKE_FOG_END);
    spike_shape s[SPIKE_N]; int n = spike_shapes(s);
    for (int i = 0; i < n; i++) {
        arc_mesh_style st; memset(&st, 0, sizeof st);
        st.tint = s[i].tint; st.recolour = s[i].recolour; st.emissive = s[i].emissive; st.whiten = s[i].whiten;
        arc_draw_mesh_styled(meshes[s[i].mesh], s[i].model, &st);
    }
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "webgpu-spike-ref", .width = SPIKE_W, .height = SPIKE_H, .init = init, .draw = draw, .clear_color = SPIKE_CLEAR };
}
