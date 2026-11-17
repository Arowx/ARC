# Making an ARC web build load fast (0.8.6)

A plain ARC web build (`build.sh web`, a game's `build.bat web`) packs all of `assets/` into one `.data` file with
`--preload-file assets`. The page downloads all of it before `main` runs, so the player looks at a bar until the last
byte is in; a reload often downloads it all again; and then the loader decodes every JPEG and PNG with ARC's WASM
decoders on the main thread. ARC 0.8.6 adds four things, each opt-in (a game that changes nothing builds and behaves
exactly as before):

| What | How to turn it on | What it buys |
|---|---|---|
| Packs fetched on demand | `WEB_PACKS="levels music"` (or `"*"`) in the game's build.sh / build.bat | the title screen in well under a second instead of after the whole download; packs and the first package cached in the browser: a second visit downloads only the page and the code |
| Browser image decoding | `arc_app.browser_decode = true` | JPEG/PNG decoded off the main thread by the browser's SIMD decoders, uploaded straight to the GPU; less main-thread time while loading |
| `.arct` block-compressed textures | `mesh2arcm.py --compress` / `texcompress.py --arcm` | no image decoding at all on desktop browsers (uploaded as blocks); 4-8x less GPU memory |
| Caching of the first package | on with `WEB_PACKS`; `WEB_CACHE=1` for ARC's own build.sh | the `.data` comes from IndexedDB on later visits |

Nothing here uses threads (no SharedArrayBuffer, so no cross-origin isolation headers, which itch.io does not send) and
nothing changes on desktop or headless: agents' tests, dumps and screenshots stay the same.

## 1. Packs fetched on demand

Put what the title screen needs in `assets/first/` (or any folder you do not list), and list the folders the game can
fetch while the player is on the title screen:

```sh
WEB_PACKS="levels music" ./build.sh web          # a game made by tools/new_game.py (also: set WEB_PACKS=... in build.bat)
python tools/new_game.py D:/Projects/MyGame --web-packs "*"   # a new game, packs on by default ("*" = every folder but first/)
python tools/new_game.py D:/Projects/MyGame --update-scripts --web-packs "levels"   # an existing game's scripts
```

The build runs `tools/web_packs.py`: each listed folder becomes `build/web/<folder>.arcpak` (an ordinary `.arcpak`,
zlib-compressed where that helps; the files keep their names, `assets/levels/l1.arcm`), the rest goes into the preloaded
`.data` with `arc_packs.txt`, the list of which pack holds which file and each pack's size and content hash. The first
package is cached in IndexedDB (`--use-preload-cache`).

In the game, load those files through a queued group, as for any background load:

```c
static void init(void) {
    G.title = arc_texture_load("assets/first/title.png");          /* preloaded: at once */
    arc_load_begin(ARC_LOAD_BACKGROUND);                            /* the levels: while the title screen shows */
    G.level1 = arc_mesh_load("assets/levels/l1.arcm");
    G.music = arc_music_load("assets/music/theme.mp3");
    G.levels = arc_load_end();
}
static void draw(float a) {
    ...
    if (!arc_load_done(G.levels)) arc_draw_loading_screen(arc_load_progress(G.levels), "Loading the station");
}
```

A queued load of a file that is listed in a pack which is not here yet fetches that pack first (one download per pack,
however many files), then loads the file from it; progress counts the download's bytes. `arc_pack_fetch("levels.arcpak")`
starts a download yourself (earlier, or for a pack whose files you will load later) and returns its load group;
`arc_pack_state` / `arc_pack_ready` say where it is. Rules:

- **Only queued loads wait for packs.** A load at once (outside `arc_load_begin`/`end`) of a file whose pack has not
  arrived fails like a missing file. Once the pack is mounted, every loader finds its files, at once or queued.
- Use `ARC_LOAD_BACKGROUND` for groups that wait on a download, so the game runs meanwhile; a group with flags 0 holds
  the ticks (and shows the loading screen) until the pack is in, which is still correct, just not faster.
- `arc_load_wait` cannot wait for a download on the web (it returns; images the browser is decoding are finished by ARC
  instead).
- Desktop and headless builds do not split anything: the files are loose under `assets/`, `arc_pack_fetch` is done at
  once (a pack file of that name next to the game is mounted), and the same code works there.
- A single-package web build (no `WEB_PACKS`) has no `arc_packs.txt`: `arc_pack_fetch` of a pack that is not there is
  "ready" too (its files were preloaded). A pack you host yourself, made with `tools/arcpak.py`, is fetched by its URL
  (relative to the page) and cached only by the browser's HTTP cache.

**Caching.** Packs are stored in the browser's Cache API (`caches`, name `arc-packs`) under `<name>?arc=<hash>`; the
browser writes the copy from the download stream itself. On a later visit (or a reload) a pack with the same hash comes
from the cache, with no network at all; a new build whose pack changed has a new hash, so it is downloaded once and the
old copy is deleted. The Cache API needs a secure context (https, or localhost): itch.io is https. In an insecure
context the packs are downloaded each time (the game still works).

## 2. Browser image decoding

```c
return (arc_app){ ..., .browser_decode = true };
```

Every JPEG and PNG in queued loads (textures, and the colour, normal, ORM and emissive maps inside `.arcm` meshes) is
handed to the browser. Up to 16 decode at once, started ahead of the loader's finish point; when one is decoded it is
uploaded straight into its GL texture (`texImage2D` from the browser's image, with no flip, no premultiplying and no
colour conversion, so normal and ORM maps stay data), and mip levels are made on the GPU (as ARC's RGBA8 path does).
The pixels come back as bytes instead only where the CPU needs them (the software renderer; `--browser-decode=bytes`
for tests). Which browser API:

| Browser | API ARC uses | Why |
|---|---|---|
| Chrome, Edge, other Chromium | `createImageBitmap(blob, { premultiplyAlpha: 'none', colorSpaceConversion: 'none' })` | decodes off the main thread |
| Firefox | `ImageDecoder` (WebCodecs) for JPEGs and opaque PNGs, `createImageBitmap` for see-through PNGs | only ImageDecoder decodes off the main thread in Firefox; its frames are premultiplied, so not for alpha |
| Safari | an `<img>` + `decode()` for JPEGs and opaque PNGs; see-through PNGs decoded by ARC | no ImageDecoder; `<img>` is Safari's fastest; its pixels are premultiplied |

ARC keeps an image for its own decoder (the result would differ, or the browser could not do it exactly):
16-bit PNGs, PNGs with `gAMA`, `cHRM`, `iCCP` or `eXIf` chunks, JPEGs with an EXIF orientation other than 1 (browsers
turn them, ARC does not), CMYK, lossless and arithmetic-coded JPEGs, and any image over `arc_set_texture_max_size`
(ARC's box filter halves it). Any failure in the browser (a decode error, an upload error) falls back to ARC's decoder on
the same bytes. The browser also reads progressive JPEGs and interlaced or low-bit-depth PNGs, which ARC's own decoders
refuse.

**Results.** PNGs are identical to ARC's decode (checked byte for byte, including every alpha level and palette + tRNS).
JPEGs differ slightly because decoders round their IDCT and chroma upsampling differently: on Droids' 98 real textures
(512x512 colour, 256x256 normal and ORM maps) the largest difference was 1 level (of 255) and the mean 0.06 levels per
channel with createImageBitmap, 0.0003 with ImageDecoder (Chromium; `tests/web_load.c --webload-compare`); the 6 PNGs
(UI art with alpha, a normal map) were identical. Games whose logic reads pixels (not just draws
them) should read them from `arc_image_load`, which always uses ARC's decoder.

Plain `arc_texture_load` / `arc_mesh_load` at once still use ARC's decoders: they must return a finished texture (its
size, its pixels) before they return, which the browser cannot do synchronously. Queue the loads to get the browser.

URL arguments for testing a build: `index.html?--browser-decode` (turn it on), `?--no-browser-decode`,
`?--image-decoder=bitmap|imagedecoder|img` (force an API), `?--browser-decode=bytes`.

## 3. Block-compressed textures

`.arct` textures (and `.arcm` version 4 meshes, `mesh2arcm.py --compress`, `texcompress.py --arcm`) are uploaded to the
GPU as they are when the browser has the extension: `WEBGL_compressed_texture_s3tc` (BC1, BC3), `EXT_texture_compression_rgtc`
(BC4, BC5), `EXT_texture_compression_bptc` (BC7). Then nothing is decoded at all and the texture takes 4-8x less GPU
memory. Without the extension ARC decodes the blocks on the CPU to RGBA8 (`--no-compressed-textures` forces that path
for tests, on the web too: `?--no-compressed-textures`).

| Platform | S3TC (BC1/BC3) present |
|---|---|
| Windows browsers | ~99.9% |
| Linux | ~96% |
| macOS | ~88% |
| Phones | ~30-40% (most Android and iOS GPUs have ETC/ASTC instead) |

Recommendation: for a game played on desktop browsers, `mesh2arcm.py --compress` is the best loading choice. The cost
is on phones: the files are larger than JPEG (BC1 is 4 bits a texel, zlib-compressed in packs; JPEG is often 1 bit a
texel or less), so phones download more and then spend CPU decoding blocks. For a game meant for phones too, keep JPEG
and turn on `browser_decode`.

## 4. The page and the server

- **WebAssembly.instantiateStreaming**: Emscripten's loader (and so `web/shell.html`) compiles the `.wasm` while it
  downloads when the server sends it as `application/wasm`; otherwise it falls back to downloading, then compiling. itch.io
  sends `application/wasm`. Nothing to do.
- **Cache headers**: on your own server, send `Cache-Control: no-cache` (revalidate, with an ETag or Last-Modified) for
  `index.html`, the `.js` and the `.data`, and long caching for `.arcpak` files only if their names change with their
  content (ARC's own cache already keys them by hash). On **itch.io** you cannot set headers: its CDN serves the files
  with its own caching and compression settings (not documented, and not under your control), and each new upload is
  served from new URLs. So on itch.io, ARC's Cache API copy
  of the packs and the IndexedDB copy of the first package are what make a second visit fast; the `.wasm` and `.js` are
  the browser's to cache.
- itch.io's "SharedArrayBuffer support" option is not needed (ARC uses no threads on the web).
- Keep the first package small: the title screen's images, the fonts, the UI, the menu music if it must play at once.

## Measurements

Headless Chromium (SwiftShader WebGL2: software rendering, so frames are slow and GPU uploads cost CPU; real GPUs do
better), `tests/web_load.c --webload-bench`: a title image drawn at once, then 28 of Droids' textured models (19 droids,
9 deck pieces; 64 embedded images: 512x512 JPEG colour maps, 256x256 JPEG normal / ORM maps, one PNG normal map;
10.2 MB of files) loaded in one `ARC_LOAD_BACKGROUND` group, then all drawn once. Times in ms from navigation start
(`performance.now()`), medians of 3 runs. "loader" is main-thread time inside ARC's loader (`arc__load_busy_ms`);
"longest" the longest frame while loading; "task" the page's main-thread task time (Chrome DevTools
`Performance.getMetrics` TaskDuration) for the visit. Visit 2 opens the page again in a new tab of the same profile;
the test server sends no cache headers, so only ARC's caches help there (the worst case for a host's HTTP caching).

