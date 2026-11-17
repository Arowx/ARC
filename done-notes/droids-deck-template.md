# Request from Droids: a "deck crawler" genre template, and 3D level editing for decks

**Why.** ARC 0.9.1's templates (tactics, shooter, platformer, racer, cards) cover flat arenas. There isn't one for
the game Droids is: a ship of indoor levels.

Droids has the whole loop:
- decks from text maps (`tools/ship_decks.py` → `src/ship.h`);
- doors that open, and locked doors;
- lifts between decks;
- consoles and terminals;
- a fog-of-war map, a minimap and line of sight;
- enemies on patrol;
- clearing a deck dims its lights.

But it's 2850 lines of one game's code. The next game like it (a Moonbase Raid real-time mode, a horror jam game,
an arowx.com arcade cabinet) would start from nothing.

The decks are edited as text in a Python file. ARC 0.7.2's tile editor edits grids, but only flat: no walls in 3D,
no doors, lifts or kit placed and seen as the player sees them.

**Ask.**
1. **Template `deck`** (`tools/new_game.py DIR --template deck`), built on what ARC now has:
   - two or three decks from strings (`#` wall, `D` door, `L` lift, `T` terminal, `E` energiser, `S` start,
     `e` enemy), built into meshes with cells and portals (rooms behind closed doors aren't drawn);
   - doors that slide when approached, and locked doors opened by a terminal;
   - a lift with up and down between decks;
   - fog-of-war map, minimap, line of sight;
   - enemies on navmesh agents with the AI from `droids-ai-behaviour.md`, if it exists by then (otherwise simple
     patrol and chase);
   - shooting with `droids-weapons.md`, if it exists;
   - the camera rigs from `droids-camera-rigs.md`, if they exist (top-down, third and first person);
   - lights per room that go dim when a deck is cleared (0.9.5 lights);
   - saves, `dump`, agent commands (`deck N`, `tp X Y`, `open_doors`, `clear`), and acceptance checks, as the other
     templates have.
2. **3D view in the tile editor:**
   - a toggle that shows the grid as the game builds it: walls raised, door, lift and kit meshes placed through a
     game callback `tile -> mesh`;
   - orbit, pan and zoom, painting in either view;
   - layers per deck;
   - saving to the same text or grid format the game loads, so Droids could edit `ship_decks.py`'s maps there.
- **Must not change:** the existing templates and the 2D editor.

**Game side.**
- Droids would use the 3D editor view for its six decks plus Training.
- The template is the "next game like Droids" path, so Droids itself needn't move onto it.

## Acceptance

```arc-check
game templates/deck/main.c
do step 30; tp 5 5; step 10; open_doors; step 30; deck 2; step 30
expect game.deck == 2
expect frame.meshes[name=door].on_screen >= 0
```

In words: the template's own acceptance checks:
- walking into a door opens it;
- a locked door stays shut until its terminal is used;
- the lift changes deck;
- clearing the enemies dims the lights and is reported in `dump`;
- the map fills in as you explore;
- save and load restore the deck, doors and map.

## Done in ARC 0.10.3

`#if defined(ARC_HAS_EDITOR_3D)`. Both parts of the ask are in.

**1. Template `deck`** (`python tools/new_game.py DIR --template deck`):
- Three decks from strings: `#` wall, `.` floor, `D` door, `K` locked door, `T` terminal, `L` lift, `E` energiser,
  `S` start, `e` enemy. (`K` is my choice for "locked door"; the ask didn't name one.)
- Rooms are found from the map and become cells; doors are portals, so rooms behind shut doors aren't drawn in first
  person.
- Doors slide apart when you (or an enemy) come near; `K` doors stay shut until the deck's terminal is used (F).
- Lifts: UP / DOWN on a lift; lifts that line up on neighbouring decks join.
- Fog of war (world mask), minimap, line of sight; a light per room that dims when the deck is cleared.
- Enemies use the 0.10.0 pieces from your other requests: `arc_brain` (patrol, attack, search), `arc_senses`
  (sight through open doors, hearing shots), nav agents per deck, shots both ways, and the camera rigs (top-down,
  follow, first person on V).
- Saves (`save` / `load` commands and `save_state`), `dump`, agent commands `deck N`, `tp X Y`, `open_doors`,
  `clear`, `use`, `lift up|down`, `fire`, `face DEG`, `view N`, `restart`.
- `tests/acceptance.md`: 31 checks covering each item in your list (door opens on approach, locked door until the
  terminal, lift changes deck, clearing dims the lights and shows in `dump`, the map fills in, save/load restores deck,
  doors and map), plus the editor. Your own acceptance block passes too (`game templates/deck/main.c` now builds from
  the ARC folder).

**2. 3D view in the tile editor:**
- `arc_editor_desc.tile_mesh(x, y, id, parts, max, user)`: the game returns the meshes it builds for a cell (walls
  raised, door, lift and kit meshes); doors can turn by their neighbours since x, y are passed.
- V (or `editor view 3d`) shows the grid that way from the editor's own orbit camera: middle drag (or SPACE + right)
  orbits, SHIFT + middle (or SPACE + left) pans, wheel zooms, arrows pan, HOME fits. Paint in it as in 2D.
- Layers: `arc_editor_desc.layers` / `layer_count` / `layer_names`; PAGE UP / DOWN or `editor layer N`. Undo goes
  back across layers; `arc_editor_layer()` tells `changed` / `save` which deck.
- Saving is still the game's own `save` hook, so it writes whatever format the game loads (the template writes
  `decks.txt`).
- The 2D editor and the existing templates are unchanged.

**For Droids:** to edit `ship_decks.py`'s maps in the editor, give `arc_editor_begin` the seven decks as `layers`, a
`tile_mesh` that returns your wall/door/lift/console meshes, and a `save` hook that writes the Python file (or a text
file `ship_decks.py` reads).
