/* ARC 0.7.1 sample and benchmark: a Droids-like ship deck of 40 rooms (8 x 5), 2,560 floor tiles and ~1,500 wall
   blocks, and 200 droids with three levels of detail. Each speed-up can be switched on and off while it runs, so the
   same scene measures each one:
     deck.cells   1 = rooms are cells joined by door portals; tagged draws in hidden rooms are skipped
     deck.static  1 = tiles and walls merged into static batches (per room with cells); 0 = one draw per tile
     deck.lod     1 = droids use lower-detail meshes when small on screen
     deck.view    0 = first person in a room, looking down the deck; 1 = top-down over the player (Droids' view);
                  2 = high overview of the whole deck
     deck.doors   1 = doors open, 0 = every second door shut (fewer rooms seen in first person)
     deck.shadows 1 = directional shadows
     deck.chunk   > 0: the static batch used without cells is split on an x/z grid this size (set at start)
   Agent commands: `stats` (draw calls, culled, LOD counts, frame budget), `cells`, `lod`. Benchmark:
     build/deck_headless --headless --render-all --size 960x540 --frames 400 --set deck.static=0 --dump-at-exit d.json */
#include "../../include/arc.h"

enum { RX = 8, RZ = 5, RS = 8, ROOMS = RX * RZ, DROIDS = 200 };   /* rooms of RS x RS tiles, a wall line between */
#define PITCH ((float)(RS + 1))                                     /* room + one wall tile */
static float v_cells, v_static, v_lod, v_view, v_doors, v_shadows, v_chunk;
static arc_mesh floor_a, floor_b, wall, droid[3];
static arc_static batch_cells, batch_flat;   /* the same level built with and without cells */
static int door_portal[ROOMS * 2]; static int ndoor;
typedef struct { arc_v3 pos, prev; float dir, speed; int room; uint32_t col; } droid_t;
static droid_t D[DROIDS];
typedef struct { arc_mesh m; arc_m4 model; uint32_t tint; int cell; } item;
static item items[8192]; static int nitems;
static float cam_yaw;

