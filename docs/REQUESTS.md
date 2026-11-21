# Asking for an ARC change

Games ask for engine changes with a markdown file in `D:\Projects\ARC\incoming\` (one request per file, named
`<game>-<topic>.md`). This template keeps requests quick to act on, and its acceptance block runs as a test with
`tools/arc_check.py`, so "done" means the same thing to the game and to ARC.

When a request is done, ARC adds a "Done in ARC x.y.z" section to the end of the file (what changed, how to use it,
which test covers it) and moves it to `D:\Projects\ARC\done-notes\`. So `incoming` holds only what is still to do,
and `done-notes` is the record of every request and its answer.

## Template

````markdown
# Request from <Game>: <one line: what ARC should do>

**Why.** What the game is trying to do, and what goes wrong today (a screenshot path or numbers help).

**Ask.** The API you'd like (names, fields, defaults), or the behaviour, as precisely as you can. Say what must not
change (old pictures, save files, performance).

**Game side (ready / to do).** What the game already has in place, and how it will use the change.

## Acceptance

```arc-check
game samples/demo/demo.c
do step 30; press D; step 20; release D
expect game.score >= 10
expect frame.meshes[name=player].on_screen == 1
expect frame.texts[*].text contains "SCORE"
shot accept.png
```
````

## The acceptance block

`tools/arc_check.py REQUEST.md` builds the game named on the `game` line (or runs `exe PATH`), plays the `do` lines
as agent commands, and tests each `expect` line against the game's output at that moment:

| Line | Meaning |
|---|---|
| `game path/to/game.c` | build it headless (`build.sh headless`; on Windows `build.bat`) |
| `exe build/x_headless` | run an already built game |
| `args --seed 3 --set speed=2` | extra command-line flags (0.10.12: relative paths given to `--save-dir`, `--record`, `--replay`, `--script`, `--editor`, `--dump-at-exit` are read from the check file's folder, not from `build/` where the game runs) |
| `continue` | (0.10.12) carry on in the previous block's game instead of a fresh one |
| `do step 60; press D; ...` | agent commands (see AGENTS.md) |
| `expect PATH OP VALUE` | a check (below) |
| any other line | passed to the game as an agent command (`shot`, `compare REF.png 0.5`, `ui_click Play`, ...) |

**Several blocks (0.10.12):** every ```arc-check block in a file runs, in order, each in a fresh game process, named by
the `##` heading above it. A block without `game` / `exe` / `args` lines uses the last ones given; `continue` keeps the
previous block's game running. The last line reads `arc_check: N/M checks passed, B/C blocks passed`; any failure
exits 1. `--block N` or `--name TEXT` runs one; `--fail-fast` stops at the first block that fails.

Paths read three outputs:
- **the dump** (`dump`): `game.score` is what the game's `dump()` wrote; `stats.draw_calls`, `tick` ... are ARC's;
- **the frame** (`frame`, 0.9.0): `frame.meshes` (name, `count`, `on_screen`, `items` with screen `box`, world `pos`,
  `rgb`), `frame.texts` (`text`, `box`, `rgb`), `frame.sprites`, `frame.rects`, `frame.lights`, `frame.camera`;
- **the UI** (`ui`): the widgets of the last tick (`label`, `kind`, `x y w h`, `value`, `text`).

`name[k=v]` keeps list items whose `k` is `v`; `name[*]` takes all; `name[2]` the third. When a path gives several
values a check passes if any of them does; `count(PATH)` counts them. Operators: `==`, `!=`, `<`, `<=`, `>`, `>=`,
`contains`, `exists`, `missing`. Values: numbers, `"strings"`, `true`, `false`, `null`.

Examples:

```
expect game.phase == "won"
expect count(frame.meshes[name=enemy].items) >= 3
expect frame.texts[*].text contains "GAME OVER"
expect frame.rects[rgb=#e04040] exists
expect ui[label=Play].kind == "button"
expect stats.dropped == 0
```

For engine features the game can't show yet, describe the test in words; ARC adds it to its own tests
(`tests/test_post.c`, `tests/test_core.c`), as with every request so far.
