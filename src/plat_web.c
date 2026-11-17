/* ARC web platform (Emscripten): WebGL2 canvas, DOM input, touch, the Gamepad API, Web Audio, localStorage saves.
 * Agents/tests can drive a page with:   Module.arcCommand("step 60; shot a.png; dump")
 * Output lines ("ARC ...") go to the browser console. URL query args become argv: page.html?--seed=4&--cmd=step%2060 */
#include "arc_internal.h"
#include <emscripten.h>
#include <emscripten/html5.h>

static struct { EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx; bool audio, resizable, hidpi, pads[ARC_MAX_PADS]; int min_w, min_h; long touch_id;  bool cap_want;   /* mouse look (0.6.7) */ } WB;
static float web_dpr(void) { double r = WB.hidpi ? emscripten_get_device_pixel_ratio() : 1.0; return r >= 1.0 && r <= 4.0 ? (float)r : 1.0f; }

static arc_key map_code(const char *code) {
    if (!strncmp(code, "Key", 3) && code[3] && !code[4]) return (arc_key)code[3];
    if (!strncmp(code, "Digit", 5) && code[5] && !code[6]) return (arc_key)code[5];
    if (code[0] == 'F' && code[1] >= '1' && code[1] <= '9') { int n = atoi(code + 1); if (n >= 1 && n <= 12) return (arc_key)(ARC_KEY_F1 + n - 1); }
    static const struct { const char *c; arc_key k; } m[] = {
        { "Space", ARC_KEY_SPACE }, { "Escape", ARC_KEY_ESCAPE }, { "Enter", ARC_KEY_ENTER }, { "Tab", ARC_KEY_TAB }, { "Backspace", ARC_KEY_BACKSPACE },
        { "ArrowLeft", ARC_KEY_LEFT }, { "ArrowRight", ARC_KEY_RIGHT }, { "ArrowUp", ARC_KEY_UP }, { "ArrowDown", ARC_KEY_DOWN },
        { "ShiftLeft", ARC_KEY_SHIFT }, { "ShiftRight", ARC_KEY_SHIFT }, { "ControlLeft", ARC_KEY_CTRL }, { "ControlRight", ARC_KEY_CTRL },
        { "AltLeft", ARC_KEY_ALT }, { "AltRight", ARC_KEY_ALT }, { "Backquote", ARC_KEY_GRAVE },
        { "Minus", ARC_KEY_MINUS }, { "Equal", ARC_KEY_EQUAL }, { "BracketLeft", ARC_KEY_LBRACKET }, { "BracketRight", ARC_KEY_RBRACKET },
        { "Backslash", ARC_KEY_BACKSLASH }, { "Semicolon", ARC_KEY_SEMICOLON }, { "Quote", ARC_KEY_APOSTROPHE }, { "Comma", ARC_KEY_COMMA },
        { "Period", ARC_KEY_PERIOD }, { "Slash", ARC_KEY_SLASH }, { "Insert", ARC_KEY_INSERT }, { "Delete", ARC_KEY_DELETE },
        { "Home", ARC_KEY_HOME }, { "End", ARC_KEY_END }, { "PageUp", ARC_KEY_PAGEUP }, { "PageDown", ARC_KEY_PAGEDOWN },
        { "NumpadEnter", ARC_KEY_KP_ENTER } };
    for (int i = 0; i < ARC__COUNTOF(m); i++) if (!strcmp(code, m[i].c)) return m[i].k;
    return ARC_KEY_NONE;
}

EMSCRIPTEN_KEEPALIVE void arc_web_audio_fill(float *out, int frames) { arc__audio_mix(out, frames); }
EMSCRIPTEN_KEEPALIVE void arc_web_command(const char *text) { arc__agent_push(text); }

