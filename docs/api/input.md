# Input (sampled once per tick; deterministic and recordable)

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Input (sampled once per tick; deterministic and recordable)** · [quick reference](../QUICK.md) · [full API](../API.md)

```c
typedef enum { ARC_KEY_NONE = 0, ARC_KEY_A = 'A', ARC_KEY_B, ARC_KEY_C, ARC_KEY_D, ARC_KEY_E, ARC_KEY_F, ARC_KEY_G, ARC_KEY_H, ARC_KEY_I, ARC_KEY_J, ARC_KEY_K, ARC_KEY_L, ARC_KEY_M, ARC_KEY_N, ARC_KEY_O, ARC_KEY_P, ARC_KEY_Q, ARC_KEY_R, ARC_KEY_S, ARC_KEY_T, ARC_KEY_U, ARC_KEY_V, ARC_KEY_W, ARC_KEY_X, ARC_KEY_Y, ARC_KEY_Z, ARC_KEY_0 = '0', ARC_KEY_1, ARC_KEY_2, ARC_KEY_3, ARC_KEY_4, ARC_KEY_5, ARC_KEY_6, ARC_KEY_7, ARC_KEY_8, ARC_KEY_9, ARC_KEY_SPACE = ' ', ARC_KEY_ESCAPE = 128, ARC_KEY_ENTER, ARC_KEY_TAB, ARC_KEY_BACKSPACE, ARC_KEY_LEFT, ARC_KEY_RIGHT, ARC_KEY_UP, ARC_KEY_DOWN, ARC_KEY_SHIFT, ARC_KEY_CTRL, ARC_KEY_ALT, ARC_KEY_F1, ARC_KEY_F2, ARC_KEY_F3, ARC_KEY_F4, ARC_KEY_F5, ARC_KEY_F6, ARC_KEY_F7, ARC_KEY_F8, ARC_KEY_F9, ARC_KEY_F10, ARC_KEY_F11, ARC_KEY_F12, ARC_KEY_GRAVE, ARC_KEY_MINUS, ARC_KEY_EQUAL, ARC_KEY_LBRACKET, ARC_KEY_RBRACKET, ARC_KEY_BACKSLASH, ARC_KEY_SEMICOLON, ARC_KEY_APOSTROPHE, ARC_KEY_COMMA, ARC_KEY_PERIOD, ARC_KEY_SLASH, ARC_KEY_INSERT, ARC_KEY_DELETE, ARC_KEY_HOME, ARC_KEY_END, ARC_KEY_PAGEUP, ARC_KEY_PAGEDOWN, ARC_KEY_KP_ENTER, ARC_MOUSE_LEFT = 200, ARC_MOUSE_RIGHT, ARC_MOUSE_MIDDLE, ARC_PAD_A = 210, ARC_PAD_B, ARC_PAD_X, ARC_PAD_Y, ARC_PAD_LB, ARC_PAD_RB, ARC_PAD_LT, ARC_PAD_RT, ARC_PAD_BACK, ARC_PAD_START, ARC_PAD_LSTICK, ARC_PAD_RSTICK, ARC_PAD_UP, ARC_PAD_DOWN, ARC_PAD_LEFT, ARC_PAD_RIGHT, ARC_PAD_GUIDE, ARC_KEY_COUNT = 256 } arc_key;
ARC_API bool arc_key_down(arc_key k);    // held this tick
ARC_API bool arc_key_pressed(arc_key k);    // went down this tick
ARC_API bool arc_key_released(arc_key k);    // went up this tick
ARC_API arc_v2 arc_mouse(void);    // pixels, origin top-left
ARC_API arc_v2 arc_mouse_delta(void);    // pixels moved this tick
```

0.6.7 mouse look: while captured the pointer is hidden and held inside the window, and arc_mouse_delta() keeps
   reporting movement at any edge (arc_mouse() is then an unbounded virtual position: use only its changes). It is
   let go while the window is out of focus and taken back on return; on the web the browser needs a click first, so
   arc_mouse_captured() says whether it is in effect. Call it every frame with what the game wants (cheap).

```c
ARC_API void arc_mouse_capture(bool on);
ARC_API bool arc_mouse_captured(void);
ARC_API float arc_mouse_wheel(void);    // wheel steps this tick
ARC_API arc_key arc_key_from_name(const char *name);    // "A", "SPACE", "LEFT", "MOUSE_LEFT", "PAD_A"...
ARC_API const char *arc_key_name(arc_key k);
ARC_API arc_key arc_key_any_pressed(void);    // a keyboard key that went down this tick (for rebinding), else NONE
```

A press and release between two ticks still counts: the key is down for one tick.

```c
ARC_API const char *arc_text_input(void);    // UTF-8 text typed this tick ("" if none); for name entry, chat
```

Gamepad: buttons are arc_keys (ARC_PAD_*); sticks and triggers are axes. Sticks -1..1 (+Y down), triggers 0..1.

```c
typedef enum { ARC_AXIS_LX, ARC_AXIS_LY, ARC_AXIS_RX, ARC_AXIS_RY, ARC_AXIS_LT, ARC_AXIS_RT, ARC_AXIS_COUNT } arc_axis;
ARC_API float arc_pad_axis(arc_axis a);
ARC_API bool arc_pad_connected(void);
```

0.10.5 several pads (couch multiplayer): pad N is a slot that keeps its number while the pad stays connected
   (Windows: the XInput player number, its light; Linux: the order pads were found; web: the browser's gamepad index).
   The ARC_PAD_* keys and arc_pad_axis above read the first connected pad, as before; these read one pad each.
   "Pad N drives player N": see docs/split-screen.md. Recorded in replays (format 3; format 2 replays still play).
   Agents: `pad N connect|disconnect`, `pad N press|release BUTTON`, `pad N axis LX 0.5`.

```c
#define ARC_MAX_PADS 4
ARC_API int arc_pad_count(void);    // pads connected now
ARC_API bool arc_pad_is_connected(int pad);
ARC_API bool arc_pad_down(int pad, arc_key button);    // button: ARC_PAD_A .. ARC_PAD_GUIDE
ARC_API bool arc_pad_pressed(int pad, arc_key button);
ARC_API bool arc_pad_released(int pad, arc_key button);
ARC_API float arc_pad_axis_of(int pad, arc_axis a);
ARC_API int arc_pad_first(void);    // the pad the ARC_PAD_* keys and arc_pad_axis read (-1: none)
ARC_API int arc_pad_any_pressed(arc_key *button);    // a pad on which a button went down this tick ("press A to join"), -1: none
```

Touch (web): the first finger drives the mouse and MOUSE_LEFT. True while the last pointer input was a finger
   (no hover between taps), false again once a real mouse moves.

```c
ARC_API bool arc_touch_mode(void);
```
