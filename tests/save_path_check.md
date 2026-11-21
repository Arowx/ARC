# ARC 0.10.12 (Vector Trader): a relative --save-dir in `args` is read from this file's folder, not build/

```arc-check
game samples/demo/demo.c
args --save-dir check_saves
do set save_test 1; step 2
expect game.saved == true
```
