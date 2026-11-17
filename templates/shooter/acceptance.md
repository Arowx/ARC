# Acceptance: shooter starter

The rules this template promises. Run from the game folder: `python <ARC>/tools/arc_check.py tests/acceptance.md`.
Keep these passing while you change the game, and add a block like this for every feature you add.

```arc-check
game .
expect game.phase == "play"
expect game.wave == 1
expect game.enemies == 5
expect game.lives == 3
expect frame.meshes[name=player].on_screen == 1
expect frame.meshes[name=enemy].on_screen >= 4
expect frame.texts[*].text contains "SCORE 0"
# WASD moves the player (speed 6 a second)
do press D; step 30; release D
expect game.x > 2.5
expect game.x < 3.5
# one shot per press of fire while the gun cools down
do clear; aim 0; fire; fire
expect game.bullets == 1
# a shot removes the enemy it hits and scores 10 x wave
do spawn_at 7 0; step 20
expect game.kills == 1
expect game.score == 10
expect game.enemies == 0
# a cleared wave brings the next, bigger one (3 + 2 x wave)
do step 100
expect game.wave == 2
expect game.enemies == 7
# touching an enemy costs a life; the last life ends the game
do clear; spawn_at 3 0; step 2
expect game.lives == 2
do clear; step 100; lives 1; spawn_at 3 0; step 2
expect game.phase == "over"
expect frame.texts[*].text contains "GAME OVER"
```
