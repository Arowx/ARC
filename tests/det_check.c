/* ARC 0.9.7 determinism check: runs the scenarios of tests/det_scenarios.h and shows their hashes against the golden
   ones (made on Linux gcc), in a window or a web page; prints them too. Build it with any compiler ARC supports:
     build.bat tests\det_check.c        (MSVC)        build.sh linux tests/det_check.c   (gcc)
     build.bat web tests\det_check.c    (Emscripten)  then open build\web\det_check.html
   --trace FILE writes a hash every few steps of every scenario; tools/det_compare.py A B names the first step two
   machines part. Headless: --headless --cmd "step 1; quit" (lines "ARC ok/error determinism ..."). */
#include "../include/arc.h"
#include <stdio.h>
#include "det_scenarios.h"
#include "test_v097_golden.h"

static uint64_t got[DETS_N]; static int ok_count; static FILE *tf;
static void trace_line(const char *sc, int step, uint64_t h) {
    if (tf) fprintf(tf, "%s %d %016llx\n", sc, step, (unsigned long long)h);
    else printf("ARC trace %s %d %016llx\n", sc, step, (unsigned long long)h);
}
static void init(void) {
    const char *tp = arc_arg_value("trace");
    bool trace = tp != NULL;
    if (tp && strcmp(tp, "-")) tf = fopen(tp, "w");
    for (int i = 0; i < DETS_N; i++) {
        got[i] = DETS[i].run(trace ? trace_line : NULL);
        bool ok = got[i] == DETS_GOLDEN[i]; ok_count += ok;
        printf("ARC %s determinism %s: %016llx (golden %016llx)\n", ok ? "ok" : "error", DETS[i].name, (unsigned long long)got[i], (unsigned long long)DETS_GOLDEN[i]);
    }
    if (tf) { fclose(tf); tf = NULL; printf("ARC ok trace written to %s\n", tp); }
    printf("ARC %s determinism: %d of %d scenarios match the golden hashes\n", ok_count == DETS_N ? "ok" : "error", ok_count, DETS_N);
    fflush(stdout);
}
static void tick(void) {}
static void draw(float alpha) {
    (void)alpha;
    bool all = ok_count == DETS_N;
    arc_draw_text(20, 20, 3, all ? 0x60FF80 : 0xFF6060, all ? "DETERMINISM: SAME AS GOLDEN" : "DETERMINISM: DIFFERS");
    for (int i = 0; i < DETS_N; i++) {
        bool ok = got[i] == DETS_GOLDEN[i];
        arc_draw_textf(20, (float)(70 + i * 28), 2, ok ? 0xC0FFC0 : 0xFFB0B0, "%-11s %016llx  %s", DETS[i].name, (unsigned long long)got[i], ok ? "ok" : "DIFFERENT");
    }
    arc_draw_textf(20, 70 + DETS_N * 28 + 16, 2, 0xC0C8D8, "ARC %s on %s", ARC_VERSION, arc_stats().backend);
}
static void dump(arc_json *j) { arc_json_num_k(j, "matching", ok_count); arc_json_num_k(j, "scenarios", DETS_N); }
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC determinism check", .width = 900, .height = 340, .clear_color = 0x101418, .init = init, .tick = tick, .draw = draw, .dump = dump };
}
