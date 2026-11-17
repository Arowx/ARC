/* ARC Windows platform: Win32 window + input, Direct3D 11 presentation, waveOut audio,
 * XInput gamepads (the DLL is loaded at runtime, so nothing extra is linked or shipped). */
#include "arc_internal.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

void arc__d3d11_set_window(HWND hwnd, int w, int h);
void arc__d3d11_resize(int w, int h);
void arc__d3d11_present(bool vsync);

static struct {
    HWND hwnd; bool vsync; LARGE_INTEGER freq; bool resizable; int min_w, min_h;
    wchar_t high_surrogate; bool fullscreen; WINDOWPLACEMENT placement; DWORD style; HICON icon;
    bool cap_want, cap_on, raw_seen;   /* mouse look (0.6.7): asked for / in effect / raw input arriving */
} W;

static arc_key map_vk(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') return (arc_key)vk;
    if (vk >= '0' && vk <= '9') return (arc_key)vk;
    if (vk >= VK_F1 && vk <= VK_F12) return (arc_key)(ARC_KEY_F1 + (vk - VK_F1));
    switch (vk) {
    case VK_SPACE: return ARC_KEY_SPACE; case VK_ESCAPE: return ARC_KEY_ESCAPE; case VK_RETURN: return ARC_KEY_ENTER;
    case VK_TAB: return ARC_KEY_TAB; case VK_BACK: return ARC_KEY_BACKSPACE;
    case VK_LEFT: return ARC_KEY_LEFT; case VK_RIGHT: return ARC_KEY_RIGHT; case VK_UP: return ARC_KEY_UP; case VK_DOWN: return ARC_KEY_DOWN;
    case VK_SHIFT: return ARC_KEY_SHIFT; case VK_CONTROL: return ARC_KEY_CTRL; case VK_MENU: return ARC_KEY_ALT; case VK_OEM_3: return ARC_KEY_GRAVE;
    case VK_OEM_MINUS: return ARC_KEY_MINUS; case VK_OEM_PLUS: return ARC_KEY_EQUAL; case VK_OEM_4: return ARC_KEY_LBRACKET;
    case VK_OEM_6: return ARC_KEY_RBRACKET; case VK_OEM_5: return ARC_KEY_BACKSLASH; case VK_OEM_1: return ARC_KEY_SEMICOLON;
    case VK_OEM_7: return ARC_KEY_APOSTROPHE; case VK_OEM_COMMA: return ARC_KEY_COMMA; case VK_OEM_PERIOD: return ARC_KEY_PERIOD;
    case VK_OEM_2: return ARC_KEY_SLASH; case VK_INSERT: return ARC_KEY_INSERT; case VK_DELETE: return ARC_KEY_DELETE;
    case VK_HOME: return ARC_KEY_HOME; case VK_END: return ARC_KEY_END; case VK_PRIOR: return ARC_KEY_PAGEUP; case VK_NEXT: return ARC_KEY_PAGEDOWN;
    default: return ARC_KEY_NONE;
    }
}

/* mouse look: raw mouse movement (Raw Input), the cursor hidden and clipped to the window; only while the window has
   the focus, so alt-tab and other windows behave as usual */
