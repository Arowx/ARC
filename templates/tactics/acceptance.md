# Acceptance: tactics starter

The rules this template promises. Run from the game folder: `python <ARC>/tools/arc_check.py tests/acceptance.md`.
Keep these passing while you change the game, and add a block like this for every feature you add.

```arc-check
game .
expect game.phase == "player"
expect game.blue == 3
expect game.red == 3
expect frame.meshes[name=blue_unit].on_screen == 3
expect frame.meshes[name=red_unit].on_screen == 3
expect frame.texts[*].text contains "YOUR TURN"
# a move longer than MOVE (3) is refused; a short one is made
do select 0; move 6 6
expect game.units[0].x == 0
do move 2 0
expect game.units[0].x == 2
expect game.units[0].moved == true
# the red side walks toward the blue units
do end; step 120
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
