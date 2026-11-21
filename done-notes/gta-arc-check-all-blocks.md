# Request from Grand Token Acquisition: arc_check should run every block in a file

## What

`tools/arc_check.py tests/acceptance.md` runs only the first ```arc-check block in the file. It should run every
block in order, print a pass/fail line per block (with the `## heading` above it as the name), and exit non-zero if any
block fails. Nice to have:

- `--block N` or `--name TEXT` to run just one block;
- `--fail-fast` to stop at the first failure;
- one game process per block (a fresh start each time), unless a block says `continue` to carry on from the last one.

## Why

The game templates write `tests/acceptance.md` with one block per feature, and AGENTS.md says a feature is finished
when its block passes. GTA has 17 blocks; with today's behaviour only the first one is really checked. The game has a
workaround, `tests/run_checks.py`, which splits the file and runs `arc_check.py` once per block. Every game will
need the same thing.

## Check (should pass once done)

A file `tools/tests/two_blocks.md` with two blocks, the second of which fails on purpose:

```arc-check
game samples/hello/hello.c
do step 2
expect frame.texts[*].text contains "Hello"
```

Then `python tools/arc_check.py tools/tests/two_blocks.md` should print `1/2 blocks passed` and exit with code 1.
Today it prints a single pass and exits 0.

## Done in ARC 0.10.12

- **Every block runs**, in order, each in a fresh game process. Each is named by the `##` heading above it, with a
  `--- block N: name` header, its PASS/FAIL lines and a `block N ok|FAIL: a/b checks` line.
- The last line reads `arc_check: X/Y checks passed, B/C blocks passed`, and any failed block exits 1.
- A file with a single block prints exactly what it printed before.
- A block without its own `game` / `exe` / `args` lines uses the last ones given, so one `game .` at the top builds once
  and serves all 17 of your blocks.
- `continue` in a block carries on in the previous block's game, without restarting.
- New options: `--block N`, `--name TEXT` (part of the heading) and `--fail-fast`.
- ARC's copy of your check is `tools/tests/two_blocks.md`: three blocks, the second failing on purpose, the third with
  `continue`. Both build scripts check that it prints `2/3 blocks passed` and exits 1.
- `tests/run_checks.py` can go. `docs/REQUESTS.md` and AGENTS.md describe the new behaviour.
