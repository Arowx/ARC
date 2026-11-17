/* ARC 0.9.7 self-test: cross-machine determinism. The scenarios in det_scenarios.h must give these exact hashes on
   every compiler and platform (made on Linux gcc). A mismatch means this build computes something differently: run
   tests/det_check.c with --trace on both machines and compare the files (tools/det_compare.py) to find the first step. */
#include "det_scenarios.h"
#include "test_v097_golden.h"
static void v097d_init(void) {
    for (int i = 0; i < DETS_N; i++) {
        uint64_t a = DETS[i].run(NULL), b = DETS[i].run(NULL);
        printf("ARC note determinism %s: %016llx%s\n", DETS[i].name, (unsigned long long)a, a == DETS_GOLDEN[i] ? "" : " (golden differs)");
        CHECK(a == b);                 /* the same twice in one run */
        CHECK(a == DETS_GOLDEN[i]);    /* and the same as on every other machine */
    }
}