static void w_clip(void) {
    RECT r; GetClientRect(W.hwnd, &r); MapWindowPoints(W.hwnd, NULL, (POINT *)&r, 2); ClipCursor(&r);
}
static void w_center(void) {   /* the cursor to the middle of the window (the fallback below measures from there) */
    RECT r; GetClientRect(W.hwnd, &r); POINT c = { (r.right - r.left) / 2, (r.bottom - r.top) / 2 }; ClientToScreen(W.hwnd, &c); SetCursorPos(c.x, c.y);
}
static void w_apply_capture(void) {
    bool on = W.cap_want && W.hwnd && GetForegroundWindow() == W.hwnd;
    if (on == W.cap_on) { if (on) w_clip(); return; }
    W.cap_on = on;
    RAWINPUTDEVICE rid = { 0x01, 0x02, on ? 0u : (DWORD)RIDEV_REMOVE, on ? W.hwnd : NULL };   /* generic desktop: mouse */
    RegisterRawInputDevices(&rid, 1, sizeof rid);
    if (on) { w_clip(); while (ShowCursor(FALSE) >= 0) {} w_center(); }
    else { ClipCursor(NULL); while (ShowCursor(TRUE) < 0) {} }
}
static void w_mouse_capture(bool on) { W.cap_want = on; w_apply_capture(); }
static bool w_mouse_captured(void) { return W.cap_on; }
static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE: arc__quit_requested = true; return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP: {
        arc_key k = map_vk(wp);
        if (k == ARC_KEY_ENTER && (lp & (1 << 24))) k = ARC_KEY_KP_ENTER;      /* extended Enter = the keypad one */
        if (k) arc__input_key(k, msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        if (wp == VK_F4 && (msg == WM_SYSKEYDOWN)) arc__quit_requested = true;
        return 0; }
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: arc__input_key(ARC_MOUSE_LEFT, msg == WM_LBUTTONDOWN); if (msg == WM_LBUTTONDOWN) SetCapture(h); else ReleaseCapture(); return 0;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: arc__input_key(ARC_MOUSE_RIGHT, msg == WM_RBUTTONDOWN); return 0;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: arc__input_key(ARC_MOUSE_MIDDLE, msg == WM_MBUTTONDOWN); return 0;
    case WM_MOUSEMOVE:
        if (!W.cap_on) arc__input_mouse((float)(short)LOWORD(lp), (float)(short)HIWORD(lp));
        else if (!W.raw_seen) {   /* no raw input (some drivers, remote desktops, Wine): movement from the centre, then back */
            RECT r; GetClientRect(h, &r); int cx = (r.right - r.left) / 2, cy = (r.bottom - r.top) / 2;
            int dx = (short)LOWORD(lp) - cx, dy = (short)HIWORD(lp) - cy;
            if (dx || dy) { arc__input_mouse_rel((float)dx, (float)dy); w_center(); }
        }
        return 0;
    case WM_INPUT: if (W.cap_on) {
        RAWINPUT ri; UINT sz = sizeof ri;
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
            ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
            { if (ri.data.mouse.lLastX || ri.data.mouse.lLastY) W.raw_seen = true; if (W.raw_seen) arc__input_mouse_rel((float)ri.data.mouse.lLastX, (float)ri.data.mouse.lLastY); }
        }
        return DefWindowProcW(h, msg, wp, lp);
    case WM_SETFOCUS: case WM_MOVE: w_apply_capture(); return 0;
    case WM_MOUSEWHEEL: arc__input_wheel((float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA); return 0;
    case WM_KILLFOCUS: arc__input_clear();
        if (W.cap_on) { W.cap_on = false; ClipCursor(NULL); while (ShowCursor(TRUE) < 0) {} RAWINPUTDEVICE rid = { 0x01, 0x02, RIDEV_REMOVE, NULL }; RegisterRawInputDevices(&rid, 1, sizeof rid); }
        return 0;
    case WM_CHAR: {                                       /* typed text, UTF-16 (pairs for characters past U+FFFF) */
        wchar_t c = (wchar_t)wp;
        if (c >= 0xD800 && c <= 0xDBFF) { W.high_surrogate = c; return 0; }
        if (c >= 0xDC00 && c <= 0xDFFF) { if (W.high_surrogate) arc__input_char(0x10000u + (((uint32_t)W.high_surrogate - 0xD800u) << 10) + ((uint32_t)c - 0xDC00u)); W.high_surrogate = 0; return 0; }
        arc__input_char((uint32_t)c); return 0; }
    case WM_GETMINMAXINFO: if (W.resizable && (W.min_w > 0 || W.min_h > 0)) {
        RECT r = { 0, 0, W.min_w > 0 ? W.min_w : 64, W.min_h > 0 ? W.min_h : 64 }; AdjustWindowRect(&r, (DWORD)GetWindowLongW(h, GWL_STYLE), FALSE);
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = r.right - r.left; ((MINMAXINFO *)lp)->ptMinTrackSize.y = r.bottom - r.top; }
        return 0;
    case WM_SIZE: if (wp != SIZE_MINIMIZED) { int w = LOWORD(lp), hh = HIWORD(lp); arc__set_size(w, hh); arc__d3d11_resize(w, hh); if (W.cap_on) w_clip(); } return 0;
    default: return DefWindowProcW(h, msg, wp, lp);
    }
}

