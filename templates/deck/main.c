/* {title} - a deck crawler starter: a ship of indoor decks (ARC template "deck").
 *
 * You are the green droid. Three decks built from text maps: rooms, sliding doors, locked doors opened from a
 * terminal, lifts between decks, an energiser that repairs you, and security droids that patrol, see you (and hear
 * your shots), chase and fire back. Clear a deck and its lights go dim. The map fills in as you explore.
 * Keys: W/S move, A/D turn, SPACE fire, F use (a terminal next to you), UP / DOWN on a lift change deck,
 * V view (top-down, follow, first person: they blend), R restart. F2 (with --dev, or run with --editor) opens the deck
 * editor: 2D, V shows the deck in 3D as it is built, PAGE UP / DOWN change deck, the Objects tab places enemies with
 * their hit points and facing, the Layer tab names the deck and sets its light, T test-plays from the cell under the
 * pointer (F2 comes back), CTRL+S writes decks.arclvl.
 *
 * Map characters: # wall, . floor, D door, K locked door (opened by a terminal T on the same deck), L lift (lifts on
 * neighbouring decks join where they line up), E energiser, S start, e enemy. Objects (in the level file): enemy
 * (hp, facing).
 *
 * Where things are:
 *   MAPS / LV        the decks as text, made into a level (arc_level: a layer per deck, its settings and objects);
 *                    decks.arclvl (written by the editor) replaces them when it exists.
 *   build_deck()     everything made from a map: rooms, doors, the navmesh, cells and portals (rooms behind closed
 *                    doors are not drawn in first person). Rebuilt after loads and edits.
 *   Game G           all game state, pointer-free: save_state/load_state and the `save` / `load` commands copy it.
 *   tick()           player, doors, fog of war, enemies (arc_brain + arc_senses + nav agents), shots, deck cleared.
 *   dump()           deck, hp, position, nearest door, explored cells, enemies, cleared, light level.
 *   command()        deck N, tp X Y, open_doors, clear, use, lift up|down, fire, face DEG, view N, save, load, restart.
 * Grow it: more decks (NDECKS), enemy kinds, pick-ups, a boss, keycards (a K door per colour), sounds per door.
 */
#include "arc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#define NDECKS 3
#define MW 24
#define MH 11
#define MAX_DOORS 16
#define MAX_ROOMS 16
#define MAX_ENEMIES 16
#define WALL_H 1.4f
static const char *MAPS[NDECKS][MH] = {
  { "########################",
    "#S.....#.......#.......#",
    "#......D...e...D.......#",
    "#..E...#.......#...e...#",
    "#......#.......#.......#",
    "###########D#######D####",
    "#......#.......#.......#",
    "#..L...K...e...#.......#",
    "#......#.......D...T...#",
    "#......#.......#.......#",
    "########################" },
  { "########################",
    "#......#.......#.......#",
    "#..e...D.......D...e...#",
    "#......#...E...#.......#",
    "#......#.......#.......#",
    "###D###############D####",
    "#......#.......#.......#",
    "#..L...D...e...D...L...#",
    "#......#.......#.......#",
    "#......#.......#.......#",
    "########################" },
  { "########################",
    "#......#.......#.......#",
    "#..e...D...e...D...e...#",
    "#......#.......#.......#",
    "#......#...E...#.......#",
    "###########D#######D####",
    "#......#.......#.......#",
    "#..e...D.......K...L...#",
    "#...T..#.......#.......#",
    "#......#.......#.......#",
    "########################" } };
static arc_level *LV;   /* the decks being played (and edited): a layer per deck, enemy objects, each deck's light */
#define LEVEL_FILE "decks.arclvl"
#define MAP(d, z, x) (arc_level_cells(LV, d)[(z) * (MW + 1) + (x)])

/* ---- made from the maps (not saved: build_deck() remakes it) ---- */
typedef struct { int x, z, along_x, room_a, room_b, portal; bool lock; } DoorInfo;
typedef struct {
    int room[MH][MW];                    /* 0 = wall / door, else room 1.. */
    int nrooms; float room_x[MAX_ROOMS], room_z[MAX_ROOMS];
    DoorInfo door[MAX_DOORS]; int ndoors;
    arc_navmesh nav;
    bool dirty;
} DeckInfo;
static DeckInfo DI[NDECKS];

/* ---- game state ---- */
enum { PATROL, ATTACK, SEARCH };
enum { B_SEES, B_KNOWS };
typedef struct { float open; bool locked; } Door;
typedef struct {
    bool alive; int deck; arc_v3 pos, home; float yaw; int hp; float cool, flash;
    arc_nav_agent nav; arc_brain brain; int sense;
} Enemy;
enum { PHASE_PLAY, PHASE_OVER };
typedef struct {
    int phase, deck, hp, kills, view;
    arc_v3 pos; float yaw, cool, hurt;
    Door door[NDECKS][MAX_DOORS];
    bool unlocked[NDECKS], cleared[NDECKS];
    uint8_t seen[NDECKS][MH][MW];        /* 255 in sight, 90 remembered, 0 never seen */
    float light[NDECKS];                 /* room light level: 1, going to 0.25 once cleared */
    Enemy e[MAX_ENEMIES];
    arc_senses senses;                   /* the current deck's enemies */
    arc_cam_rig cam;
} Game;
static Game G;

static arc_brain_def DEF;
static arc_mesh m_wall, m_floor, m_door, m_lift, m_term, m_energy, m_body, m_enemy, m_marker;
static arc_texture fog_tex;
static arc_fx SPARKS = { .life = 0.35f, .speed = 4, .speed_var = 2, .spread = 1.2f, .gravity = { 0, -9.8f, 0 }, .size = 0.06f, .color = 0xFFD070, .color_end = 0xFF5010, .glow = 2, .additive = true, .stretch = 0.03f, .burst = 14 };
static int FX_SPARKS;
static const arc_fx FX_BLAST = { .life = 0.5f, .life_var = 0.15f, .speed = 3.5f, .speed_var = 1.5f, .spread = 3.14159f, .radius = 0.15f, .size = 0.6f,
                                 .size_end = 0.1f, .color = 0xFFE080, .color_end = 0xFF3010, .glow = 2.5f, .additive = true, .drag = 3, .burst = 22 };
static const arc_fx FX_SMOKE = { .life = 1.2f, .life_var = 0.3f, .speed = 1.2f, .spread = 3.14159f, .radius = 0.2f, .size = 0.4f, .size_end = 1.1f,
                                 .color = 0x303438, .alpha = 0.6f, .gravity = { 0, 1, 0 }, .drag = 1.5f, .soft = 0.3f, .burst = 10 };
static const arc_fx FX_PARTS = { .life = 1.2f, .life_var = 0.4f, .speed = 5, .speed_var = 2, .spread = 1.1f, .size = 0.1f, .color = 0x505860,
                                 .gravity = { 0, -16, 0 }, .bounce = 0.4f, .spin = 8, .spin_var = 6, .burst = 10 };
static const arc_fx FX_CHARGE = { .life = 0.8f, .speed = 1.5f, .spread = 3.14159f, .radius = 0.4f, .size = 0.1f, .size_end = 0.02f, .color = 0xFFF080,
                                  .glow = 2, .additive = true, .gravity = { 0, 3, 0 }, .burst = 30 };
