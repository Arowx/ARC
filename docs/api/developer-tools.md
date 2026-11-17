# Developer tools (0.7.2): tile level editor, replay timeline

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Developer tools (0.7.2): tile level editor, replay timeline** · [quick reference](../QUICK.md) · [full API](../API.md)


---- 0.10.8 level files (arc_level): layers of character rows, settings and objects, in a text format ARC reads and
   writes, so games don't write their own (Droids' request). A file:
     arclvl 1
     title = "Droids"                       # the file's settings: name = value
     layer "Deck 1" 32 16                   # a layer: name, width, height
     light = 0.8                            # the layer's settings
     |################################|     # height rows, each '|' + exactly width cells + '|'
     |#....e.........................#|
     object 3 5 droid class=4 facing=2      # objects: cell, kind, fields name=value
     end
   Values are bare words (letters, digits, _ . + - / :) or "quoted" (\" \\ \n inside); lines starting with # are comments
   (outside rows). Saving writes the same text for the same level (settings and objects in the order they were set), so
   files diff well in git and a saved file loads and saves back byte for byte. A malformed file is refused:
   arc_level_load returns NULL and arc_level_error() says where ("decks.arclvl:12: a row must be ..."). With --hot (or
   arc_app.hot_reload) a loaded level reloads when its file changes (arc_level_reloaded says so once; the editor reloads
   with it); cells keep their memory when the layer sizes are unchanged. Layers need not share a size (the editor wants
   them to). Cells are printable ASCII. Every getter takes layer -1 for the file's own settings.

```c
typedef struct arc_level arc_level;
ARC_API arc_level *arc_level_new(void);
ARC_API arc_level *arc_level_load(const char *path);    // NULL on failure: arc_level_error()
ARC_API arc_level *arc_level_parse(const char *text, size_t len, const char *name);    // name: for error messages
ARC_API const char *arc_level_error(void);    // the last load / parse / save problem ("" = none)
ARC_API bool arc_level_save(arc_level *l, const char *path);    // path NULL = where it was loaded or last saved
ARC_API bool arc_level_reload(arc_level *l, const char *path);    // read again in place (path NULL = its own)
ARC_API char *arc_level_text(const arc_level *l, size_t *len);    // the file's text (free() it)
ARC_API bool arc_level_reloaded(arc_level *l);    // true once after a hot reload (rebuild what you build from it)
ARC_API const char *arc_level_path(const arc_level *l);
ARC_API void arc_level_free(arc_level *l);
ARC_API int arc_level_layer_count(const arc_level *l);
ARC_API int arc_level_add_layer(arc_level *l, const char *name, int w, int h, char fill);    // its index, -1
ARC_API void arc_level_remove_layer(arc_level *l, int layer);
ARC_API const char *arc_level_layer_name(const arc_level *l, int layer);
ARC_API void arc_level_set_layer_name(arc_level *l, int layer, const char *name);
ARC_API int arc_level_width(const arc_level *l, int layer);
ARC_API int arc_level_height(const arc_level *l, int layer);
```

the layer's cells: height rows of width chars, each row followed by a '\0' (stride width + 1; edit them in place)

```c
ARC_API char *arc_level_cells(arc_level *l, int layer);
ARC_API char arc_level_cell(const arc_level *l, int layer, int x, int y);    // 0 outside
ARC_API void arc_level_set_cell(arc_level *l, int layer, int x, int y, char c);
ARC_API const char *arc_level_get(const arc_level *l, int layer, const char *key, const char *def);    // layer -1: the file's
ARC_API float arc_level_get_num(const arc_level *l, int layer, const char *key, float def);
ARC_API void arc_level_set(arc_level *l, int layer, const char *key, const char *value);    // NULL removes it
ARC_API void arc_level_set_num(arc_level *l, int layer, const char *key, float value);
ARC_API int arc_level_setting_count(const arc_level *l, int layer);
ARC_API const char *arc_level_setting_key(const arc_level *l, int layer, int i);
ARC_API int arc_level_object_count(const arc_level *l, int layer);
ARC_API int arc_level_object_add(arc_level *l, int layer, int x, int y, const char *kind);    // its index, -1
ARC_API void arc_level_object_remove(arc_level *l, int layer, int i);    // later objects move down one
ARC_API const char *arc_level_object_kind(const arc_level *l, int layer, int i);
ARC_API int arc_level_object_x(const arc_level *l, int layer, int i);
ARC_API int arc_level_object_y(const arc_level *l, int layer, int i);
ARC_API void arc_level_object_move(arc_level *l, int layer, int i, int x, int y);
ARC_API const char *arc_level_object_get(const arc_level *l, int layer, int i, const char *key, const char *def);
ARC_API float arc_level_object_get_num(const arc_level *l, int layer, int i, const char *key, float def);
ARC_API void arc_level_object_set(arc_level *l, int layer, int i, const char *key, const char *value);    // NULL removes
ARC_API void arc_level_object_set_num(arc_level *l, int layer, int i, const char *key, float value);
ARC_API int arc_level_object_field_count(const arc_level *l, int layer, int i);
ARC_API const char *arc_level_object_field_key(const arc_level *l, int layer, int i, int k);
ARC_API int arc_level_object_at(const arc_level *l, int layer, int x, int y, int from);    // first index >= from there, -1
```

---- Tile level editor: any game with a grid of tiles can edit it in-game, the game owning the data and its file ----
   The game describes its palette and hands ARC a pointer to its grid; ARC draws a 2D top-down view over the game (or, with
   `world_cell` and `camera`, a grid over the game's own 3D view) and edits the cells in place, calling `changed` for every
   cell so the game rebuilds what it builds from them (meshes, collision, spawns). Nothing happens until arc_editor_begin.
     static const arc_editor_tile pal[] = { { '#', "Wall", 0x707880, ARC_KEY_W }, { '.', "Floor", 0x283038, ARC_KEY_F }, ... };
     arc_editor_begin(&(arc_editor_desc){ .name = "Deck 1", .width = 32, .height = 16, .cells = deck, .stride = 33,
                      .tiles = pal, .tile_count = 10, .changed = on_cell, .save = save_deck, .user = NULL });
   Opening: F2 (only with dev keys: arc_app.dev_keys or --dev; or desc.any_build), arc_editor_open(), or `editor on`.
   Ticks: the editor runs inside the tick, from the tick's input, like the UI kit (arc_ui_*): every click and key it reacts
   to is in recordings, so replays and `seek` redo the same edits. While it is open the game's tick() is not called
   (the game is paused; ARC's tick count still advances) until P ("play from here") lets it run with the editor open.
   draw() keeps being called. save/load hooks are not called while a replay plays or `seek` re-simulates (a replay never
   writes files); a replay that pressed load reads whatever the file holds now.
   Mouse: left = the tool with the selected tile, right = the same tool with the erase tile, middle drag (or SPACE + left)
   pans, wheel zooms at the pointer. Keys: B brush, L line, R rectangle (filled; SHIFT = outline), F flood fill, I eyedropper
   (or ALT + click), [ ] previous / next tile, a tile's own `key` selects it (a tile key wins over a tool key of the same letter;
   SHIFT + the tool letter always picks the tool), CTRL+Z undo, CTRL+Y or CTRL+SHIFT+Z redo, CTRL+S save, CTRL+O load (revert),
   arrows pan, HOME fits the grid, G grid lines, V 2D / 3D view (with world_cell and camera), P play / pause the game, F2 or
   ESC closes. Undo is a bounded ring of cell edits (a fill is one step).
   0.10.8: M the select tool (drag a rectangle); CTRL+C / CTRL+X copy / cut it (cells, and with a level its objects),
   CTRL+V pastes at the pointer (R turns the clipboard 90 degrees, F mirrors it, SHIFT+F top-bottom; click places; right
   or ESC gives up), DEL clears the selection (or deletes the selected object); O shows the layer above, then below,
   then none, faintly in 2D; T test-plays from the cell under the pointer (desc.playtest); TAB switches the panel's
   tabs. With desc.level: the Objects tab (an object kind's key selects it; left places one or picks and drags the one
   there; right removes it; its fields are edited in the panel: - / + steps, a click on a number or text types it,
   Enter keeps it) and the Layer tab (the layer's name and layer_fields). Every edit is one undo step, cells and
   objects together. A letter the editor uses (G P V O T, tools) goes to a tile with that key first; SHIFT + it then.
   0.10.3 the editor's own 3D view (with `tile_mesh`): the grid as the game builds it (walls raised, doors, lifts and kit),
   seen from an orbit camera of the editor's own, over the whole editing area; paint in it as in 2D (the pointer picks
   the floor cell). V cycles 2D -> own 3D -> the game's view (when `camera` is given too). In the 3D view: middle drag
   (or SPACE + right drag) orbits, SHIFT + middle drag (or SPACE + left drag) pans, wheel zooms, arrows pan, HOME fits.
   It is drawn with the game's lights, sky and post (the lights the game added in draw() light it too).
   0.10.3 layers (`layers`, `layer_count`): several grids of the same size and type (a ship's decks, a building's
   floors), PAGE UP / PAGE DOWN switch; undo goes back across layers. `changed` and `save` see arc_editor_layer().
   Agent commands (headless level editing; these change the grid directly and, like `set`, are not part of recordings):
   editor [on|off] | editor paint X Y CH | editor fill X0 Y0 X1 Y1 CH | editor flood X Y CH | editor undo | editor redo |
   editor save | editor load | editor dump (the grid as `ARC editor row` text lines) | editor tile CH | editor tool NAME |
   editor play|pause | editor info | editor layer N | editor view 2d|3d|game | editor orbit YAW PITCH DIST (degrees,
   cells). CH is a character ('#'), or a number with '=' in front ("=12") for int grids.
   0.10.8: editor object add X Y KIND [name=value ...] | object set N NAME [VALUE] | object move N X Y | object del N |
   object select N | object list | setting KEY [VALUE] | rename NAME | select X0 Y0 X1 Y1 | select none | copy | cut |
   paste X Y | clear | rotate | flip h|v | onion above|below|off | playtest X Y | tab tiles|objects|layer | kind KIND.
   dump's editor object adds tab, onion (the layer shown, -1 none), pasting, selection, clipboard (its size), and with a
   level: file, layer_name, object_kind, object_selected, settings, objects ({i, kind, x, y, fields}).
   --editor [FILE] starts in the editor (and F2 works on any build); FILE is read into desc.level when the game gives
   one (arc_editor_launch_file() says which).

```c
enum { ARC_EDITOR_CHAR = 0, ARC_EDITOR_U8 = 0, ARC_EDITOR_I32 = 1 };    // cell types: one byte (chars) or int32
typedef struct { int id; const char *name; uint32_t rgb; arc_key key; arc_texture icon; float sx, sy, sw, sh; int layer; } arc_editor_tile;
```

0.10.3 one mesh of a cell in the editor's own 3D view. model is in cell units around the cell's floor centre: a unit
   cube scaled (1, 2, 1) and moved up 1 is a wall two cells high filling the cell.

```c
typedef struct { arc_mesh mesh; arc_m4 model; uint32_t tint; } arc_editor_part;
#define ARC_EDITOR_PARTS 8
```

0.10.8 a field of an object kind (or of a layer's settings), edited in the editor's panel and kept in the arc_level as
   text. FACING: 0 = up the grid (-y), turning clockwise: 0..3 (N E S W), or 0..7 (N NE E ... NW) with max 7.

```c
enum { ARC_FIELD_INT, ARC_FIELD_NUM, ARC_FIELD_TEXT, ARC_FIELD_FACING, ARC_FIELD_CHOICE, ARC_FIELD_BOOL };
typedef struct { const char *name; int type; float min, max, step; const char *def; const char *choices; bool optional; } arc_editor_field;
```

0.10.8 a kind of object the editor places (a droid spawn, a camera, a lift link)

```c
typedef struct { const char *kind; const char *name; uint32_t rgb; arc_key key; arc_texture icon; float sx, sy, sw, sh; const arc_editor_field *fields; int field_count; } arc_editor_object;
enum { ARC_EDITOR_VIEW_2D, ARC_EDITOR_VIEW_GAME, ARC_EDITOR_VIEW_3D };
typedef struct { const char *name; int width, height; void *cells; int cell_type; int stride; const arc_editor_tile *tiles; int tile_count; int erase_tile; float cell_px; int undo_cells; void *user; void (*changed)(int x, int y, int old_id, int new_id, void *user); bool (*save)(void *user); bool (*load)(void *user); bool (*validate)(char *msg, size_t n, void *user); bool any_build; bool start_playing; arc_v3 origin; float world_cell; void (*camera)(arc_camera *out, void *user); int (*tile_mesh)(int x, int y, int id, arc_editor_part *out, int max, void *user); void *const *layers; const char *const *layer_names; int layer_count; arc_level *level; const char *level_path; const arc_editor_object *objects; int object_count; const arc_editor_field *layer_fields; int layer_field_count; int (*object_mesh)(int layer, int index, arc_editor_part *out, int max, void *user); void (*objects_changed)(int layer, void *user); void (*playtest)(int layer, int x, int y, void *user); } arc_editor_desc;
ARC_API void arc_editor_begin(const arc_editor_desc *d);    // (again to switch grids: the undo history is cleared)
ARC_API void arc_editor_end(void);    // closes and forgets the grid
ARC_API void arc_editor_open(bool on);
ARC_API bool arc_editor_is_open(void);
ARC_API void arc_editor_set_playing(bool on);    // the game's tick() runs while the editor is open
ARC_API bool arc_editor_playing(void);
ARC_API int arc_editor_get(int x, int y);    // -1 outside the grid
```

edits, each one undo step (false / 0 when nothing changed or out of the grid); also what the agent commands do

```c
ARC_API bool arc_editor_paint(int x, int y, int id);
ARC_API int arc_editor_fill(int x0, int y0, int x1, int y1, int id);    // a filled rectangle (corners in any order)
ARC_API int arc_editor_flood(int x, int y, int id);    // the 4-connected area of the same id
ARC_API int arc_editor_line(int x0, int y0, int x1, int y1, int id);
ARC_API bool arc_editor_undo(void);
ARC_API bool arc_editor_redo(void);
ARC_API int arc_editor_undo_steps(void);    // steps that can be undone now
ARC_API bool arc_editor_save(void);    // calls desc.save; clears the "changed" mark
ARC_API bool arc_editor_load(void);
ARC_API bool arc_editor_dirty(void);    // edited since the last save or load
ARC_API const char *arc_editor_message(void);    // the last validate message ("" = fine)
ARC_API bool arc_editor_hover(int *x, int *y);    // the cell under the pointer last tick
ARC_API int arc_editor_layer(void);    // 0.10.3 the layer being edited (0 without layers)
ARC_API void arc_editor_set_layer(int layer);
ARC_API void arc_editor_set_view(int view);    // 0.10.3 ARC_EDITOR_VIEW_*; one the desc can't show is ignored
ARC_API int arc_editor_view(void);
```

0.10.8 objects, settings, copy and paste, onion skin, test-play: each edit one undo step, on the current layer

```c
ARC_API arc_level *arc_editor_level(void);
ARC_API int arc_editor_object_add(int x, int y, const char *kind);    // its index (fields get their defaults), -1
ARC_API bool arc_editor_object_set(int index, const char *key, const char *value);    // value NULL removes the field
ARC_API bool arc_editor_object_move(int index, int x, int y);
ARC_API bool arc_editor_object_delete(int index);
ARC_API int arc_editor_object_selected(void);    // -1 = none
ARC_API bool arc_editor_set_setting(const char *key, const char *value);    // the current layer's (NULL removes)
ARC_API bool arc_editor_set_layer_name(const char *name);
ARC_API bool arc_editor_select(int x0, int y0, int x1, int y1);    // a rectangle (corners in any order); x0 < 0 = none
ARC_API bool arc_editor_copy(bool cut);    // the selection's cells and objects to the clipboard
ARC_API bool arc_editor_paste(int x, int y);    // the clipboard's top-left at x, y
ARC_API bool arc_editor_clear_selection(void);    // erase tile + no objects in the selection
ARC_API void arc_editor_rotate_clipboard(void);    // 90 degrees clockwise (FACING fields turn with it)
ARC_API void arc_editor_flip_clipboard(bool vertical);    // left-right (or top-bottom)
ARC_API void arc_editor_onion(int which);    // the layer above (-1: layer - 1), below (+1) or none (0), faint in 2D
ARC_API bool arc_editor_playtest(int x, int y);    // calls desc.playtest (false without it)
ARC_API const char *arc_editor_launch_file(void);    // --editor FILE's file ("" without)
```

---- Replay timeline (scrubber) ----
   A bar along the bottom of the screen over a running game or a replay (--replay FILE, e.g. a play session's input.rec):
   the current tick and the length, F8 bookmarks and arc_event markers (from events.jsonl beside the replay, and from
   this run), play / pause, one tick forward / back, speed 0.25x..8x, and click or drag to go to any tick.
   F9 shows it (with dev keys, or in any --replay run); --timeline opens it at the start. Keys while it is open: SPACE
   play / pause, LEFT / RIGHT (or , .) a tick back / forward, - = slower / faster, HOME / END the start / the end. In a live
   game those keys stay the game's while it plays: only when paused does the timeline take them; clicks on the bar never
   reach the game. A paused desktop game runs no ticks but keeps drawing. Headless runs ignore the pause (`step` steps).
   Going back needs the game's save_state / load_state: ARC keeps snapshots every 60 ticks (spaced out further on long
   runs) and re-simulates silently from the nearest one, so seeking is exact: the state at tick T is the same whether
   you seek there or play straight to it. Without the callbacks only forward seeking works (a game cannot be restarted
   inside one run: replay it again from the start). Going forward in a replay re-simulates silently, without drawing.
   In a live game the timeline's end is the newest tick played; going back and playing again replays the recorded input
   up to there and then hands control back to the player. While recording (--record, play sessions) going back is
   allowed this way and the recording stays exact (the past is only replayed, never re-recorded).
   Agent commands: timeline [on|off] | timeline seek T | timeline step [N] | timeline back [N] | timeline play | timeline pause |
   timeline speed X | timeline marks (prints `ARC mark tick=N kind=bookmark|event name=...`) | timeline info.
   Display only: nothing here changes what a tick computes.

```c
ARC_API void arc_timeline_show(bool on);
ARC_API bool arc_timeline_shown(void);
ARC_API bool arc_timeline_seek(uint64_t tick);    // false (and an "ARC error" line) if it can't get there
ARC_API void arc_timeline_set_paused(bool paused);
ARC_API bool arc_timeline_paused(void);
ARC_API void arc_timeline_set_speed(float x);    // clamped to 0.25..8
ARC_API uint64_t arc_timeline_length(void);    // the replay's length, or the newest tick of a live game
ARC_API int arc_timeline_mark_count(void);
ARC_API bool arc_timeline_mark(int i, uint64_t *tick, const char **name, bool *bookmark);
```
