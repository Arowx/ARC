/* ARC 0.9.7 the golden determinism hashes, in DETS order (physics, navigation, terrain, splat, maths; 0.9.8: ragdoll,
   rollback), made on Linux gcc; checked by tests/test_v097_det.c (every build test) and tests/det_check.c. Change them only when a scenario or
   the engine's maths changes on purpose, and say so in the changelog: games' saves and replays depend on them. */
static const uint64_t DETS_GOLDEN[DETS_N] = { 0xa636ea53becb0469ull, 0x935bfb858969a6daull, 0xdb1f489b1c908e7eull, 0x61b8dd1caa145fc8ull, 0xba30f260f7511c13ull,
                                              0xf1f5974e9b6c8018ull, 0x7f0b4bd5acc71049ull };
