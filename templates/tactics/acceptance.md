# Acceptance: tactics starter

The rules this template promises. Run from the game folder: `python <ARC>/tools/arc_check.py tests/acceptance.md`.
Keep these passing while you change the game, and add a block like this for every feature you add.

```arc-check
game .
expect game.phase == "player"
expect game.blue == 3
expect game.red == 3
# blue: two knights and an archer; red: two orcs and a goblin (Ludo models)
expect frame.meshes[name=knight].on_screen == 2
expect frame.meshes[name=archer].on_screen == 1
expect frame.meshes[name=orc].on_screen == 2
expect frame.meshes[name=goblin].on_screen == 1
expect frame.texts[*].text contains "YOUR TURN"
# a move longer than MOVE (3) is refused; a short one is made
do select 0; move 6 6
expect game.units[0].x == 0
do move 2 0
expect game.units[0].x == 2
expect game.units[0].moved == true
# the red side walks toward the blue units (a step at a time, so give it a few seconds)
do end; step 240
expect game.phase == "player"
expect game.turn == 2
expect game.units[1].x < 7
# attack an adjacent red unit: ATTACK (4) off its HP (10)
do select 0; move 3 0; attack 1
expect game.units[1].hp == 6
expect game.units[0].acted == true
do win
expect game.phase == "won"
```

## Animation and juice

Hits show a damage number and land a moment after the swing; the unit walks its path cell by cell.

```arc-check
game .
do select 0; move 2 0
expect game.units[0].x == 2
do step 4
expect frame.meshes[name=knight].count >= 1
do end; step 240; select 0; move 3 0; attack 1; step 36
expect frame.texts[*].text contains "-4"
do step 80
expect frame.texts[text=-4] missing
# an archer (range 3) shoots over open ground
do restart; select 5; move 3 3; step 30
expect game.units[5].x == 3
```
