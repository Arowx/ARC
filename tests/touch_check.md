# ARC 0.10.11 (Vector Trader): two fingers, one moved, one lifted (released for one tick), as input.* in the dump

```arc-check
game samples/demo/demo.c
do touch on; touch down 1 200 400; step 2; touch down 2 1100 600; step 2
expect input.touches == 2
expect input.touch[id=2].start_x == 1100
do touch move 1 260 380; step 1
expect input.touch[id=1].x == 260
expect input.touch[id=1].pressed == false
do touch up 2; step 1
expect input.touch[id=2].released == true
do step 1
expect input.touches == 1
expect input.touch[id=2] missing
```
