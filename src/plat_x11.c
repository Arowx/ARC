/* ARC Linux platform: Xlib window, GLX 3.3 core context, ALSA audio (loaded at runtime if present),
 * gamepads through evdev (/dev/input/event*), text input through XIM. */
#include "arc_internal.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <sys/ioctl.h>
#include <GL/glx.h>
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>

static struct { Display *dpy; Window win; GLXContext ctx; Atom wm_delete; int w, h; XIM im; XIC ic; bool fullscreen;
                bool cap_want, cap_on, focused; Cursor blank; } X;   /* cap: mouse look (0.6.7) */
static void x_apply_capture(void);

void *arc__gl_get_proc(const char *name) { return (void *)glXGetProcAddressARB((const GLubyte *)name); }

static arc_key map_key(KeySym k) {
    if (k >= XK_a && k <= XK_z) return (arc_key)('A' + (k - XK_a));
    if (k >= XK_A && k <= XK_Z) return (arc_key)('A' + (k - XK_A));
    if (k >= XK_0 && k <= XK_9) return (arc_key)('0' + (k - XK_0));
    if (k >= XK_F1 && k <= XK_F12) return (arc_key)(ARC_KEY_F1 + (k - XK_F1));
    switch (k) {
    case XK_space: return ARC_KEY_SPACE; case XK_Escape: return ARC_KEY_ESCAPE; case XK_Return: return ARC_KEY_ENTER;
    case XK_Tab: return ARC_KEY_TAB; case XK_BackSpace: return ARC_KEY_BACKSPACE;
    case XK_Left: return ARC_KEY_LEFT; case XK_Right: return ARC_KEY_RIGHT; case XK_Up: return ARC_KEY_UP; case XK_Down: return ARC_KEY_DOWN;
    case XK_Shift_L: case XK_Shift_R: return ARC_KEY_SHIFT; case XK_Control_L: case XK_Control_R: return ARC_KEY_CTRL;
    case XK_Alt_L: case XK_Alt_R: return ARC_KEY_ALT; case XK_grave: return ARC_KEY_GRAVE;
    case XK_minus: return ARC_KEY_MINUS; case XK_equal: return ARC_KEY_EQUAL; case XK_bracketleft: return ARC_KEY_LBRACKET;
    case XK_bracketright: return ARC_KEY_RBRACKET; case XK_backslash: return ARC_KEY_BACKSLASH; case XK_semicolon: return ARC_KEY_SEMICOLON;
    case XK_apostrophe: return ARC_KEY_APOSTROPHE; case XK_comma: return ARC_KEY_COMMA; case XK_period: return ARC_KEY_PERIOD;
    case XK_slash: return ARC_KEY_SLASH; case XK_Insert: return ARC_KEY_INSERT; case XK_Delete: return ARC_KEY_DELETE;
    case XK_Home: return ARC_KEY_HOME; case XK_End: return ARC_KEY_END; case XK_Prior: return ARC_KEY_PAGEUP; case XK_Next: return ARC_KEY_PAGEDOWN;
    case XK_KP_Enter: return ARC_KEY_KP_ENTER;
    default: return ARC_KEY_NONE;
    }
}

/* ---- gamepads through evdev: up to 4 devices with gamepad buttons and a left stick; pad N = the Nth found, kept
   while it stays connected (a new one takes the lowest free number) (0.10.5) ---- */
