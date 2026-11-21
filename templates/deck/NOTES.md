## This game started from the deck template

- Decks: `MAPS` (text, `#` wall, `.` floor, `D` door, `K` locked door, `T` terminal, `L` lift, `E` energiser, `S` start,
  `e` enemy) made into `LV`, an `arc_level` (a layer per deck, enemy objects with `hp` and `facing`, each deck's
  `light` setting). `decks.arclvl` (written by the editor, CTRL+S) replaces them when it has 3 decks of 24 x 11.
- `build_deck(d)` makes rooms, doors, the navmesh and (for the current deck) cells and portals from a map. Call it
  after changing a map; `after_load()` does it after loads.
- State: `Game G`, pointer-free (doors, fog of war, enemies with their nav agents and brains, the senses, the camera
  rig); `save_state`/`load_state` and the `save` / `load` commands copy it whole. Only the current deck's enemies move.
- Enemies: `arc_brain` (PATROL, ATTACK, SEARCH) + `arc_senses` (sight with line of sight through open doors, hearing
  shots) + `arc_nav_agent` on the deck's navmesh. Locked doors are out of the navmesh until unlocked.
- Fog of war: `G.seen` (255 in sight, 90 remembered) drives `arc_set_world_mask` and the minimap.
- Views: `arc_cam_rig` TOPDOWN, FOLLOW, FIRST (V blends between them).
- Editor: F2 with `--dev`, `editor on`, or run with `--editor [decks.arclvl]`. Each deck is a layer (PAGE UP / DOWN);
  V shows the deck in 3D as `ed_tile_mesh` builds it; the Objects tab places enemies (`OBJS`, `ENEMY_FIELDS`), the
  Layer tab names the deck and sets its light (`DECK_FIELDS`); M selects, CTRL+C / V copy and paste rooms (R turns,
  F mirrors); O shows the deck above or below; T test-plays from the pointer (`ed_playtest`; F2 comes back);
  `ed_validate` reports a deck without a start or lift. CTRL+S writes `decks.arclvl`; with `--hot` a changed file
  reloads.
- **Looks:** walls and floor are textured cubes (`wall.jpg`, `floor.jpg`). Door, lift, terminal and energiser models
  are imported into a 1 x 1 x 1 box (`--fit 1 1 1 --origin centre`) and scaled per use. The droid (player) and the
  security robots are about 0.8 and 0.9 tall with their feet at 0.
- **Juice:** shots spark (`FX_SPARKS`); kills blast, smoke and throw parts (`FX_BLAST`, `FX_SMOKE`, `FX_PARTS`) with
  shake. A red flash shows when you are hurt. A "!" pops over a robot that spots you, and "DOORS UNLOCKED",
  "REPAIRED" and "DESTROYED" pop up too. Bloom is on. The droid fades as the follow camera is pulled close by a
  wall, and is hidden in first person.
- **Assets:** made with Ludo (ludo.ai). `assets/manifest.json` lists every file: how it was made, a description, and
  for models the `arc_import.py` options. Put the source GLBs in `art/` and
  `python3 <ARC>/tools/arc_import.py --manifest --game .` re-imports every model the same way. Import new models
  with `--prompt "..."` and they are recorded too. Models face +z (check the green FRONT +Z arrow in
  `build/import/NAME.png`).
- Agent commands: `deck N`, `tp X Y` (cells), `open_doors`, `clear`, `use`, `lift up|down`, `fire`, `face DEG`,
  `view N`, `save`, `load`, `restart`.
- Tests: `tests/acceptance.md` has two blocks: the rules, and models and juice.