EM_JS(void, web_audio_start, (void), {
    if (Module.arcAudio) { if (Module.arcAudio.state !== 'running') Module.arcAudio.resume(); return; }
    var AC = window.AudioContext || window.webkitAudioContext; if (!AC) return;
    var ctx = new AC({ sampleRate: 44100 }); Module.arcAudio = ctx;
    var node = ctx.createScriptProcessor(1024, 0, 2);
    var ptr = Module._malloc(1024 * 8);
    node.onaudioprocess = function(e) {
        Module._arc_web_audio_fill(ptr, 1024);
        var f = new Float32Array(HEAPF32.buffer, ptr, 2048), L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
        for (var i = 0; i < 1024; i++) { L[i] = f[i * 2]; R[i] = f[i * 2 + 1]; }
    };
    node.connect(ctx.destination);
});
EM_JS(void, web_expose_api, (void), {
    Module.arcCommand = function(s) { var n = lengthBytesUTF8(s) + 1, p = Module._malloc(n); stringToUTF8(s, p, n); Module._arc_web_command(p); Module._free(p); };
});

static bool on_key(int type, const EmscriptenKeyboardEvent *e, void *u) {
    (void)u; arc_key k = map_code(e->code);
    if (!WB.audio && !arc_arg_flag("mute")) { web_audio_start(); WB.audio = true; }
    if (k) arc__input_key(k, type == EMSCRIPTEN_EVENT_KEYDOWN);
    if (type == EMSCRIPTEN_EVENT_KEYDOWN && !e->ctrlKey && !e->metaKey) {   /* typed text: e.key is one character */
        const char *q = e->key; uint32_t cp = arc_utf8_next(&q);
        if (cp && !*q) arc__input_char(cp);
    }
    return k != ARC_KEY_NONE && k != ARC_KEY_F5 && k != ARC_KEY_F12;   /* swallow game keys, keep reload/devtools */
}
/* 0.8.4 (HOSTLESS's request): the canvas's top-left on the page. Mouse positions are taken from clientX/Y minus this
   for every mouse event: mouseup is registered on the window (so a release outside the canvas still counts), and a
   window target's targetX/Y are page coordinates, so on a centred, letterboxed canvas the release landed off the
   button and arc_ui buttons never fired */
EM_JS(void, web_canvas_origin, (double *x, double *y), {
    var c = Module['canvas'] || document.getElementById('canvas');
    var r = c ? c.getBoundingClientRect() : { left: 0, top: 0 };
    HEAPF64[x >> 3] = r.left; HEAPF64[y >> 3] = r.top;
});
static void pointer_at(double x, double y) {           /* CSS pixels on the canvas -> render pixels */
    double cw, ch; emscripten_get_element_css_size("#canvas", &cw, &ch);
    float sx = cw > 0 ? (float)arc_width() / (float)cw : 1, sy = ch > 0 ? (float)arc_height() / (float)ch : 1;
    arc__input_mouse((float)x * sx, (float)y * sy);
}
/* mouse look: the browser's pointer lock. It needs a user gesture, so it is asked for at once (deferred to the next
   event) and again on each click while wanted; the browser lets go on Esc, and arc_mouse_captured() then says so */
