# Acceptance: deck crawler starter

The rules this template promises. Run from the game folder: `python <ARC>/tools/arc_check.py tests/acceptance.md`.
Keep these passing while you change the game, and add a block like this for every feature you add.

```arc-check
game .
expect game.phase == "play"
expect game.deck == 1
expect game.rooms == 6
expect game.enemies == 3
expect game.explored > 10
expect frame.meshes[name=wall].on_screen > 20
expect frame.meshes[name=door].on_screen >= 1
expect frame.texts[*].text contains "DECK 1"
# walking up to a door opens it, and you can walk through
do tp 6 2; face 90; step 30
expect game.near_door_open == 1
do press W; step 30; release W
expect game.x > 8
# a locked door stays shut until its terminal is used
do tp 6 7; step 30
expect game.near_door_locked == true
expect game.near_door_open == 0
do tp 18 8; use; tp 6 7; step 30
expect game.unlocked == true
expect game.near_door_open == 1
# the map fills in as you explore
expect game.explored > 40
# the lift changes deck (and the lifts line up)
do tp 3 7; lift down; step 2
expect game.deck == 2
do lift up; step 2
expect game.deck == 1
expect game.x == 3.5
# clearing the enemies dims the lights and is reported
do clear; step 2
expect game.cleared == true
expect game.enemies == 0
do step 90
expect game.light_level < 0.3
# save and load bring back the deck, the doors and the map
do save; deck 3; step 2
expect game.deck == 3
do load; step 2
expect game.deck == 1
expect game.unlocked == true
expect game.cleared == true
expect game.explored > 40
# the deck editor: each deck a layer, a 3D view of the deck as it is built
do editor on; editor view 3d; editor layer 2; step 2
expect editor.view == "3d"
expect editor.layer == 2
expect frame.viewports[0].meshes[name=wall].count == 98
do editor paint 2 2 Wall; step 2
expect frame.viewports[0].meshes[name=wall].count == 99
expect editor.undo == 1
do editor undo; editor off; step 2
```
