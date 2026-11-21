# ARC 0.10.13 (Vector Trader): a music loop section. loop_test.qoa is 8 s; the loop is 2..6 s, so after 10 s of play
# the position is back inside the section (headless has no mixer: `--audio-sim` runs it at the tick rate)

```arc-check
game samples/demo/demo.c
args --audio-sim
do set music_loop_test 1; step 600
expect game.music_playing == 1
expect game.music_position >= 2
expect game.music_position < 6
```
