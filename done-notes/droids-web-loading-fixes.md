# Request from Droids: web pack loading fixes, a wrong hint, doctor false alarms, `editor rename` with spaces

**Why.** Droids now builds for the web with `WEB_PACKS` (droids, lod, deck, duel, music) and loads its models as one
queued group. The page preloads 1.3 MB instead of 26.8 MB. On a 20 Mbit line the title shows after 1.5 s (was 11.9 s)
and the menu after 10.5 s (was 13.2 s). Getting there turned up six problems in ARC 0.10.8. Droids works around each
one today, and the workarounds are listed so they can come out once these land.

## 1. Look-ahead starts files of a pack that has downloaded but not mounted (bug)

**What happens.** One group queues meshes from several packs, interleaved, as a game naturally does: each droid model,
then its lower-detail copy in `lod/`, then the next model. Some files then fail with `cannot load mesh
assets/lod/droids/class0.arcm` even though they are in the pack. In Droids' first try this happened to three lod files,
two deck files and all six duel files.

**Cause.** `arc_load.c`, `load_ahead()`:

```c
if (it->kind == ARC__LOAD_PACK && it->state != 2) break;      /* what follows may be inside it */
```

`load_poll` sets a pack item to state 2 as soon as its **download** finishes. The pack is only **mounted** later, by
`load_finish` → `arc__pack_finish`, in queue order. So once a later pack has downloaded but sits behind unfinished items,
look-ahead walks past it and `load_begin` reads files that aren't mounted yet.

**Fix.** Every item at or after `next_fin` is unfinished, so any pack item there means "not mounted yet":

```c
if (it->kind == ARC__LOAD_PACK) break;
```

(Or add a separate "mounted" state.)

**Droids' workaround.** It fetches all its packs first in one group (`arc_pack_fetch`). It queues the models only once
that group is done.

## 2. A false hint for queued loads

`arc_mesh_load` (and the other loaders) call `load_hints(path)` before checking `arc__load_queueing()`. A load queued
from `tick()` inside `arc_load_begin(ARC_LOAD_BACKGROUND)` / `arc_load_end` then prints:

`ARC hint assets/droids/class0.arcm loaded during tick(): ... queue it with arc_load_begin(ARC_LOAD_BACKGROUND).`

It is already queued. The fix: skip the "loaded during tick/draw" hint when `arc__load_queueing()` is true. Keep the
"loaded 4 times" hint.

## 3. `arc_pack_fetch` in a single-package web build tries to download

`arc.h` says that for single-package web builds `arc_pack_fetch`'s group "is done at once and nothing changes". In a
web build made with `WEB_PACKS=` (no `arc_packs.txt`), `arc_pack_fetch("droids.arcpak")` starts a fetch instead. The
console then shows `Failed to load resource: 404` and `ARC log pack http://.../droids.arcpak: Error: HTTP 404` for each
pack, before ARC's "no manifest" fallback succeeds.

**Fix.** With no manifest (`!WP.have`), and no file of that name, treat the pack as ready without fetching.

**Droids' workaround.** It calls `arc_pack_fetch` only if `arc_packs.txt` exists.

## 4. `arc_file_exists` doesn't know files of packs still to come (small)

`arc_file_exists("assets/lod/droids/class1.arcm")` is false on the web until `lod.arcpak` has mounted, though
`arc_packs.txt` lists it. A queued load of it would work. Games that check before loading, to avoid "cannot load"
noise for optional files like Droids' lower-detail copies, can't tell "coming" from "missing".

**Ask.** Either `arc_file_exists` also counts files listed in the manifest, or add `arc_file_listed(path)` that does.

**Droids' workaround.** It looks files up only after its packs are mounted (see 1).

## 5. `arc_doctor` false alarms

The release dry run failed on these ERRORs, none of them real:
- **Comments are scanned for asset names.** The comment `/* "file PACK BYTES assets/x/y.arcm" lines */` was reported
  as a missing asset. The asset scan reads `raw`; it should read the comment-stripped `code`, as the other checks do.
- **Web-only files.** `arc_packs.txt` and the `.arcpak` names of the game's own `WEB_PACKS` only exist in a web build.
  The doctor could skip `*.arcpak` named in `WEB_PACKS`, and `arc_packs.txt`.
