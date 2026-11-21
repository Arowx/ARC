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

## Slimes, animation and juice

Landing on a slime squashes it (and bounces you); walking into one costs a life. Coins pop up a count.

```arc-check
game .
expect game.slimes == 4
expect frame.meshes[name=player].on_screen == 1
expect frame.meshes[name=slime].count >= 1
# stomp: drop onto the first slime
do tp 13.5 4.5; step 40
expect game.stomps == 1
expect game.slimes == 3
expect game.deaths == 0
# side hit: run into it
do restart; step 2; tp 11.5 2; press D; step 40; release D
expect game.deaths == 1
expect game.slimes == 4
# a coin shows a popup with the count
do restart; step 2; tp 4.5 3; step 2
expect game.coins == 1
expect frame.texts[*].text contains "1/10"
do step 70
expect frame.texts[text=1/10] missing
```
