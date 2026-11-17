/* ARC unity build: compile this one file (plus your game) for any platform.
 *   Windows : cl /O2 game.c arc/src/arc.c            (Win32 + Direct3D 11)
 *   Linux   : cc -O2 game.c arc/src/arc.c -lX11 -lGL -lm -ldl
 *   Web     : emcc -O2 game.c arc/src/arc.c -sUSE_WEBGL2=1 ...   (see build.sh)
 *   Headless only (no window code at all): add -DARC_HEADLESS_ONLY
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
/* 0.9.7: no fused multiply-add anywhere in ARC (a*b + c rounded once instead of twice gives other bits): physics,
   navigation, terrain and arc_dmath must come out the same on every compiler. build.sh passes -ffp-contract=off for gcc;
   clang (and Emscripten) and MSVC are told here. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")   /* gcc ignores the STDC pragma; its GNU mode (-std=gnu11) fuses on any FMA cpu */
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif
#include "arc_internal.h"
#include "arc_core.c"
#include "arc_texc.c"
#include "arc_gfx.c"
#include "arc_marker.c"
#include "arc_scene.c"
#include "arc_anim.c"
#include "arc_fx.c"
#include "arc_bill.c"
#include "arc_collide.c"
#include "arc_nav.c"
#include "arc_phys.c"
#include "arc_terrain.c"
#include "arc_ui.c"
#include "arc_ux.c"
#include "arc_level.c"   /* 0.10.8 */
#include "arc_editor.c"
#include "arc_gif.c"
#include "arc_meshb.c"
#include "arc_jpeg.c"
#include "arc_font.c"
#include "arc_soft.c"
#include "arc_mp3.c"
#include "arc_audio.c"
#include "arc_load.c"
#include "arc_webload.c"
#include "arc_turns.c"
#include "arc_board.c"
#include "arc_brain.c"     /* 0.10.0 */
#include "arc_senses.c"
#include "arc_shots.c"
#include "arc_rig.c"
#include "arc_review.c"   /* 0.10.1 */
#include "arc_dmath.c"
#include "arc_net.c"
#include "arc_rollback.c"

#if defined(ARC_HEADLESS_ONLY)
int main(int argc, char **argv) { return arc__run(argc, argv, &arc__platform_headless); }
#elif defined(__EMSCRIPTEN__)
#define ARC_GL 1
#define ARC_GLES 1
#include "arc_gl.c"
#include "plat_web.c"
#elif defined(_WIN32)
#define ARC_D3D11 1
#include "arc_d3d11.c"
#include "plat_win32.c"
#elif defined(__linux__)
#define ARC_GL 1
#include "arc_gl.c"
#include "plat_x11.c"
#else
#error "ARC: unsupported platform (build with -DARC_HEADLESS_ONLY for a headless-only binary)"
#endif