typedef struct { int fd; char path[32]; struct input_absinfo abs[ABS_CNT]; bool has_abs[ABS_CNT]; } x_pad;
static struct { x_pad p[ARC_MAX_PADS]; double next_scan; bool init; } P;
#define BIT_SET(arr, b) ((arr[(b) / (8 * sizeof(long))] >> ((b) % (8 * sizeof(long)))) & 1)
static void pad_close(int s) { if (P.p[s].fd >= 0) close(P.p[s].fd); P.p[s].fd = -1; P.p[s].path[0] = 0; arc__input_pad_slot(s, false); }
static void pad_scan(void) {
    for (int i = 0; i < 32; i++) {
        int s = -1; for (int k = 0; k < ARC_MAX_PADS && s < 0; k++) if (P.p[k].fd < 0) s = k;
        if (s < 0) return;                                /* every slot taken */
        char path[32]; snprintf(path, sizeof path, "/dev/input/event%d", i);
        bool have = false; for (int k = 0; k < ARC_MAX_PADS; k++) if (P.p[k].fd >= 0 && !strcmp(P.p[k].path, path)) have = true;
        if (have) continue;
        int fd = open(path, O_RDONLY | O_NONBLOCK); if (fd < 0) continue;
        unsigned long keys[KEY_CNT / (8 * sizeof(long)) + 1] = { 0 }, abs[ABS_CNT / (8 * sizeof(long)) + 1] = { 0 };
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys); ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs);
        if (!BIT_SET(keys, BTN_SOUTH) || !BIT_SET(abs, ABS_X)) { close(fd); continue; }
        x_pad *x = &P.p[s]; x->fd = fd; snprintf(x->path, sizeof x->path, "%s", path); memset(x->has_abs, 0, sizeof x->has_abs);
        for (int a = 0; a < ABS_CNT; a++) if (BIT_SET(abs, a) && ioctl(fd, EVIOCGABS(a), &x->abs[a]) == 0) x->has_abs[a] = true;
        arc__input_pad_slot(s, true);
    }
}
static float pad_norm(const x_pad *x, int code, int v) {
    const struct input_absinfo *ai = &x->abs[code]; float range = (float)(ai->maximum - ai->minimum); if (range <= 0) return 0;
    float t = (float)(v - ai->minimum) / range;                     /* 0..1 */
    if (code == ABS_Z || code == ABS_RZ) return t;                  /* triggers */
    float x2 = t * 2 - 1; return fabsf(x2) < 0.08f ? 0.0f : x2;      /* sticks, with a small dead zone */
}
static arc_key pad_button(int code) {
    switch (code) {   /* Linux gamepad API: BTN_NORTH is the top button (Y), BTN_WEST the left one (X) */
    case BTN_SOUTH: return ARC_PAD_A; case BTN_EAST: return ARC_PAD_B; case BTN_WEST: return ARC_PAD_X; case BTN_NORTH: return ARC_PAD_Y;
    case BTN_TL: return ARC_PAD_LB; case BTN_TR: return ARC_PAD_RB; case BTN_TL2: return ARC_PAD_LT; case BTN_TR2: return ARC_PAD_RT;
    case BTN_SELECT: return ARC_PAD_BACK; case BTN_START: return ARC_PAD_START; case BTN_MODE: return ARC_PAD_GUIDE;
    case BTN_THUMBL: return ARC_PAD_LSTICK; case BTN_THUMBR: return ARC_PAD_RSTICK;
    case BTN_DPAD_UP: return ARC_PAD_UP; case BTN_DPAD_DOWN: return ARC_PAD_DOWN; case BTN_DPAD_LEFT: return ARC_PAD_LEFT; case BTN_DPAD_RIGHT: return ARC_PAD_RIGHT;
    default: return ARC_KEY_NONE;
    }
}
static void pad_read(int s) {
    x_pad *x = &P.p[s]; struct input_event ev[64];
    for (;;) {
        ssize_t n = read(x->fd, ev, sizeof ev);
        if (n < 0) { if (errno != EAGAIN && errno != EINTR) pad_close(s); return; }
        for (int i = 0; i < (int)(n / (ssize_t)sizeof ev[0]); i++) {
            const struct input_event *e = &ev[i];
            if (e->type == EV_KEY) { arc_key k = pad_button(e->code); if (k) arc__input_pad_button(s, k, e->value != 0); }
            else if (e->type == EV_ABS && e->code < ABS_CNT && x->has_abs[e->code]) {
                switch (e->code) {
                case ABS_X: arc__input_pad_axis(s, ARC_AXIS_LX, pad_norm(x, e->code, e->value)); break;
                case ABS_Y: arc__input_pad_axis(s, ARC_AXIS_LY, pad_norm(x, e->code, e->value)); break;
                case ABS_RX: arc__input_pad_axis(s, ARC_AXIS_RX, pad_norm(x, e->code, e->value)); break;
                case ABS_RY: arc__input_pad_axis(s, ARC_AXIS_RY, pad_norm(x, e->code, e->value)); break;
                case ABS_Z: { float t = pad_norm(x, e->code, e->value); arc__input_pad_axis(s, ARC_AXIS_LT, t); arc__input_pad_button(s, ARC_PAD_LT, t > 0.5f); break; }
                case ABS_RZ: { float t = pad_norm(x, e->code, e->value); arc__input_pad_axis(s, ARC_AXIS_RT, t); arc__input_pad_button(s, ARC_PAD_RT, t > 0.5f); break; }
                case ABS_HAT0X: arc__input_pad_button(s, ARC_PAD_LEFT, e->value < 0); arc__input_pad_button(s, ARC_PAD_RIGHT, e->value > 0); break;
                case ABS_HAT0Y: arc__input_pad_button(s, ARC_PAD_UP, e->value < 0); arc__input_pad_button(s, ARC_PAD_DOWN, e->value > 0); break;
                default: break;
                }
            }
        }
        if (n < (ssize_t)sizeof ev) return;
    }
}
static void pad_poll(double now) {
    if (!P.init) { P.init = true; for (int s = 0; s < ARC_MAX_PADS; s++) P.p[s].fd = -1; }
    if (now >= P.next_scan) { P.next_scan = now + 2.0; pad_scan(); }   /* new pads (cheap when every slot is taken) */
    for (int s = 0; s < ARC_MAX_PADS; s++) if (P.p[s].fd >= 0) pad_read(s);
}

