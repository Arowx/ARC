# Acceptance: racer starter

The rules this template promises. Run from the game folder: `python <ARC>/tools/arc_check.py tests/acceptance.md`.
Keep these passing while you change the game, and add a block like this for every feature you add.

```arc-check
game .
expect game.phase == "countdown"
expect game.lap == 0
expect frame.meshes[name=car].on_screen == 1
expect frame.meshes[name=ai_car].on_screen == 1
expect frame.texts[*].text contains "LAP 1/3"
# the race starts after the 3-second countdown
do step 180
expect game.phase == "race"
# the computer car drives the track checkpoint by checkpoint
do step 300
expect game.ai_next_cp > 3
# throttle: on the road and speeding up
do restart; go; press W; step 60; release W
expect game.speed > 8
expect game.on_track == true
# a lap counts when the car comes back to checkpoint 0 after the last one
do ai off; tp_cp 12; press W; step 90; release W
expect game.lap == 1
expect game.best_lap > 0
# the third lap finishes the race
do lap 2; tp_cp 12; press W; step 90; release W
expect game.phase == "finished"
expect frame.texts[*].text contains "FINISHED"
```