static bool web_locked(void) { EmscriptenPointerlockChangeEvent st; return emscripten_get_pointerlock_status(&st) == EMSCRIPTEN_RESULT_SUCCESS && st.isActive; }
static void web_mouse_capture(bool on) {
    WB.cap_want = on;
    if (on && !web_locked()) emscripten_request_pointerlock("#canvas", 1);
    else if (!on && web_locked()) emscripten_exit_pointerlock();
}
static bool web_mouse_captured(void) { return web_locked(); }
static bool on_mouse(int type, const EmscriptenMouseEvent *e, void *u) {
    (void)u;
    if (web_locked()) {
        if (type == EMSCRIPTEN_EVENT_MOUSEMOVE) {
            double cw, ch; emscripten_get_element_css_size("#canvas", &cw, &ch);
            float sx = cw > 0 ? (float)arc_width() / (float)cw : 1, sy = ch > 0 ? (float)arc_height() / (float)ch : 1;
            arc__input_mouse_rel((float)e->movementX * sx, (float)e->movementY * sy);
        }
    } else { double ox = 0, oy = 0; web_canvas_origin(&ox, &oy); pointer_at((double)e->clientX - ox, (double)e->clientY - oy); }
    if (type == EMSCRIPTEN_EVENT_MOUSEDOWN && WB.cap_want && !web_locked()) emscripten_request_pointerlock("#canvas", 0);
    if (type == EMSCRIPTEN_EVENT_MOUSEMOVE && (e->movementX || e->movementY)) arc__input_touch_mode(false);   /* a real mouse again */
    if (type == EMSCRIPTEN_EVENT_MOUSEDOWN || type == EMSCRIPTEN_EVENT_MOUSEUP) {
        bool d = type == EMSCRIPTEN_EVENT_MOUSEDOWN;
        if (d && !WB.audio && !arc_arg_flag("mute")) { web_audio_start(); WB.audio = true; }
        arc__input_key(e->button == 0 ? ARC_MOUSE_LEFT : e->button == 2 ? ARC_MOUSE_RIGHT : ARC_MOUSE_MIDDLE, d);
    }
    return true;
}
/* touch: the first finger is the mouse and its left button; the rest are ignored */
static bool on_touch(int type, const EmscriptenTouchEvent *e, void *u) {
    (void)u;
    if (!WB.audio && !arc_arg_flag("mute")) { web_audio_start(); WB.audio = true; }
    for (int i = 0; i < e->numTouches; i++) {
        const EmscriptenTouchPoint *t = &e->touches[i];
        if (!t->isChanged) continue;
        if (type == EMSCRIPTEN_EVENT_TOUCHSTART && WB.touch_id < 0) {
            WB.touch_id = t->identifier; arc__input_touch_mode(true); pointer_at(t->targetX, t->targetY); arc__input_key(ARC_MOUSE_LEFT, true);
        } else if (t->identifier == WB.touch_id) {
            pointer_at(t->targetX, t->targetY);
            if (type == EMSCRIPTEN_EVENT_TOUCHEND || type == EMSCRIPTEN_EVENT_TOUCHCANCEL) { arc__input_key(ARC_MOUSE_LEFT, false); WB.touch_id = -1; }
        }
    }
    return true;                                       /* no emulated mouse events or page scrolling */
}
static bool on_wheel(int type, const EmscriptenWheelEvent *e, void *u) { (void)type; (void)u; arc__input_wheel(e->deltaY < 0 ? 1.0f : -1.0f); return true; }
static bool on_blur(int type, const EmscriptenFocusEvent *e, void *u) { (void)type; (void)e; (void)u; arc__input_clear(); return false; }

EM_JS(void, web_fill_window, (void), {
    var c = Module.canvas || document.getElementById('canvas');
    var r = c.getBoundingClientRect();                 /* a page that already sizes the canvas to the window keeps its CSS */
    if (r.width >= window.innerWidth - 1 && r.height >= window.innerHeight - 1) return;
    c.style.width = '100vw'; c.style.height = '100vh'; c.style.maxWidth = 'none'; c.style.maxHeight = 'none'; c.style.aspectRatio = 'auto';
});
EM_JS(void, web_set_title, (const char *t), { document.title = UTF8ToString(t); });
/* 0.10.1 the safe area (notches, rounded corners, browser bars) over the canvas, in canvas pixels: the shell's
   --arc-s* CSS variables carry env(safe-area-inset-*); a page without them gives 0 */
EM_JS(void, web_safe_insets, (float *out, double dpr), {
  try {
    var st = getComputedStyle(document.documentElement), c = document.getElementById('canvas'); if (!c) return;
    var r = c.getBoundingClientRect(), vw = window.innerWidth, vh = window.innerHeight;
    var l = parseFloat(st.getPropertyValue('--arc-sl')) || 0, t = parseFloat(st.getPropertyValue('--arc-st')) || 0;
    var rr = parseFloat(st.getPropertyValue('--arc-sr')) || 0, b = parseFloat(st.getPropertyValue('--arc-sb')) || 0;
    var v = [Math.max(0, l - r.left), Math.max(0, t - r.top), Math.max(0, rr - (vw - r.right)), Math.max(0, b - (vh - r.bottom))];
    for (var i = 0; i < 4; i++) HEAPF32[(out >> 2) + i] = v[i] * dpr;
  } catch (e) {}
});
/* the backbuffer for a canvas of cw x ch CSS pixels (0.8.2, MechBattleArc's request): CSS size x dpr, scaled up
   uniformly when a side would fall below its minimum, so the browser never stretches the frame one way (text squashed,
   the mouse off). Pointer mapping divides by the CSS size per axis, so it stays exact. Capped at 8192 a side. */
