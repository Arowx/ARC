# Request (test fixture): `--editor FILE` (0.10.8)

**Why.** Droids' request: the game starts straight in the editor on a level file, on any build (no dev keys).

**Ask.** Nothing; `tests/level_game.c` with `--editor ../tests/level_fixture.arclvl`.

## Acceptance

```arc-check
game tests/level_game.c
args --editor ../tests/level_fixture.arclvl
do step 2
expect editor.open == true
expect editor.file == "../tests/level_fixture.arclvl"
expect editor.layer_name == "Engine room"
expect count(editor.objects[*]) == 2
expect editor.objects[1].fields.number == "476"
do tap F2; step 2
expect editor.open == false
do tap F2; step 2
expect editor.open == true
```
Described in words: the file's level is the one edited, and F2 works without --dev.