/* ---- XInput: up to 4 controllers; pad N = XInput player N (0.10.5) ---- */
typedef struct { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; } xi_pad;
typedef struct { DWORD packet; xi_pad pad; } xi_state;
typedef DWORD (WINAPI *xi_get_fn)(DWORD, xi_state *);
static struct { xi_get_fn get; bool on[4]; double next_scan[4]; bool tried; } XI;
static float xi_stick(SHORT v) { float x = v < 0 ? (float)v / 32768.0f : (float)v / 32767.0f; return fabsf(x) < 0.08f ? 0.0f : x; }
static void xi_poll(double now) {
    if (!XI.tried) {
        XI.tried = true;
        const char *dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
        for (int i = 0; i < 3 && !XI.get; i++) { HMODULE m = LoadLibraryA(dlls[i]); if (m) XI.get = (xi_get_fn)(void *)GetProcAddress(m, "XInputGetState"); }
    }
    if (!XI.get) return;
    static const struct { WORD mask; arc_key key; } map[] = {
        { 0x1000, ARC_PAD_A }, { 0x2000, ARC_PAD_B }, { 0x4000, ARC_PAD_X }, { 0x8000, ARC_PAD_Y }, { 0x0100, ARC_PAD_LB }, { 0x0200, ARC_PAD_RB },
        { 0x0020, ARC_PAD_BACK }, { 0x0010, ARC_PAD_START }, { 0x0040, ARC_PAD_LSTICK }, { 0x0080, ARC_PAD_RSTICK },
        { 0x0001, ARC_PAD_UP }, { 0x0002, ARC_PAD_DOWN }, { 0x0004, ARC_PAD_LEFT }, { 0x0008, ARC_PAD_RIGHT } };
    for (int slot = 0; slot < 4; slot++) {
        xi_state st;
        if (!XI.on[slot]) {                              /* asking an empty slot is slow: each one again every 2 s, staggered */
            if (now < XI.next_scan[slot]) continue;
            XI.next_scan[slot] = now + 2.0 + 0.5 * slot;
            if (XI.get((DWORD)slot, &st) != 0) continue;
            XI.on[slot] = true; arc__input_pad_slot(slot, true);
        } else if (XI.get((DWORD)slot, &st) != 0) { XI.on[slot] = false; XI.next_scan[slot] = now + 2.0; arc__input_pad_slot(slot, false); continue; }
        for (int i = 0; i < (int)(sizeof map / sizeof map[0]); i++) arc__input_pad_button(slot, map[i].key, (st.pad.buttons & map[i].mask) != 0);
        arc__input_pad_axis(slot, ARC_AXIS_LX, xi_stick(st.pad.lx)); arc__input_pad_axis(slot, ARC_AXIS_LY, -xi_stick(st.pad.ly));   /* ARC: +Y is down */
        arc__input_pad_axis(slot, ARC_AXIS_RX, xi_stick(st.pad.rx)); arc__input_pad_axis(slot, ARC_AXIS_RY, -xi_stick(st.pad.ry));
        arc__input_pad_axis(slot, ARC_AXIS_LT, (float)st.pad.lt / 255.0f); arc__input_pad_axis(slot, ARC_AXIS_RT, (float)st.pad.rt / 255.0f);
        arc__input_pad_button(slot, ARC_PAD_LT, st.pad.lt > 127); arc__input_pad_button(slot, ARC_PAD_RT, st.pad.rt > 127);
    }
}

/* ---- waveOut audio on its own thread ---- */
static struct { HWAVEOUT dev; WAVEHDR hdr[3]; int16_t buf[3][1024 * 2]; HANDLE ev, thread; volatile LONG run; } AU;
static DWORD WINAPI audio_thread(LPVOID p) {
    (void)p; float mix[1024 * 2];
    while (AU.run) {
        for (int i = 0; i < 3; i++) {
            if (AU.hdr[i].dwFlags & WHDR_INQUEUE) continue;
            arc__audio_mix(mix, 1024);
            for (int k = 0; k < 2048; k++) AU.buf[i][k] = (int16_t)(mix[k] * 32767.0f);
            waveOutWrite(AU.dev, &AU.hdr[i], sizeof(WAVEHDR));
        }
        WaitForSingleObject(AU.ev, 50);
    }
    return 0;
}
static void audio_start(void) {
    arc__audio_init(44100);
    if (arc_arg_flag("mute")) return;
    WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 2, 44100, 44100 * 4, 4, 16, 0 };
    AU.ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (waveOutOpen(&AU.dev, WAVE_MAPPER, &wf, (DWORD_PTR)AU.ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) { AU.dev = NULL; return; }
    for (int i = 0; i < 3; i++) {
        AU.hdr[i].lpData = (LPSTR)AU.buf[i]; AU.hdr[i].dwBufferLength = sizeof AU.buf[i];
        waveOutPrepareHeader(AU.dev, &AU.hdr[i], sizeof(WAVEHDR)); AU.hdr[i].dwFlags |= WHDR_DONE;
        AU.hdr[i].dwFlags &= ~WHDR_INQUEUE;
    }
    AU.run = 1; AU.thread = CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
}
static void audio_stop(void) {
    if (AU.run) { AU.run = 0; SetEvent(AU.ev); WaitForSingleObject(AU.thread, 1000); }
    if (AU.dev) { waveOutReset(AU.dev); for (int i = 0; i < 3; i++) waveOutUnprepareHeader(AU.dev, &AU.hdr[i], sizeof(WAVEHDR)); waveOutClose(AU.dev); AU.dev = NULL; }
}