static void web_fit_size(double cw, double ch, int *w, int *h) {
    double bw = cw * arc__dpi, bh = ch * arc__dpi, k = 1.0;
    double mw = WB.min_w > 0 ? WB.min_w : 64, mh = WB.min_h > 0 ? WB.min_h : 64;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;
    if (mw / bw > k) k = mw / bw;
    if (mh / bh > k) k = mh / bh;
    double big = (bw > bh ? bw : bh) * k;
    if (big > 8192.0) k *= 8192.0 / big;               /* a very thin page: keep the shape, give up on the minimum */
    *w = (int)(bw * k + 0.5); *h = (int)(bh * k + 0.5);
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
}
static bool web_init(const arc_app *app, int w, int h) {
    if (app->title && *app->title) web_set_title(app->title);   /* the tab shows the game's name */
    WB.resizable = app->resizable; WB.min_w = app->min_width; WB.min_h = app->min_height; WB.touch_id = -1; WB.hidpi = app->hidpi;
    arc__dpi = web_dpr();
    if (WB.resizable) {                                /* the canvas fills the page and the game follows its size */
        web_fill_window();
        double cw, ch; emscripten_get_element_css_size("#canvas", &cw, &ch);
        if (cw >= 1 && ch >= 1) { web_fit_size(cw, ch, &w, &h); arc__set_size(w, h); }
    } else if (arc__dpi > 1) {                         /* fixed size: the same layout, drawn with more pixels */
        w = (int)((float)w * arc__dpi + 0.5f); h = (int)((float)h * arc__dpi + 0.5f); arc__set_size(w, h);
    }
    emscripten_set_canvas_element_size("#canvas", w, h);
    EmscriptenWebGLContextAttributes at; emscripten_webgl_init_context_attributes(&at);
    at.majorVersion = 2; at.minorVersion = 0; at.depth = 1; at.antialias = arc__msaa >= 2 && arc__depth_mode == ARC_DEPTH_STANDARD;   /* reversed-Z: ARC multisamples its own screen */ at.alpha = 0; at.preserveDrawingBuffer = 0;
    WB.ctx = emscripten_webgl_create_context("#canvas", &at);
    if (WB.ctx <= 0) { fprintf(stderr, "ARC error WebGL2 is not available\n"); return false; }
    emscripten_webgl_make_context_current(WB.ctx);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, 1, on_key);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, 1, on_key);
    emscripten_set_mousedown_callback("#canvas", NULL, 1, on_mouse);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, 1, on_mouse);
    emscripten_set_mousemove_callback("#canvas", NULL, 1, on_mouse);
    emscripten_set_wheel_callback("#canvas", NULL, 1, on_wheel);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, 1, on_blur);
    emscripten_set_touchstart_callback("#canvas", NULL, 1, on_touch);
    emscripten_set_touchend_callback("#canvas", NULL, 1, on_touch);
    emscripten_set_touchmove_callback("#canvas", NULL, 1, on_touch);
    emscripten_set_touchcancel_callback("#canvas", NULL, 1, on_touch);
    web_expose_api();
    arc__audio_init(44100);
    return arc__gfx_init(&arc__backend_gl, w, h);
}
static void web_pad_poll(void) {   /* 0.10.5: pad N = the browser's gamepad index N (up to 4) */
    if (emscripten_sample_gamepad_data() != EMSCRIPTEN_RESULT_SUCCESS) return;
    int n = emscripten_get_num_gamepads();
    /* the browser's "standard" layout */
    static const arc_key b[17] = { ARC_PAD_A, ARC_PAD_B, ARC_PAD_X, ARC_PAD_Y, ARC_PAD_LB, ARC_PAD_RB, ARC_PAD_LT, ARC_PAD_RT, ARC_PAD_BACK,
                                   ARC_PAD_START, ARC_PAD_LSTICK, ARC_PAD_RSTICK, ARC_PAD_UP, ARC_PAD_DOWN, ARC_PAD_LEFT, ARC_PAD_RIGHT, ARC_PAD_GUIDE };
    for (int s = 0; s < ARC_MAX_PADS; s++) {
        EmscriptenGamepadEvent g;
        bool on = s < n && emscripten_get_gamepad_status(s, &g) == EMSCRIPTEN_RESULT_SUCCESS && g.connected;
        if (on != WB.pads[s]) { arc__input_pad_slot(s, on); WB.pads[s] = on; }
        if (!on) continue;
        for (int i = 0; i < 17 && i < g.numButtons; i++) arc__input_pad_button(s, b[i], g.digitalButton[i] != 0);
        for (int a = 0; a < 4 && a < g.numAxes; a++) { float v = (float)g.axis[a]; arc__input_pad_axis(s, (arc_axis)(ARC_AXIS_LX + a), fabsf(v) < 0.08f ? 0.0f : v); }
        if (g.numButtons > 7) { arc__input_pad_axis(s, ARC_AXIS_LT, (float)g.analogButton[6]); arc__input_pad_axis(s, ARC_AXIS_RT, (float)g.analogButton[7]); }
    }
}
static void web_poll(void) {
    web_pad_poll();
    { static int n; if ((n++ % 30) == 0) { extern float arc__safe_insets[4]; float s4[4] = { 0, 0, 0, 0 }; web_safe_insets(s4, (double)arc__dpi); if (!arc_arg_value("safe-area")) memcpy(arc__safe_insets, s4, sizeof s4); } }   /* 0.10.1 */
    if (WB.resizable) {
        double cw, ch; emscripten_get_element_css_size("#canvas", &cw, &ch);
        arc__dpi = web_dpr();                          /* the page may have moved to another screen, or been zoomed */
        int w = 0, h = 0; web_fit_size(cw, ch, &w, &h);
        if (cw >= 1 && ch >= 1 && (w != arc_width() || h != arc_height())) { emscripten_set_canvas_element_size("#canvas", w, h); arc__set_size(w, h); }
    }
}
static void web_toggle_fullscreen(void) {
    EmscriptenFullscreenChangeEvent st;
    if (emscripten_get_fullscreen_status(&st) == EMSCRIPTEN_RESULT_SUCCESS && st.isFullscreen) emscripten_exit_fullscreen();
    else emscripten_request_fullscreen("#canvas", 1);  /* browsers allow it only inside an input event: deferred to the next one */
}
static bool web_fullscreen(void) { EmscriptenFullscreenChangeEvent st; return emscripten_get_fullscreen_status(&st) == EMSCRIPTEN_RESULT_SUCCESS && st.isFullscreen; }

