#!/usr/bin/env sh
# ARC build script (Linux / WSL / macOS shell). No CMake, no libraries: one compiler call per target.
#   ./build.sh linux    [game.c] [out]   X11 + OpenGL 3.3            -> build/<name>
#   ./build.sh headless [game.c] [out]   no window code at all        -> build/<name>_headless
#   ./build.sh win      [game.c] [out]   MinGW cross-compile, D3D11   -> build/<name>.exe
#   ./build.sh web      [game.c] [out]   Emscripten, WebGL2           -> build/web/<name>.html
#                                        WEB_PACKS="levels music" (or "*"): those folders of assets/ become packs
#                                        fetched on demand (tools/web_packs.py, docs/WEB.md); WEB_CACHE=1: keep the
#                                        preloaded package in IndexedDB (on with WEB_PACKS)
#   ./build.sh hot      [game.c]         hot reload: build/arc_host (the engine) + build/libgame.so (the game; run
#                                        this again after editing and the running host loads the new code)
#   ./build.sh hot-headless [game.c]     the same with a headless host (agents)
#   ./build.sh test                      build + run the self-test headless
#   ./build.sh relay                     arc_relay, the online host (0.8.5): build/arc_relay (+ build/arc_relay.exe with MinGW)
#   ./build.sh all                       every target for the sample game
# Default game: samples/demo/demo.c. Set DEBUG=1 for -O0 -g with sanitizers (linux/headless).
set -e
cd "$(dirname "$0")"
target=${1:-linux}
game=${2:-samples/demo/demo.c}
if [ -z "$2" ] && { [ "$target" = hot ] || [ "$target" = hot-headless ]; }; then game=samples/hot/hot.c; fi
name=${3:-$(basename "$game" .c)}
mkdir -p build
CFLAGS="-std=c11 -Wall -Wextra -O2 -ffp-contract=off -Iinclude"   # no fused multiply-adds: the same maths everywhere
[ -n "$DEBUG" ] && CFLAGS="-std=c11 -Wall -Wextra -O0 -g -ffp-contract=off -fsanitize=address,undefined -Iinclude"
case "$target" in
  linux)    ${CC:-cc} $CFLAGS -D_GNU_SOURCE "$game" src/arc.c -o "build/$name" -lX11 -lGL -ldl -lpthread -lm ;;
  headless) ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HEADLESS_ONLY "$game" src/arc.c -o "build/${name}_headless" -lm ;;
  win)      x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -O2 -ffp-contract=off -Iinclude "$game" src/arc.c -o "build/$name.exe" -static -ld3d11 -ldxguid -lwinmm -luuid ;;
  web)      mkdir -p build/web
            PRE=""; [ -d assets ] && PRE="--preload-file assets"
            [ -n "$WEB_CACHE" ] && [ -d assets ] && PRE="$PRE --use-preload-cache"
            if [ -n "$WEB_PACKS" ] && [ -d assets ]; then   # 0.8.6: a small first package + packs next to the page
              python3 tools/web_packs.py assets build/web build/webstage --packs "$WEB_PACKS"
              PRE="--preload-file build/webstage@/ --use-preload-cache"
            fi
            emcc -std=c11 -Iinclude -Wall -O2 -ffp-contract=off "$game" src/arc.c -o "build/web/$name.html" --shell-file web/shell.html \
              -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 \
              -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_arc_web_audio_fill,_arc_web_command \
              -sEXPORTED_RUNTIME_METHODS=lengthBytesUTF8,stringToUTF8,HEAPF32 \
              $PRE ;;
  hot|hot-headless)
            if [ "$target" = hot ]; then HDEF=""; HLIB="-lX11 -lGL"; HN=arc_host; else HDEF="-DARC_HEADLESS_ONLY"; HLIB=""; HN=arc_host_headless; fi
            if [ ! -f "build/$HN" ] || [ src/arc.c -nt "build/$HN" ] || [ -n "$(find src include -newer "build/$HN" 2>/dev/null | head -1)" ]; then
              ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HOT_HOST $HDEF src/arc.c -o "build/$HN" -rdynamic $HLIB -ldl -lpthread -lm
              echo "built build/$HN"
            fi
            ${CC:-cc} $CFLAGS -fPIC -shared -DARC_HOT_GAME "$game" -o build/libgame.so.tmp && mv build/libgame.so.tmp build/libgame.so
            echo "built build/libgame.so  (run: build/$HN --game build/libgame.so; rebuild to reload)" ;;
  win-hot)  # MinGW: the host exports the engine; the game DLL links against its import library
            x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -O2 -ffp-contract=off -DARC_HOT_HOST ${HOT_HEADLESS:+-DARC_HEADLESS_ONLY} src/arc.c -o build/arc_host.exe -Wl,--out-implib,build/libarc_host.a -ld3d11 -ldxguid -lwinmm -luuid
            x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -O2 -ffp-contract=off -shared -DARC_HOT_GAME "$game" build/libarc_host.a -o build/game.dll
            echo "built build/arc_host.exe + build/game.dll" ;;
  test)     mkdir -p build; sh "$0" test-run 2>&1 | tee build/test.log
            python3 tools/test_summary.py build/test.log ;;   # 0.9.1: one line per stage at the end
  test-run) ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HEADLESS_ONLY tests/test_core.c src/arc.c -o build/arc_test -lm
            python3 tools/mesh2arcm.py tests/pyramid.obj build/pyramid.arcm >/dev/null
            python3 tools/mesh2arcm.py tests/pyramid.obj build/pyramid.axm --axm >/dev/null
            python3 tools/mesh2arcm.py tests/pyramid.obj build/pyramid_mat.arcm --metallic 1 --roughness 0.25 >/dev/null
            python3 tools/mesh2arcm.py tests/pyramid.obj build/pyramid_v2.arcm --v2 >/dev/null
            mkdir -p build/packsrc/inpack && cp build/pyramid.arcm build/packsrc/inpack/pyr.arcm && printf 'hello pack %.0s' $(seq 1 50) > build/packsrc/inpack/hello.txt
            (cd build/packsrc && python3 ../../tools/arcpak.py ../test.arcpak inpack >/dev/null)
            rm -rf build/wpsrc && mkdir -p build/wpsrc/assets/first build/wpsrc/assets/lvl && cp build/pyramid.arcm build/wpsrc/assets/lvl/pyr.arcm
            printf 'first file' > build/wpsrc/assets/first/a.txt && printf 'level text %.0s' $(seq 1 40) > build/wpsrc/assets/lvl/hello.txt
            (cd build/wpsrc && python3 ../../tools/web_packs.py assets ../wpout ../wpstage --packs lvl >/dev/null)
            (cd build && ./arc_test --headless --cmd "step 2; tap A; type hi; axis RX 0.25; release; args a1 a2 a3 a4 a5 a6 a7 a8 a9 a10 a11 a12 a13 a14 a15 a16 a17 a18 a19 a20; step 3; shot arc_test_shot.png; step 20")
            ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HEADLESS_ONLY tests/check_game.c src/arc.c -o build/check_game_headless -lm
            python3 tools/arc_check.py tests/check_request.md --exe build/check_game_headless > build/check.txt || { cat build/check.txt; exit 1; }; tail -1 build/check.txt   # 0.9.0 acceptance checks + the frame command
            ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HEADLESS_ONLY samples/board/board.c src/arc.c -o build/board_headless -lm   # 0.9.9 ground markers and arc_board
            python3 tools/arc_check.py tests/board_check.md --exe build/board_headless > build/board_check.txt || { cat build/board_check.txt; exit 1; }; tail -1 build/board_check.txt
            ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HEADLESS_ONLY samples/terrain/terrain.c src/arc.c -o build/terrain_headless -lm   # 0.10.13 terrain colour map
            python3 tools/arc_check.py tests/terrain_colour_check.md --exe build/terrain_headless > build/terrain_colour_check.txt || { cat build/terrain_colour_check.txt; exit 1; }; tail -1 build/terrain_colour_check.txt
            ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HEADLESS_ONLY samples/action/action.c src/arc.c -o build/action_headless -lm   # 0.10.0 brains, senses, shots, camera rigs
            python3 tools/arc_check.py tests/action_check.md --exe build/action_headless > build/action_check.txt || { cat build/action_check.txt; exit 1; }; tail -1 build/action_check.txt
            ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HEADLESS_ONLY tests/review_game.c src/arc.c -o build/review_headless -lm   # 0.10.1 review sheets, UI layout
            python3 tools/arc_check.py tests/review_check.md --exe build/review_headless > build/review_check.txt || { cat build/review_check.txt; exit 1; }; tail -1 build/review_check.txt
            python3 tools/arc_check.py tests/card_check.md --exe build/review_headless > build/card_check.txt || { cat build/card_check.txt; exit 1; }; tail -1 build/card_check.txt   # 0.10.6 landscape card, SDF UI font
            ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HEADLESS_ONLY tests/target_game.c src/arc.c -o build/target_headless -lm   # 0.10.7 frame lists render targets' contents
            python3 tools/arc_check.py tests/target_check.md --exe build/target_headless > build/target_check.txt || { cat build/target_check.txt; exit 1; }; tail -1 build/target_check.txt
            python3 tools/arc_check.py tests/target_immediate_check.md --exe build/target_headless > build/target_immediate_check.txt || { cat build/target_immediate_check.txt; exit 1; }; tail -1 build/target_immediate_check.txt
            ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HEADLESS_ONLY tests/level_game.c src/arc.c -o build/level_headless -lm   # 0.10.8 level files, the deck editor
            python3 tools/arc_check.py tests/level_check.md --exe build/level_headless > build/level_check.txt || { cat build/level_check.txt; exit 1; }; tail -1 build/level_check.txt
            python3 tools/arc_check.py tests/level_launch_check.md --exe build/level_headless > build/level_launch_check.txt || { cat build/level_launch_check.txt; exit 1; }; tail -1 build/level_launch_check.txt
            ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HEADLESS_ONLY tests/split_game.c src/arc.c -o build/split_headless -lm   # 0.10.2 split-screen viewports
            python3 tools/arc_check.py tests/split_check.md --exe build/split_headless > build/split_check.txt || { cat build/split_check.txt; exit 1; }; tail -1 build/split_check.txt
            ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HEADLESS_ONLY samples/demo/demo.c src/arc.c -o build/demo_headless -lm   # 0.10.11 lines and touch (Vector Trader)
            for c in line touch nan text save_path command_clash offset2d lines2d music_loop; do python3 tools/arc_check.py tests/${c}_check.md --exe build/demo_headless > build/${c}_check.txt || { cat build/${c}_check.txt; exit 1; }; tail -1 build/${c}_check.txt; done
            python3 tests/line_width_test.py build/demo_headless || exit 1
            python3 tests/touch_replay_test.py build/demo_headless || exit 1
            rc=0; python3 tools/arc_check.py tools/tests/two_blocks.md > build/two_blocks.txt || rc=$?   # 0.10.12: every block runs; one fails on purpose
            if [ $rc -ne 1 ] || ! grep -q "2/3 blocks passed" build/two_blocks.txt; then cat build/two_blocks.txt; echo "ARC error arc_check blocks"; exit 1; fi; tail -1 build/two_blocks.txt
            python3 tools/arc_doctor.py tests/doctor_fixture > build/doctor.txt || true; grep -q 'asset "missing.png" not found' build/doctor.txt && grep -q 'rand() in game code' build/doctor.txt && grep -q 'inside tick()' build/doctor.txt && ! grep -qE 'comment_only|arc_packs.txt|lvl.arcpak|view.png|notes_out|made_at_run_time' build/doctor.txt && grep -q 'uses ARC 0.10.6' build/doctor.txt && echo "ARC ok doctor" || { echo "ARC error doctor"; cat build/doctor.txt; exit 1; }
            python3 tools/gen_api_docs.py --check   # docs/API.md, docs/QUICK.md and docs/api/*.md match include/arc.h
            python3 -c "import re;s=open('docs/QUICK.md').read();open('build/quick_example.c','w').write(re.search(r'\`\`\`c\n(.*?)\`\`\`',s,re.S).group(1))"
            ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HEADLESS_ONLY -Iinclude build/quick_example.c src/arc.c -o build/quick_example -lm
            (cd build && ./quick_example --headless --cmd "press D; step 90; dump; quit" | grep -q '"tick":90' && echo "ARC ok quick example") || { echo "ARC error quick example"; exit 1; }
            rm -rf build/import_test && python3 tools/arc_import.py tests/pyramid.obj --game build/import_test --height 2 > build/import.txt && grep -q 'triangles 6' build/import.txt && grep -q 'arc_mesh_load("assets/meshes/pyramid.arcm")' build/import.txt && [ -f build/import_test/build/import/pyramid.png ] && echo "ARC ok import" || { echo "ARC error import"; cat build/import.txt; exit 1; }
            python3 tools/arc_release.py --game tests/doctor_fixture --dry-run > build/release.txt || true; grep -q 'doctor  FAIL' build/release.txt && grep -q 'build.sh not found' build/release.txt && echo "ARC ok release dry run" || { echo "ARC error release dry run"; cat build/release.txt; exit 1; }
            n=0; for t in $(python3 tools/new_game.py --list-templates | awk '{print $1}'); do   # 0.9.1: every genre template, made and checked
              rm -rf "build/tpl_$t"; python3 tools/new_game.py "build/tpl_$t" --template "$t" --title "Template $t" >/dev/null
              python3 tools/arc_check.py "build/tpl_$t/tests/acceptance.md" > "build/tpl_$t.txt" 2>&1 || { echo "ARC error template $t"; cat "build/tpl_$t.txt"; exit 1; }
              echo "template $t: $(tail -1 "build/tpl_$t.txt")"; n=$((n+1)); done; echo "ARC ok templates $n"
            python3 tools/warn_check.py || true   # 0.9.8: conversion warnings (clang stands in for MSVC C4244 / C4267); none allowed; the summary marks it FAIL
            ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HEADLESS_ONLY tests/headless_game.c src/arc.c -o build/headless_game -lm   # 0.9.8: --draw-last, --shot-at, --no-assets, reset, arc_batch, import --fit
            python3 tests/headless_tools_test.py build/headless_game || exit 1
            python3 tests/mcp_test.py build/target_headless || exit 1   # 0.10.10: tools/arc_mcp.py over MCP
            ${CC:-cc} $CFLAGS -D_GNU_SOURCE -DARC_HEADLESS_ONLY tests/test_post.c src/arc.c -o build/arc_test_post -lm
            (cd build && ./arc_test_post --headless --render-all --msaa 0 --frames 300) ;;
  relay)    ${CC:-cc} -std=c11 -Wall -Wextra -O2 tools/arc_relay/arc_relay.c -o build/arc_relay && echo "built build/arc_relay"
            if command -v x86_64-w64-mingw32-gcc >/dev/null; then
              x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -O2 tools/arc_relay/arc_relay.c -o build/arc_relay.exe -static -lws2_32 && echo "built build/arc_relay.exe"
            fi ;;
  all)      "$0" linux "$game" "$name"; "$0" headless "$game" "$name"; "$0" test
            command -v x86_64-w64-mingw32-gcc >/dev/null && "$0" win "$game" "$name" || echo "skip win (no mingw)"
            command -v emcc >/dev/null && "$0" web "$game" "$name" || echo "skip web (no emcc)" ;;
  *) echo "unknown target: $target"; exit 1 ;;
esac
