# Request (test fixture): AI brains, senses, shots and camera rigs (0.10.0)

**Why.** This file checks `samples/action` the way Droids' requests asked: brain states and senses in `dump`, the
`brain` and `cam` commands, tracers and the camera rig in `frame`.

**Ask.** Nothing; the sample already does this.

## Acceptance

```arc-check
game samples/action/action.c
do step 5
expect count(game.droids[*]) == 4
expect brains.droid[0].state == "PATROL"
expect count(senses.droids[*]) == 4
expect frame.camera.rig.kind == "follow"
do goto 9.5 2.5; face 90; step 30; fire; step 200
expect game.droids[0].knows == true
expect brains.droid[0].state != "PATROL"
do fire; step 1
expect frame.shots.tracers >= 1
expect frame.shots.decals >= 1
do view 0; step 40
expect frame.camera.rig.kind == "topdown"
expect game.blending == false
do brain droid 2 FLEE; step 2
expect brains.droid[2].state == "FLEE"
do cam shake 0.8; step 1
expect frame.camera.rig.shake > 0.5
do cam rig orbit; step 40
expect frame.camera.rig.kind == "orbit"
```