**20 Mbit/s, 50 ms latency** (Chrome DevTools network emulation):

| Configuration | download | title shown | all loaded | longest frame | loader | task | visit 2: title / all loaded / downloaded |
|---|---|---|---|---|---|---|---|
| A. one preload, ARC decode (0.8.5 behaviour) | 10.9 MB | 5050 | 5498 | 31 | 321 | 1363 | 4903 / 5344 / 10.9 MB |
| B. one preload, browser decode | 10.9 MB | 4994 | 5366 | 23 | 195 | 1203 | 4883 / 5255 / 10.9 MB |
| G. one preload + IndexedDB cache, ARC decode | 10.9 MB | 5178 | 5618 | 28 | 323 | 1333 | 686 / 1136 / 0.71 MB |
| C. packs, ARC decode | 8.4 MB | 871 | 4776 | 51 | 333 | 1291 | 657 / 1196 / 0.71 MB |
| D. packs, browser decode | 8.4 MB | **844** | **4719** | 46 | 242 | 1246 | **652 / 1104 / 0.71 MB** |
| E. one preload, `.arct` BC (as blocks) | 13.3 MB | 6000 | 6348 | 27 | 181 | 1361 | 5916 / 6300 / 13.3 MB |
| F. packs, `.arct` BC | 10.8 MB | 906 | 5836 | 56 | 246 | 1436 | 639 / 1138 / 0.71 MB |