- **Output files.** A tool's screenshot name (`"view.png"`, passed to `arc_screenshot`) is something it writes, not an
  asset. It would help to skip strings passed to `arc_screenshot`, or to have a marker such as
  `/* doctor: not an asset */` on the line.
- **The version note's formatting.** For 0.10.8 it prints "this ARC is 1.0.8", and "0.0.6" for an older copy, because
  `ARC_VERSION_NUM` 1008 is split as digits. It should show 0.10.8.

**Droids' workaround.** The three literals are split (`"arc_packs" ".txt"`, `arc_fmt("%s.arcpak", name)`,
`"view" ".png"`) and the comment was reworded.

## 6. `editor rename` keeps only the first word

`editor rename Cargo Hold` names the layer "Cargo". `editor rename "Cargo Hold"` names it `"Cargo` (with the quote).
The handler uses `argv[2]` only (`arc_editor.c`, the `rename` branch). Droids' deck names have spaces ("Cargo Hold",
"Engineering Bay"). The fix: join `argv[2..argc-1]` with spaces, and/or honour quotes in the command splitter.

## Acceptance

In words:
- **1:** a test web build (or the loader's stand-in fetcher) has two packs whose items are interleaved in one group:
  A's file, B's file, A's file, ... With B finishing its download first, every file loads and none logs "cannot load".
- **2:** queuing a mesh from `tick()` inside a background group prints no "loaded during tick()" hint. A plain load
  from `tick()` still does.
- **3:** `arc_pack_fetch("x.arcpak")` with no manifest and no such file returns a done group and logs nothing.
- **5:** `arc_doctor` on a game that names an asset only in a comment, or names `arc_packs.txt`, reports no ERROR. The
  version note reads "0.10.8".
- **6:** with a level file open in the editor, `editor rename Cargo Hold` gives `editor.layer_name == "Cargo Hold"` in
  the dump, and so does `editor rename "Cargo Hold"`.

## Done in ARC 0.10.9

All six are in. Each workaround can come out:

1. **Look-ahead past unmounted packs.** Your fix: `load_ahead` stops at any pack item. The self-test
   (`tests/test_v109.c`) queues six meshes from two packs, A B A B A B, with B downloading in one poll and A in six.
   All six load, with none failed; on 0.10.8 three failed. *Workaround to remove:* fetching every pack in a group
   before queuing the models.
2. **The hint.** A load queued with `arc_load_begin` / `arc_load_end` from `tick()` no longer asks for "loaded during
   tick()". A plain load from `tick()` still does, and the "loaded N times" hint is unchanged. The self-test checks
   both.
3. **`arc_pack_fetch` without a manifest** is done at once and fetches nothing (no 404s), as on desktop. *Workaround
   to remove:* the `arc_packs.txt` check.
4. **New `arc_file_listed(path)`**: true when the file is in the assets or in a pack named by `arc_packs.txt` that has
   not failed. `arc_file_exists` is unchanged (true only once the pack is in). Use `arc_file_listed` for the optional
   lower-detail copies. `ARC_HAS_FILE_LISTED` (1009) marks it.
5. **Doctor:**
   - The asset scan reads the comment-stripped code.
   - It skips `arc_packs.txt` and the `.arcpak` names of `WEB_PACKS` (read from build.bat / build.sh).
   - It skips names passed to `arc_screenshot`, `arc_screenshot_after_frame`, `arc_image_save_png`, `arc_file_write`,
     `arc_level_save` and `arc_save_write`, and to `fopen` with "w" or "a".
   - A line containing `doctor: not an asset` is skipped.
   - The version note prints `0.10.8` (and `0.10.6`, not `0.0.6`).
   - ARC's doctor fixture (`tests/doctor_fixture/src/web.c`) has each of your cases; both build scripts check none is
     reported. *Workaround to remove:* the split literals and the reworded comment.
6. **Quoted words.** `editor rename Cargo Hold` and `editor rename "Cargo Hold"` both name the layer "Cargo Hold".
   - Every agent command now honours "double quotes": a quoted run is one word, and the quotes are dropped.
   - `editor setting KEY VALUE` takes every word after the key, too.
   - `type` still takes the raw rest of the line.
   - `tests/level_check.md` checks both forms (43/43).