/* ---- ALSA, loaded with dlopen so the binary has no hard dependency ---- */
typedef struct _snd_pcm snd_pcm_t;
static struct {
    void *lib; snd_pcm_t *pcm; pthread_t thread; volatile int run;
    int (*open)(snd_pcm_t **, const char *, int, int);
    int (*set_params)(snd_pcm_t *, int, int, unsigned, unsigned, int, unsigned);
    long (*writei)(snd_pcm_t *, const void *, unsigned long);
    int (*recover)(snd_pcm_t *, int, int);
    int (*close)(snd_pcm_t *);
} A;
static void *audio_thread(void *arg) {
    (void)arg; enum { N = 512 };
    float mix[N * 2]; int16_t out[N * 2];
    while (A.run) {
        arc__audio_mix(mix, N);
        for (int i = 0; i < N * 2; i++) out[i] = (int16_t)(mix[i] * 32767.0f);
        long r = A.writei(A.pcm, out, N);
        if (r < 0) A.recover(A.pcm, (int)r, 1);
    }
    return NULL;
}
static void audio_start(void) {
    arc__audio_init(44100);
    if (arc_arg_flag("mute")) return;
    A.lib = dlopen("libasound.so.2", RTLD_NOW); if (!A.lib) return;
    A.open = (int (*)(snd_pcm_t **, const char *, int, int))dlsym(A.lib, "snd_pcm_open");
    A.set_params = (int (*)(snd_pcm_t *, int, int, unsigned, unsigned, int, unsigned))dlsym(A.lib, "snd_pcm_set_params");
    A.writei = (long (*)(snd_pcm_t *, const void *, unsigned long))dlsym(A.lib, "snd_pcm_writei");
    A.recover = (int (*)(snd_pcm_t *, int, int))dlsym(A.lib, "snd_pcm_recover");
    A.close = (int (*)(snd_pcm_t *))dlsym(A.lib, "snd_pcm_close");
    if (!A.open || !A.set_params || !A.writei || !A.recover || !A.close) return;
    /* SND_PCM_STREAM_PLAYBACK=0, SND_PCM_FORMAT_S16_LE=2, SND_PCM_ACCESS_RW_INTERLEAVED=3 */
    if (A.open(&A.pcm, "default", 0, 0) < 0) { A.pcm = NULL; return; }
    if (A.set_params(A.pcm, 2, 3, 2, 44100, 1, 50000) < 0) { A.close(A.pcm); A.pcm = NULL; return; }
    A.run = 1; pthread_create(&A.thread, NULL, audio_thread, NULL);
}
static void audio_stop(void) { if (A.run) { A.run = 0; pthread_join(A.thread, NULL); } if (A.pcm) A.close(A.pcm); A.pcm = NULL; }

typedef GLXContext (*glXCreateContextAttribsARBProc)(Display *, GLXFBConfig, GLXContext, Bool, const int *);