**No throttling** (localhost: CPU-bound):

| Configuration | title shown | all loaded | longest frame | loader | task | visit 2: title / all loaded |
|---|---|---|---|---|---|---|
| A. one preload, ARC decode | 402 | 858 | 29 | 312 | 719 | 276 / 713 |
| B. one preload, browser decode | 444 | 794 | 24 | 191 | 694 | 267 / 626 |
| C. packs, ARC decode | 355 | 972 | 46 | 339 | 777 | 201 / 722 |
| D. packs, browser decode | 374 | 822 | 36 | 242 | 713 | 202 / 661 |
| E. one preload, `.arct` BC | 459 | 774 | 24 | 184 | 637 | 292 / 668 |
| F. packs, `.arct` BC | 308 | 853 | 53 | 251 | 653 | 203 / 661 |

What this says:

- **Packs are the big win on a real connection**: the title shows after 0.85 s instead of 5 s (the first package is
  26 KB), and the models stream in while it shows, finishing a little sooner than the single package (packs are
  zlib-compressed: 8.4 MB to download instead of 10.9 MB). A second visit downloads only the page and the code
  (0.71 MB: `.html`, `.js`, `.wasm`) and has everything loaded in about 1.1 s instead of 5.3 s. G shows the IndexedDB
  cache alone gives the same second visit for a single-package build, but not the fast first one.
- **Browser decoding** cuts the loader's main-thread time by about 25-40% here (321 -> 195 ms with one package) and the
  longest frame by a few ms. These textures are small (512x512 JPEGs decode in a few ms even in WASM), so most of what
  remains is reading files out of packs (inflate), building meshes and uploads; with larger textures the gain grows
  with the pixel count, since the browser's decode costs the main thread nothing.
- **`.arct`** removes decoding entirely (loader 181 ms) but the files are 30% bigger than JPEG, so on a network it loads
  later; it pays on GPU memory and on fast connections, not on download time.
- The longest frame with packs (46-56 ms on visit 1) is the frame the pack arrives in: the bytes are copied into the
  heap and mounted, and the browser finishes the download and its cache copy around it. It happens once, while the
  title screen is up.
- Visit-1 "all loaded" is limited by the download in every case (20 Mbit/s = 2.5 MB/s): the way to make it shorter is a
  smaller download (packs that hold only what the next screen needs; JPEG over BC for phones).
