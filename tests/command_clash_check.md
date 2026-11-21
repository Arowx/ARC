# ARC 0.10.12 (Vector Trader): a game command named like an ARC command gets the line when ARC's can't use it

```arc-check
game samples/demo/demo.c
do set demo_setting 3; step 1
expect game.demo_setting == 3
do set line_test 1; step 1
expect frame.lines.thick >= 5
```