static bool w_init(const arc_app *app, int w, int h) {
    SetProcessDPIAware();
    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc); wc.cbSize = sizeof wc; wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC; wc.lpfnWndProc = wndproc; wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = L"ARCWindow";
    /* 0.8.2 (MechBattleArc's request): the exe's own icon (resource 1, "1 ICON game.ico" in an .rc) for the title bar,
       taskbar and Alt-Tab, with a crisp small one; the generic icon when the exe has none */
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    W.resizable = app->resizable; W.min_w = app->min_width; W.min_h = app->min_height;
    RECT r = { 0, 0, w, h }; DWORD style = app->resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPEDWINDOW & ~(DWORD)(WS_THICKFRAME | WS_MAXIMIZEBOX));
    AdjustWindowRect(&r, style, FALSE);
    wchar_t title[256]; MultiByteToWideChar(CP_UTF8, 0, app->title, -1, title, 256);
    W.hwnd = CreateWindowExW(0, L"ARCWindow", title, style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    if (!W.hwnd) return false;
    ShowWindow(W.hwnd, SW_SHOW);
    RECT cr; GetClientRect(W.hwnd, &cr); w = cr.right - cr.left; h = cr.bottom - cr.top; arc__set_size(w, h);
    arc__d3d11_set_window(W.hwnd, w, h);
    POINT cp; if (GetCursorPos(&cp) && ScreenToClient(W.hwnd, &cp)) arc__input_mouse((float)cp.x, (float)cp.y);   /* where the pointer already is */
    W.vsync = !arc_arg_flag("no-vsync");
    timeBeginPeriod(1);
    if (!arc__gfx_init(&arc__backend_d3d11, w, h)) return false;
    audio_start();
    return true;
}
static void w_poll(void) {
    MSG m; while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) arc__quit_requested = true; TranslateMessage(&m); DispatchMessageW(&m); }
    LARGE_INTEGER c; if (!W.freq.QuadPart) QueryPerformanceFrequency(&W.freq); QueryPerformanceCounter(&c);
    xi_poll((double)c.QuadPart / (double)W.freq.QuadPart);
}
/* GDI is loaded at runtime so games link exactly what they linked before (no -lgdi32) */
typedef HBITMAP (WINAPI *create_dib_fn)(HDC, const BITMAPINFO *, UINT, void **, HANDLE, DWORD);
typedef HBITMAP (WINAPI *create_bitmap_fn)(int, int, UINT, UINT, const void *);
typedef BOOL (WINAPI *delete_object_fn)(HGDIOBJ);
static void w_set_icon(const uint8_t *rgba, int w, int h) {
    HMODULE gdi = LoadLibraryA("gdi32.dll"); if (!gdi) return;
    create_dib_fn CreateDIBSection_ = (create_dib_fn)(void *)GetProcAddress(gdi, "CreateDIBSection");
    create_bitmap_fn CreateBitmap_ = (create_bitmap_fn)(void *)GetProcAddress(gdi, "CreateBitmap");
    delete_object_fn DeleteObject_ = (delete_object_fn)(void *)GetProcAddress(gdi, "DeleteObject");
    if (!CreateDIBSection_ || !CreateBitmap_ || !DeleteObject_) return;
    BITMAPV5HEADER bi; memset(&bi, 0, sizeof bi);
    bi.bV5Size = sizeof bi; bi.bV5Width = w; bi.bV5Height = -h; bi.bV5Planes = 1; bi.bV5BitCount = 32; bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00; bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
    uint8_t *px = NULL; HDC dc = GetDC(NULL);
    HBITMAP color = CreateDIBSection_(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&px, NULL, 0); ReleaseDC(NULL, dc);
    HBITMAP mask = CreateBitmap_(w, h, 1, 1, NULL);
    if (!color || !mask || !px) { if (color) DeleteObject_(color); if (mask) DeleteObject_(mask); return; }
    for (int i = 0; i < w * h; i++) { px[i * 4] = rgba[i * 4 + 2]; px[i * 4 + 1] = rgba[i * 4 + 1]; px[i * 4 + 2] = rgba[i * 4]; px[i * 4 + 3] = rgba[i * 4 + 3]; }
    ICONINFO ii; memset(&ii, 0, sizeof ii); ii.fIcon = TRUE; ii.hbmMask = mask; ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii); DeleteObject_(color); DeleteObject_(mask);
    if (!icon) return;
    SendMessageW(W.hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon); SendMessageW(W.hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    if (W.icon) DestroyIcon(W.icon);
    W.icon = icon;
}
static void w_toggle_fullscreen(void) {           /* borderless window covering the monitor, and back */
    if (!W.fullscreen) {
        MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
        W.placement.length = sizeof W.placement; GetWindowPlacement(W.hwnd, &W.placement);
        W.style = (DWORD)GetWindowLongW(W.hwnd, GWL_STYLE);
        if (!GetMonitorInfoW(MonitorFromWindow(W.hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) return;
        SetWindowLongW(W.hwnd, GWL_STYLE, (LONG)((W.style & ~(DWORD)WS_OVERLAPPEDWINDOW) | WS_POPUP));
        SetWindowPos(W.hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        W.fullscreen = true;
    } else {
        SetWindowLongW(W.hwnd, GWL_STYLE, (LONG)W.style);
        SetWindowPlacement(W.hwnd, &W.placement);
        SetWindowPos(W.hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        W.fullscreen = false;
    }
}
static bool w_fullscreen(void) { return W.fullscreen; }
static void w_present(void) { arc__d3d11_present(W.vsync); }
static void w_shutdown(void) { if (W.cap_on) { ClipCursor(NULL); while (ShowCursor(TRUE) < 0) {} } audio_stop(); timeEndPeriod(1); if (W.hwnd) DestroyWindow(W.hwnd); if (W.icon) DestroyIcon(W.icon); }
static double w_time(void) { LARGE_INTEGER c; if (!W.freq.QuadPart) QueryPerformanceFrequency(&W.freq); QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)W.freq.QuadPart; }
static void w_sleep(double s) { if (s > 0) Sleep((DWORD)(s * 1000.0)); }
/* 0.7.1 threads for the background loader (Win32: CreateThread, SRW locks, condition variables) */
typedef struct { void (*fn)(void *); void *arg; HANDLE h; } w_thread;
static DWORD WINAPI w_thread_main(LPVOID p) { w_thread *t = (w_thread *)p; t->fn(t->arg); return 0; }
static void *w_thread_start(void (*fn)(void *), void *arg) {
    w_thread *t = (w_thread *)calloc(1, sizeof *t); if (!t) return NULL;
    t->fn = fn; t->arg = arg; t->h = CreateThread(NULL, 0, w_thread_main, t, 0, NULL);
    if (!t->h) { free(t); return NULL; }
    return t;
}
static void w_thread_join(void *p) { w_thread *t = (w_thread *)p; WaitForSingleObject(t->h, INFINITE); CloseHandle(t->h); free(t); }
static void *w_lock_create(void) { SRWLOCK *l = (SRWLOCK *)calloc(1, sizeof *l); if (l) InitializeSRWLock(l); return l; }
static void w_lock(void *l) { AcquireSRWLockExclusive((SRWLOCK *)l); }
static void w_unlock(void *l) { ReleaseSRWLockExclusive((SRWLOCK *)l); }
static void w_lock_destroy(void *l) { free(l); }
static void *w_cond_create(void) { CONDITION_VARIABLE *c = (CONDITION_VARIABLE *)calloc(1, sizeof *c); if (c) InitializeConditionVariable(c); return c; }
static void w_wait(void *c, void *l) { SleepConditionVariableSRW((CONDITION_VARIABLE *)c, (SRWLOCK *)l, INFINITE, 0); }
static void w_wake_all(void *c) { WakeAllConditionVariable((CONDITION_VARIABLE *)c); }
static void w_cond_destroy(void *c) { free(c); }
static int w_cpus(void) { SYSTEM_INFO si; GetSystemInfo(&si); return si.dwNumberOfProcessors > 0 ? (int)si.dwNumberOfProcessors : 1; }
static const arc__threads w_threads = { w_thread_start, w_thread_join, w_lock_create, w_lock, w_unlock, w_lock_destroy,
    w_cond_create, w_wait, w_wake_all, w_cond_destroy, w_cpus };
static const arc__platform plat_win32 = { "win32", w_init, w_poll, w_present, w_shutdown, w_time, w_sleep,
                                          w_set_icon, w_toggle_fullscreen, w_fullscreen, NULL, NULL, NULL, w_mouse_capture, w_mouse_captured, &w_threads };

int main(int argc, char **argv) {
    /* make stdout line-buffered so agents see ARC lines immediately even when piped */
    setvbuf(stdout, NULL, _IOLBF, 1 << 14);
    return arc__run(argc, argv, &plat_win32);
}
