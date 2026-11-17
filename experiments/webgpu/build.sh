#!/usr/bin/env sh
# WebGPU spike (not part of ARC's build): the reference from ARC's software renderer, the WebGPU page, a headless run
# and the comparison. Needs cc, emcc (source emsdk_env.sh), node + Playwright's Chromium, python3 + Pillow.
#   ./build.sh          build/ref.png, build/meshes.bin, build/spike.html (+ .js .wasm)
#   ./build.sh run      also draw it in headless Chromium (WebGPU on SwiftShader, offscreen) and compare with ref.png
set -e
cd "$(dirname "$0")"
mkdir -p build
cc -std=c11 -Wall -Wextra -O2 -ffp-contract=off -D_GNU_SOURCE -DARC_HEADLESS_ONLY ref.c ../../src/arc.c -o build/ref -lm
build/ref --headless --size 320x180 --cmd "step 2; shot build/ref.png; quit" >/dev/null
cp build/meshes.bin meshes.bin
emcc -std=c11 -Wall -Wextra -O2 spike.c -o build/spike.html --shell-file shell.html --embed-file meshes.bin \
  -sEXPORTED_FUNCTIONS=_main,_spike_wgsl -sEXPORTED_RUNTIME_METHODS=UTF8ToString,HEAPU8,HEAPF32,HEAP32
rm -f meshes.bin
if [ "$1" = run ]; then
  node run_spike.js build offscreen
  node run_spike.js build "offscreen&msaa"
  python3 compare.py build
fi