static arc_sound s_door, s_shot, s_eshot, s_boom, s_lift, s_term, s_charge, s_alert;
static arc_music music;

static char at(int d, int x, int z) { return (x < 0 || z < 0 || x >= MW || z >= MH) ? '#' : MAP(d, z, x); }
static int door_index(int d, int x, int z) { for (int i = 0; i < DI[d].ndoors; i++) if (DI[d].door[i].x == x && DI[d].door[i].z == z) return i; return -1; }
static bool door_shut(int d, int x, int z) { int i = door_index(d, x, z); return i >= 0 && G.door[d][i].open < 0.9f; }
/* blocks movement, sight and shots on the current deck: walls, terminals, doors not yet open */
static bool blocks(int x, int z, void *u) { (void)u; char c = at(G.deck, x, z); return c == '#' || c == 'T' || ((c == 'D' || c == 'K') && door_shut(G.deck, x, z)); }
static arc_v3 cell_centre(int x, int z) { return arc_v3_make((float)x + 0.5f, 0, (float)z + 0.5f); }
static arc_v3 fwd(float yaw) { return arc_v3_make(arc_sinf(yaw), 0, arc_cosf(yaw)); }
static float yaw_to(arc_v3 a, arc_v3 b) { return arc_atan2f(b.x - a.x, b.z - a.z); }
static uint32_t mix(uint32_t a, uint32_t b, float t) {   /* colour a..b */
    uint32_t r = 0;
    for (int sh = 0; sh < 24; sh += 8) { float ca = (float)((a >> sh) & 255), cb = (float)((b >> sh) & 255); r |= (uint32_t)(ca + (cb - ca) * arc_clampf(t, 0, 1) + 0.5f) << sh; }
    return r;
}
static bool los(arc_v3 a, arc_v3 b, void *u) {   /* sight between two points: walls, terminals and shut doors block it */
    (void)u; float dx = b.x - a.x, dz = b.z - a.z; int steps = (int)(sqrtf(dx * dx + dz * dz) * 4.0f) + 1;
    int bx = (int)floorf(b.x), bz = (int)floorf(b.z);
    for (int k = 1; k < steps; k++) { float t = (float)k / (float)steps; int cx = (int)floorf(a.x + dx * t), cz = (int)floorf(a.z + dz * t); if ((cx != bx || cz != bz) && blocks(cx, cz, NULL)) return false; }
    return true;
}

/* ---- building a deck from its map ---- */
static bool walk_open(uint8_t v) { return v == 0; }
static void build_nav(int d) {
    if (DI[d].nav.id) arc_navmesh_destroy(DI[d].nav);
    arc_grid g; arc_grid_init(&g, NULL, MW, MH);
    for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) {
        char c = at(d, x, z);
        arc_grid_set(&g, x, z, (uint8_t)(c == '#' || c == 'T' || (c == 'K' && !G.unlocked[d])));   /* enemies use open and unlocked doors */
    }
    DI[d].nav = arc_navmesh_create(NULL); arc_navmesh_add_grid(DI[d].nav, &g, arc_v3_make(0, 0, 0), 1, walk_open); arc_navmesh_build(DI[d].nav);
    free(g.cells);
}
static void build_deck(int d) {
    DeckInfo *I = &DI[d]; memset(I->room, 0, sizeof I->room); I->nrooms = 0; I->ndoors = 0; I->dirty = false;
    static int stack[MW * MH];
    for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) {   /* rooms: 4-connected floor, walls and doors part them */
        char c = at(d, x, z); if (c == '#' || c == 'D' || c == 'K' || I->room[z][x] || I->nrooms >= MAX_ROOMS - 1) continue;
        int id = ++I->nrooms, sp = 0, n = 0; float sx = 0, sz = 0; I->room[z][x] = id; stack[sp++] = z * MW + x;
        while (sp) {
            int k = stack[--sp], cx = k % MW, cz = k / MW; sx += (float)cx; sz += (float)cz; n++;
            const int nx[4] = { cx - 1, cx + 1, cx, cx }, nz[4] = { cz, cz, cz - 1, cz + 1 };
            for (int q = 0; q < 4; q++) {
                char cc = at(d, nx[q], nz[q]);
                if (cc == '#' || cc == 'D' || cc == 'K' || I->room[nz[q]][nx[q]]) continue;
                I->room[nz[q]][nx[q]] = id; stack[sp++] = nz[q] * MW + nx[q];
            }
        }
        I->room_x[id] = sx / (float)n + 0.5f; I->room_z[id] = sz / (float)n + 0.5f;
    }
    for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) {   /* doors: which way they run, the rooms either side */
        char c = at(d, x, z); if ((c != 'D' && c != 'K') || I->ndoors >= MAX_DOORS) continue;
        DoorInfo *o = &I->door[I->ndoors++]; memset(o, 0, sizeof *o);
        o->x = x; o->z = z; o->lock = c == 'K'; o->along_x = at(d, x - 1, z) == '#' || at(d, x + 1, z) == '#';
        if (o->along_x) { o->room_a = z > 0 ? I->room[z - 1][x] : 0; o->room_b = z + 1 < MH ? I->room[z + 1][x] : 0; }
        else { o->room_a = x > 0 ? I->room[z][x - 1] : 0; o->room_b = x + 1 < MW ? I->room[z][x + 1] : 0; }
    }
    build_nav(d);
}
static void build_cells(void) {   /* the current deck's rooms as cells, its doors as portals */
    DeckInfo *I = &DI[G.deck]; arc_cells_clear();
    for (int z = 0; z < MH; z++) for (int x = 0; x < MW; ) {   /* one box per run of a room's cells along a row */
        int id = I->room[z][x]; if (!id) { x++; continue; }
        int x1 = x; while (x1 + 1 < MW && I->room[z][x1 + 1] == id) x1++;
        arc_cell_add(id, arc_v3_make((float)x, -0.1f, (float)z), arc_v3_make((float)x1 + 1, WALL_H + 0.5f, (float)z + 1));
        x = x1 + 1;
    }
    for (int i = 0; i < I->ndoors; i++) {
        DoorInfo *o = &I->door[i]; float x = (float)o->x, z = (float)o->z;
        if (o->room_a) arc_cell_add(o->room_a, arc_v3_make(x, -0.1f, z), arc_v3_make(x + 1, WALL_H + 0.5f, z + 1));   /* standing in a doorway */
        o->portal = o->room_a && o->room_b ? arc_portal_add(o->room_a, o->room_b, arc_v3_make(x, 0, z), arc_v3_make(x + 1, WALL_H, z + 1)) : 0;
    }
}

