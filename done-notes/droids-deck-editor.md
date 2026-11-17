# Request from Droids: turn the tile editor into a full deck editor (level files, objects with settings, copy and paste, test-play)

**Why.** ARC 0.10.3's tile editor already paints a grid in 2D or 3D with layers, undo and the game's own save hook.
Droids still can't use it as its deck editor, because:
- **The decks are compiled in.** The seven decks (six ship decks plus Training, 32x16 each) are text in
  `tools/ship_decks.py`. That script checks them and writes `src/ship.h`, which is compiled into the game. Editing a
  deck means changing Python, re-running the script and rebuilding.
- **No way to load or save level files.** Without a level file format in ARC, every game writes its own parser and
  writer. Droids would need one just to load decks at run time and save them from the editor.
- **One character per cell isn't enough.** It's fine for floor, walls and kit, but some things need settings:
  - a droid spawn wants a class and optionally a fixed number (e.g. 476, the repair droid);
  - a facing;
  - a security camera wants the wall it hangs on;
  - a locked door wants the terminal that opens it;
  - each deck has a name and settings (music, lighting tint).
- **Missing editing tools:** no copy and paste or mirroring of rooms, no way to see the deck above or below while
  lining up lifts, and no way to try a deck from the editor at the spot you're looking at.

**Ask.**
1. **Level files (`arc_level`).** A small text format ARC reads and writes, so games stop writing their own:
   - layers of character rows;
   - `key = value` settings per layer and for the whole file;
   - a list of objects per layer: cell, kind, and a few named number or text fields such as `class=4 facing=2`;
   - something like `arc_level_load(path, &lvl)`, `arc_level_save(path, &lvl)`, and getters and setters for layers,
     settings and objects.

   It must stay readable and diff-friendly in git. Hot reload should work with `--hot`, so a saved deck reappears in
   a running game.
2. **Objects in the editor.**
   - An object palette alongside the tiles: droid, camera, start, lift link and so on, each with an icon or a
     `tile_mesh`-style 3D preview.
   - Placing one shows a small panel with its fields: number boxes, a facing picker, a dropdown of choices the game
     declares.
   - The fields are saved through `arc_level` and listed in the editor's `dump`.
   - Agent commands, e.g. `editor object add X Y droid class=4`, `editor object set N facing 2`,
     `editor object del N`.
3. **Settings for each layer:** a panel for the layer's (deck's) name and the game-declared settings, saved the same way.
4. **Copy, paste and transform:**
   - a rectangle select, then copy, cut and paste, with the objects inside it;
   - rotate 90° and mirror before pasting;
   - all one undo step each.
5. **Onion skin:** show the layer above or below, faintly, in the 2D view (lifts line up between decks), with a key
   to switch between above and below.
6. **Test-play:** a `playtest(layer, x, y, user)` hook and a key (T) that saves to a temporary copy, closes the
   editor and starts the game on that layer at that cell. Pressing F2 again returns to the editor where you were,
   with the edits still there. Also the agent command `editor playtest X Y`.
7. **Editor as a launch mode:** `--editor [FILE]` starts the game straight into the editor on that level file (for a
   game with `any_build`), so the editor can be shipped or used without dev keys.

**Must not change:** games that use the editor today (the deck template, `samples/tiles`) keep working as they are.
Everything above is opt-in through new `arc_editor_desc` fields.

**Game side (to do once this lands).**
- Droids moves its decks to `assets/decks.arclvl`, with `ship_decks.py`'s checks becoming the editor's `validate`
  hook. The checks:
  - each deck has a start or lift;
  - lifts line up between decks;
  - every locked door has its terminal;
  - every droid class is valid;
  - no droid stands in a wall.
- Droid spawns become objects (class, optional number, facing).
- Cameras become objects on walls.
- `tile_mesh` returns Droids' own wall, door, lift and kit meshes, so the 3D view looks like the game.
- Test-play starts the game on that deck.

## Acceptance

