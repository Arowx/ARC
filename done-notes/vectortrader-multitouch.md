# Request from Vector Trader: more than one finger on touch screens

Vector Trader (a 3D space trader, being ported to ARC in `D:\Projects\3DSpaceGame\arc_port`) is mostly played on
phones. A player's feedback on the 1.0 web version: too hard on a phone. A flight game wants one thumb steering
while the other thumb fires or taps a button, and a pinch on the star charts.

**Now.** `arc_touch_mode()` says the last pointer was a finger, and the first finger drives the mouse and
`MOUSE_LEFT`. A second finger is ignored, so you can't steer and tap a button at the same time. The game works
round it for now (one-thumb steering, automatic fire, buttons only between moves), but it is the biggest limit for
any action game on a phone.

## Ask

Up to 10 touches, each with a stable id, read in `tick()` like the keys, and recorded and replayed like other input:

```c
#define ARC_TOUCH_MAX 10
typedef struct {
    int      id;          /* stable while the finger is down (0.. when reused) */
    float    x, y;        /* window pixels, same space as arc_mouse_x/y */
    float    start_x, start_y;
    bool     pressed;     /* went down this tick */
    bool     released;    /* lifted this tick (still listed for that tick) */
} arc_touch;

ARC_API int              arc_touch_count(void);          /* fingers listed this tick */
ARC_API const arc_touch *arc_touch_get(int i);           /* NULL past the end */
ARC_API const arc_touch *arc_touch_find(int id);
```

- The first finger keeps driving the mouse and `MOUSE_LEFT`, so existing games don't change.
- Web: `touchstart` / `touchmove` / `touchend` / `touchcancel` with `changedTouches`; prevent the page from
  scrolling or zooming while the canvas has a finger on it.
- UI kit: a button pressed by any finger (so a steering thumb doesn't block taps).
- Headless commands for tests: `touch down ID X Y`, `touch move ID X Y`, `touch up ID`.
- `ARC_HAS_MULTITOUCH`.

## Acceptance

```arc-check
game samples/demo/demo.c
do touch on; touch down 1 200 400; step 2; touch down 2 1100 600; step 2
expect input.touches == 2
do touch move 1 260 380; step 1
expect input.touch[id=1].x == 260
do touch up 2; step 1; step 1
expect input.touches == 1
```

(`input.touches` / `input.touch[...]` in the dump would be a nice addition so games can be checked without their own
dump fields; if that isn't wanted, the game will dump them itself.)

## Done in ARC 0.10.11

As asked: `ARC_TOUCH_MAX` 10, `arc_touch` with `id`, `x`, `y`, `start_x`, `start_y`, `pressed` and `released`, plus
`arc_touch_count`, `arc_touch_get` (in the order the fingers went down) and `arc_touch_find(id)`.

- **Ids:** small (0, 1, ...) and steady while a finger is down. A lifted finger's id is reused, but never in the same
  tick it was listed as released.
- **Pressed / released:** work like the keys. A lifted finger is listed for one more tick with `released`, at its last
  place. A tap shorter than a tick (down and up between two ticks) is still seen for one tick.
- **Coordinates:** window pixels, the same as the mouse before viewports.
- **The first finger** still drives the mouse and `MOUSE_LEFT`, so nothing changes for games that ignore the new
  calls.
- **Replays:** fingers are part of each tick's input, so recordings, replays, rewind, the timeline and play sessions
  include them (replay format 4; older replays still load).
- **Web:** `touchstart` / `touchmove` / `touchend` / `touchcancel`, every changed finger. ARC's page already stops
  scrolling and zooming on the canvas (`touch-action: none`, no page zoom). Tested in headless Chrome with real touch
  events (`tests/web_touch_test.js`): two fingers, a move, lifts. Phones themselves are the next test.
- **Windows touch screens** too (`WM_POINTER`).
- **UI kit:** a widget answers any finger that goes down and comes up on it, so a steering thumb doesn't block a tap.
  A finger that slides onto a button and lifts there doesn't press it.
- **Headless:** `touch down ID X Y`, `touch move ID X Y`, `touch up ID`. The first scripted finger is the mouse too, so
  `ui_click`-style taps work. `dump.input` has `touches` (fingers down), `touch_mode` and `touch[*]` (each with
  `pressed` / `released`). Your check passes as written: ARC's `tests/touch_check.md` adds checks of the start point,
  pressed and the one-tick release.
- Macro `ARC_HAS_MULTITOUCH` (1011).
- A pinch for the star charts is two fingers' distance: `start_*` against `x` / `y` of both gives the scale.