/* ---- enemies ---- */
static Enemy *enemy_of(arc_brain *b) { return (Enemy *)((char *)b - offsetof(Enemy, brain)); }
static const arc_senses_memory *knows(const Enemy *e) { return e->sense >= 0 ? arc_senses_knows(&G.senses, e->sense, 1) : NULL; }
static void patrol_tick(arc_brain *b, void *u, float dt) {
    (void)u; (void)dt; Enemy *e = enemy_of(b);
    if (e->nav.moving && !e->nav.arrived) return;
    arc_v3 pts[6]; int n = arc_senses_search_points(DI[e->deck].nav, e->home, 3.0f, 6, pts);
    if (n > 0) arc_nav_agent_goto(&e->nav, DI[e->deck].nav, pts[arc_rand_range(0, n - 1)]);
}
static void attack_enter(arc_brain *b, void *u, float dt) {
    (void)u; (void)dt; Enemy *e = enemy_of(b); arc_nav_agent_stop(&e->nav); e->cool = 0.5f; arc_senses_alert(&G.senses, e->sense, 6.0f);
    arc_play_var(s_alert, 0.5f, 0, 0.08f); arc_popup_text(arc_v3_add(e->pos, arc_v3_make(0, 1.3f, 0)), 0xFF4040, 4, "!");   /* spotted */
}
static void attack_tick(arc_brain *b, void *u, float dt) {
    (void)u; Enemy *e = enemy_of(b); e->yaw = yaw_to(e->pos, G.pos);
    if ((e->cool -= dt) > 0) return;
    e->cool = 1.0f;
    arc_shot_desc s; memset(&s, 0, sizeof s);
    s.from = arc_v3_add(e->pos, arc_v3_make(fwd(e->yaw).x * 0.45f, 0.6f, fwd(e->yaw).z * 0.45f));
    s.dir = arc_v3_sub(arc_v3_add(G.pos, arc_v3_make(0, 0.5f, 0)), s.from);
    s.speed = 8; s.radius = 0.08f; s.range = 20; s.owner = 10 + (int)(e - G.e); s.mask = 1;
    s.tracer_rgb = 0xFF4060; s.tracer_width = 0.12f; s.projectile_size = 0.5f; s.flash_light = 2; s.flash_rgb = 0xFF3050; s.impact_fx = FX_SPARKS;
    arc_shot_fire(&s); arc_play_var(s_eshot, 0.35f, 0, 0.1f);
}
static void search_enter(arc_brain *b, void *u, float dt) {
    (void)u; (void)dt; Enemy *e = enemy_of(b); const arc_senses_memory *m = knows(e);
    if (m) arc_nav_agent_goto(&e->nav, DI[e->deck].nav, arc_v3_make(m->pos.x, 0, m->pos.z));
}
static void make_brain(void) {
    arc_brain_def *D = &DEF; D->name = "enemy";
    arc_brain_def_state(D, "PATROL", NULL, patrol_tick, NULL);
    arc_brain_def_state(D, "ATTACK", attack_enter, attack_tick, NULL);
    arc_brain_def_state(D, "SEARCH", search_enter, NULL, NULL);
    arc_brain_def_transition(D, PATROL, ATTACK, ARC_BRAIN_IF(ARC_BRAIN_F_GE, B_SEES, 1), ARC_BRAIN_NONE);
    arc_brain_def_transition(D, PATROL, SEARCH, ARC_BRAIN_IF(ARC_BRAIN_F_GE, B_KNOWS, 1), ARC_BRAIN_NONE);
    arc_brain_def_transition(D, ATTACK, SEARCH, ARC_BRAIN_IF(ARC_BRAIN_F_LE, B_SEES, 0), ARC_BRAIN_IF(ARC_BRAIN_TIME_GE, 0, 0.3f));
    arc_brain_def_transition(D, SEARCH, ATTACK, ARC_BRAIN_IF(ARC_BRAIN_F_GE, B_SEES, 1), ARC_BRAIN_NONE);
    arc_brain_def_transition(D, SEARCH, PATROL, ARC_BRAIN_IF(ARC_BRAIN_F_LE, B_KNOWS, 0), ARC_BRAIN_NONE);
    arc_brain_def_transition(D, SEARCH, PATROL, ARC_BRAIN_IF(ARC_BRAIN_TIME_GE, 0, 8), ARC_BRAIN_NONE);
}
static int enemies_on(int d) { int n = 0; for (int i = 0; i < MAX_ENEMIES; i++) n += G.e[i].alive && G.e[i].deck == d; return n; }
static void senses_for_deck(void) {   /* the senses watch the current deck's enemies only */
    arc_senses_init(&G.senses);
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &G.e[i]; e->sense = -1;
        if (!e->alive || e->deck != G.deck) continue;
        e->sense = arc_senses_add(&G.senses, e->pos, fwd(e->yaw));
        arc_senses_agent *a = &G.senses.agent[e->sense]; a->eye_height = 0.7f; a->range = 9; a->build_up = 0.5f; a->forget = 6;
    }
}

/* ---- views ---- */
static arc_cam_rig view_rig(int v) {
    arc_cam_rig r; memset(&r, 0, sizeof r);
    r.target = arc_v3_add(G.pos, arc_v3_make(0, 0.5f, 0)); r.yaw = G.yaw; r.follow_lag = 0.12f;
    if (v == 0) { r.kind = ARC_RIG_TOPDOWN; r.ortho_half = 4.5f; r.has_bounds = true; r.bounds[2] = MW; r.bounds[3] = MH; r.yaw = 3.14159265f; }
    else if (v == 1) { r.kind = ARC_RIG_FOLLOW; r.distance = 3.2f; r.height = 3.0f; r.aim_height = 0.2f; r.look_ahead = 1.0f; r.collide_radius = 0.3f; }
    else { r.kind = ARC_RIG_FIRST; r.target = arc_v3_add(G.pos, arc_v3_make(0, 0.7f, 0)); r.follow_lag = 0.04f; }
    return r;
}
static void set_view(int v) { if (v < 0 || v > 2) return; arc_cam_rig to = view_rig(v); G.view = v; arc_cam_rig_blend(&G.cam, &to, 0.5f); }

