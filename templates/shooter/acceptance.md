# Acceptance: shooter starter (Neon Arena)

The rules this template promises. Run from the game folder: `python <ARC>/tools/arc_check.py tests/acceptance.md`.
Keep these passing while you change the game, and add a block like this for every feature you add.

## Start, move, shoot, waves, game over

```arc-check
game .
expect game.phase == "play"
expect game.wave == 1
expect game.enemies == 5
expect game.drones == 5
expect game.lives == 3
expect game.crates == 6
expect frame.meshes[name=player].on_screen == 1
expect frame.texts[*].text contains "SCORE 0"
# enemies pop in over 20 ticks and hold still for 30
do step 30
expect frame.meshes[name=enemy].on_screen >= 4
# WASD moves the player (speed 6 a second)
do press D; step 30; release D
expect game.x > 2.5
expect game.x < 3.5
# one shot per press of fire while the gun cools down
do clear; aim 0; fire; fire
expect game.bullets == 1
# a shot removes the drone it hits and scores 10 x wave x combo; a kill freezes the rules for 3 ticks (hit-stop)
do spawn_at 7 0; step 12
expect game.kills == 1
expect game.score == 10
expect game.enemies == 0
# a cleared wave brings the next, bigger one (3 + 2 x wave)
do step 100
expect game.wave == 2
expect game.enemies == 7
# touching an enemy costs a life; the last life plays the death, then the game is over
do clear; spawn_at 3 0; step 2
expect game.lives == 2
do clear; step 100; lives 1; spawn_at 3 0; step 2
expect game.phase == "dying"
do step 110
expect game.phase == "over"
expect frame.texts[*].text contains "GAME OVER"
```

## Beetles take three hits; kills in a row build a combo

```arc-check
game .
do clear; aim 0; spawn_at 6 0 beetle; fire; step 18
expect game.beetles == 1
do fire; step 18; fire; step 25
expect game.beetles == 0
expect game.score == 30
do spawn_at 5 0; spawn_at 7 0; fire; step 20; fire; step 20
expect game.kills == 3
expect game.combo == 3
expect frame.texts[*].text contains "COMBO x3"
```

## Crystals give power-ups; crates stop shots and break

```arc-check
game .
do clear; crystal 1 0 spread; step 2
expect game.crystals == 1
do press D; step 15; release D
expect game.power == "spread"
expect game.crystals == 0
do aim 0; fire; step 2
expect game.bullets == 3
do restart; clear; crystal 0 1 shield; press S; step 15; release S; spawn_at 0.6 1.5; step 3
expect game.shield == false
expect game.lives == 3
# a crate at (4.5, 4.5): shots from the middle stop on it, and the fourth breaks it
do restart; clear; aim 45; fire; step 20; fire; step 20; fire; step 20
expect game.crates == 6
do fire; step 20
expect game.crates == 5
```

## Every 5th wave brings a boss

```arc-check
game .
do wave 5; step 10
expect game.bosses == 1
expect frame.texts[*].text contains "BOSS"
```
