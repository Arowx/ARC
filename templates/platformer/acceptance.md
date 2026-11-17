# Acceptance: platformer starter

The rules this template promises. Run from the game folder: `python <ARC>/tools/arc_check.py tests/acceptance.md`.
Keep these passing while you change the game, and add a block like this for every feature you add.

```arc-check
game .
do step 10
expect game.phase == "play"
expect game.x == 1.5
expect game.on_ground == true
expect frame.meshes[name=player].on_screen == 1
expect frame.texts[*].text contains "COINS 0/10"
# running right (7 units a second once up to speed)
do press D; step 40; release D
expect game.x > 5
expect game.on_ground == true
# a jump leaves the ground and lands again
do tp 3 2; step 5; press SPACE; step 12
expect game.on_ground == false
expect game.y > 3
do release SPACE; step 60
expect game.on_ground == true
expect game.y == 2
# coins are picked up once
do tp 4.5 3; step 1; tp 4.5 3; step 1
expect game.coins == 1
# spikes send you back to the start
do tp 8.5 2; step 2
expect game.deaths == 1
expect game.x < 2
# a checkpoint moves where you come back
do tp 26.5 2; step 2
expect game.checkpoint_x == 26.5
do tp 35.5 2; step 2
expect game.deaths == 2
expect game.x == 26.5
# the flag ends the level
do win; step 2
expect game.phase == "won"
expect frame.texts[*].text contains "LEVEL CLEAR"
```