static bool x_init(const arc_app *app, int w, int h) {
    X.dpy = XOpenDisplay(NULL);
    if (!X.dpy) { fprintf(stderr, "ARC error cannot open X display (use --headless on servers)\n"); return false; }
    static int fb_attr[] = { GLX_X_RENDERABLE, True, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT, GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_RED_SIZE, 8,
                             GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, True, None };
    int n = 0; GLXFBConfig *cfgs = glXChooseFBConfig(X.dpy, DefaultScreen(X.dpy), fb_attr, &n);
    if (!cfgs || n == 0) { fprintf(stderr, "ARC error no suitable GLX framebuffer config\n"); return false; }
    GLXFBConfig cfg = cfgs[0]; XFree(cfgs);
    XVisualInfo *vi = glXGetVisualFromFBConfig(X.dpy, cfg);
    Window root = RootWindow(X.dpy, vi->screen);
    XSetWindowAttributes swa; memset(&swa, 0, sizeof swa);
    swa.colormap = XCreateColormap(X.dpy, root, vi->visual, AllocNone);
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask | FocusChangeMask;
    X.win = XCreateWindow(X.dpy, root, 0, 0, (unsigned)w, (unsigned)h, 0, vi->depth, InputOutput, vi->visual, CWColormap | CWEventMask, &swa);
    XFree(vi);
    XStoreName(X.dpy, X.win, app->title);
    XSizeHints *sh = XAllocSizeHints();                 /* fixed size unless the game asked to be resizable */
    if (sh) {
        sh->flags = PMinSize; sh->min_width = app->resizable ? (app->min_width > 0 ? app->min_width : 64) : w;
        sh->min_height = app->resizable ? (app->min_height > 0 ? app->min_height : 64) : h;
        if (!app->resizable) { sh->flags |= PMaxSize; sh->max_width = w; sh->max_height = h; }
        XSetWMNormalHints(X.dpy, X.win, sh); XFree(sh);
    }
    X.im = XOpenIM(X.dpy, NULL, NULL, NULL);            /* text input (UTF-8) */
    if (X.im) X.ic = XCreateIC(X.im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, X.win, XNFocusWindow, X.win, NULL);
    X.wm_delete = XInternAtom(X.dpy, "WM_DELETE_WINDOW", False); XSetWMProtocols(X.dpy, X.win, &X.wm_delete, 1);
    XMapWindow(X.dpy, X.win);
    glXCreateContextAttribsARBProc create = (glXCreateContextAttribsARBProc)glXGetProcAddressARB((const GLubyte *)"glXCreateContextAttribsARB");
    int ctx_attr[] = { GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 3, GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB, None };
    X.ctx = create ? create(X.dpy, cfg, NULL, True, ctx_attr) : NULL;
    if (!X.ctx) { fprintf(stderr, "ARC error OpenGL 3.3 core context not available\n"); return false; }
    glXMakeCurrent(X.dpy, X.win, X.ctx);
    typedef void (*swap_ext)(Display *, GLXDrawable, int); typedef int (*swap_mesa)(unsigned);
    swap_ext se = (swap_ext)glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalEXT");
    swap_mesa sm = (swap_mesa)glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalMESA");
    int vsync = arc_arg_flag("no-vsync") ? 0 : 1;
    if (se) se(X.dpy, X.win, vsync); else if (sm) sm((unsigned)vsync);
    X.w = w; X.h = h; X.focused = true;   /* until told otherwise (FocusOut) */
    {   /* where the pointer already is: games should not wait for the first movement to know */
        Window r, c; int rx, ry, wx, wy; unsigned mask;
        if (XQueryPointer(X.dpy, X.win, &r, &c, &rx, &ry, &wx, &wy, &mask)) arc__input_mouse((float)wx, (float)wy);
    }
    if (!arc__gfx_init(&arc__backend_gl, w, h)) return false;
    audio_start();
    return true;
}
static void x_poll(void) {
    while (XPending(X.dpy)) {
        XEvent e; XNextEvent(X.dpy, &e);
        if (XFilterEvent(&e, None)) continue;
        switch (e.type) {
        case KeyPress: case KeyRelease: {
            if (e.type == KeyRelease && XEventsQueued(X.dpy, QueuedAfterReading)) {   /* drop auto-repeat release/press pairs */
                XEvent nx; XPeekEvent(X.dpy, &nx);
                if (nx.type == KeyPress && nx.xkey.time == e.xkey.time && nx.xkey.keycode == e.xkey.keycode) { XNextEvent(X.dpy, &nx); break; }
            }
            arc_key k = map_key(XLookupKeysym(&e.xkey, 0)); if (k) arc__input_key(k, e.type == KeyPress);
            if (e.type == KeyPress) {                   /* typed text */
                char buf[64]; KeySym ks; int n;
                if (X.ic) { Status st; n = Xutf8LookupString(X.ic, &e.xkey, buf, (int)sizeof buf - 1, &ks, &st); if (st != XLookupChars && st != XLookupBoth) n = 0; }
                else n = XLookupString(&e.xkey, buf, (int)sizeof buf - 1, &ks, NULL);
                buf[n > 0 ? n : 0] = 0;
                if (X.ic) { for (const char *q = buf; *q;) arc__input_char(arc_utf8_next(&q)); }
                else for (int i = 0; i < n; i++) arc__input_char((unsigned char)buf[i]);   /* Latin-1 */
            }
            break; }
        case ButtonPress: case ButtonRelease: {
            bool d = e.type == ButtonPress; unsigned b = e.xbutton.button;
            if (b == 1) arc__input_key(ARC_MOUSE_LEFT, d); else if (b == 3) arc__input_key(ARC_MOUSE_RIGHT, d); else if (b == 2) arc__input_key(ARC_MOUSE_MIDDLE, d);
            else if (d && b == 4) arc__input_wheel(1); else if (d && b == 5) arc__input_wheel(-1);
            break; }
        case MotionNotify:
            if (X.cap_on) {
                int dx = e.xmotion.x - X.w / 2, dy = e.xmotion.y - X.h / 2;
                if (dx || dy) { arc__input_mouse_rel((float)dx, (float)dy); XWarpPointer(X.dpy, None, X.win, 0, 0, 0, 0, X.w / 2, X.h / 2); }
            } else arc__input_mouse((float)e.xmotion.x, (float)e.xmotion.y);
            break;
        case ConfigureNotify: X.w = e.xconfigure.width; X.h = e.xconfigure.height; arc__set_size(X.w, X.h); break;
        case FocusOut: arc__input_clear(); if (X.ic) XUnsetICFocus(X.ic); X.focused = false; x_apply_capture(); break;
        case FocusIn: if (X.ic) XSetICFocus(X.ic); X.focused = true; x_apply_capture(); break;
        case ClientMessage: if ((Atom)e.xclient.data.l[0] == X.wm_delete) arc__quit_requested = true; break;
        default: break;
        }
    }
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    pad_poll((double)ts.tv_sec + (double)ts.tv_nsec * 1e-9);
}
static void x_set_icon(const uint8_t *rgba, int w, int h) {
    size_t n = 2 + (size_t)w * h; long *d = (long *)arc__xalloc(sizeof(long) * n);   /* _NET_WM_ICON wants longs */
    d[0] = w; d[1] = h;
    for (size_t i = 0; i < (size_t)w * h; i++) { const uint8_t *p = rgba + i * 4; d[2 + i] = (long)((unsigned long)p[3] << 24 | (unsigned long)p[0] << 16 | (unsigned long)p[1] << 8 | p[2]); }
    XChangeProperty(X.dpy, X.win, XInternAtom(X.dpy, "_NET_WM_ICON", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)d, (int)n);
    XFlush(X.dpy); free(d);
}
static void x_toggle_fullscreen(void) {
    X.fullscreen = !X.fullscreen;
    XEvent ev; memset(&ev, 0, sizeof ev);
    ev.type = ClientMessage; ev.xclient.window = X.win; ev.xclient.message_type = XInternAtom(X.dpy, "_NET_WM_STATE", False);
    ev.xclient.format = 32; ev.xclient.data.l[0] = X.fullscreen ? 1 : 0; ev.xclient.data.l[1] = (long)XInternAtom(X.dpy, "_NET_WM_STATE_FULLSCREEN", False);
    ev.xclient.data.l[3] = 1;
    XSendEvent(X.dpy, DefaultRootWindow(X.dpy), False, SubstructureRedirectMask | SubstructureNotifyMask, &ev); XFlush(X.dpy);
}
static bool x_fullscreen(void) { return X.fullscreen; }
static void x_present(void) { glXSwapBuffers(X.dpy, X.win); }
static void x_shutdown(void) {
    audio_stop(); if (P.init) for (int s = 0; s < ARC_MAX_PADS; s++) pad_close(s);
    if (X.ic) XDestroyIC(X.ic);
    if (X.im) XCloseIM(X.im);
    if (X.dpy) { glXMakeCurrent(X.dpy, None, NULL); if (X.ctx) glXDestroyContext(X.dpy, X.ctx); XDestroyWindow(X.dpy, X.win); XCloseDisplay(X.dpy); }
}
static double x_time(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9; }
static void x_sleep(double s) { if (s > 0) usleep((useconds_t)(s * 1e6)); }
/* mouse look: the pointer grabbed into the window with a blank cursor, warped back to the centre after each move
   (the move to the centre itself is ignored); only while the window has the focus */