In words (ARC's own tests):
- **Files:** a level with two layers, settings and objects saves and loads back byte for byte. A malformed file is
  refused with a line number.
- **Objects:** an object added, edited and deleted from agent commands appears and disappears in `dump` and in the
  saved file. Undo restores it.
- **Copy and paste:** a copied 5x4 region with two objects, rotated 90° and pasted elsewhere, puts the cells and
  objects where expected. One undo removes it all.
- **Onion skin:** draws the neighbouring layer, and `frame` shows it.
- **Test-play:** `editor playtest 5 5` starts the game with the player at 5,5 on the current layer. Re-opening the
  editor returns to the same layer and view, with the unsaved edits still there.
- **`--editor FILE`:** opens the editor on that file, on any build.

```arc-check
game templates/deck/main.c
do editor on; editor object add 3 3 enemy; editor playtest 4 4; step 10
expect game.player.x == 4
```

## Done in ARC 0.10.8

All seven parts. Everything is opt-in through new `arc_editor_desc` fields; `samples/tiles` and games without `level`
work as before.

1. **Level files (`arc_level`).** A text format ARC reads and writes:
   - `arclvl 1`, then `key = value` lines for the whole file.
   - Then per layer: `layer "Deck 1" 32 16`, its `key = value` settings, exactly 16 `|row|` lines of 32 cells,
     `object X Y kind name=value ...` lines, and `end`.
   - Values are bare words or "quoted". `#` lines are comments.
   - Saving writes the same text for the same level, so files diff well and save back byte for byte.
   - A malformed file is refused: `arc_level_load` returns NULL and `arc_level_error()` gives
     `decks.arclvl:12: a row must be ...`.
   - API: `arc_level_load` / `save` / `parse` / `reload` / `text`; layers (`add_layer`, `cells` with stride
     width + 1, `cell`, names); settings (`get` / `set`, `_num`, layer -1 = the file's); objects (`object_add` /
     `remove` / `move` / `kind` / `x` / `y` / `get` / `set` / `at`).
   - With `--hot` a loaded level reloads when its file changes. `arc_level_reloaded(lvl)` says so once, so rebuild
     then. Cells keep their memory when the layer sizes are unchanged.
2. **Objects in the editor.** Give `.level` (and `.level_path`), `.objects` (`arc_editor_object`: kind, name, colour,
   key, optional icon, fields) and optionally `.object_mesh` (meshes for the editor's 3D view).
   - Field types: `ARC_FIELD_INT` / `NUM` (range, step), `TEXT`, `FACING` (0 = up the grid, clockwise; 4 or 8
     directions), `CHOICE` (`"rifle|laser|melee"`), `BOOL`. A field can be optional (e.g. the droid's fixed number)
     or have a default.
   - Objects tab: the object kinds, then the selected object's fields. - / + step a value, a click on a number or
     text types it, and facing and choices cycle.
   - In the grid: left places an object, or picks and drags the one already there; right removes it.
   - `objects_changed(layer)` is called after any object change. `dump`'s `editor.objects` lists
     `{i, kind, x, y, fields}`.
   - Agent commands: `editor object add X Y droid class=4 facing=2`, `editor object set N facing 2`,
     `editor object move N X Y`, `editor object del N`, `editor object list`.
3. **Layer settings:** the Layer tab edits the deck's name and your `.layer_fields`. Agent commands: `editor rename
   NAME`, `editor setting KEY VALUE`.
4. **Copy, paste, transform:**
   - The select tool (M) drags a rectangle. CTRL+C / CTRL+X copy or cut the cells and the objects inside it.
   - CTRL+V shows the clipboard at the pointer: R turns it 90 degrees, F mirrors it (SHIFT+F top-bottom), a click
     places it.
   - DEL clears the selection.
   - FACING fields turn and mirror with the clipboard.
   - Each edit is one undo step, cells and objects together.
   - Agent commands: `editor select X0 Y0 X1 Y1`, `copy`, `cut`, `rotate`, `flip h|v`, `paste X Y`, `clear`.
5. **Onion skin:** O shows the layer above, then below, then none, faintly in the 2D view (its objects too). The
   see-through cells are in `frame.rects` (with `alpha`). `dump`'s `editor.onion` gives the layer shown. Agent
   command: `editor onion above|below|off`.
6. **Test-play:** give `.playtest(layer, x, y, user)`. T (or the Test button, or `editor playtest X Y`) does this:
   - copies the level to `level_path` + `.playtest`;
   - closes the editor;
   - calls your hook.

   F2 then reopens the editor on the same layer, view and zoom, with the unsaved edits still there.
7. **`--editor [FILE]`:** the game starts in the editor, and F2 works on any build. FILE is read into `.level` before
   anything is built from it; `arc_editor_launch_file()` says which file.

**Validate:** `ship_decks.py`'s checks go in the existing `validate` hook. It sees the level, so it can check lifts
between decks, terminals for locked doors, droid classes and droids in walls. The message shows in the status line
and in `dump`'s `editor.message`.

**The `deck` template uses all of it:**
- its decks are an `arc_level`, and `decks.arclvl` replaces `decks.txt`;
- enemy objects with `hp` and `facing`;
- a `light` setting per deck;
- a validate hook;
- test-play restarts the game on that deck at that cell;
- `dump.player` gives the player's cell.

Your acceptance check passes on it as written:
`do editor on; editor object add 3 3 enemy; editor playtest 4 4; step 10` → `game.player.x == 4`.

**ARC's own tests:**
- `tests/level_check.md` (40 checks): objects added, edited, deleted and undone; settings; the saved file identical
  to the level's text; a 5x4 region with two objects copied, turned and pasted at the expected cells (facing turned),
  removed by one undo; the onion skin in `frame`; test-play and back with the edits.
- `tests/level_launch_check.md` (7 checks): `--editor ../tests/level_fixture.arclvl`, and F2 without `--dev`.
- The self-test (27 checks): round trips, saving, reloading in place, and nine malformed files refused with their
  line.

Limits:
- Layers edited together must be the same size.
- Cells are printable ASCII.
- Up to 64 object kinds, 64 layers in the editor, and 256 object or setting edits in the undo history (cell edits
  have their own, larger history).