/* ---- decks, lifts, saves ---- */
static bool find_cell(int d, char c, int *x, int *z) { for (int zz = 0; zz < MH; zz++) for (int xx = 0; xx < MW; xx++) if (MAP(d, zz, xx) == c) { *x = xx; *z = zz; return true; } return false; }
static void update_fog(void);
static void enter_deck(int d, int x, int z) {
    if (d < 0 || d >= NDECKS) return;
    if (DI[d].dirty) build_deck(d);
    G.deck = d; G.pos = cell_centre(x, z);
    build_cells(); senses_for_deck(); arc_shots_clear(); arc_cam_rig_snap(&G.cam); update_fog();
    arc_event("deck", "\"deck\":%d", d + 1);
}
static void go_deck(int d) {   /* by lift: the lift that lines up, else the deck's first lift (or its start) */
    if (d < 0 || d >= NDECKS) return;
    int px = (int)floorf(G.pos.x), pz = (int)floorf(G.pos.z), x = 1, z = 1;
    if (at(d, px, pz) == 'L') { x = px; z = pz; } else if (!find_cell(d, 'L', &x, &z)) find_cell(d, 'S', &x, &z);
    enter_deck(d, x, z);
}
static void after_load(void) {   /* the parts of the world that are made, not saved */
    for (int d = 0; d < NDECKS; d++) build_deck(d);
    build_cells(); arc_shots_clear();
}
static void reset(void) {
    memset(&G, 0, sizeof G); G.hp = 10;
    for (int d = 0; d < NDECKS; d++) { build_deck(d); G.light[d] = arc_clampf(arc_level_get_num(LV, d, "light", 1), 0.25f, 1); for (int i = 0; i < DI[d].ndoors; i++) G.door[d][i].locked = DI[d].door[i].lock; }
    int ne = 0;
    for (int d = 0; d < NDECKS; d++) for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) if (MAP(d, z, x) == 'e' && ne < MAX_ENEMIES) {
        Enemy *e = &G.e[ne++]; e->alive = true; e->deck = d; e->hp = 3; e->pos = e->home = cell_centre(x, z); e->yaw = (float)ne;
        arc_nav_agent_init(&e->nav, e->pos, 1.4f, 0.3f); arc_brain_init(&e->brain, PATROL);
    }
    for (int d = 0; d < NDECKS; d++) for (int i = 0; i < arc_level_object_count(LV, d) && ne < MAX_ENEMIES; i++) {   /* enemy objects */
        if (strcmp(arc_level_object_kind(LV, d, i), "enemy")) continue;
        Enemy *e = &G.e[ne++]; e->alive = true; e->deck = d; e->hp = (int)arc_level_object_get_num(LV, d, i, "hp", 3);
        e->pos = e->home = cell_centre(arc_level_object_x(LV, d, i), arc_level_object_y(LV, d, i));
        e->yaw = 3.14159265f - (float)arc_level_object_get_num(LV, d, i, "facing", 0) * 1.5707963f;   /* facing 0 = up the map (-z) */
        arc_nav_agent_init(&e->nav, e->pos, 1.4f, 0.3f); arc_brain_init(&e->brain, PATROL);
    }
    int sx = 1, sz = 1; find_cell(0, 'S', &sx, &sz);
    G.view = 0; G.cam = view_rig(0);
    enter_deck(0, sx, sz);
}
static void unlock(int d) {
    G.unlocked[d] = true;
    arc_play(s_term, 0.6f, 0); arc_popup_text(arc_v3_add(G.pos, arc_v3_make(0, 1.4f, 0)), 0x60FF90, 3, "DOORS UNLOCKED");
    for (int i = 0; i < DI[d].ndoors; i++) G.door[d][i].locked = false;
    build_nav(d); arc_event("unlocked", "\"deck\":%d", d + 1);
}
static void use(void) {   /* a terminal within reach unlocks the deck's locked doors */
    int px = (int)floorf(G.pos.x), pz = (int)floorf(G.pos.z);
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) if (at(G.deck, px + dx, pz + dz) == 'T' && !G.unlocked[G.deck]) { unlock(G.deck); return; }
}
static void fire(void) {
    if (G.cool > 0 || G.phase != PHASE_PLAY) return;
    G.cool = 0.25f;
    arc_shot_desc s; memset(&s, 0, sizeof s);
    s.from = arc_v3_add(G.pos, arc_v3_make(fwd(G.yaw).x * 0.45f, 0.55f, fwd(G.yaw).z * 0.45f)); s.dir = fwd(G.yaw); s.range = 25; s.owner = 1; s.mask = 1;
    s.tracer_rgb = 0x80FFB0; s.tracer_width = 0.04f; s.tracer_life = 0.1f; s.flash_light = 2; s.flash_rgb = 0x80FFB0; s.decal_size = 0.3f; s.impact_fx = FX_SPARKS;
    arc_shot_fire(&s); arc_play_var(s_shot, 0.3f, 0, 0.1f);
    arc_senses_noise(&G.senses, G.pos, 10, 1, 1, 1);
}

/* ---- the deck editor (F2 with --dev, `editor on`, or --editor [FILE]): each deck a layer of LV; saves decks.arclvl ---- */
static const arc_editor_tile PAL[] = {
    { '.', "Floor", 0x2A3038, ARC_KEY_0, { 0 }, 0, 0, 0, 0, 0 }, { '#', "Wall", 0x8A93A6, ARC_KEY_1, { 0 }, 0, 0, 0, 0, 0 },
    { 'D', "Door", 0xD0B040, ARC_KEY_2, { 0 }, 0, 0, 0, 0, 1 }, { 'K', "Locked door", 0xE04040, ARC_KEY_3, { 0 }, 0, 0, 0, 0, 1 },
    { 'T', "Terminal", 0x40D080, ARC_KEY_4, { 0 }, 0, 0, 0, 0, 1 }, { 'L', "Lift", 0x40B0E0, ARC_KEY_5, { 0 }, 0, 0, 0, 0, 1 },
    { 'E', "Energiser", 0xF0E060, ARC_KEY_6, { 0 }, 0, 0, 0, 0, 1 }, { 'S', "Start", 0x60FF60, ARC_KEY_7, { 0 }, 0, 0, 0, 0, 1 },
    { 'e', "Enemy", 0xFF6060, ARC_KEY_8, { 0 }, 0, 0, 0, 0, 1 } };
