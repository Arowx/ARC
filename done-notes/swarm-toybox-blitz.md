# Request from Allan (via the Vector Trader session): remake the SWARM demo as TOY BOX BLITZ, with mouse look, art, sound, music, font and juice

**Why.** `samples/demo/demo.c` (SWARM) is the first thing people build and see. Today it's code-made boxes in a
plain arena with a fixed camera and keyboard-only steering. Allan wants it to show off what ARC can do now: better
assets, mouse-look handling, sound, juice, music, a proper font, a real arena, and a fun art style and theme. He
asked for this in the Vector Trader session. That session wrote the plan, and he asked for it to be passed to the
ARC session, since only the ARC session edits ARC.

**Ask.**

*Theme and art style: TOY BOX BLITZ.* You're a wind-up tin robot defending a child's bedroom floor from swarms of
clockwork bugs. The look is bright toy plastic with black cartoon outlines (`arc_mesh_style` outline), a warm desk
lamp giving soft shadows, and a touch of bloom. It's chosen to look nothing like the template shooter's Neon Arena.

*Arena.* Keep the 20-unit arena so the existing tests still fit:
- a play-mat floor texture with roads and a grid;
- a ring of wooden alphabet blocks for walls;
- scattered blocks, crayons and a toy drum as cover, which also break up straight-line charges.

*Assets.* Make them with Ludo and import them with `arc_import.py` into `samples/demo/assets/`, keeping the total
around 3 MB, as for `templates/shooter/assets`:
- the tin robot, rigged, with idle, run, hit and death animations and a wind-up key that spins;
- an ant (small, swarms);
- a beetle: the old kind 1 heavy, keeping its 4 HP so tests still match;
- a flea that hops (new);
- the props and the floor texture.

*Mouse look.*
- Click captures the pointer (`arc_mouse_capture`). The mouse turns a third-person camera behind the robot,
  including pitch within limits.
- WASD moves relative to the camera. Holding the left button fires at the crosshair. Esc releases the pointer.
- Tunables `mouse.sensitivity` and `mouse.invert`.
- The gamepad's right stick turns the camera.
- Keyboard-only controls and the `C` top-down camera stay, so agent scripts and `--bench` behave as now.

*Juice.* Use the 0.10.14 juice kit where it fits:
- hit-stop, screen shake, white hit flashes, squash-and-stretch pop-ins;
- recoil on the robot when it fires;
- spark and confetti bursts, floating score numbers;
- a combo meter with a multiplier, and a wave banner.

*Sound and music.* Six Ludo sound effects: toy pop-gun (shoot), plastic clack (hit), spring "sproing" (kill),
pickup, wave start, robot hurt. One upbeat toy-box music loop played with `arc_music_set_loop`.

*Font.* A chunky rounded open-licence font (Fredoka or Baloo from the google/fonts repo, OFL), baked with
`font_bake.py --sdf`, for the HUD, the banners and the title.

*Must not change:*
- every test tunable and command the engine checks use on the demo: `line_test`, `nan_test`, `text_test`,
  `save_test`, `offset2d_test`, `lines2d_test`, `music_loop_test`, `set demo_setting`, `god`, and the rest;
- the existing dump fields;
- `--bench N`;
- `tests/{line,touch,nan,text,save_path,command_clash,offset2d,lines2d,music_loop}_check.md`;
- `tests/line_width_test.py` and `sh build.sh test`, which should end ALL PASSED;
- no MSVC warnings (`warn_check`).

The headless demo should still start without assets (`--no-assets`), falling back to the code-made meshes, so the
engine tests don't depend on art.

**Game side (ready / to do).** Nothing in any game depends on this. It touches `samples/demo/` and a new
`samples/demo/assets/`, plus a new `tests/demo_check.md`, the CHANGELOG and AGENTS.md notes. The dump field names in
the check below (`cam_yaw`, `captured`, `combo`, `theme`) are suggestions; rename them freely.

## Acceptance

