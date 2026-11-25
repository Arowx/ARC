# ARC 0.10.18 (#13): the demo is TOY BOX BLITZ: mouse look, Ludo art, sound, music, a font and juice

The request's block, in parts (a second `frame` in one block isn't read again). `frame.meshes[name=block].count` counts
the alphabet blocks drawn (the ring and the cover).

```arc-check
game samples/demo/demo.c
args --size 1280x720
do step 30
expect game.theme == "toybox"
expect game.art == true
expect frame.meshes[name=player].on_screen == 1
expect frame.meshes[name=block].count >= 8
expect frame.meshes[name=crayon].count >= 3
expect frame.meshes[name=drum].count >= 2
```

A click captures the pointer, the mouse turns the camera, W moves the way the camera faces; juice and audio are wired up.

```arc-check
game samples/demo/demo.c
args --size 1280x720
do step 30; click; step 2; mouse 900 360; step 5
expect game.captured == true
expect game.cam_yaw != 0
do press W; step 30; release W
expect game.score >= 0
do step 600
expect game.combo >= 0
expect game.music == true
expect frame.texts[*].text contains "SCORE"
```

Mouse sensitivity: 100 pixels at sensitivity 2 turn the camera 0.7 radians.

```arc-check
game samples/demo/demo.c
do set mouse.sensitivity 2; mouse 640 360; click; step 2; mouse 740 360; step 2
expect game.cam_yaw == -0.7
```

With the camera turned half round, W walks the other way (toward +z).

```arc-check
game samples/demo/demo.c
do god 1; look 180; press W; step 30; release W
expect game.player[2] > 3
```

ESC lets the pointer go; C (the top view) does too.

```arc-check
game samples/demo/demo.c
do mouse 640 360; click; step 2; tap ESC; step 2
expect game.captured == false
do click; step 2; tap C; step 2
expect game.captured == false
expect game.cam_mode == 1
```

A kill: a floating score, sparks and confetti, the combo starts, a moment of hit-stop and shake.

```arc-check
game samples/demo/demo.c
do spawn_at 0 -5 ant; tap SPACE; step 12
expect game.score == 10
expect game.kills == 1
expect game.combo == 1
expect game.particles > 20
expect frame.texts[*].text contains "+10"
```

The beetle still takes 4 hits and scores 50.

```arc-check
game samples/demo/demo.c
do god 1; spawn_at 0 -6 beetle; press SPACE; step 40; release SPACE; step 2
expect game.kills == 1
expect game.score == 50
```

Five kills in a row double the score of the fifth (the multiplier).

```arc-check
game samples/demo/demo.c
do god 1; tap D; spawn_at 2.5 0 ant; spawn_at 3.5 0 ant; spawn_at 4.5 0 ant; spawn_at 5.5 0 ant; spawn_at 6.5 0 ant; press SPACE; step 50; release SPACE
expect game.kills == 5
expect game.best_combo == 5
expect game.score == 60
```

A flea hops (a new kind).

```arc-check
game samples/demo/demo.c
do god 1; spawn_at 4 -9 flea; step 20
expect game.kinds.flea == 1
expect frame.meshes[name=flea].on_screen == 1
```

Without the art (`--no-assets`) the demo still runs, on code-made shapes.

```arc-check
game samples/demo/demo.c
args --no-assets
do step 30
expect game.art == false
expect frame.meshes[name=player].on_screen == 1
```

Snapshots hold everything (rewinds and replays).

```arc-check
game samples/demo/demo.c
do spawn_at 0 -5 ant; tap SPACE; step 12; state_check
expect game.kills == 1
```
