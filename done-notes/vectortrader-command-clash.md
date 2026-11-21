# Request from Vector Trader: say when a game command is shadowed by an engine command

**Now.** Vector Trader added a debug command `set sfx 8` (game settings). ARC's own `set NAME V` (tunables) runs
first, so the game's command never arrived, and the only clue was `ARC error no var sfx` in the output of a long
acceptance run. It took a while to see why the setting "didn't work".

**Workaround in the game.** The command is now `opt`.

## Ask

When a game's `command()` would have handled a name the engine took first, say so once:

```
ARC hint command "set" is ARC's (tunables); the game's command of that name is never called - rename it
```

Cheapest version: when an engine command fails (like `set` with an unknown var), offer the game's `command()` the
same line before reporting the error, and if the game handles it, print the hint instead of the error.

## Acceptance

```arc-check
game samples/demo/demo.c
do set demo_setting 3
expect game.demo_setting == 3
```

(The demo's `command()` handles `set demo_setting N`; with the fallback it gets the line instead of an error.)

## Done in ARC 0.10.12

The cheapest version, as you suggested. When ARC's `set NAME V` finds no tunable called NAME, the game's `command()`
gets the same line before any error. If the game handles it, ARC answers `ARC ok set NAME (the game's command)` and
prints once:

    ARC hint command "set" is ARC's (tunables: arc_var); the game's `set NAME` only runs because no tunable has that name - rename the game's command so it always runs

If the game doesn't handle it, the old `ARC error no var NAME` stands. Your check passes on the demo (it handles
`set demo_setting N`); ARC's copy is `tests/command_clash_check.md`. `opt` is still the better name.