static const arc_editor_field ENEMY_FIELDS[] = { { "hp", ARC_FIELD_INT, 1, 9, 1, "3", NULL, false }, { "facing", ARC_FIELD_FACING, 0, 3, 1, "2", NULL, false } };
static const arc_editor_object OBJS[] = { { "enemy", "Enemy", 0xFF6060, ARC_KEY_E, { 0 }, 0, 0, 0, 0, ENEMY_FIELDS, 2 } };
static const arc_editor_field DECK_FIELDS[] = { { "light", ARC_FIELD_NUM, 0.25f, 1, 0.25f, "1", NULL, true } };
static void ed_changed(int x, int y, int o, int n, void *u) { (void)x; (void)y; (void)o; (void)n; (void)u; DI[arc_editor_layer()].dirty = true; }
static bool ed_validate(char *msg, size_t n, void *u) {   /* what the game needs of a deck */
    (void)u; int x, z;
    for (int d = 0; d < NDECKS; d++) {
        if (!find_cell(d, 'S', &x, &z) && !find_cell(d, 'L', &x, &z)) { snprintf(msg, n, "%s has no start or lift", arc_level_layer_name(LV, d)); return false; }
        for (int i = 0; i < arc_level_object_count(LV, d); i++)
            if (at(d, arc_level_object_x(LV, d, i), arc_level_object_y(LV, d, i)) == '#') { snprintf(msg, n, "%s: an enemy stands in a wall at %d,%d", arc_level_layer_name(LV, d), arc_level_object_x(LV, d, i), arc_level_object_y(LV, d, i)); return false; }
    }
    return true;
}
static void ed_playtest(int layer, int x, int y, void *u) {   /* start again on that deck, at that cell, with the edits */
    (void)u; reset();
    if (at(layer, x, y) == '#') find_cell(layer, 'S', &x, &y);
    enter_deck(layer, x, y);
}
static int ed_object_mesh(int layer, int i, arc_editor_part *out, int max, void *u) {
    (void)u; if (max < 1) return 0;
    float yaw = 3.14159265f - arc_level_object_get_num(LV, layer, i, "facing", 0) * 1.5707963f;
    out[0] = (arc_editor_part){ m_enemy, arc_m4_trs_y(arc_v3_make(0, 0, 0), yaw, arc_v3_make(1, 1, 1)), 0xFFFFFF };
    return 1;
}
static void make_level(void) {   /* the built-in decks, or decks.arclvl when it has three decks of the right size */
    arc_level *f = arc_file_exists(LEVEL_FILE) ? arc_level_load(LEVEL_FILE) : NULL;
    bool ok = f && arc_level_layer_count(f) == NDECKS;
    for (int d = 0; ok && d < NDECKS; d++) ok = arc_level_width(f, d) == MW && arc_level_height(f, d) == MH;
    if (f && !ok) arc_log("%s: %s", LEVEL_FILE, arc_level_error()[0] ? arc_level_error() : "needs 3 decks of 24 x 11: using the built-in decks");
    if (ok) { arc_level_free(LV); LV = f; return; }
    arc_level_free(f);
    if (!LV) LV = arc_level_new();
    while (arc_level_layer_count(LV)) arc_level_remove_layer(LV, 0);
    for (int d = 0; d < NDECKS; d++) {
        char name[16]; snprintf(name, sizeof name, "Deck %d", d + 1);
        arc_level_add_layer(LV, name, MW, MH, '.');
        for (int z = 0; z < MH; z++) memcpy(arc_level_cells(LV, d) + z * (MW + 1), MAPS[d][z], MW);
    }
}
static bool level_fits(void) {
    if (arc_level_layer_count(LV) != NDECKS) return false;
    for (int d = 0; d < NDECKS; d++) if (arc_level_width(LV, d) != MW || arc_level_height(LV, d) != MH) return false;
    return true;
}
static int ed_tile_mesh(int x, int y, int id, arc_editor_part *out, int max, void *u) {   /* the deck as the game builds it */
    (void)u; int n = 0; int d = arc_editor_layer();
    arc_m4 floor = arc_m4_trs_y(arc_v3_make(0, -0.05f, 0), 0, arc_v3_make(1, 0.1f, 1));
    if (id == '#') { out[n++] = (arc_editor_part){ m_wall, arc_m4_trs_y(arc_v3_make(0, WALL_H * 0.5f, 0), 0, arc_v3_make(1, WALL_H, 1)), 0xD0D4DC }; return n; }
    out[n++] = (arc_editor_part){ m_floor, floor, 0xB0B4BC };
    if (n >= max) return n;
    bool ax = at(d, x - 1, y) == '#' || at(d, x + 1, y) == '#';
    if (id == 'D' || id == 'K') out[n++] = (arc_editor_part){ m_door, arc_m4_trs_y(arc_v3_make(0, WALL_H * 0.5f, 0), ax ? 0 : 1.5707963f, arc_v3_make(1, WALL_H, 0.2f)), id == 'K' ? 0xD04040 : 0xC8A040 };
    else if (id == 'T') out[n++] = (arc_editor_part){ m_term, arc_m4_trs_y(arc_v3_make(0, 0.45f, 0), 0, arc_v3_make(0.7f, 0.9f, 0.5f)), 0x40C070 };
    else if (id == 'L') out[n++] = (arc_editor_part){ m_lift, arc_m4_trs_y(arc_v3_make(0, 0.05f, 0), 0, arc_v3_make(0.95f, 0.1f, 0.95f)), 0xFFFFFF };
    else if (id == 'E') out[n++] = (arc_editor_part){ m_energy, arc_m4_trs_y(arc_v3_make(0, 0.45f, 0), 0, arc_v3_make(0.5f, 0.9f, 0.5f)), 0xFFFFFF };
    else if (id == 'S') out[n++] = (arc_editor_part){ m_marker, arc_m4_trs_y(arc_v3_make(0, 0.05f, 0), 0, arc_v3_make(0.6f, 0.1f, 0.6f)), 0x60FF60 };
    else if (id == 'e') out[n++] = (arc_editor_part){ m_enemy, arc_m4_identity(), 0xFFFFFF };
    return n;
}
static void ed_camera(arc_camera *out, void *u) { (void)u; *out = arc_cam_rig_camera(&G.cam); }

