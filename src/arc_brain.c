/* ARC 0.10.0 AI brains: state machines as data (arc_brain_def) driving plain-data agents (arc_brain). */

int arc_brain_def_state(arc_brain_def *d, const char *name, arc_brain_fn enter, arc_brain_fn tick, arc_brain_fn exit) {
    if (!d || d->state_count >= ARC_BRAIN_STATES) return -1;
    int k = d->state_count++;
    d->states[k].name = name ? name : "?"; d->states[k].enter = enter; d->states[k].tick = tick; d->states[k].exit = exit;
    return k;
}
int arc_brain_def_transition(arc_brain_def *d, int from, int to, arc_brain_cond c0, arc_brain_cond c1) {
    if (!d || d->transition_count >= ARC_BRAIN_TRANSITIONS || to < 0 || to >= ARC_BRAIN_STATES) return -1;
    int k = d->transition_count++;
    d->tr[k].from = from; d->tr[k].to = to; d->tr[k].c[0] = c0; d->tr[k].c[1] = c1; d->tr[k].func = NULL;
    return k;
}
int arc_brain_def_transition_fn(arc_brain_def *d, int from, int to, arc_brain_test func) {
    int k = arc_brain_def_transition(d, from, to, ARC_BRAIN_IF(ARC_BRAIN_FUNC, 0, 0), ARC_BRAIN_NONE);
    if (k >= 0) d->tr[k].func = func;
    return k;
}
int arc_brain_def_find(const arc_brain_def *d, const char *name) {
    if (!d || !name) return -1;
    for (int k = 0; k < d->state_count; k++) if (d->states[k].name && !strcmp(d->states[k].name, name)) return k;
    for (int k = 0; k < d->state_count; k++) {   /* any case */
        const char *a = d->states[k].name, *b = name; if (!a) continue;
        while (*a && *b && (*a | 32) == (*b | 32)) { a++; b++; }
        if (!*a && !*b) return k;
    }
    return -1;
}
void arc_brain_init(arc_brain *b, int state) {
    if (!b) return;
    memset(b, 0, sizeof *b); b->state = state; b->prev = -1;
}
void arc_brain_trigger(arc_brain *b, int t) { if (b && t >= 0 && t < 32) b->triggers |= 1u << t; }
void arc_brain_go(arc_brain *b, int state) { if (b && state >= 0) { b->pending = state + 1; b->forced = false; } }
void arc_brain_force(arc_brain *b, int state) { if (b && state >= 0) { b->pending = state + 1; b->forced = true; } }
const char *arc_brain_state_name(const arc_brain *b, const arc_brain_def *d) {
    return b && d && b->state >= 0 && b->state < d->state_count ? d->states[b->state].name : "?";
}
static bool brain_cond(const arc_brain *b, const arc_brain_def *d, int t, const arc_brain_cond *c, void *user, uint32_t *took) {
    switch (c->kind) {
    case ARC_BRAIN_COND_NONE: return true;
    case ARC_BRAIN_TRIGGER: if (c->slot >= 0 && c->slot < 32 && (b->triggers >> c->slot & 1u)) { *took |= 1u << c->slot; return true; } return false;
    case ARC_BRAIN_F_GE: return c->slot >= 0 && c->slot < ARC_BRAIN_F && b->f[c->slot] >= c->value;
    case ARC_BRAIN_F_LE: return c->slot >= 0 && c->slot < ARC_BRAIN_F && b->f[c->slot] <= c->value;
    case ARC_BRAIN_I_EQ: return c->slot >= 0 && c->slot < ARC_BRAIN_I && b->i[c->slot] == (int)c->value;
    case ARC_BRAIN_I_NE: return c->slot >= 0 && c->slot < ARC_BRAIN_I && b->i[c->slot] != (int)c->value;
    case ARC_BRAIN_I_GE: return c->slot >= 0 && c->slot < ARC_BRAIN_I && b->i[c->slot] >= (int)c->value;
    case ARC_BRAIN_I_LE: return c->slot >= 0 && c->slot < ARC_BRAIN_I && b->i[c->slot] <= (int)c->value;
    case ARC_BRAIN_TIME_GE: return b->time >= c->value;
    case ARC_BRAIN_FUNC: return d->tr[t].func && d->tr[t].func(b, user);
    default: return false;
    }
}
static void brain_switch(arc_brain *b, const arc_brain_def *d, int to, void *user) {
    if (to < 0 || to >= d->state_count) return;
    if (b->started && b->state >= 0 && b->state < d->state_count && d->states[b->state].exit) d->states[b->state].exit(b, user, 0);
    int from = b->started ? b->state : -1;
    b->prev = from; b->state = to; b->ticks = 0; b->time = 0; b->changes++;
    if (b->hist_n < 8) b->hist_n++; else memmove(b->hist[0], b->hist[1], sizeof b->hist[0] * 7);
    b->hist[b->hist_n - 1][0] = (uint8_t)(from < 0 ? 255 : from); b->hist[b->hist_n - 1][1] = (uint8_t)to;
    b->started = true;
    if (d->states[to].enter) d->states[to].enter(b, user, 0);
}
void arc_brain_update(arc_brain *b, const arc_brain_def *d, void *user, float dt) {
    if (!b || !d || d->state_count <= 0) return;
    if (!b->started) {   /* the first update enters the initial state */
        int s0 = b->state >= 0 && b->state < d->state_count ? b->state : 0;
        b->started = false; b->state = -1; brain_switch(b, d, s0, user); b->changes = 0; b->hist_n = 0;
    }
    if (b->pending) { int to = b->pending - 1; b->pending = 0; b->forced = false; brain_switch(b, d, to, user); }
    else {
        for (int t = 0; t < d->transition_count; t++) {
            if (d->tr[t].from != ARC_BRAIN_ANY && d->tr[t].from != b->state) continue;
            if (d->tr[t].from == ARC_BRAIN_ANY && d->tr[t].to == b->state) continue;
            uint32_t took = 0;
            if (!brain_cond(b, d, t, &d->tr[t].c[0], user, &took) || !brain_cond(b, d, t, &d->tr[t].c[1], user, &took)) continue;
            b->triggers &= ~took;
            brain_switch(b, d, d->tr[t].to, user);
            break;
        }
    }
    if (d->states[b->state].tick) d->states[b->state].tick(b, user, dt);
    b->ticks++; b->time += dt; b->triggers = 0;
    if (b->pending && !b->forced) { int to = b->pending - 1; b->pending = 0; brain_switch(b, d, to, user); }
}
/* ---- watched groups: dump and the `brain` command ---- */
static struct { char group[24]; arc_brain *first; int count; size_t stride; const arc_brain_def *d; } BRW[8];
void arc_brain_watch(const char *group, arc_brain *first, int count, size_t stride, const arc_brain_def *d) {
    if (!group) return;
    int slot = -1;
    for (int k = 0; k < 8 && slot < 0; k++) if (BRW[k].count > 0 && !strcmp(BRW[k].group, group)) slot = k;
    if (count <= 0 || !first) { if (slot >= 0) memset(&BRW[slot], 0, sizeof BRW[slot]); return; }
    for (int k = 0; k < 8 && slot < 0; k++) if (BRW[k].count <= 0) slot = k;
    if (slot < 0) return;
    snprintf(BRW[slot].group, sizeof BRW[slot].group, "%s", group);
    BRW[slot].first = first; BRW[slot].count = count; BRW[slot].stride = stride ? stride : sizeof(arc_brain); BRW[slot].d = d;
}
static arc_brain *brw_at(int k, int i) { return (arc_brain *)((char *)BRW[k].first + BRW[k].stride * (size_t)i); }
void arc__brain_dump_json(arc_json *j) {
    bool any = false; for (int k = 0; k < 8; k++) any |= BRW[k].count > 0;
    if (!any) return;
    arc_json_obj_k(j, "brains");
    for (int k = 0; k < 8; k++) {
        if (BRW[k].count <= 0) continue;
        const arc_brain_def *d = BRW[k].d;
        arc_json_arr_k(j, BRW[k].group);
        for (int i = 0; i < BRW[k].count; i++) {
            const arc_brain *b = brw_at(k, i);
            arc_json_obj(j);
            arc_json_str_k(j, "state", arc_brain_state_name(b, d));
            arc_json_num_k(j, "time", b->time);
            if (b->prev >= 0 && d && b->prev < d->state_count) arc_json_str_k(j, "prev", d->states[b->prev].name);
            arc_json_int_k(j, "changes", b->changes);
            if (d) {
                for (int s = 0; s < ARC_BRAIN_F; s++) if (d->f_name[s]) arc_json_num_k(j, d->f_name[s], b->f[s]);
                for (int s = 0; s < ARC_BRAIN_I; s++) if (d->i_name[s]) arc_json_int_k(j, d->i_name[s], b->i[s]);
                for (int s = 0; s < ARC_BRAIN_V; s++) if (d->v_name[s]) { arc_json_arr_k(j, d->v_name[s]); arc_json_num(j, b->v[s].x); arc_json_num(j, b->v[s].y); arc_json_num(j, b->v[s].z); arc_json_end(j); }
            }
            arc_json_end(j);
        }
        arc_json_end(j);
    }
    arc_json_end(j);
}
void arc__brain_command(int argc, const char **argv) {   /* brain | brain GROUP | brain GROUP N STATE */
    if (argc <= 2) {
        int shown = 0;
        for (int k = 0; k < 8; k++) {
            if (BRW[k].count <= 0 || (argc == 2 && strcmp(BRW[k].group, argv[1]))) continue;
            for (int i = 0; i < BRW[k].count; i++) { const arc_brain *b = brw_at(k, i); printf("ARC brain %s %d %s %.2f\n", BRW[k].group, i, arc_brain_state_name(b, BRW[k].d), b->time); shown++; }
        }
        if (shown) printf("ARC ok brain %d\n", shown); else printf("ARC error brain: no watched brains (arc_brain_watch)\n");
        fflush(stdout);
        return;
    }
    if (argc < 4) { printf("ARC error brain: use 'brain', 'brain GROUP' or 'brain GROUP N STATE'\n"); fflush(stdout); return; }
    for (int k = 0; k < 8; k++) {
        if (BRW[k].count <= 0 || strcmp(BRW[k].group, argv[1])) continue;
        int i = atoi(argv[2]);
        if (i < 0 || i >= BRW[k].count) { printf("ARC error brain %s has %d agents\n", argv[1], BRW[k].count); fflush(stdout); return; }
        int s = arc_brain_def_find(BRW[k].d, argv[3]);
        if (s < 0) { printf("ARC error brain no state %s\n", argv[3]); fflush(stdout); return; }
        arc_brain_force(brw_at(k, i), s);
        printf("ARC ok brain %s %d %s\n", argv[1], i, BRW[k].d->states[s].name); fflush(stdout); return;
    }
    printf("ARC error brain no group %s\n", argv[1]); fflush(stdout);
}