/* saves in localStorage: valid UTF-8 text is stored as is (so existing JS saves stay readable), anything else as base64 */
EM_JS(int, web_ls_set, (const char *key, const uint8_t *data, int size), {
    var bytes = HEAPU8.slice(data, data + size), text = null;
    try { text = new TextDecoder('utf-8', { fatal: true }).decode(bytes); if (text.indexOf('\0') >= 0 || text.indexOf('arc64:') === 0) text = null; } catch (e) { text = null; }
    if (text === null) { var b = ""; for (var i = 0; i < bytes.length; i++) b += String.fromCharCode(bytes[i]); text = 'arc64:' + btoa(b); }
    try { localStorage.setItem(UTF8ToString(key), text); return 1; } catch (e) { return 0; }
});
EM_JS(uint8_t *, web_ls_get, (const char *key, int *size_out), {
    var v = null; try { v = localStorage.getItem(UTF8ToString(key)); } catch (e) {}
    if (v === null) return 0;
    var bytes;
    if (v.indexOf('arc64:') === 0) { var b = atob(v.substring(6)); bytes = new Uint8Array(b.length); for (var i = 0; i < b.length; i++) bytes[i] = b.charCodeAt(i); }
    else bytes = new TextEncoder().encode(v);
    var p = _malloc(bytes.length + 1); HEAPU8.set(bytes, p); HEAPU8[p + bytes.length] = 0; HEAP32[size_out >> 2] = bytes.length; return p;
});
EM_JS(int, web_ls_remove, (const char *key), { try { var k = UTF8ToString(key), had = localStorage.getItem(k) !== null; localStorage.removeItem(k); return had ? 1 : 0; } catch (e) { return 0; } });
/* 0.8.2 (MechBattleArc's request): keys are namespaced "<save_folder>/<key>" (save_folder defaults to the title, as on
   desktop), because itch.io serves every HTML5 game from one origin, so games shared one localStorage and overwrote each
   other's saves and arc_audio volumes. A key saved by an older build (bare) is read once and copied to the new key, so
   players keep their saves. Removing a save that also exists bare leaves a marker, so the old copy can't come back. */
