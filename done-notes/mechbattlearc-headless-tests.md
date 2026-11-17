# Request from MechBattleArc: lighter headless runs and a reset command for test batches

**Why.** MOLTEN BREACH has 20 smoke test files (about 400 checks) plus balance sweeps of 150-300 battles. Each check
starts a new headless process (`--cmd "...; dump; quit"`), and each process loads every mesh, texture and font even
though nothing is drawn - so the full suite takes over 10 minutes, and more than 4 test files at once run the 8 GB
cloud machine out of memory.

**Ask.**
1. `--headless` (or a new `--no-assets`): texture and font loads keep their sizes and metrics but skip the pixel data
   (or keep a tiny mip); meshes keep vertices, bounds and skeleton (picking and `frame` still work) but no GPU copies.
   Memory per headless process for a game like this under 150 MB.
2. A `reset` agent command: calls the game's `init` again on a fresh state (or loads the state saved at the first
   tick), clears vars back to defaults and `--set` values, so one `--stdin` process can run many scenarios in turn.
   `ARC ready` after it, as now.
3. `tools/arc_batch.py SCRIPT...`: runs scripts through N persistent `--stdin` processes (`-j`), separated by `reset`,
   and collects each script's dumps as JSON lines.

**Game side (ready / to do).** All game state is in `init` / `tick`; tests and `tools/*_sweep.py` would switch to
`arc_batch.py` with `reset` between cases.

## Acceptance

Described in words: a headless run of `samples/demo` with `--no-assets` uses under half the memory of today's
headless run and dumps the same `game.*` values; after `reset`, a dump equals the first dump of a fresh process
(same tick 0 state, same seed).

```arc-check
exe build/mechbattlearc_headless
args --no-session --stdin
do newbattle 5; endturn
reset
do newbattle 5
expect game.round == 0
expect game.spawned == 5
```

---

## Done in ARC 0.9.8 (15 Nov 2026)

All three parts are done.

1. **`--no-assets`.**
   - Loaded images keep their size but not their pixels: a 1 x 1 stand-in in the image's average colour.
     `arc_texture_size`, sprite UVs, `frame` and picking still see the real size.
   - Loaded sounds keep their length (`arc_sound_length`) but not their samples.
   - Meshes keep their vertices, bounds and skeleton. Headless already makes no GPU copies; textures embedded in meshes
     shrink too.
   - Measured: a test game with six 2048 x 2048 PNGs and a 30 s WAV went from 156 MB to 56 MB peak, with the same dump.
     (`samples/demo` makes everything in code and already uses 14 MB headless, so it can't show the difference.)
   - Pictures taken with `--no-assets` are flat colours: leave it off for screenshot checks.
2. **`reset`.**
   - It returns to the state saved right after init: tick 0, the same seed, vars at their defaults and `--set` values.
     Virtual input is released and the timeline history is cleared.
   - A game without save_state / load_state gets its init run again instead.
   - It prints `ARC ok reset tick=0 (...)`, then `ARC ready` as usual.
   - **If the game has its own `reset` command, the game's wins.** `arc_reset` always reaches ARC's. Your acceptance
     block uses `reset`: if MOLTEN BREACH has its own command of that name, write `arc_reset` there.
3. **`tools/arc_batch.py`.** It needs `--headless` and `--stdin` (it adds both itself).
   ```
   python3 <ARC>/tools/arc_batch.py --exe build/mechbattlearc_headless -j 4 --args "--no-assets --no-session" cases/*.txt
   python3 <ARC>/tools/arc_batch.py --exe build/mechbattlearc_headless --cases sweep.txt   # cases split by lines of ---
   ```
   - Each case is agent commands: one or more per line, `#` for comments; `quit` is skipped.
   - It keeps N processes running and sends `arc_reset` between cases.
   - It writes one JSON line per case, in order: `{"case", "ok", "dumps": [game objects], "errors", "hints",
     "seconds", "worker"}`. `--full` keeps whole dumps.
   - A case fails on an `ARC error` line, a timeout (`--timeout`, default 300 s) or a crash; the process is restarted
     for the next case. The exit code is 1 if any case failed.

Tests:
- `tests/headless_tools_test.py`, a new stage in `build test` called "headless tools", checks that:
  - `reset` gives the same dump as a fresh process, and a case run after it matches a fresh run;
  - `arc_batch` gives every case what a fresh process gives;
  - `--no-assets` keeps texture sizes and sound lengths.
- I couldn't run your acceptance block here, because it needs your headless build.

Also new in 0.9.8, and useful for screenshot checks:
- `--draw-last N` draws only the last N ticks before the end;
- `--shot-at 40,80,120` takes screenshots at those ticks, drawn only then.

See CHANGELOG 0.9.8 and AGENTS.md.
