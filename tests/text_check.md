# ARC 0.10.12 (Vector Trader): the built-in font on whole pixels and whole sizes ("DOCK" centred in a 63-pixel button at x = 384.5)

```arc-check
game samples/demo/demo.c
args --size 844x390
do set text_test 1; step 2
expect frame.texts[text=DOCK].scale == 1
expect frame.texts[text=DOCK].box[0] == 400
```
