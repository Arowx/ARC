# HOSTLESS: web clicks are lost when the canvas is not at the page's top-left (ARC 0.8.3)

## What
On web, `arc_ui_button` never fires for a real mouse click whenever the canvas is offset in the page, which is
the default ARC shell's centred, letterboxed 16:9 canvas in any window that isn't exactly 16:9. Keys work, and
`ui_click` works. Music plays, so the game looks alive but the menu is dead.

## Why
`plat_web.c` registers `mouseup` on `EMSCRIPTEN_EVENT_TARGET_WINDOW`, and `on_mouse` calls
`pointer_at(e->targetX, e->targetY)` for every event. For a window target, Emscripten's targetX/Y are
relative to the window (page coordinates), not the canvas. So on release the mouse jumps by the canvas offset
(for example +32 px down in a 1024×640 window, or +128/+72 at 1536×864), `s.hover` is false on the release tick,
and `released_on` never sets. Mousedown and mousemove are on `#canvas`, so they're correct.

## Suggested fix
In `on_mouse`, map the position from `clientX/clientY` minus the canvas's `getBoundingClientRect()` for every
event, or at least for the window-registered mouseup. Keeping mouseup on the window is good (it catches a
release outside the canvas). Only the coordinates are wrong. For example:

```c
EM_JS(void, web_canvas_origin, (double *x, double *y), {
    var r = (Module.canvas || document.getElementById('canvas')).getBoundingClientRect();
    HEAPF64[x >> 3] = r.left; HEAPF64[y >> 3] = r.top;
});
/* in on_mouse, not locked: */
double ox, oy; web_canvas_origin(&ox, &oy); pointer_at(e->clientX - ox, e->clientY - oy);
```

## Repro / test
Headless Chrome (puppeteer), HOSTLESS web build, viewport 1024×640 (canvas at 0,32): move to the centre of
"New game", mouse down, wait 120 ms, mouse up → the state stays `title`. At 1024×576 (canvas at 0,0) → `play`.
A test case: click a `arc_ui_button` in a letterboxed page and expect it to fire.

## Workaround in HOSTLESS meanwhile
`web/shell.html` (the game's own page) re-sends window mouseups with canvas-relative coordinates from a
capture listener. Remove it once ARC has the fix.

## Reply from ARC 0.8.5 (phase G1): an automated test for the 0.8.4 fix

`tests/web_mouse_test.py` builds `tests/web_mouse.c` (an `arc_ui_button` in a fixed 960x540 game on the default page)
for the web and clicks the button in headless Chromium with real mouse events (move, down, 120 ms, up) in windows of
1000x900 (letterboxed), 1500x600 (pillarboxed), 800x700 and 1200x500 (scaled down and offset) and 960x540. The button
fires every time with 0.8.4's clientX/Y-minus-canvas-rect mapping; built with the 0.8.3 mapping (targetX/Y) it fails in
the letterboxed window (the release lands 20,180 px off), so the test guards the fix. No further change was needed.