static int room_id(int rx, int rz) { return 1 + rz * RX + rx; }
static void room_origin(int rx, int rz, float *x, float *z) { *x = (float)rx * PITCH; *z = (float)rz * PITCH; }
static void add_item(arc_mesh m, arc_m4 model, uint32_t tint, int cell) { if (nitems < 8192) items[nitems++] = (item){ m, model, tint, cell }; }
static arc_mesh droid_mesh(int segments) {   /* a droid: a domed body on a skirt (a lathe), smooth */
    arc_v2 prof[] = { { 0.05f, 0.0f }, { 0.42f, 0.02f }, { 0.45f, 0.12f }, { 0.36f, 0.2f }, { 0.36f, 0.75f }, { 0.4f, 0.8f },
                      { 0.38f, 0.92f }, { 0.3f, 1.02f }, { 0.18f, 1.1f }, { 0.05f, 1.13f } };
    arc_mesh_builder *b = arc_mb_begin(); arc_mb_color(b, 0xD8D8E0);
    arc_mb_lathe(b, prof, (int)(sizeof prof / sizeof prof[0]), segments, true);
    return arc_mb_finish(b, 60, 0);
}
static void build_level(void) {
    arc_cells_clear(); nitems = 0; ndoor = 0;
    arc_m4 flat = arc_m4_scale(arc_v3_make(1, 0.1f, 1));
    for (int rz = 0; rz < RZ; rz++) for (int rx = 0; rx < RX; rx++) {
        float ox, oz; room_origin(rx, rz, &ox, &oz); int id = room_id(rx, rz);
        arc_cell_add(id, arc_v3_make(ox - 1, -0.2f, oz - 1), arc_v3_make(ox + RS, 3.0f, oz + RS));
        for (int z = 0; z < RS; z++) for (int x = 0; x < RS; x++) {
            arc_m4 at = arc_m4_translate(arc_v3_make(ox + (float)x, -0.05f, oz + (float)z));
            bool b = (x + z) & 1;
            add_item(b ? floor_a : floor_b, b ? at : arc_m4_mul(at, flat), ((x * 7 + z * 3 + rx) % 5) ? 0xFFFFFF : 0xC8D0FF, id);
        }
        /* each room owns a half-thick wall all round (so a wall never vanishes with the room behind it), with a door in
           the middle of each side that leads to another room */
        for (int k = -1; k <= RS; k++) {
            bool mid = k == RS / 2 - 1 || k == RS / 2;
            bool n_door = mid && rz > 0, s_door = mid && rz < RZ - 1, w_door = mid && rx > 0, e_door = mid && rx < RX - 1;
            arc_v3 along_x = arc_v3_make(1, 2.5f, 0.5f), along_z = arc_v3_make(0.5f, 2.5f, 1);
            arc_v3 sill_x = arc_v3_make(1, 1, 0.5f), sill_z = arc_v3_make(0.5f, 1, 1);   /* half the floor of a doorway */
            if (!n_door) add_item(wall, arc_m4_trs_y(arc_v3_make(ox + (float)k, 1.25f, oz - 0.75f), 0, along_x), 0xA8B0C0, id);
            else add_item(floor_a, arc_m4_trs_y(arc_v3_make(ox + (float)k, -0.05f, oz - 0.75f), 0, sill_x), 0xFFFFFF, id);
            if (!s_door) add_item(wall, arc_m4_trs_y(arc_v3_make(ox + (float)k, 1.25f, oz + RS - 0.25f), 0, along_x), 0xA8B0C0, id);
            else add_item(floor_a, arc_m4_trs_y(arc_v3_make(ox + (float)k, -0.05f, oz + RS - 0.25f), 0, sill_x), 0xFFFFFF, id);
            if (k < 0 || k >= RS) continue;   /* corners belong to the north and south runs */
            if (!w_door) add_item(wall, arc_m4_trs_y(arc_v3_make(ox - 0.75f, 1.25f, oz + (float)k), 0, along_z), 0xB0B8C8, id);
            else add_item(floor_a, arc_m4_trs_y(arc_v3_make(ox - 0.75f, -0.05f, oz + (float)k), 0, sill_z), 0xFFFFFF, id);
            if (!e_door) add_item(wall, arc_m4_trs_y(arc_v3_make(ox + RS - 0.25f, 1.25f, oz + (float)k), 0, along_z), 0xB0B8C8, id);
            else add_item(floor_a, arc_m4_trs_y(arc_v3_make(ox + RS - 0.25f, -0.05f, oz + (float)k), 0, sill_z), 0xFFFFFF, id);
        }
        if (rx < RX - 1) door_portal[ndoor++] = arc_portal_add(id, room_id(rx + 1, rz), arc_v3_make(ox + RS - 0.5f, 0, oz + RS / 2 - 1.5f), arc_v3_make(ox + RS + 0.5f, 2.5f, oz + RS / 2 + 0.5f));
        if (rz < RZ - 1) door_portal[ndoor++] = arc_portal_add(id, room_id(rx, rz + 1), arc_v3_make(ox + RS / 2 - 1.5f, 0, oz + RS - 0.5f), arc_v3_make(ox + RS / 2 + 0.5f, 2.5f, oz + RS + 0.5f));
    }
    batch_cells = arc_static_create(); batch_flat = arc_static_create(); arc_static_set_chunk(batch_flat, v_chunk);
    for (int i = 0; i < nitems; i++) {
        arc_static_add_cell(batch_cells, items[i].m, items[i].model, items[i].tint, items[i].cell);
        arc_static_add(batch_flat, items[i].m, items[i].model, items[i].tint);
    }
    arc_static_build(batch_cells); arc_static_build(batch_flat);
    arc_log("deck: %d static items, %d merged meshes with cells, %d without", nitems, arc_static_mesh_count(batch_cells), arc_static_mesh_count(batch_flat));
}
static void init(void) {
    arc_var("deck.cells", &v_cells, 1); arc_var("deck.static", &v_static, 1); arc_var("deck.lod", &v_lod, 1);
    arc_var("deck.chunk", &v_chunk, 0);   /* > 0: the batch without cells is also split on a grid this size (read at start) */
    arc_var("deck.view", &v_view, 0); arc_var("deck.doors", &v_doors, 1); arc_var("deck.shadows", &v_shadows, 0);
    arc_mesh_builder *b = arc_mb_begin(); arc_mb_color(b, 0x707884); arc_mb_box(b, arc_v3_make(-0.5f, -0.05f, -0.5f), arc_v3_make(0.5f, 0.05f, 0.5f)); floor_a = arc_mb_finish(b, 0, 0);
    uint8_t px[8 * 8 * 4];
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { uint8_t *p = px + (y * 8 + x) * 4, e = (x == 0 || y == 0) ? 60 : 140; p[0] = e; p[1] = (uint8_t)(e + 10); p[2] = (uint8_t)(e + 30); p[3] = 255; }
    floor_b = arc_mesh_cube(0xFFFFFF);
    arc_mesh_set_texture(floor_b, arc_texture_create_ex(8, 8, px, ARC_TEX_MESH));
    b = arc_mb_begin(); arc_mb_color(b, 0xFFFFFF); arc_mb_box(b, arc_v3_make(-0.5f, -0.5f, -0.5f), arc_v3_make(0.5f, 0.5f, 0.5f)); wall = arc_mb_finish(b, 0, 0);   /* scaled per block */
    /* floor_b: a textured unit cube squashed into a tile by its matrix */
    droid[0] = droid_mesh(64); droid[1] = droid_mesh(16); droid[2] = droid_mesh(6);
    arc_mesh_set_lod(droid[0], 1, droid[1], 0.12f);
    arc_mesh_set_lod(droid[0], 2, droid[2], 0.04f);
    build_level();
    arc_rng r; arc_rng_seed(&r, 71, 1);
    for (int i = 0; i < DROIDS; i++) {
        int room = i % ROOMS; float ox, oz; room_origin(room % RX, room / RX, &ox, &oz);
        D[i].pos = D[i].prev = arc_v3_make(ox + arc_rng_rangef(&r, 1, RS - 2), 0, oz + arc_rng_rangef(&r, 1, RS - 2));
        D[i].dir = arc_rng_rangef(&r, 0, ARC_TAU); D[i].speed = arc_rng_rangef(&r, 0.6f, 1.6f); D[i].room = room_id(room % RX, room / RX);
        static const uint32_t cols[5] = { 0xFFFFFF, 0xFFD060, 0x80C0FF, 0xFF8080, 0xA0FFA0 }; D[i].col = cols[i % 5];
    }
}
static void tick(void) {
    float dt = arc_dt();
    for (int i = 0; i < DROIDS; i++) {
        droid_t *d = &D[i]; d->prev = d->pos;
        int room = d->room - 1; float ox, oz; room_origin(room % RX, room / RX, &ox, &oz);
        arc_v3 n = arc_v3_make(d->pos.x + cosf(d->dir) * d->speed * dt, 0, d->pos.z + sinf(d->dir) * d->speed * dt);
        if (n.x < ox + 0.5f || n.x > ox + RS - 1.5f || n.z < oz + 0.5f || n.z > oz + RS - 1.5f) d->dir += 2.0f; else d->pos = n;
    }
    cam_yaw += dt * 0.2f;
    for (int i = 0; i < ndoor; i++) arc_portal_set_open(door_portal[i], v_doors > 0.5f || (i & 1) == 0);
}
static void draw(float alpha) {
    arc_cells_set_culling(v_cells > 0.5f);
    arc_set_lod_force(v_lod > 0.5f ? -1 : 0);
    int view = (int)v_view;
    float cx = PITCH * 1 + 3, cz = PITCH * 2 + 3.5f;   /* the player: room (1, 2) */
    arc_camera cam;
    if (view == 0) cam = (arc_camera){ { cx - 2, 1.6f, cz }, { cx - 2 + cosf(0.15f * sinf(cam_yaw)) * 10, 1.4f, cz + sinf(0.15f * sinf(cam_yaw)) * 10 }, { 0, 1, 0 }, 1.1f, 0, 0.1f, 200 };
    else if (view == 1) cam = (arc_camera){ { cx, 13, cz + 7 }, { cx, 0, cz }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 200 };
    else cam = (arc_camera){ { PITCH * RX * 0.5f, 55, PITCH * RZ * 0.5f + 35 }, { PITCH * RX * 0.5f, 0, PITCH * RZ * 0.5f }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 300 };
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.4f, 1.0f, 0.25f), 0xE0E0E8, 0x404858);
    arc_set_shadows(cam.target, v_shadows > 0.5f ? 20.0f : 0.0f, 2048);
    bool cells = v_cells > 0.5f;
    if (v_static > 0.5f) arc_static_draw(cells ? batch_cells : batch_flat);
    else for (int i = 0; i < nitems; i++) {
        arc_set_draw_cell(cells ? items[i].cell : 0);
        arc_draw_mesh(items[i].m, items[i].model, items[i].tint);
    }
    for (int i = 0; i < DROIDS; i++) {
        const droid_t *d = &D[i];
        arc_set_draw_cell(cells ? d->room : 0);
        arc_v3 p = arc_v3_lerp(d->prev, d->pos, alpha);
        arc_draw_mesh(droid[0], arc_m4_trs_y(p, d->dir, arc_v3_make(1, 1, 1)), d->col);
    }
    arc_set_draw_cell(0);
    arc_gfx_stats st = arc_stats();
    arc_draw_textf(8, (float)arc_height() - 20, 1.5f, 0xFFFFFF, "cells %d static %d lod %d | draws %d tris %d culled %d lod %d/%d/%d",
                   (int)v_cells, (int)v_static, (int)v_lod, st.draw_calls, st.triangles, st.culled, st.lod_instances[0], st.lod_instances[1], st.lod_instances[2]);
}
static void dump(arc_json *j) {
    arc_json_int_k(j, "rooms", ROOMS); arc_json_int_k(j, "droids", DROIDS); arc_json_int_k(j, "static_items", nitems);
    arc_json_int_k(j, "merged_cells", arc_static_mesh_count(batch_cells)); arc_json_int_k(j, "merged_flat", arc_static_mesh_count(batch_flat));
    arc_json_v3_k(j, "droid0", D[0].pos);
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC deck", .width = 960, .height = 540, .init = init, .tick = tick, .draw = draw, .dump = dump };
}
