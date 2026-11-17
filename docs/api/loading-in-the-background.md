# Loading in the background (0.7.1)

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Loading in the background (0.7.1)** · [quick reference](../QUICK.md) · [full API](../API.md)


Loads between arc_load_begin and arc_load_end are queued instead of done at once: arc_texture_load(_ex),
   arc_mesh_load, arc_sound_load and arc_music_load return their handle straight away ({0} if the file is missing, as
   always) and it becomes ready once the file has been read, decoded and handed to the GPU.
     init():  arc_load_begin(0); G.ship = arc_mesh_load("assets/ship.arcm"); ...; arc_load_end();
   - Desktop: worker threads read and decode (PNG, JPEG, .arct, .arcm, sounds; --load-threads N, default the CPU count
     less one, 1 to 4); the main thread uploads, within a time budget each frame (arc_load_set_budget, 4 ms), so frames
     stay smooth. Web: the main thread does both, a slice within the budget each frame (0.8.6: with
     arc_app.browser_decode the browser decodes JPEG/PNG off the main thread; packs fetched on demand: arc_pack_fetch).
   - Until ready a handle is safe: a texture draws nothing and is 0 x 0, a mesh draws nothing (no triangles, zero
     bounds), a sound plays nothing (arc_play gives no voice), arc_music_play waits and starts the music when it is
     ready. Settings made meanwhile (arc_mesh_set_texture / _material / _double_sided / _cast_shadow, arc_sound_set_*,
     arc_texture_set_smooth, arc_music_set_volume) are kept. A load that fails logs as usual and stays empty.
   - While a group (flags 0) loads, ticks wait: tick() is not called, and draw() is replaced by arc_app.draw_loading
     (or ARC's loading screen). The game's first tick then sees everything loaded, so ticks, dumps and replays do not
     depend on how long loading took. arc_load_on_done runs a function (main thread) when a group is done: work that
     needs the data (bounds, copying a material) goes there or in tick().
   - ARC_LOAD_BACKGROUND groups load while the game runs (the next level's assets). On desktop when they finish depends
     on the machine, so tick logic must not depend on it (check arc_load_done only to start using them, and keep that
     out of anything a replay must reproduce).
   - Headless runs (agents, tests) are deterministic: arc_load_end finishes the group before it returns, in queue order
     on the main thread, so a headless run gives the same dumps whether the game queues or loads at once.
   Fonts, animations and arc_image_load always load at once; packs are looked in first, as with every loader; hot reload
   (--hot) watches queued files too. Handles get the same ids in every run (they are reserved in queue order).

```c
typedef struct { uint32_t id; } arc_load_group;    // {0} = every load queued so far
enum { ARC_LOAD_BACKGROUND = 1 };
typedef struct { int total, done, failed; long long bytes_total, bytes_done; float progress; double seconds; } arc_load_info;
ARC_API void arc_load_begin(int flags);    // flags: 0 or ARC_LOAD_BACKGROUND; groups do not nest
ARC_API arc_load_group arc_load_end(void);
ARC_API bool arc_load_done(arc_load_group g);
ARC_API float arc_load_progress(arc_load_group g);
ARC_API arc_load_info arc_load_status(arc_load_group g);
ARC_API void arc_load_wait(arc_load_group g);    // finish it now, on this thread (blocks)
ARC_API void arc_load_on_done(arc_load_group g, void (*fn)(void *user), void *user);    // at once if done already
ARC_API void arc_load_set_budget(float ms);    // main-thread upload time per frame (default 4)
ARC_API bool arc_texture_ready(arc_texture t);    // loaded and usable (true for any texture that was not queued)
ARC_API bool arc_mesh_ready(arc_mesh m);
ARC_API bool arc_sound_ready(arc_sound s);
ARC_API bool arc_music_ready(arc_music m);
ARC_API void arc_draw_loading_screen(float progress, const char *text);    // ARC's own: text over a bar, centred
```

---- Packs fetched on demand (0.8.6): a web build that starts small and downloads the rest while the game runs ----
   A web build made with WEB_PACKS (build.sh / build.bat, tools/web_packs.py; docs/WEB.md) preloads only part of assets/
   (the title screen's files) and puts each listed folder in a pack next to index.html: assets/levels/ -> levels.arcpak.
   The page starts as soon as the small first package is in, and:
     arc_load_begin(ARC_LOAD_BACKGROUND); G.lvl = arc_mesh_load("assets/levels/l1.arcm"); ...; G.grp = arc_load_end();
   fetches levels.arcpak by itself (a file listed in a pack that is not here yet: its pack is fetched first, then the
   file loads from it), with progress in arc_load_progress(G.grp). arc_pack_fetch("levels.arcpak") starts a download
   early (e.g. on the title screen) and returns its group (inside an open group the pack becomes part of that group).
   Fetched packs are kept in the browser's Cache API keyed by their content hash, so a second visit (or a reload) reads
   them from the cache; a new build's changed pack is downloaded again and the old copy deleted.
   - Desktop and headless (and single-package web builds): the files are already here, loose under assets/ (or in a
     pack file of that name, which is mounted at once): arc_pack_fetch's group is done at once and nothing changes.
   - Only queued loads (arc_load_begin / end) wait for a pack; loading a file of a pack that is not here yet at once
     (outside a group) fails as for any missing file. arc_load_wait cannot wait for a download on the web.

```c
enum { ARC_PACK_NONE = 0, ARC_PACK_FETCHING, ARC_PACK_READY, ARC_PACK_FAILED };
ARC_API arc_load_group arc_pack_fetch(const char *name);    // "levels.arcpak" (a URL relative to the page, or a file)
ARC_API int arc_pack_state(const char *name);    // ARC_PACK_*: NONE before arc_pack_fetch (or a load) asked for it
ARC_API bool arc_pack_ready(const char *name);    // mounted (or nothing to fetch: desktop, a single-package build)
```

0.10.9 arc_file_exists, or a file that a pack still to come holds (listed in arc_packs.txt, its pack not failed): a
   queued load of it will work. On desktop the same as arc_file_exists.

```c
ARC_API bool arc_file_listed(const char *path);
```
