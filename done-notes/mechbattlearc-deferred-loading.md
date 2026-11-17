# Request from MechBattleArc (Molten Breach): faster start (load in the background, cap texture size)

**What we saw (8 Nov).** The game opened its window ~4.5 s after launch. All of that was `arc_mesh_load` on 66
models, decoding their 1024 px textures. Molten Breach now:
- re-converts its props with `mesh2arcm.py --texture-size 512` (3.7 s -> 1.1 s), and
- loads models one per frame while the title shows, with `look_load_all()` before any other screen (game code).

**Asks (any of these would help every game).**
1. Background decoding: `arc_mesh_load_async(path)` / `arc_texture_load_async(path)`, returning a handle at once and
   `arc_*_ready(handle)` once a worker thread has decoded it (uploaded on the main thread at frame start). The web
   build could fall back to loading synchronously.
2. A load-time cap: `arc_set_texture_max_size(512)`, or a field in `arc_app`, so games can trade texture detail for
   start time (and memory on the web) without re-converting their assets.
3. `mesh2arcm.py`: print each texture's size, and the decode time it will cost, in its summary, so a 2048 px texture
   on a one-tile prop is easy to spot.