```arc-check
game samples/demo/demo.c
args --size 1280x720
do step 30
expect game.theme == "toybox"
expect frame.meshes[name=player].on_screen == 1
expect count(frame.meshes[name=block]) >= 8
# mouse look: a captured pointer turns the camera, and W then moves the way the camera faces
do click; step 2; mouse 900 360; step 5
expect game.captured == true
expect game.cam_yaw != 0
do press W; step 30; release W
expect game.score >= 0
# juice and audio are wired up
do step 600
expect game.combo >= 0
expect frame.texts[*].text contains "SCORE"
shot toybox.png
```


## Done in ARC 0.10.18

The demo is now TOY BOX BLITZ: a wind-up tin robot defending a child's bedroom floor from clockwork bugs.

- **Art:** made with Ludo and imported with `arc_import.py` into `samples/demo/assets/` (3.5 MB). `manifest.json` has
  the prompt and import options for each file.
  - The tin robot: rigged, with idle, run (jog), hit and death clips. A code-made key spins on its back.
  - The ant, the beetle (still 4 HP) and a new flea that hops in arcs toward you.
  - Alphabet blocks, three crayons and a toy drum. The blocks form the wall ring (84) and some of the cover.
  - The play-mat floor texture.
- **Look:** black cartoon outlines on the robot, bugs and cover; a warm desk-lamp light with shadows; light bloom and
  a touch of tilt-shift for the toy look. Ludo's GLBs come marked double-sided, which turns the outline hull off, so
  the demo sets them one-sided.
- **Mouse look:**
  - A click captures the pointer. The mouse turns a third-person camera (pitch limited); the right stick does too.
  - WASD and the left stick move relative to the camera. Holding the mouse button fires at the crosshair.
  - ESC lets go, and so does switching to the `C` top view.
  - `mouse.sensitivity` and `mouse.invert` are tunables.
  - No capture over the UI or with fingers.
  - Keyboard-only play is as before with the camera unturned, so old agent scripts and `--bench` behave the same.
- **Juice:** hit-stop (longer for beetles and when you're hurt), camera shake, white hit flashes, squash-and-stretch
  pop-ins and hops, recoil, sparks and confetti, `arc_popup_text` scores, a combo meter (x2 at 5 kills in a row, up
  to x5), a wave banner and a title card.
- **Sound and font:** six Ludo sound effects with `arc_play_var` pitch variation, and a 60-second toy-box loop with
  `arc_music_set_loop_fade`. The HUD, banners and title use Fredoka SemiBold (OFL; `fonts/OFL.txt`), baked with
  `font_bake.py --sdf`.
- **Kept:**
  - every test tunable, command and existing dump field;
  - all ten demo checks, `line_width_test.py` and `touch_replay_test.py`.
  - `line_test` and `nan_test` draw SWARM's plain arena and camera (and no post effects), so their pixel tests read
    the same.
  - With `--no-assets`, or with no assets folder, the demo runs on the code-made shapes and sounds.
  - No MSVC-style warnings (`warn_check` on the demo).
- **New dump fields:** `theme`, `art`, `cam_yaw`, `cam_pitch`, `captured`, `cam_mode`, `combo`, `best_combo`,
  `multiplier`, `kills`, `hitstop`, `shake`, `kinds`, `music`.
- **New commands:** `spawn N [kind]`, `spawn_at X Z [kind]`, `look YAW [PITCH]`, `capture [0|1]`.
- **Web:** `build.sh web` and `build.bat web` preload the demo's assets. It runs in Chromium (WebGL2).

One line of the acceptance block can't pass as written. `count(frame.meshes[name=block]) >= 8` counts mesh entries,
and there is one `block` entry, holding 92 instances. `tests/demo_check.md` uses
`frame.meshes[name=block].count >= 8` instead. The rest of the request's block passes as written, and the check file
adds:
- the sensitivity maths and camera-relative movement;
- ESC and C letting go of the pointer;
- a kill's score, popup, confetti and combo;
- the beetle's 4 hits;
- the x2 multiplier;
- the flea;
- `--no-assets`;
- `state_check`.

Ludo credits used: about 50.
