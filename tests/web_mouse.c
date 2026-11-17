/* ARC 0.8.5: the web mouse check (HOSTLESS's request, done-notes/hostless-web-mouseup.md). A fixed 960x540 game with one
   arc_ui_button in the middle-right of the screen. The default page (web/shell.html) keeps the canvas 16:9 and centres
   it, so in a window of another shape it is letterboxed (bars above and below) or pillarboxed (bars left and right):
   the canvas is not at the page's top-left. tests/web_run.js --click "Press me" clicks the button with real mouse
   events at its page position (mousedown on the canvas, mouseup on the window), and this prints the result:
     "ARC test web click OK ..." when the button fired, "ARC test web click FAILED ..." when it did not within 5 s.
   Build: emcc ... tests/web_mouse.c src/arc.c --shell-file web/shell.html (see tests/relay_test.py / TESTING). */
#include "../include/arc.h"
#include <stdio.h>
static int clicks, presses, ready_at = 30; static arc_v2 down_at, up_at; static bool was_down;
static void tick(void) {
    arc_ui_begin("Web mouse", 560, 300, 260);
    arc_ui_label("Click the button");
    if (arc_ui_button("Press me")) clicks++;
    arc_ui_end();
    bool d = arc_key_down(ARC_MOUSE_LEFT);
    if (d && !was_down) { presses++; down_at = arc_mouse(); }
    if (!d && was_down) up_at = arc_mouse();
    was_down = d;
    uint64_t t = arc_tick_count();
    if ((int)t == ready_at) { printf("ARC ready-click\n"); fflush(stdout); }
    if (clicks && t > (uint64_t)ready_at) {
        printf("ARC test web click OK clicks=%d down=%.0f,%.0f up=%.0f,%.0f\n", clicks, down_at.x, down_at.y, up_at.x, up_at.y); fflush(stdout);
        arc_quit();
    } else if ((int)t == ready_at + 300) {
        printf("ARC test web click FAILED presses=%d down=%.0f,%.0f up=%.0f,%.0f\n", presses, down_at.x, down_at.y, up_at.x, up_at.y); fflush(stdout);
        arc_quit();
    }
}
static void draw(float alpha) { (void)alpha; arc_draw_rect(0, 0, 960, 540, 0x203040); }
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "web mouse", .width = 960, .height = 540, .tick = tick, .draw = draw };
}