/* ---- init / tick / draw ---- */
static void init(void) {
    /* models fitted to a 1 x 1 x 1 box round their centre (arc_import --fit 1 1 1 --origin centre), so they scale and
       sit like the cubes and cylinders they replace; walls and floors are cubes with Ludo textures */
    m_wall = arc_mesh_cube(0xFFFFFF); arc_mesh_set_name(m_wall, "wall");
    arc_mesh_set_texture(m_wall, arc_texture_load_ex("assets/textures/wall.jpg", ARC_TEX_MESH));
    m_floor = arc_mesh_cube(0xFFFFFF); arc_mesh_set_name(m_floor, "floor");
    arc_mesh_set_texture(m_floor, arc_texture_load_ex("assets/textures/floor.jpg", ARC_TEX_MESH));
    m_door = arc_mesh_load("assets/meshes/door.arcm"); arc_mesh_set_name(m_door, "door");
    m_lift = arc_mesh_load("assets/meshes/lift.arcm"); arc_mesh_set_name(m_lift, "lift");
    m_term = arc_mesh_load("assets/meshes/terminal.arcm"); arc_mesh_set_name(m_term, "terminal");
    m_energy = arc_mesh_load("assets/meshes/energiser.arcm"); arc_mesh_set_name(m_energy, "energiser");
    m_body = arc_mesh_load("assets/meshes/droid.arcm"); arc_mesh_set_name(m_body, "droid");             /* 0.8 tall, feet at 0 */
    m_enemy = arc_mesh_load("assets/meshes/security.arcm"); arc_mesh_set_name(m_enemy, "enemy");       /* 0.9 tall, feet at 0 */
    m_marker = arc_mesh_cylinder(0xFFFFFF, 16); arc_mesh_set_name(m_marker, "start");
    FX_SPARKS = arc_shots_fx(&SPARKS);
    s_door = arc_sound_load("assets/sfx/door.mp3"); s_shot = arc_sound_load("assets/sfx/shot.mp3");
    s_eshot = arc_sound_load("assets/sfx/eshot.mp3"); s_boom = arc_sound_load("assets/sfx/boom.mp3");
    s_lift = arc_sound_load("assets/sfx/lift.mp3"); s_term = arc_sound_load("assets/sfx/terminal.mp3");
    s_charge = arc_sound_load("assets/sfx/charge.mp3"); s_alert = arc_sound_load("assets/sfx/alert.mp3");
    music = arc_music_load("assets/music/music.mp3"); arc_music_set_volume(music, 0.35f); arc_music_play(music, true);
    arc_set_post(&(arc_post){ .bloom = 0.5f, .bloom_threshold = 1.2f, .tonemap = true, .vignette = 0.35f, .contrast = 0.08f, .fxaa = true });
    make_brain();
    make_level();
    uint8_t blank[MW * MH * 4]; memset(blank, 0, sizeof blank); fog_tex = arc_texture_create(MW, MH, blank, true);
    arc_shots_set_world((arc_cworld){ 0 }, (arc_physics){ 0 }, blocks, NULL);
    arc_cam_rig_set_world((arc_cworld){ 0 }, (arc_physics){ 0 }, 0, blocks, NULL, 1);
    arc_editor_desc ed = { .name = "Decks", .tiles = PAL, .tile_count = (int)(sizeof PAL / sizeof PAL[0]), .changed = ed_changed,
                           .validate = ed_validate, .world_cell = 1, .camera = ed_camera, .tile_mesh = ed_tile_mesh,
                           .level = LV, .level_path = LEVEL_FILE, .objects = OBJS, .object_count = 1, .layer_fields = DECK_FIELDS,
                           .layer_field_count = 1, .object_mesh = ed_object_mesh, .playtest = ed_playtest };
    arc_editor_begin(&ed);   /* (with --editor FILE this reads that file into LV) */
    if (!level_fits()) { arc_log("%s: needs 3 decks of 24 x 11: using the built-in decks", arc_editor_launch_file()); make_level(); ed.level = LV; arc_editor_begin(&ed); }
    reset();
    arc_brain_watch("enemy", &G.e[0].brain, MAX_ENEMIES, sizeof(Enemy), &DEF);
}
static void update_fog(void) {   /* cells in sight from the player now 255, seen before 90 */
    uint8_t (*s)[MW] = G.seen[G.deck];
    for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) if (s[z][x] == 255) s[z][x] = 90;
    float px = G.pos.x, pz = G.pos.z;
    for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) {
        float tx = (float)x + 0.5f, tz = (float)z + 0.5f, dx = tx - px, dz = tz - pz, L = sqrtf(dx * dx + dz * dz);
        if (L > 8.0f) continue;
        int steps = (int)(L * 4.0f) + 1; bool ok = true;
        for (int k = 1; k < steps && ok; k++) {   /* the cells on the way (the target itself may be a wall: it is seen) */
            float t = (float)k / (float)steps; int cx = (int)floorf(px + dx * t), cz = (int)floorf(pz + dz * t);
            if ((cx != x || cz != z) && blocks(cx, cz, NULL)) ok = false;
        }
        if (ok) s[z][x] = 255;
    }
}
static void tick(void) {
    float dt = arc_dt();
    if (arc_key_pressed(ARC_KEY_R)) reset();
    if (arc_level_reloaded(LV)) { if (level_fits()) { for (int d = 0; d < NDECKS; d++) DI[d].dirty = true; } else { make_level(); reset(); } }   /* saved elsewhere (--hot), or loaded in the editor */
    for (int d = 0; d < NDECKS; d++) if (DI[d].dirty) { build_deck(d); if (d == G.deck) build_cells(); }   /* edited */
    if (arc_key_pressed(ARC_KEY_V)) set_view((G.view + 1) % 3);
    if (G.phase == PHASE_PLAY) {
        if (arc_key_down(ARC_KEY_A)) G.yaw += 2.6f * dt;
        if (arc_key_down(ARC_KEY_D)) G.yaw -= 2.6f * dt;
        float mv = (arc_key_down(ARC_KEY_W) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_S) ? 1.0f : 0.0f);
        arc_v3 np = arc_v3_add(G.pos, arc_v3_scale(fwd(G.yaw), mv * 3.2f * dt));
        if (!blocks((int)floorf(np.x + (np.x > G.pos.x ? 0.3f : -0.3f)), (int)floorf(G.pos.z), NULL)) G.pos.x = np.x;
        if (!blocks((int)floorf(G.pos.x), (int)floorf(np.z + (np.z > G.pos.z ? 0.3f : -0.3f)), NULL)) G.pos.z = np.z;
        if (arc_key_down(ARC_KEY_SPACE)) fire();
        if (arc_key_pressed(ARC_KEY_F)) use();
        char under = at(G.deck, (int)floorf(G.pos.x), (int)floorf(G.pos.z));
        if (under == 'L' && arc_key_pressed(ARC_KEY_UP) && G.deck > 0) { go_deck(G.deck - 1); arc_play(s_lift, 0.5f, 0); }
        if (under == 'L' && arc_key_pressed(ARC_KEY_DOWN) && G.deck < NDECKS - 1) { go_deck(G.deck + 1); arc_play(s_lift, 0.5f, 0); }
        if (under == 'E' && G.hp < 10) {
            G.hp = 10; arc_play(s_charge, 0.6f, 0); arc_popup_text(arc_v3_add(G.pos, arc_v3_make(0, 1.4f, 0)), 0xFFE060, 3, "REPAIRED");
            arc_fx_burst(&FX_CHARGE, G.pos, arc_v3_make(0, 1, 0), 0);
        }
    }
    if (G.cool > 0) G.cool -= dt;
    if (G.hurt > 0) G.hurt -= dt;
    /* doors: open for anyone near (not when locked); close when no one is near or in the doorway */
    DeckInfo *I = &DI[G.deck];
    for (int i = 0; i < I->ndoors; i++) {
        Door *dr = &G.door[G.deck][i]; arc_v3 c = cell_centre(I->door[i].x, I->door[i].z);
        bool near = arc_v3_len(arc_v3_sub(c, G.pos)) < 1.7f;
        for (int k = 0; k < MAX_ENEMIES && !near; k++) if (G.e[k].alive && G.e[k].deck == G.deck && arc_v3_len(arc_v3_sub(c, G.e[k].pos)) < 1.3f) near = true;
        float want = near && !dr->locked ? 1.0f : 0.0f, was = dr->open;
        dr->open = want > dr->open ? arc_minf(1.0f, dr->open + 3.0f * dt) : arc_maxf(0.0f, dr->open - 2.0f * dt);
        if (was == 0 && dr->open > 0) arc_play(s_door, 0.3f, 0);
        if (I->door[i].portal) arc_portal_set_open(I->door[i].portal, dr->open > 0.02f);
    }
    update_fog();
    /* enemies: senses, brain, movement (the current deck's only: the others wait) */
    arc_shot_target tg[1 + MAX_ENEMIES]; int nt = 0;
    tg[nt++] = (arc_shot_target){ 1, 1, arc_v3_add(G.pos, arc_v3_make(0, 0.3f, 0)), arc_v3_add(G.pos, arc_v3_make(0, 0.8f, 0)), 0.3f };
    for (int i = 0; i < MAX_ENEMIES; i++) if (G.e[i].alive && G.e[i].deck == G.deck) tg[nt++] = (arc_shot_target){ 10 + i, 1, arc_v3_add(G.e[i].pos, arc_v3_make(0, 0.3f, 0)), arc_v3_add(G.e[i].pos, arc_v3_make(0, 0.8f, 0)), 0.3f };
    arc_shots_targets(tg, nt);
    arc_senses_target me; memset(&me, 0, sizeof me); me.id = 1; me.npoints = 3;
    me.points[0] = arc_v3_add(G.pos, arc_v3_make(0, 0.6f, 0)); me.points[1] = arc_v3_add(G.pos, arc_v3_make(0.25f, 0.4f, 0)); me.points[2] = arc_v3_add(G.pos, arc_v3_make(-0.25f, 0.4f, 0));
    for (int i = 0; i < MAX_ENEMIES; i++) if (G.e[i].alive && G.e[i].sense >= 0) arc_senses_set(&G.senses, G.e[i].sense, G.e[i].pos, fwd(G.e[i].yaw));
    arc_senses_update(&G.senses, dt, &me, G.phase == PHASE_PLAY ? 1 : 0, los, NULL);
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &G.e[i]; if (!e->alive || e->deck != G.deck || e->sense < 0) continue;
        const arc_senses_memory *m = knows(e);
        e->brain.f[B_SEES] = m && m->seen_now ? 1.0f : 0.0f; e->brain.f[B_KNOWS] = m ? 1.0f : 0.0f;
        arc_brain_update(&e->brain, &DEF, e, dt);
        arc_nav_agent_update(&e->nav, DI[G.deck].nav, dt);
        arc_v3 v = e->nav.vel; if (v.x * v.x + v.z * v.z > 0.04f && e->brain.state != ATTACK) e->yaw = arc_atan2f(v.x, v.z);
        e->pos = e->nav.pos;
        if (e->flash > 0) e->flash -= dt;
    }
    arc_shots_update(dt);
    arc_shot_hit hits[32]; int nh = arc_shots_hits(hits, 32);
    for (int k = 0; k < nh; k++) {
        if (hits[k].kind != ARC_HIT_TARGET) continue;
        if (hits[k].id == 1 && G.phase == PHASE_PLAY) {
            G.hp--; G.hurt = 0.3f; arc_cam_rig_shake(&G.cam, 0.4f); arc_play_var(s_boom, 0.3f, 0, 0.15f);
            if (G.hp <= 0) { G.phase = PHASE_OVER; arc_event("destroyed", "\"deck\":%d", G.deck + 1); }
        }
        int i = hits[k].id - 10;
        if (i >= 0 && i < MAX_ENEMIES && G.e[i].alive) {
            Enemy *e = &G.e[i]; e->hp--; e->flash = 0.15f;
            if (e->hp <= 0) {
                e->alive = false; arc_senses_remove(&G.senses, e->sense); e->sense = -1; G.kills++;
                arc_v3 c = arc_v3_add(e->pos, arc_v3_make(0, 0.5f, 0));   /* a robot blown apart: fire, smoke, bouncing parts */
                arc_fx_burst(&FX_BLAST, c, arc_v3_make(0, 1, 0), 0); arc_fx_burst(&FX_SMOKE, c, arc_v3_make(0, 1, 0), 0);
                arc_fx_burst(&FX_PARTS, c, arc_v3_make(0, 1, 0), 0);
                arc_play_var(s_boom, 0.7f, 0, 0.1f); arc_cam_rig_shake(&G.cam, 0.35f);
                arc_popup_text(arc_v3_add(e->pos, arc_v3_make(0, 1.3f, 0)), 0xFFE060, 3, "DESTROYED");
            }
        }
    }
    /* a deck with no enemies left is cleared: its lights go dim */
    if (!G.cleared[G.deck] && enemies_on(G.deck) == 0) { G.cleared[G.deck] = true; arc_event("deck_cleared", "\"deck\":%d", G.deck + 1); }
    for (int d = 0; d < NDECKS; d++) if (G.cleared[d]) G.light[d] = arc_maxf(0.25f, G.light[d] - 0.75f * dt);
    arc_cam_rig want = view_rig(G.view);
    G.cam.target = want.target; G.cam.yaw = want.yaw; G.cam.pitch = want.pitch;
    arc_cam_rig_update(&G.cam, dt);
}
static void draw(float alpha) {
    (void)alpha;
    arc_camera cam = arc_cam_rig_camera(&G.cam); arc_set_camera(&cam);
    float L = G.light[G.deck];
    arc_set_light(arc_v3_make(0.3f, 1, 0.4f), mix(0x203040, 0x7A8496, L), mix(0x080C14, 0x202838, L));
    DeckInfo *I = &DI[G.deck];
    for (int r = 1; r <= I->nrooms; r++) arc_light_point(arc_v3_make(I->room_x[r], WALL_H, I->room_z[r]), mix(0x3050A0, 0xFFE0B0, L), 1.1f * L + 0.3f, 6.0f);
    /* fog of war: the world mask from what has been seen */
    static uint8_t px[MW * MH * 4];
    for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) { uint8_t *p = px + (z * MW + x) * 4; p[0] = p[1] = p[2] = G.seen[G.deck][z][x]; p[3] = 255; }
    arc_texture_update(fog_tex, px);
    arc_set_world_mask(fog_tex, arc_v3_make(0, 0, 0), 1, 0x000000, 0.5f);
    for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) {
        char c = at(G.deck, x, z); arc_v3 cc = cell_centre(x, z);
        if (c == '#') { arc_set_draw_cell(0); arc_draw_mesh(m_wall, arc_m4_trs_y(arc_v3_make(cc.x, WALL_H * 0.5f, cc.z), 0, arc_v3_make(1, WALL_H, 1)), 0xD0D4DC); continue; }
        arc_set_draw_cell(I->room[z][x]);
        arc_draw_mesh(m_floor, arc_m4_trs_y(arc_v3_make(cc.x, -0.05f, cc.z), 0, arc_v3_make(1, 0.1f, 1)), 0x7C8290);
        if (c == 'T') arc_draw_mesh_styled(m_term, arc_m4_trs_y(arc_v3_make(cc.x, 0.45f, cc.z), 0, arc_v3_make(0.7f, 0.9f, 0.5f)),
                                           &(arc_mesh_style){ .tint = G.unlocked[G.deck] ? 0x80FFA0 : 0xFFFFFF, .emissive = G.unlocked[G.deck] ? 0.25f : 0.1f });
        else if (c == 'L') arc_draw_mesh_ex(m_lift, arc_m4_trs_y(arc_v3_make(cc.x, 0.05f, cc.z), 0, arc_v3_make(0.95f, 0.1f, 0.95f)), 0xFFFFFF, 0.2f + 0.15f * arc_sinf((float)arc_tick_count() * 0.1f));
        else if (c == 'E') { arc_draw_mesh_ex(m_energy, arc_m4_trs_y(arc_v3_make(cc.x, 0.45f, cc.z), (float)arc_tick_count() * 0.02f, arc_v3_make(0.5f, 0.9f, 0.5f)), 0xFFFFFF, 0.2f);
                             arc_light_point(arc_v3_make(cc.x, 0.8f, cc.z), 0xFFE060, 1.2f + 0.3f * arc_sinf((float)arc_tick_count() * 0.08f), 2.5f); }
    }
    arc_set_draw_cell(0);
    for (int i = 0; i < I->ndoors; i++) {   /* two panels that slide apart into the wall */
        const DoorInfo *o = &I->door[i]; const Door *dr = &G.door[G.deck][i]; arc_v3 c = cell_centre(o->x, o->z);
        float s = 0.25f + 0.5f * dr->open, w = 0.5f * (1.0f - dr->open) + 0.02f; uint32_t col = dr->locked ? 0xD04040 : 0xC8A040;
        for (int k = -1; k <= 1; k += 2) {
            arc_v3 p = o->along_x ? arc_v3_make(c.x + (float)k * s, WALL_H * 0.5f, c.z) : arc_v3_make(c.x, WALL_H * 0.5f, c.z + (float)k * s);
            arc_draw_mesh_styled(m_door, arc_m4_trs_y(p, o->along_x ? 0 : 1.5707963f, arc_v3_make(w, WALL_H, 0.2f)),
                                 &(arc_mesh_style){ .tint = col, .recolour = dr->locked ? 0.6f : 0 });
        }
    }
    if (G.phase == PHASE_PLAY && G.view != 2) {   /* first person: the camera is inside the droid */
        arc_set_draw_cell(arc_cell_at(arc_v3_add(G.pos, arc_v3_make(0, 0.5f, 0))));
        float hover = 0.04f * arc_sinf((float)arc_tick_count() * 0.1f);   /* looks: droids hover */
        /* looks: a wall can pull the follow camera right up to the droid; fade it out then so it never fills the view */
        float near_eye = arc_v3_len(arc_v3_sub(G.cam.eye_now, arc_v3_add(G.pos, arc_v3_make(0, 0.5f, 0))));
        float fade = arc_clampf((1.6f - near_eye) / 0.8f, 0, 0.85f);
        arc_draw_mesh_styled(m_body, arc_m4_trs_y(arc_v3_add(G.pos, arc_v3_make(0, hover, 0)), G.yaw, arc_v3_make(1, 1, 1)), &(arc_mesh_style){ .whiten = G.hurt > 0 ? 0.7f : 0, .dissolve = fade });
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &G.e[i]; if (!e->alive || e->deck != G.deck) continue;
        arc_set_draw_cell(arc_cell_at(arc_v3_add(e->pos, arc_v3_make(0, 0.5f, 0))));
        float hover = 0.04f * arc_sinf((float)arc_tick_count() * 0.1f + (float)i);
        arc_draw_mesh_styled(m_enemy, arc_m4_trs_y(arc_v3_add(e->pos, arc_v3_make(0, hover, 0)), e->yaw, arc_v3_make(1, 1, 1)),
                             &(arc_mesh_style){ .emissive = e->brain.state == ATTACK ? 0.35f : 0, .whiten = e->flash > 0 ? 0.8f : 0 });
    }
    arc_set_draw_cell(0);
    arc_draw_decals(); arc_draw_particles(); arc_shots_draw();
    /* HUD and minimap (what has been seen) */
    arc_draw_textf(12, 12, 2, G.hp > 3 ? 0xFFFFFF : 0xFF6060, "DECK %d  HP %d  ENEMIES %d%s", G.deck + 1, G.hp, enemies_on(G.deck), G.cleared[G.deck] ? "  CLEARED" : "");
    float k = 6, mx = (float)arc_width() - k * MW - 12, my = 12;
    arc_draw_rect(mx - 2, my - 2, k * MW + 4, k * MH + 4, 0xA0000000u);
    for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) {
        uint8_t s = G.seen[G.deck][z][x]; if (!s) continue;
        char c = at(G.deck, x, z);
        uint32_t col = c == '#' ? 0x8A93A6 : (c == 'D' || c == 'K') ? 0xD0B040 : c == 'L' ? 0x40B0E0 : c == 'T' ? 0x40D080 : 0x303840;
        arc_draw_rect(mx + (float)x * k, my + (float)z * k, k, k, s == 255 ? col : mix(0x000000, col, 0.55f));
    }
    arc_draw_rect(mx + G.pos.x * k - 2, my + G.pos.z * k - 2, 4, 4, 0x60FF60);
    char under = at(G.deck, (int)floorf(G.pos.x), (int)floorf(G.pos.z));
    if (under == 'L') arc_draw_text(12, 40, 2, 0x40B0E0, "LIFT: UP / DOWN");
    arc_draw_text(12, (float)arc_height() - 24, 1.5f, 0x9AA4B8, "W/S move  A/D turn  SPACE fire  F use  V view  R restart");
    if (G.hurt > 0) arc_draw_rect(0, 0, (float)arc_width(), (float)arc_height(), arc_rgba(0xFF2020, G.hurt));
    if (G.phase == PHASE_OVER) arc_draw_text_ex((float)arc_width() * 0.5f, (float)arc_height() * 0.4f, 4, 0xFF4040, ARC_TEXT_CENTER | ARC_TEXT_OUTLINE, "DESTROYED - R to restart");
}
static int explored(int d) { int n = 0; for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) n += G.seen[d][z][x] > 0; return n; }
static void dump(arc_json *j) {
    arc_json_str_k(j, "phase", G.phase == PHASE_PLAY ? "play" : "over");
    arc_json_int_k(j, "deck", G.deck + 1); arc_json_int_k(j, "hp", G.hp); arc_json_int_k(j, "kills", G.kills);
    arc_json_num_k(j, "x", G.pos.x); arc_json_num_k(j, "z", G.pos.z); arc_json_int_k(j, "view", G.view);
    arc_json_obj_k(j, "player"); arc_json_int_k(j, "x", (int)floorf(G.pos.x)); arc_json_int_k(j, "y", (int)floorf(G.pos.z)); arc_json_end(j);   /* its cell */
    arc_json_int_k(j, "enemies", enemies_on(G.deck)); arc_json_bool_k(j, "cleared", G.cleared[G.deck]);
    arc_json_num_k(j, "light_level", G.light[G.deck]); arc_json_bool_k(j, "unlocked", G.unlocked[G.deck]);
    arc_json_int_k(j, "explored", explored(G.deck));
    int best = -1; float bd = 1e9f; DeckInfo *I = &DI[G.deck];
    for (int i = 0; i < I->ndoors; i++) { float d = arc_v3_len(arc_v3_sub(cell_centre(I->door[i].x, I->door[i].z), G.pos)); if (d < bd) { bd = d; best = i; } }
    if (best >= 0) { arc_json_num_k(j, "near_door_open", G.door[G.deck][best].open); arc_json_bool_k(j, "near_door_locked", G.door[G.deck][best].locked); arc_json_num_k(j, "near_door_dist", bd); }
    arc_json_arr_k(j, "doors");
    for (int i = 0; i < I->ndoors; i++) { arc_json_obj(j); arc_json_int_k(j, "x", I->door[i].x); arc_json_int_k(j, "z", I->door[i].z); arc_json_num_k(j, "open", G.door[G.deck][i].open); arc_json_bool_k(j, "locked", G.door[G.deck][i].locked); arc_json_end(j); }
    arc_json_end(j);
    arc_json_int_k(j, "rooms", I->nrooms);
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "deck") && argc >= 1) { go_deck(atoi(argv[0]) - 1); return true; }   /* decks count from 1 */
    if (!strcmp(name, "tp") && argc >= 2) { G.pos = cell_centre(atoi(argv[0]), atoi(argv[1])); arc_cam_rig_snap(&G.cam); return true; }
    if (!strcmp(name, "open_doors")) { unlock(G.deck); for (int i = 0; i < DI[G.deck].ndoors; i++) G.door[G.deck][i].open = 1; return true; }
    if (!strcmp(name, "clear")) { for (int i = 0; i < MAX_ENEMIES; i++) if (G.e[i].deck == G.deck) G.e[i].alive = false; senses_for_deck(); return true; }
    if (!strcmp(name, "use")) { use(); return true; }
    if (!strcmp(name, "lift") && argc >= 1) { if (at(G.deck, (int)floorf(G.pos.x), (int)floorf(G.pos.z)) == 'L') go_deck(G.deck + (!strcmp(argv[0], "up") ? -1 : 1)); return true; }
    if (!strcmp(name, "fire")) { G.cool = 0; fire(); return true; }
    if (!strcmp(name, "face") && argc >= 1) { G.yaw = (float)atof(argv[0]) * 3.14159265f / 180.0f; return true; }
    if (!strcmp(name, "view") && argc >= 1) { set_view(atoi(argv[0])); return true; }
    if (!strcmp(name, "save")) { return arc_save("deck_save", &G, sizeof G) || true; }
    if (!strcmp(name, "load")) {
        size_t n = 0; uint8_t *p = arc_load("deck_save", arc_frame(), &n);
        if (p && n == sizeof G) { memcpy(&G, p, sizeof G); after_load(); }
        return true;
    }
    if (!strcmp(name, "restart")) { reset(); return true; }
    return false;
}
static size_t save_state(void *buf, size_t cap) { if (cap >= sizeof G) memcpy(buf, &G, sizeof G); return sizeof G; }
static bool load_state(const void *buf, size_t n) { if (n != sizeof G) return false; memcpy(&G, buf, sizeof G); after_load(); return true; }

arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "{title}", .width = 1280, .height = 720, .clear_color = 0x05070A,
                      .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state };
}
