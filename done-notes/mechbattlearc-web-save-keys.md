# Request from MechBattleArc (Molten Breach): web saves collide between games on one site (bug)

**Problem.** On the web, `arc_save(key)` writes `localStorage[key]` with the key as it is. itch.io serves every HTML5
game from one origin (html-classic.itch.zone), so every ARC game on itch shares one localStorage.
- `arc_audio.c` saves volumes as `"arc_audio"`: two ARC games overwrite each other's volume settings.
- A game's generic keys (`"settings"`, `"save"`, `"run"`) collide with any other game on the site, ARC or not, and a
  game can load another game's data. Its version check might refuse it; a text format might half-parse it.

Molten Breach works around it by prefixing its own keys (`moltenbreach.run`, `moltenbreach.settings`), but it can't
fix `arc_audio`, and every other ARC game has the same problem.

**Ask.** In `plat_web.c`, namespace every key with the app's `save_folder` (it is already in `arc_app`):
`localStorage["<save_folder>/<key>"]`. When the namespaced key is missing, read the bare key once, so existing players
keep their saves, and write it back namespaced. Desktop builds already separate games by folder, so they are unchanged.
Games that already prefix (like ours) just get a longer key.