static void x_apply_capture(void) {
    bool on = X.cap_want && X.focused && X.dpy && X.win;
    if (on == X.cap_on) return;
    if (on) {
        if (!X.blank) {
            static char zero[8]; XColor c = { 0 };
            Pixmap pm = XCreateBitmapFromData(X.dpy, X.win, zero, 8, 8);
            X.blank = XCreatePixmapCursor(X.dpy, pm, pm, &c, &c, 0, 0); XFreePixmap(X.dpy, pm);
        }
        if (XGrabPointer(X.dpy, X.win, True, PointerMotionMask | ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync,
                         X.win, X.blank, CurrentTime) != GrabSuccess) return;
        XWarpPointer(X.dpy, None, X.win, 0, 0, 0, 0, X.w / 2, X.h / 2);
    } else XUngrabPointer(X.dpy, CurrentTime);
    XFlush(X.dpy);
    X.cap_on = on;
}
static void x_mouse_capture(bool on) { X.cap_want = on; x_apply_capture(); }
static bool x_mouse_captured(void) { return X.cap_on; }
/* 0.7.1 threads for the background loader (pthreads) */
typedef struct { void (*fn)(void *); void *arg; pthread_t t; } x_thread;
static void *x_thread_main(void *p) { x_thread *t = (x_thread *)p; t->fn(t->arg); return NULL; }
static void *x_thread_start(void (*fn)(void *), void *arg) {
    x_thread *t = (x_thread *)calloc(1, sizeof *t); if (!t) return NULL;
    t->fn = fn; t->arg = arg;
    if (pthread_create(&t->t, NULL, x_thread_main, t)) { free(t); return NULL; }
    return t;
}
static void x_thread_join(void *p) { x_thread *t = (x_thread *)p; pthread_join(t->t, NULL); free(t); }
static void *x_lock_create(void) { pthread_mutex_t *m = (pthread_mutex_t *)calloc(1, sizeof *m); if (m && pthread_mutex_init(m, NULL)) { free(m); m = NULL; } return m; }
static void x_lock(void *m) { pthread_mutex_lock((pthread_mutex_t *)m); }
static void x_unlock(void *m) { pthread_mutex_unlock((pthread_mutex_t *)m); }
static void x_lock_destroy(void *m) { pthread_mutex_destroy((pthread_mutex_t *)m); free(m); }
static void *x_cond_create(void) { pthread_cond_t *c = (pthread_cond_t *)calloc(1, sizeof *c); if (c && pthread_cond_init(c, NULL)) { free(c); c = NULL; } return c; }
static void x_wait(void *c, void *m) { pthread_cond_wait((pthread_cond_t *)c, (pthread_mutex_t *)m); }
static void x_wake_all(void *c) { pthread_cond_broadcast((pthread_cond_t *)c); }
static void x_cond_destroy(void *c) { pthread_cond_destroy((pthread_cond_t *)c); free(c); }
static int x_cpus(void) { long n = sysconf(_SC_NPROCESSORS_ONLN); return n > 0 ? (int)n : 1; }
static const arc__threads x_threads = { x_thread_start, x_thread_join, x_lock_create, x_lock, x_unlock, x_lock_destroy,
    x_cond_create, x_wait, x_wake_all, x_cond_destroy, x_cpus };
static const arc__platform plat_x11 = { "x11", x_init, x_poll, x_present, x_shutdown, x_time, x_sleep,
                                        x_set_icon, x_toggle_fullscreen, x_fullscreen, NULL, NULL, NULL, x_mouse_capture, x_mouse_captured, &x_threads };

int main(int argc, char **argv) { return arc__run(argc, argv, &plat_x11); }
