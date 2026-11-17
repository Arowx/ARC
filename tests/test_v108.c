/* ARC 0.10.8 self-test: level files (arc_level): building, the text round trip byte for byte, saving and loading,
   malformed files refused with their line, objects and settings, reloading in place. Included by test_core.c;
   v108_init() runs in init. Names start with v108_. */
static bool v108_err_line(const char *text, int line) {   /* refused, and the message names that line */
    arc_level *l = arc_level_parse(text, strlen(text), "t.arclvl");
    char want[32]; snprintf(want, sizeof want, "t.arclvl:%d:", line);
    bool ok = !l && strstr(arc_level_error(), want) != NULL;
    if (!ok) printf("ARC note level error: wanted line %d, got \"%s\"\n", line, arc_level_error());
    arc_level_free(l);
    return ok;
}
static void v108_init(void) {
    arc_level *l = arc_level_new();
    CHECK(arc_level_add_layer(l, "Deck 1", 6, 3, '.') == 0 && arc_level_add_layer(l, "Bridge", 6, 3, '#') == 1 && arc_level_layer_count(l) == 2);
    char *c = arc_level_cells(l, 0); memcpy(c, "#....#", 6); c[2 * 7 + 3] = '|';   /* ('|' is fine inside a row) */
    arc_level_set_cell(l, 1, 2, 1, 'L');
    arc_level_set(l, -1, "title", "Droids \"Paradroid\" \\ test\nline two");
    arc_level_set_num(l, -1, "version", 3);
    arc_level_set(l, 0, "music", "deck1.ogg"); arc_level_set_num(l, 0, "light", 0.8f);
    int o0 = arc_level_object_add(l, 0, 1, 1, "droid"), o1 = arc_level_object_add(l, 0, 4, 2, "camera");
    arc_level_object_set_num(l, 0, o0, "class", 4); arc_level_object_set(l, 0, o0, "note", "the repair one");
    arc_level_object_set(l, 0, o1, "wall", "n");
    CHECK(o0 == 0 && o1 == 1 && arc_level_object_count(l, 0) == 2 && arc_level_object_at(l, 0, 4, 2, 0) == 1 && arc_level_object_at(l, 0, 3, 2, 0) == -1);
    size_t n1 = 0, n2 = 0; char *t1 = arc_level_text(l, &n1);
    arc_level *p = arc_level_parse(t1, n1, "mem");
    CHECK(p != NULL);
    char *t2 = p ? arc_level_text(p, &n2) : NULL;
    CHECK(t2 && n1 == n2 && !memcmp(t1, t2, n1));   /* the same text back, byte for byte */
    CHECK(p && !strcmp(arc_level_get(p, -1, "title", ""), "Droids \"Paradroid\" \\ test\nline two") && arc_level_get_num(p, -1, "version", 0) == 3.0f);
    CHECK(p && !strcmp(arc_level_layer_name(p, 1), "Bridge") && arc_level_cell(p, 1, 2, 1) == 'L' && arc_level_cell(p, 0, 3, 2) == '|' && arc_level_cell(p, 0, 9, 9) == 0);
    CHECK(p && arc_level_get_num(p, 0, "light", 0) > 0.79f && !strcmp(arc_level_object_kind(p, 0, 1), "camera") && arc_level_object_get_num(p, 0, 0, "class", 0) == 4.0f);
    CHECK(p && !strcmp(arc_level_object_get(p, 0, 0, "note", ""), "the repair one") && arc_level_object_x(p, 0, 1) == 4 && arc_level_object_y(p, 0, 1) == 2);
    CHECK(strstr(t1, "layer \"Deck 1\" 6 3\n") && strstr(t1, "music = deck1.ogg\n") && strstr(t1, "|#....#|\n") && strstr(t1, "object 1 1 droid class=4 note=\"the repair one\"\n"));
    /* saved and loaded: the file is that text */
    CHECK(arc_level_save(l, "arc_test_level.arclvl"));
    size_t fn = 0; uint8_t *fb = arc_file_read("arc_test_level.arclvl", arc_frame(), &fn);
    CHECK(fb && fn == n1 && !memcmp(fb, t1, n1));
    arc_level *q = arc_level_load("arc_test_level.arclvl"); size_t n3 = 0; char *t3 = q ? arc_level_text(q, &n3) : NULL;
    CHECK(q && n3 == n1 && !memcmp(t3, t1, n1) && !strcmp(arc_level_path(q), "arc_test_level.arclvl"));
    /* in place: the same sizes keep the cell memory */
    char *cells_before = q ? arc_level_cells(q, 0) : NULL;
    arc_level_set_cell(l, 0, 1, 1, 'E'); arc_level_object_remove(l, 0, 0); arc_level_save(l, NULL);
    CHECK(q && arc_level_reload(q, NULL) && arc_level_cells(q, 0) == cells_before && arc_level_cell(q, 0, 1, 1) == 'E' && arc_level_object_count(q, 0) == 1 && arc_level_reloaded(q) && !arc_level_reloaded(q));
    /* settings: removing one keeps the others' order; values that need quotes get them */
    arc_level_set(l, 0, "music", NULL); CHECK(arc_level_setting_count(l, 0) == 1 && !strcmp(arc_level_setting_key(l, 0, 0), "light"));
    arc_level_set(l, 0, "bad key", "x"); CHECK(arc_level_setting_count(l, 0) == 1);   /* (not a name: ignored) */
    arc_level_object_set(l, 0, 0, "wall", NULL); CHECK(arc_level_object_field_count(l, 0, 0) == 0);
    free(t1); free(t2); free(t3); arc_level_free(p); arc_level_free(q); arc_level_free(l); remove("arc_test_level.arclvl");
    /* malformed files: refused, with the line */
    CHECK(v108_err_line("level 1\n", 1));
    CHECK(v108_err_line("arclvl 2\n", 1));
    CHECK(v108_err_line("# a comment\n\narclvl 1\nlayer A 3 2\n|...|\n|..|\nend\n", 6));             /* a short row */
    CHECK(v108_err_line("arclvl 1\nlayer A 3 2\n|...|\nend\n", 4));                                   /* too few rows */
    CHECK(v108_err_line("arclvl 1\nlayer A 3 1\n|...|\nobject 3 0 droid\nend\n", 4));                  /* outside the layer */
    CHECK(v108_err_line("arclvl 1\ntitle = \"open\n", 2));                                             /* an unclosed quote */
    CHECK(v108_err_line("arclvl 1\nlayer A 3 1\n|...|\n", 4));                                         /* no end */
    CHECK(v108_err_line("arclvl 1\n|...|\n", 2));                                                       /* a row outside a layer */
    CHECK(v108_err_line("arclvl 1\nlayer A 2 1\n|..|\nobject 0 0 droid class\nend\n", 4));             /* a field without = */
    const char *crlf = "arclvl 1\r\n# CRLF and comments\r\nlayer \"\" 2 1\r\n|ab|\r\nend\r\n";
    arc_level *ok = arc_level_parse(crlf, strlen(crlf), "crlf");
    CHECK(ok && arc_level_cell(ok, 0, 1, 0) == 'b' && !strcmp(arc_level_layer_name(ok, 0), ""));
    arc_level_free(ok);
    CHECK(!arc_level_load("no_such_level.arclvl") && strstr(arc_level_error(), "no_such_level.arclvl"));
}