#define WEB_GONE "arc:removed"
static const char *web_key(const char *key, char *buf, size_t n) {
    const char *name = G.app.save_folder && *G.app.save_folder ? G.app.save_folder : G.app.title && *G.app.title ? G.app.title : "arc_game";
    snprintf(buf, n, "%s/%s", name, key); return buf;
}
static bool web_gone(const uint8_t *p, int n) { return n == (int)sizeof WEB_GONE - 1 && memcmp(p, WEB_GONE, (size_t)n) == 0; }
static bool web_save(const char *key, const void *data, size_t size) {
    char k[512]; return web_ls_set(web_key(key, k, sizeof k), (const uint8_t *)data, (int)size) != 0;
}
static uint8_t *web_load(const char *key, arc_arena *a, size_t *size_out) {
    char k[512]; web_key(key, k, sizeof k);
    int n = 0; uint8_t *p = web_ls_get(k, &n);
    if (p && web_gone(p, n)) { free(p); return NULL; }
    if (!p) {   /* an older build's bare key: copy it to the new one */
        p = web_ls_get(key, &n); if (!p) return NULL;
        web_ls_set(k, p, n);
    }
    uint8_t *out = (uint8_t *)arc_arena_alloc(a, (size_t)n + 1, 16);
    if (out) { memcpy(out, p, (size_t)n + 1); if (size_out) *size_out = (size_t)n; }
    free(p); return out;
}
static bool web_save_remove(const char *key) {
    char k[512]; web_key(key, k, sizeof k);
    int n = 0; bool had = false, gone = false;
    uint8_t *p = web_ls_get(k, &n); if (p) { gone = web_gone(p, n); had = !gone; free(p); }
    uint8_t *b = web_ls_get(key, &n); bool bare = b != NULL; free(b);
    if (bare) { if (!gone) had = true; web_ls_set(k, (const uint8_t *)WEB_GONE, (int)sizeof WEB_GONE - 1); }   /* keep the old copy from coming back */
    else if (had) web_ls_remove(k);
    return had;
}
static void web_present(void) {}
static void web_shutdown(void) {}
static double web_time(void) { return emscripten_get_now() / 1000.0; }
static void web_sleep(double s) { (void)s; }
static const arc__platform plat_web = { "web", web_init, web_poll, web_present, web_shutdown, web_time, web_sleep,
                                        NULL, web_toggle_fullscreen, web_fullscreen, web_save, web_load, web_save_remove,
                                        web_mouse_capture, web_mouse_captured, NULL };   /* no threads: the loader slices on the main thread */

static void web_frame(void) {
    if (!arc__frame()) { arc__shutdown(); emscripten_cancel_main_loop(); }
}
int main(int argc, char **argv) {
    int rc = arc__startup(argc, argv, &plat_web);
    if (rc) return rc;
    emscripten_set_main_loop(web_frame, 0, 0);
    return 0;
}
