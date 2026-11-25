# ARC 0.10.17 (MOLTEN BREACH): scroll panels: a height limit, clipped, scrolled by drag and wheel

The demo's `scroll_panel_test` opens a 300 x 200 pixel scroll panel (the UI at half size) holding 30 buttons
"Row 1".."Row 30".

```arc-check
game samples/demo/demo.c
args --size 844x390
do set scroll_panel_test 1; step 2; frame
expect frame.ui_panels[0].rect[3] == 200
expect frame.ui[*].label contains "Row 1"
expect frame.ui[label=Row 30] missing
expect game.scroll_content_h > 400
```

A 90-pixel upward drag scrolls later rows into view and presses nothing (the request's check).

```arc-check
game samples/demo/demo.c
args --size 844x390
do set scroll_panel_test 1; step 2; mouse 100 150; press MOUSE_LEFT; step 1; mouse 100 60; step 1; release MOUSE_LEFT; step 2; frame
expect frame.ui[*].label contains "Row 12"
expect game.scroll == 90
expect game.scroll_clicks == 0
```

A click (no movement) still presses the row under it, after scrolling.

```arc-check
game samples/demo/demo.c
args --size 844x390
do set scroll_panel_test 1; step 2; mouse 100 150; press MOUSE_LEFT; step 1; mouse 100 60; step 1; release MOUSE_LEFT; step 2; ui_click Row 12; step 2
expect game.scroll_clicks == 1
expect game.scroll_last == "Row 12"
```

The wheel over the panel scrolls it (20 pixels a step at half size), and the scroll stops at the end.

```arc-check
game samples/demo/demo.c
args --size 844x390
do set scroll_panel_test 1; step 2; mouse 100 150; wheel -5; step 2
expect game.scroll == 100
```

```arc-check
game samples/demo/demo.c
args --size 844x390
do set scroll_panel_test 1; step 2; mouse 100 150; wheel -100; step 2; frame
expect game.scroll_content_h == 498
expect game.scroll == 298
expect frame.ui[*].label contains "Row 30"
expect frame.ui[label=Row 1] missing
```

The wheel away from the panel does nothing.

```arc-check
game samples/demo/demo.c
args --size 844x390
do set scroll_panel_test 1; step 2; mouse 600 300; wheel -5; step 2
expect game.scroll == 0
```

Keyboard focus moving down past the panel's bottom scrolls the focused row into view.

```arc-check
game samples/demo/demo.c
args --size 844x390
do set scroll_panel_test 1; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; tap DOWN; step 2; step 2; frame
expect frame.ui[focused=true].label == "Row 30"
expect game.scroll > 0
```

The panel's visible box fits a phone-sized window, whatever its content.

```arc-check
game samples/demo/demo.c
args --size 844x390
do set scroll_panel_test 1; step 2
ui_fit 844x390 640x360
do step 1
```
