# Request from Vector Trader: arc_check paths in `args` relative to the check file

**Now.** `arc_check.py` runs the game with its working directory set to the exe's folder (`build/`), so a path in the
`args` line is relative to `build/`, not to the check file or the game folder. Vector Trader's checks pass
`--save-dir build/test_saves`; the game then tried `build/build/test_saves`, saving failed quietly, and the save and
continue checks failed with no error. Running the same commands by hand from the game folder worked, which made it
confusing.

**Workaround in the game.** The check uses `--save-dir test_saves` (relative to `build/`), with a note in AGENTS.md.

## Ask

One of:

1. Resolve relative paths in `args` (values of `--save-dir`, `--replay`, `--record`, `--script`, `--editor`...)
   against the check file's folder (or the game folder) before running; or
2. Run the game from the game folder (the one with arc.txt) instead of `build/`, if assets allow; or
3. At least: document it in `docs/REQUESTS.md` and make `--save-dir` print `ARC error save dir ... not writable`
   when a save fails, so the failure is visible.

## Acceptance

```arc-check
game samples/demo/demo.c
args --save-dir check_saves
do set save_test 1; step 2
expect game.saved == true
```

(With the fix, `samples/demo/check_saves/` holds the save, whichever folder arc_check ran the game from.)

## Done in ARC 0.10.12

Option 1, plus option 3's error.
- **Paths in `args`:** relative paths given to `--save-dir`, `--record`, `--replay`, `--script`, `--editor` and
  `--dump-at-exit` (also as `--flag=value`) are now read from the check file's folder, wherever the game runs.
  `--save-dir build/test_saves` in a check at `tests/acceptance.md` means `tests/build/test_saves`; to keep saves in
  the game's `build/`, write `--save-dir ../build/test_saves`.
- **Failed saves are reported:** `ARC error save 'KEY' failed: cannot write PATH (is the save folder ... there and
  writable? --save-dir)`, once until a save works again. arc_check fails the run on it.
- **Your check:** ARC's copy is `tests/save_path_check.md`, using the demo's new `save_test`. The save lands in
  `tests/check_saves/`.
- **Your workaround** (`--save-dir test_saves`, relative to `build/`) now resolves next to your check file instead.
  Change it to the path you mean, relative to the check file.
