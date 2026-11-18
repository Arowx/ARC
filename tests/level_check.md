# Request (test fixture): the deck editor (0.10.8)

**Why.** This file checks `tests/level_game.c` the way Droids' deck-editor request asked: objects with fields added,
edited and removed from agent commands (in `dump`, in the saved file, undone), the layer's settings, a 5x4 region with
two objects copied, turned 90 degrees and pasted (one undo removes it), the onion skin in `frame`, and test-play
coming back to the editor with the edits.

**Ask.** Nothing; the fixture already does this.

## Acceptance

```arc-check
game tests/level_game.c
do editor on; step 1
expect editor.open == true
expect editor.layers == 2
expect editor.layer_name == "A"
# objects: added with their fields, edited, removed, undone; saved byte for byte
do editor object add 3 3 droid class=4 facing=1; editor object add 5 4 camera wall=e; step 1
expect count(editor.objects[*]) == 2
expect editor.objects[0].fields.class == "4"
expect editor.objects[0].fields.facing == "1"
expect editor.objects[0].fields.number missing
expect editor.objects[1].fields.wall == "e"
do editor object set 0 class 7; editor object set 0 number 476; step 1
expect editor.objects[0].fields.class == "7"
expect editor.objects[0].fields.number == "476"
do editor object del 1; step 1
expect count(editor.objects[*]) == 1
do editor undo; step 1
expect count(editor.objects[*]) == 2
expect editor.objects[1].kind == "camera"
# the layer's name and settings
do editor rename Bridge; editor setting music deck1.ogg; step 1
expect editor.layer_name == "Bridge"
do editor rename Cargo Hold; step 1
expect editor.layer_name == "Cargo Hold"
do editor rename "Engineering Bay"; step 1
expect editor.layer_name == "Engineering Bay"
do editor rename Bridge; step 1
expect editor.layer_name == "Bridge"
expect editor.settings.music == "deck1.ogg"
do editor save; reread; step 1
expect game.file_objects == 2
expect game.file_same == true
# copy 5 x 4 cells with two objects, turn it, paste it: cells and objects land turned; one undo takes it all away
do editor paint 6 2 D; editor select 2 2 6 5; editor copy; editor rotate; editor paste 9 0; step 1
expect editor.clipboard[0] == 4
expect editor.clipboard[1] == 5
expect count(editor.objects[*]) == 4
expect editor.objects[2].kind == "droid"
expect editor.objects[2].x == 11
expect editor.objects[2].y == 1
expect editor.objects[2].fields.facing == "2"
expect editor.objects[3].x == 10
expect editor.objects[3].y == 3
do probe 12 4; step 1
expect game.probe == "D"
do editor undo; probe 12 4; step 1
expect count(editor.objects[*]) == 2
expect game.probe == "."
# the onion skin: the layer below, faint (its locked door is drawn see-through; not without it)
do editor tab objects; editor onion below; step 1
expect editor.onion == 1
expect frame.rects[rgb=#e04040] exists
do editor onion off; step 1
expect frame.rects[rgb=#e04040] missing
# test-play: the game starts there; F2 (here `editor on`) comes back where it was, the edits still there
do editor layer 1; editor paint 2 2 D; editor playtest 5 4; step 2
expect editor.open == false
expect game.player.x == 5
expect game.player.y == 4
expect game.player.layer == 1
do editor on; probe 2 2; step 1
expect editor.open == true
expect editor.layer == 1
expect game.probe == "D"
expect editor.dirty == true
```
Described in words: every object, setting and copy-paste edit is one undo step; the saved file is the level's text.
