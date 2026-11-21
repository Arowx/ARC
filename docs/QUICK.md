# ARC quick reference

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Every public name, one line each, grouped by topic. Open the topic page for signatures and notes;
`docs/API.md` has everything on one page. Names are C functions unless marked _(type)_ or _(macro)_.

## A whole game (tick, draw, dump)

```c
#include "arc.h"
static arc_mesh cube; static float px; static int score;
static void init(void) { cube = arc_mesh_cube(0x40A0FF); arc_mesh_set_name(cube, "player"); }
static void tick(void) {                  /* fixed 60 Hz: all game logic and UI here */
    px += ((arc_key_down(ARC_KEY_D) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_A) ? 1.0f : 0.0f)) * 4.0f * arc_dt();
    if (px > 5) { score++; px = 0; }
}
static void draw(float alpha) {           /* read-only: draw what tick decided */
    (void)alpha;
    arc_camera cam = { { 0, 4, 10 }, { 0, 0, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 100 };
    arc_set_camera(&cam);
    arc_draw_mesh(cube, arc_m4_translate(arc_v3_make(px, 0.5f, 0)), 0xFFFFFF);
    arc_draw_textf(10, 10, 2, 0xFFFFFF, "SCORE %d", score);
}
static void dump(arc_json *j) { arc_json_num_k(j, "x", px); arc_json_num_k(j, "score", score); }
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "Demo", .width = 960, .height = 540,
                      .init = init, .tick = tick, .draw = draw, .dump = dump };
}
```

## Topics

- [Math (header-only, inlined)](api/math.md) - 61 names
- [Random numbers (deterministic, PCG32)](api/random-numbers.md) - 13 names
- [Memory](api/memory.md) - 28 names
- [App, time and the main loop](api/app-time-and-the-main-loop.md) - 30 names
- [Input (sampled once per tick; deterministic and recordable)](api/input.md) - 31 names
- [Tunable variables (agents can list/set them: `vars`, `set name value`)](api/tunable-variables.md) - 3 names
- [JSON writer (for dump() and tools)](api/json-writer.md) - 14 names
- [Colours, meshes and materials](api/graphics-meshes.md) - 43 names
- [Textures and images](api/graphics-textures.md) - 27 names
- [Camera and depth](api/graphics-camera.md) - 9 names
- [Lighting, shadows, reflections and post-processing](api/graphics-lighting.md) - 41 names
- [Drawing meshes and mesh styles](api/graphics-drawing.md) - 27 names
- [Skeletal animation](api/graphics-animation.md) - 48 names
- [Particles, trails, decals and stamps](api/graphics-effects.md) - 30 names
- [Immediate geometry, quick shapes and 3D lines](api/graphics-shapes.md) - 16 names
- [2D drawing: rects, sprites, nine-slice, blend, clip](api/graphics-2d.md) - 19 names
- [Text: built-in font, baked fonts, rich text](api/graphics-text.md) - 36 names
- [Render targets, screenshots and frame timing](api/graphics-targets.md) - 26 names
- [Scene speed-ups: cells, portals, LOD, static batches](api/graphics-scene.md) - 32 names
- [Audio: synth effects, sound files, streamed music, 32-voice mixer](api/audio.md) - 75 names
- [Turns and grids (time-unit scheduler, tile grid, line of sight, paths)](api/turns-and-grids.md) - 39 names
- [UI: immediate-mode widgets (menus, settings, debug panels)](api/ui.md) - 44 names
- [UX: easing, screen transitions, captions, colour blindness, localisation](api/ux.md) - 42 names
- [Collision: ray tests, a static collision world, a character controller](api/collision.md) - 18 names
- [Physics (0.9.4): deterministic rigid bodies](api/physics.md) - 53 names
- [Navigation (0.9.2): navmeshes, paths, agents that follow them](api/navigation.md) - 28 names
- [Action (0.10.0): AI brains, senses, shots, camera rigs](api/action.md) - 80 names
- [Terrain (0.9.6): height grids, splatted ground, scattered props](api/terrain.md) - 36 names
- [Profiling](api/profiling.md) - 3 names
- [Files](api/files.md) - 5 names
- [Loading in the background (0.7.1)](api/loading-in-the-background.md) - 21 names
- [Developer tools (0.7.2): tile level editor, replay timeline](api/developer-tools.md) - 99 names
- [Multiplayer (0.8): deterministic maths, turn-based LAN sessions](api/multiplayer.md) - 95 names

## [Math (header-only, inlined)](api/math.md)

- `ARC_PI` _(macro)_
- `ARC_TAU` _(macro)_
- `arc_v2` _(type)_
- `arc_v3` _(type)_
- `arc_v4` _(type)_
- `arc_m4` _(type)_ - column-major: m[col*4+row]
- `arc_quat` _(type)_
- `arc_minf`
- `arc_maxf`
- `arc_clampf`
- `arc_lerpf`
- `arc_mini`
- `arc_maxi`
- `arc_clampi`
- `arc_wrap_angle` - wrap an angle into [-PI, PI)
- `arc_v2_make`
- `arc_v2_add`
- `arc_v2_sub`
- `arc_v2_scale`
- `arc_v2_dot`
- `arc_v2_len`
- `arc_v2_norm`
- `arc_v3_make`
- `arc_v3_add`
- `arc_v3_sub`
- `arc_v3_mul`
- `arc_v3_scale`
- `arc_v3_madd`
- `arc_v3_dot`
- `arc_v3_cross`
- `arc_v3_len`
- `arc_v3_dist`
- `arc_v3_norm`
- `arc_v3_lerp`
- `arc_m4_identity`
- `arc_m4_mul`
- `arc_m4_translate`
- `arc_m4_scale`
- `arc_m4_rotate_y`
- `arc_m4_rotate_x`
- `arc_m4_rotate_z`
- `arc_m4_trs_y` - translate * rotY(yaw) * scale — the common case for game objects, without three matrix mu…
- `arc_m4_perspective`
- `arc_m4_ortho`
- `arc_m4_look_at`
- `arc_m4_mul_point`
- `arc_m4_mul_v4`
- `arc_m4_inverse`
- `arc_m4_basis` - model matrix from an origin and three (possibly scaled, possibly mirrored) axis vectors:…
- `arc_angle_lerp` - shortest-path blend between two angles
- `arc_quat_axis_angle`
- `arc_m4_from_quat`
- `arc_quat_mul`
- `arc_quat_slerp`
- `arc_quat_rotate` - rotate a vector by a quaternion; the shortest rotation taking direction `from` to `to`
- `arc_quat_from_to`
- `arc_m4_rotate` - any axis (need not be unit length), radians
- `arc_m4_trs` - translate * rotate(q) * scale: a full transform, as glTF and most editors store them
- `arc_m4_shear` - shear: x += xy*y + xz*z, y += yx*x + yz*z, z += zx*x + zy*y (a leaning tower: arc_m4_shea…
- `arc_m4_mul_dir` - a direction through a matrix (no translation)
- `arc_m4_chain` - chain several transforms: arc_m4_chain(3, parent, local, offset) = parent * local * offset

## [Random numbers (deterministic, PCG32)](api/random-numbers.md)

- `arc_rng` _(type)_
- `arc_rng_seed`
- `arc_rng_u32`
- `arc_rng_range` - inclusive lo..hi
- `arc_rng_float` - [0,1)
- `arc_rng_rangef`
- `arc_rand_u32` - the engine's own generator, seeded from --seed (default 1); use it in tick() only
- `arc_rand_range`
- `arc_rand_float`
- `arc_rand_rangef`
- `arc_seed`
- `arc_noise_value2` - Noise (0.8.4): smooth value noise for patches, masks and density maps.
- `arc_noise_fbm2`

## [Memory](api/memory.md)

- `arc_arena` _(type)_ - Linear (bump) allocator.
- `arc_arena_init`
- `arc_arena_alloc` - zeroed; returns NULL when full
- `arc_arena_new` _(macro)_
- `arc_arena_mark`
- `arc_arena_pop`
- `arc_arena_reset`
- `arc_perm` - Engine arenas.
- `arc_frame`
- `arc_fmt` - printf into the frame arena
- `arc_persist` - A named, zeroed block that lives until the program ends and survives code reloads (hot-re…
- `arc_handle` _(type)_ - 0 = invalid.
- `arc_pool` _(type)_
- `arc_pool_init`
- `arc_pool_make` _(macro)_
- `arc_pool_add` - zeroed item; invalid handle when full
- `arc_pool_remove`
- `arc_pool_get` - NULL if stale
- `arc_pool_alive`
- `arc_pool_at`
- `arc_pool_handle_at`
- `arc_writer` _(type)_ - Snapshots for arc_app.save_state / load_state.
- `arc_reader` _(type)_
- `arc_write`
- `arc_read`
- `arc_write_pool` - the items and the free list (the pool must be the same size when read)
- `arc_read_pool`

## [App, time and the main loop](api/app-time-and-the-main-loop.md)

- `arc_json` _(type)_
- `arc_net_ev` _(type)_ - 0.8 multiplayer events (see Multiplayer below)
- `arc_app` _(type)_
- `arc_main` - YOU implement this
- `arc_event` - Play-session log (see arc_app.sessions).
- `arc_bookmark` - what F8 does: a screenshot + a "bookmark" event (note may be NULL)
- `arc_session_dir` - the folder being written, or NULL
- `arc_dt` - fixed tick length in seconds
- `arc_tick_count` - ticks simulated so far
- `arc_time` - simulated seconds = ticks * dt
- `arc_real_time` - wall-clock seconds since start (not deterministic; UI only)
- `arc_width`
- `arc_height`
- `arc_aspect`
- `arc_dpi_scale` - pixels per CSS pixel on the web with hidpi (else 1)
- `arc_headless`
- `arc_window_resized` - the window size changed since the last tick
- `arc_window_set_icon` - desktop; ignored on the web
- `arc_window_toggle_fullscreen` - borderless fullscreen on desktop, the Fullscreen API on the web
- `arc_window_fullscreen`
- `arc_app_dir` - folder of the executable, ending in '/' ("/" on the web)
- `arc_save` - Saves: small files by key.
- `arc_load` - NULL if none; zero-terminated
- `arc_save_remove`
- `arc_quit`
- `arc_log` - prints "ARC log <text>" to stdout
- `arc_argc`
- `arc_argv`
- `arc_arg_value` - "--name=value" or "--name value" -> value, else NULL
- `arc_arg_flag`

## [Input (sampled once per tick; deterministic and recordable)](api/input.md)

- `arc_key` _(type)_
- `arc_key_down` - held this tick
- `arc_key_pressed` - went down this tick
- `arc_key_released` - went up this tick
- `arc_mouse` - pixels, origin top-left
- `arc_mouse_delta` - pixels moved this tick
- `arc_mouse_capture` - 0.6.7 mouse look: while captured the pointer is hidden and held inside the window, and ar…
- `arc_mouse_captured`
- `arc_mouse_wheel` - wheel steps this tick
- `arc_key_from_name` - "A", "SPACE", "LEFT", "MOUSE_LEFT", "PAD_A"...
- `arc_key_name`
- `arc_key_any_pressed` - a keyboard key that went down this tick (for rebinding), else NONE
- `arc_text_input` - UTF-8 text typed this tick ("" if none); for name entry, chat
- `arc_axis` _(type)_ - Gamepad: buttons are arc_keys (ARC_PAD_*); sticks and triggers are axes.
- `arc_pad_axis`
- `arc_pad_connected`
- `ARC_MAX_PADS` _(macro)_ - 0.10.5 several pads (couch multiplayer): pad N is a slot that keeps its number while the…
- `arc_pad_count` - pads connected now
- `arc_pad_is_connected`
- `arc_pad_down` - button: ARC_PAD_A ..
- `arc_pad_pressed`
- `arc_pad_released`
- `arc_pad_axis_of`
- `arc_pad_first` - the pad the ARC_PAD_* keys and arc_pad_axis read (-1: none)
- `arc_pad_any_pressed` - a pad on which a button went down this tick ("press A to join"), -1: none
- `arc_touch_mode` - Touch (web): the first finger drives the mouse and MOUSE_LEFT.
- `ARC_TOUCH_MAX` _(macro)_ - Multi-touch (0.10.11): every finger, read in tick() like the keys, recorded and replayed…
- `arc_touch` _(type)_
- `arc_touch_count` - fingers listed this tick (down, or lifted this tick)
- `arc_touch_get` - NULL past the end; ordered by when they went down
- `arc_touch_find` - NULL: no such finger this tick

## [Tunable variables (agents can list/set them: `vars`, `set name value`)](api/tunable-variables.md)

- `arc_var` - Register a float the game reads every tick.
- `arc_var_set`
- `arc_var_get`

## [JSON writer (for dump() and tools)](api/json-writer.md)

- `arc_json_obj` - begin unnamed object (inside array)
- `arc_json_arr`
- `arc_json_obj_k`
- `arc_json_arr_k`
- `arc_json_end`
- `arc_json_num`
- `arc_json_int`
- `arc_json_str`
- `arc_json_bool`
- `arc_json_num_k`
- `arc_json_int_k`
- `arc_json_str_k`
- `arc_json_bool_k`
- `arc_json_v3_k` - [x,y,z]

## [Colours, meshes and materials](api/graphics-meshes.md)

- `arc_rgba` - Colours: 0xRRGGBB is opaque.
- `ARC_WHITE` _(macro)_
- `ARC_BLACK` _(macro)_
- `arc_mesh` _(type)_ - 0 = none
- `arc_texture` _(type)_ - 0 = none (white)
- `arc_vertex` _(type)_
- `arc_vcol`
- `arc_mesh_create` - Upload a mesh to the GPU once; draw it thousands of times per frame (instanced).
- `arc_mesh_destroy` - also destroys a texture the mesh file brought with it
- `arc_mesh_triangles`
- `arc_mesh_vertex_count`
- `arc_skin_vertex` _(type)_ - Skinned mesh: each vertex follows up to 4 joints of an arc_anim skeleton (weights out of…
- `arc_mesh_create_skinned`
- `arc_mesh_skinned`
- `arc_mesh_update` - Replace a mesh's vertices (same count; indices stay).
- `arc_mesh_set_texture` - A texture drawn on the mesh (with its vertex colours multiplied in).
- `arc_mesh_texture`
- `arc_material` _(type)_ - Materials (0.6): extra maps and surface settings for a mesh.
- `arc_mesh_set_material` - NULL = none (plain shading)
- `arc_mesh_material` - false if the mesh has none
- `arc_mesh_cube` - 1x1x1
- `arc_mesh_sphere` - diameter 1
- `arc_mesh_cylinder` - diameter 1, height 1
- `arc_mesh_cone` - base diameter 1, height 1, tip +Y
- `arc_mesh_plane` - 1x1 in XZ, checkerboard of tiles x tiles
- `arc_mesh_builder` _(type)_ - Mesh builder: procedural shapes with the triangle winding and normals worked out for you.
- `arc_mb_begin`
- `arc_mb_color` - for the triangles that follow
- `arc_mb_transform`
- `arc_mb_reset_transform`
- `arc_mb_tri`
- `arc_mb_quad`
- `arc_mb_box`
- `arc_mb_lathe`
- `arc_mb_extrude`
- `arc_mb_vertex_count`
- `arc_mb_finish` - frees the builder
- `arc_mesh_load` - also reads AXE .axm (AXM2) files
- `arc_mesh_from_memory`
- `arc_mesh_set_double_sided` - Draw both sides of every triangle (for models with mixed winding, open shells, foliage).
- `arc_mesh_bounds` - bounding box of a mesh in model space

## [Textures and images](api/graphics-textures.md)

- `arc_texture_create` - Textures (RGBA8).
- `arc_texture_update`
- `arc_texture_load` - .png (non-interlaced 8-bit), .qoi or baseline .jpg; sharp pixels
- `arc_set_texture_max_size` - 0.8.2: the largest side of a mipmapped texture (every mesh texture and material map, and…
- `arc_texture_max_size`
- `arc_texture_create_ex`
- `ARC_TEXTURE_ARRAY_MAX` _(macro)_ - 0.9.6 a texture array for splatting (arc_material.layers): textures of the same size (RGB…
- `arc_texture_array`
- `arc_texture_load_ex`
- `arc_image_load` - Images on the CPU (RGBA8, top row first).
- `arc_image_decode` - PNG, QOI or baseline JPEG bytes (or .arct: its first level)
- `arc_image_save_png` - RGBA if any pixel has alpha < 255, else RGB
- `arc_image_save_jpeg` - baseline 4:2:0; quality 1..100 (0 = 90)
- `arc_texture_size`
- `arc_breakup` _(type)_ - Grime overlays (0.8.4): turn a dirt photo into a see-through overlay whose alpha says whe…
- `arc_image_breakup` - size x size RGBA, free() it; NULL b = defaults
- `arc_texture_breakup` - loads the image, makes the overlay, uploads it (mipmapped, smooth, repeating)
- `arc_image_multiply`
- `arc_image_overlay`
- `arc_image_noise`
- `arc_image_char`
- `arc_texture_from_memory` - PNG, QOI, JPEG or .arct bytes
- `arc_texture_format` - as stored on the GPU: ARC_TEXFMT_BC* if uploaded compressed
- `arc_texture_format_supported` - this GPU takes fmt as it is (never in the software renderer)

## [Camera and depth](api/graphics-camera.md)

- `arc_camera` _(type)_ - 3D camera for this frame.
- `arc_depth_mode` - the mode in effect (ARC_DEPTH_STANDARD / _REVERSED / _REVERSED_INFINITE)
- `arc_depth_precise` - reversed with a float buffer and a 0..1 clip range: the full precision gain
- `arc_set_camera_override` - Replace the game's screen camera while drawing (NULL = the game's own again); render targ…
- `arc_set_hud_hidden` - Hide all 2D drawing on the screen (HUD, text, UI, FPS overlay) - for page art and trailer…
- `arc_hud_hidden`
- `arc_set_camera`
- `arc_view_proj`

## [Lighting, shadows, reflections and post-processing](api/graphics-lighting.md)

- `arc_set_light` - Lighting (per pixel): one directional light + ambient, optional rim light, linear or expo…
- `arc_set_fog` - linear; end <= start disables
- `arc_set_fog_exp` - exponential-squared: 1 - exp(-(density*d)^2); 0 disables
- `arc_set_sky` - below the horizon = horizon
- `arc_set_sky_ex`
- `arc_sky_off`
- `arc_draw_billboard` - tex {0} = a soft round dot
- `arc_draw_billboard_ex`
- `arc_draw_billboard_text` - text: height = one line (world units, or pixels with ARC_BB_PIXELS).
- `arc_set_rim` - Rim light: brightens edges that face away from the camera, so models stand out from the g…
- `arc_set_environment` - What shiny (pbr) materials reflect: a gradient from straight up (top) through the horizon…
- `ARC_MAX_LIGHTS` _(macro)_ - per frame (0.9.5; was 16 per pass)
- `ARC_MAX_SHADOW_LIGHTS` _(macro)_ - lights casting shadows per frame
- `arc_light_point`
- `arc_light_spot`
- `arc_light_count` - lights added so far this frame
- `ARC_LIGHT_SHADOW` _(macro)_ - 0.9.5: the same with flags.
- `arc_light_point_ex`
- `arc_light_spot_ex`
- `arc_set_light_shadow_resolution` - each face's size: 128, 256, 512 (default) or 1024
- `arc_light_shadow_count` - shadowed lights so far this frame
- `arc_set_shadows` - Shadows from the directional light: solid meshes cast them onto everything lit, inside a…
- `arc_set_shadow_strength`
- `arc_set_shadow_cascades` - 0.6: cascades (2 or 3; 1 = the one box above): the shadow area follows the screen camera…
- `arc_set_shadow_softness`
- `arc_mesh_set_cast_shadow`
- `arc_reflection_plane` - Planar reflections (0.8.7): polished floors, water, ice.
- `arc_mesh_set_reflective` - 0..1 (0 = not reflective)
- `arc_mesh_set_reflected` - false: left out of the mirror pass (default true)
- `arc_post` _(type)_ - Post-processing of the 3D picture (2D drawing stays untouched on top).
- `arc_lut_build` - A colour-grading table for arc_post.lut: fn maps each colour (0..1, in place) to its grad…
- `arc_set_post`
- `arc_target_set_post` - 0.8.2: post effects for one render target, for 3D drawn into a target that is then placed…
- `arc_target_set_shadow_cascades` - 0.8.2: shadow cascades (arc_set_shadow_cascades) for passes into this target, following t…
- `arc_world_to_screen` - world point -> screen pixels; returns false if behind the camera
- `arc_screen_ray` - screen pixel -> world ray
- `arc_camera_to_screen` - The same two, for any camera and view size, with no drawing state: use these in tick() (h…
- `arc_camera_ray`
- `arc_camera_view_proj`
- `arc_camera_fit` - 0.10.1: the camera moved so the box fills the view: keeps base's view direction, up and f…

## [Drawing meshes and mesh styles](api/graphics-drawing.md)

- `arc_mesh_set_name` - up to 31 characters
- `arc_mesh_name` - "" if none
- `arc_texture_set_name`
- `arc_texture_name`
- `arc_draw_mesh`
- `arc_draw_mesh_ex`
- `arc_draw_mesh_tinted` - Recolour a mesh while keeping its light/dark detail (team colours, ownership, damage): co…
- `arc_draw_mesh_alpha` - See-through and glowing meshes: drawn after everything solid, sorted back to front, witho…
- `arc_draw_mesh_additive`
- `arc_paint_pattern` _(type)_
- `arc_paint_parts_rule` - 0.10.6: joints whose name matches `pattern` (case ignored; '*' any run, '?' one character…
- `arc_mesh_style` _(type)_
- `arc_mesh_set_texture_array` - 0.9.9: a mesh takes its colour from a texture array (arc_texture_array) instead of its te…
- `arc_draw_mesh_layer`
- `arc_tile_cell` _(type)_
- `arc_draw_tiles`
- `arc_set_world_mask` - 0.9.9 fog of war for every draw at once: `mask` is a small texture, one texel per tile (c…
- `arc_set_cutaway`
- `arc_mesh_set_cutaway`
- `arc_draw_mesh_styled`
- `arc_draw_mesh_posed_styled` - 0.8.8: the posed draw with a whole arc_mesh_style (outline, x-ray, recolour, paint mask,…

## [Skeletal animation](api/graphics-animation.md)

- `ARC_MAX_JOINTS` _(macro)_ - ---- skeletal animation -----------------------------------------------------------------…
- `arc_anim` _(type)_ - a skeleton and its clips
- `arc_anim_layer` _(type)_ - time in seconds
- `arc_pose` _(type)_ - joints in model space
- `arc_anim_load` - .arca
- `arc_anim_from_memory`
- `arc_anim_destroy`
- `arc_anim_clip_count`
- `arc_anim_find` - -1 if missing
- `arc_anim_clip_name`
- `arc_anim_length` - seconds
- `arc_anim_joint_count`
- `arc_anim_joint` - -1 if missing
- `arc_anim_joint_name`
- `arc_anim_pose` - Blend clips into a pose.
- `arc_pose_joint` - where a joint is (attach a weapon to a hand: model * arc_pose_joint(&p, hand))
- `arc_draw_mesh_posed` - Draw a skinned mesh in a pose.
- `arc_anim_graph` _(type)_ - ---- animation state machines, events, IK (0.9.3) ---------------------------------------…
- `ARC_ANIM_MAX_PARAMS` _(macro)_
- `ARC_ANIM_MAX_EVENTS` _(macro)_ - events one update can report
- `ARC_ANIM_ANY` _(macro)_ - a transition from any state
- `arc_anim_op` _(type)_
- `arc_anim_graph_create`
- `arc_anim_graph_destroy`
- `arc_anim_graph_param` - index, or the existing one's
- `arc_anim_graph_find_param` - -1 if none
- `arc_anim_graph_state` - a state playing one clip at `speed` (1 = as made); returns the state's index (-1 on error)
- `arc_anim_graph_blend` - a 1D blend: clips[i] plays fully at parameter value points[i] (points rising); between tw…
- `arc_anim_graph_find_state`
- `arc_anim_graph_state_name`
- `arc_anim_graph_transition` - a transition from `from` (or ARC_ANIM_ANY) to `to`, crossfading over `fade` seconds.
- `arc_anim_graph_when` - a condition on a transition (all of a transition's conditions must hold; up to 4)
- `arc_anim_graph_event` - an event at `time` seconds into a clip; the same name gives the same id (returned) in eve…
- `arc_anim_graph_event_name`
- `arc_animator` _(type)_
- `arc_animator_init`
- `arc_animator_set`
- `arc_animator_setf` - by name; false if none
- `arc_animator_trigger`
- `arc_animator_play` - go now, whatever the transitions say
- `arc_animator_update` - in tick
- `arc_animator_pose` - tick or draw
- `arc_animator_fired` - that event passed in the last update
- `arc_animator_state_name`
- `arc_animator_progress` - how far into the current state's clip, 0..1 (blend: the phase)
- `arc_pose_two_bone_ik` - Two-bone IK: bend the chain ending at `tip` (its parent is the middle joint, the parent's…
- `arc_pose_joint_pos` - a joint's position in the pose (model space)

## [Particles, trails, decals and stamps](api/graphics-effects.md)

- `arc_fx` _(type)_ - ---- particles --------------------------------------------------------------------------…
- `arc_fx_burst` - count 0 = fx->burst
- `arc_emitter` _(type)_
- `arc_emitter_create` - emits fx->rate per second until destroyed
- `arc_emitter_move` - every tick for moving things: spawns spread along the path
- `arc_emitter_set_rate` - multiplies fx->rate (0 = pause)
- `arc_emitter_set_active`
- `arc_emitter_destroy` - stops emitting; live particles finish
- `arc_emitter_alive`
- `arc_draw_particles`
- `arc_particle_count`
- `arc_particles_clear`
- `ARC_TRAIL_MAX` _(macro)_ - Trails (0.7): a camera-facing ribbon through the last positions of a moving thing (blades…
- `arc_trail` _(type)_
- `arc_trail_style` _(type)_
- `arc_trail_push` - in tick: ages points by arc_dt(), drops old ones, adds pos
- `arc_trail_age` - in tick, nothing to add (the thing is gone): the tail catches up
- `arc_trail_clear` - teleports: start again
- `arc_trail_draw` - NULL style = defaults
- `ARC_MAX_DECALS` _(macro)_ - Decals (0.7): flat marks laid on floors and walls (scorch, blast, oil, footprints), lit l…
- `arc_decal_add` - tex {0} = soft round dot
- `arc_decal_add_ex`
- `arc_decal_fade_distance` - fade out between these camera distances (0, 0 = never)
- `arc_draw_decals`
- `arc_decal_count`
- `arc_decals_clear`
- `arc_stamps` _(type)_ - Stamps (0.8.4): scattered see-through overlays over a surface (dirt, wear, dust), made an…
- `arc_draw_stamps` - Over the floor cells covering [x0, x1) x [z0, z1) (cell (cx, cz) spans cx * cell ..) at h…
- `arc_draw_stamps_on` - Any flat surface (walls, ramps): cells_u x cells_v cells from origin, one cell = u_step b…

## [Immediate geometry, quick shapes and 3D lines](api/graphics-shapes.md)

- `arc_draw_triangles` - count = vertices, multiple of 3
- `arc_draw_tri` - flat normal from the winding
- `arc_draw_quad3` - normal zero = from winding
- `arc_draw_box` - solid axis-aligned box (instanced)
- `arc_draw_cube` - Quick shapes (use built-in meshes)
- `arc_draw_sphere`
- `arc_draw_line` - 3D lines (unlit, drawn after meshes, depth-tested)
- `arc_set_line_depth_push` - Depth offset for lines (0.6.2): lines drawn after this are pushed `fraction` further from…
- `arc_line_cap` _(type)_ - Thick and glowing lines (0.10.11).
- `arc_set_line_width`
- `arc_set_line_glow`
- `arc_set_line_cap`
- `arc_line_width`
- `arc_draw_lines`
- `arc_draw_box_lines`
- `arc_draw_grid`

## [2D drawing: rects, sprites, nine-slice, blend, clip](api/graphics-2d.md)

- `arc_draw_rect` - 2D overlay in screen pixels (origin top-left), drawn after 3D in call order.
- `arc_draw_rect_lines`
- `arc_draw_line2d`
- `arc_draw_lines2d` - 1.5 px and wider: soft edges (0.10.11)
- `arc_draw_nine` - Nine-slice: the texture's corners (src_border texels square) drawn dst_border pixels squa…
- `arc_draw_sprite`
- `arc_blend` _(type)_ - ---- 2D extras: blend modes, clipping, triangles, rotated sprites ----
- `arc_set_blend` - applies to following 2D draws; ALPHA by default each frame
- `arc_get_blend`
- `arc_push_offset2d` - 2D offsets (0.10.13): 2D drawn after arc_push_offset2d(x, y, w, h) is moved by (x, y) pix…
- `arc_pop_offset2d`
- `arc_offset2d`
- `arc_set_clip` - cut off 2D draws outside this rect (pixels of the current target)
- `arc_clear_clip`
- `arc_vertex2d` _(type)_ - color 0xAARRGGBB, alpha 0 = opaque (see arc_rgba)
- `arc_draw_triangles2d` - t {0} = untextured
- `arc_draw_texture_ex` - Draw part of a texture.
- `arc_texture_set_smooth` - bilinear (true) or sharp pixels (false)
- `arc_texture_destroy`

## [Text: built-in font, baked fonts, rich text](api/graphics-text.md)

- `arc_draw_text` - Built-in 8x8 font.
- `arc_text_width`
- `arc_draw_textf`
- `arc_text_snap_builtin`
- `arc_draw_text_ex`
- `arc_text_height`
- `arc_text_fit_scale` - the largest built-in-font scale (up to max_scale) at which text fits max_width (for fitti…
- `arc_draw_text_wrapped` - 0.8.2: word-wrapped text in a column `width` pixels wide starting at (x, y).
- `arc_text_wrapped_height`
- `arc_font` _(type)_ - ---- Bitmap fonts from TTF, baked offline by tools/font_bake.py into .arcf (one file per…
- `arc_font_load`
- `arc_font_from_memory`
- `arc_font_destroy`
- `arc_font_size` - the pixel size it was baked at (= line height)
- `arc_font_is_sdf` - 0.10.1 distance-field fonts (tools/font_bake.py --sdf): one .arcf drawn sharp at any size…
- `arc_text_snap`
- `arc_font_has`
- `arc_font_draw` - returns width drawn
- `arc_font_draw_ex`
- `arc_font_width`
- `arc_font_draw_aligned` - ARC_TEXT_*
- `arc_font_draw_wrapped` - 0.8.2: word-wrapped text in a baked font, as arc_draw_text_wrapped (lines arc_font_size *…
- `arc_font_wrapped_height`
- `arc_draw_billboard_text_font` - 3D text in a loaded font
- `arc_font_draw_glyph`
- `arc_font_advance`
- `arc_utf8_next` - decode one codepoint and advance *s (0 at the end)
- `arc_glyph` _(type)_ - One glyph, for drawing text yourself (rotated, through your own transforms): its rect in…
- `arc_font_glyph`
- `arc_rich_draw` - ---- Rich text (0.7): markup inside one string, only read by these functions, ARC_TEXT_RI…
- `arc_rich_width`
- `arc_rich_height`
- `arc_rich_icon` - sw or sh 0 = the whole texture; up to 64 names
- `arc_rich_strip` - the plain text: tags dropped, [key=F] -> F; returns its length

## [Render targets, screenshots and frame timing](api/graphics-targets.md)

- `arc_target_create` - ---- Render targets: draw into a texture, then draw that texture (UI canvases, minimaps,…
- `arc_target_begin` - Until arc_target_end, every draw (2D and 3D) goes into `target`.
- `arc_target_end`
- `arc_target_width` - size of what is being drawn into now (the screen outside any target)
- `arc_target_height`
- `ARC_MAX_VIEWPORTS` _(macro)_ - ---- 0.10.2 split-screen: several views of one world in one window ---- Between arc_viewp…
- `arc_viewport_begin`
- `arc_viewport_end`
- `arc_viewport_current` - index of the viewport being drawn (in begin order), -1 outside
- `arc_viewport_count` - viewports this frame so far (in draw), else last frame's
- `arc_viewport_rect` - its window rectangle (as above)
- `arc_viewport_at` - the viewport under window pixel px (-1: none); local (may be NULL) = px in that viewport'…
- `arc_viewport_world_to_screen` - a world point to window pixels through viewport index's camera (false: behind it)
- `arc_screenshot` - .png, or .jpg/.jpeg (quality 90: a 1920x1080 shot is well under 1 MB)
- `arc_screenshot_after_frame` - Save a screenshot once the current frame has been drawn (from tick or draw; works headles…
- `arc_sheet_add` - 0.10.1 contact sheets for art review: register meshes in draw() with arc_sheet_add (it on…
- `arc_sheet_count` - items registered while collecting
- `arc_gfx_stats` _(type)_
- `arc_stats`
- `arc_show_fps` - The engine's FPS overlay (top left): frame rate, frame-time percentiles and the worst fra…
- `arc_fps_shown`
- `arc_frame_times` _(type)_ - Frame times (ms between rendered frames) over the last 10 seconds (600 frames).
- `arc_get_frame_times`
- `arc_frame_budget` _(type)_ - Frame budget (0.7.1): where the time of a frame goes, against a target frame time.
- `arc_get_frame_budget`
- `arc_set_frame_budget` - e.g.

## [Scene speed-ups: cells, portals, LOD, static batches](api/graphics-scene.md)

- `arc_sphere_visible` - false only when it is surely outside the view
- `arc_aabb_visible`
- `ARC_MAX_CELLS` _(macro)_ - ids 1 ..
- `arc_cell_add` - add a box to cell id (L-shaped rooms: several boxes)
- `arc_portal_add` - returns the portal (1..), 0 if full; open
- `arc_portal_set_open`
- `arc_portal_is_open`
- `arc_cells_clear` - forget every cell and portal
- `arc_cell_at` - the smallest cell box holding p (its id), 0 if none
- `arc_cell_visible` - from the current camera; id 0 = true
- `arc_cells_visible_count`
- `arc_cells_compute` - The same for any camera and view size, with no drawing state (tick-safe): fills bits (ARC…
- `arc_set_draw_cell`
- `arc_get_draw_cell`
- `arc_cells_set_culling` - default on; off = tagged meshes always drawn
- `arc_cells_debug_draw` - the wireframe above, as 3D lines now
- `arc_mesh_set_lod` - Levels of detail: lower-detail meshes drawn in place of `base` for instances that look sm…
- `arc_mesh_set_lod_distance`
- `arc_set_lod_bias` - Quality setting: bias 2 keeps detail to twice the distance, 0.5 drops it at half (0 = 1).
- `arc_set_lod_force`
- `arc_mesh_lod_level` - the level an instance would get from the current camera
- `arc_static` _(type)_ - Static batches: many draws that never move (a level's walls and floor tiles) handed over…
- `arc_static_create`
- `arc_static_add`
- `arc_static_add_cell` - -1 = arc_cell_at(its centre)
- `arc_static_set_chunk` - also split on an x/z grid this size (finer culling); 0 = none
- `arc_static_build`
- `arc_static_draw`
- `arc_static_clear` - forget the items (what was built stays until the next build)
- `arc_static_destroy`
- `arc_static_mesh_count` - merged meshes from the last build
- `arc_static_item_count`

## [Audio: synth effects, sound files, streamed music, 32-voice mixer](api/audio.md)

- `arc_wave` _(type)_
- `arc_sfx` _(type)_
- `arc_sound` _(type)_
- `arc_sound_make` - render a synth effect into a sample
- `arc_sound_from_pcm`
- `arc_sound_load` - .mp3, .qoa or 16-bit .wav, mono or stereo, any rate
- `arc_sound_from_memory`
- `arc_sound_length` - seconds
- `arc_voice` _(type)_ - Playing a sound returns a voice: change or stop it while it plays.
- `arc_play` - pan -1..1
- `arc_play_ex` - pitch: playback speed (1 = normal, 2 = an octave up); loop: repeat until arc_voice_stop
- `arc_voice_stop`
- `arc_voice_set_volume`
- `arc_voice_set_pan`
- `arc_voice_set_pitch`
- `arc_voice_playing`
- `arc_set_master_volume`
- `arc_set_listener` - Positional sound: place voices in the world and the mixer pans and fades them from where…
- `arc_play_3d`
- `arc_play_3d_ex`
- `arc_voice_set_position`
- `arc_voice_set_range`
- `arc_set_audio_range`
- `arc_voice_set_lowpass` - Filters and space: a low-pass per voice (muffled: behind a wall, under water; hz 0 = off)…
- `arc_voice_set_reverb` - 0..1 (default 1)
- `arc_set_reverb` - mix 0..1
- `arc_set_master_lowpass`
- `arc_stream` _(type)_ - Streams: PCM the game makes as it goes (synthesised music, speech), played back to back w…
- `arc_stream_create` - channels 1 or 2
- `arc_stream_push` - interleaved if stereo
- `arc_stream_queued` - seconds not played yet
- `arc_stream_set_volume`
- `arc_stream_clear` - drop what is queued (stop at once)
- `arc_stream_destroy`
- `arc_bus` _(type)_ - Volume groups ("buses") for a sound-settings menu: every sound plays on one.
- `arc_set_bus_volume` - 0..1 (up to 2)
- `arc_bus_volume`
- `arc_sound_set_bus`
- `arc_play_bus`
- `arc_stream_set_bus`
- `arc_audio_save_settings`
- `arc_audio_load_settings` - false if nothing was saved
- `arc_music` _(type)_ - Music: an .mp3 or .qoa file kept compressed in memory and decoded while it plays.
- `arc_music_load`
- `arc_music_from_memory`
- `arc_music_play` - from the start
- `arc_music_stop`
- `arc_music_pause`
- `arc_music_set_volume`
- `arc_music_playing`
- `arc_music_length` - seconds
- `arc_music_set_loop` - 0.10.13 a loop section: when the track loops it goes back to start_s and plays to end_s (…
- `arc_music_set_loop_fade`
- `arc_steal` _(type)_ - Variety and limits.
- `arc_sound_set_variation`
- `arc_sound_set_limit` - 0 = no limit
- `arc_sound_set_priority` - default 0; higher keeps its voice
- `arc_bus_set_limit` - voices on the bus; 0 = no limit
- `arc_audio_seed` - the variation generator (default seed 1)
- `arc_audio_random` - from that generator: per-play variety of your own
- `arc_bus_duck` - Ducking: a bus drops by db while a voice, stream or music on any of trigger_buses (a mask…
- `arc_bus_ducked` - dB it is lowered by now
- `arc_occlusion_fn` _(type)_ - from: the sound, to: the listener
- `arc_set_occlusion` - NULL = off
- `arc_voice_set_occlusion`
- `arc_set_occlusion_filter`
- `arc_sync` _(type)_ - Adaptive music.
- `arc_music_link` - lead {0}: unlink; false if rates differ
- `arc_music_fade` - level 0..1 (up to 4)
- `arc_music_set_intensity`
- `arc_music_set_tempo`
- `arc_music_position` - seconds into the track, updated by the mixer
- `arc_music_beat` - beats since the first beat (the fraction is the phase)
- `arc_music_switch`
- `arc_music_stinger`

## [Turns and grids (time-unit scheduler, tile grid, line of sight, paths)](api/turns-and-grids.md)

- `arc_turns_mode` _(type)_ - One rule set, three ways to play.
- `arc_turns_mode_name`
- `arc_actor` _(type)_
- `arc_turns` _(type)_
- `arc_turns_init`
- `arc_turns_set_mode`
- `arc_turns_frame` - once per tick
- `arc_turns_player_ready`
- `arc_turns_spend` - speed 1 = normal, 2 = twice as fast
- `arc_turns_next_ai` - index of the AI actor to act now, or -1
- `arc_turns_end_player_phase` - tactical
- `arc_turns_waiting_for_player`
- `arc_grid` _(type)_ - Tile grid of game-defined ids (uint8).
- `arc_grid_init` - cells from the arena (or malloc if a is NULL)
- `arc_grid_get`
- `arc_grid_set`
- `arc_grid_los` - Bresenham line of sight; blocks(tile) says whether a tile blocks sight (end points are no…
- `arc_grid_path_step` - breadth-first search: first 4-way step from (sx,sy) toward (tx,ty); false if unreachable.
- `arc_grid_path_length` - full path length in steps (or -1), same rules as arc_grid_path_step
- `ARC_BOARD_MAX` _(macro)_ - Boards for tactics games (0.9.9): each tile has a move cost (0 = blocked; else the points…
- `arc_board` _(type)_
- `arc_board_init` - every tile cost 1, no cover, empty
- `arc_board_set_tile`
- `arc_board_set_occupant` - 0 = empty
- `arc_board_cost` - 0 outside the board
- `arc_board_cover` - ARC_COVER_FULL outside
- `arc_board_occupant`
- `arc_board_los` - cover: the best on the way
- `arc_board_range`
- `arc_board_reach` - tiles reachable within budget points: out_cost (w * h, may be NULL) gets the cheapest cos…
- `arc_board_path` - the cheapest route: steps written to out_xy as x, y pairs (up to max, not counting the st…
- `arc_board_arc` - where (x1, y1) is seen from (x0, y0): ARC_ARC_*
- `arc_board_dir` - a facing's step; returns facing 0..7
- `arc_board_edge_tiles` - edge tiles (drop zones, exits): sides = ARC_EDGE_* bits; free_only skips blocked and occu…

## [UI: immediate-mode widgets (menus, settings, debug panels)](api/ui.md)

- `arc_ui_begin` - a panel; "" = no title bar; drag the title to move it
- `arc_ui_panel_offset` - 0.10.12 panel positions (MechBattleArc): a panel's title bar can be dragged, and ARC keep…
- `arc_ui_set_panel_offset`
- `arc_ui_panel_rect`
- `arc_ui_end`
- `arc_ui_columns` - the next widgets sit n to a row (1 = back to one per row)
- `arc_ui_label`
- `arc_ui_labelf`
- `arc_ui_text_wrapped` - 0.8.2: a paragraph wrapped to the panel (or column) width, in the UI font
- `arc_ui_separator`
- `arc_ui_button` - true on the tick it is clicked
- `arc_ui_button_ex` - disabled: drawn faded, can't be pressed ("can't afford")
- `arc_ui_enabled` - false: the widgets that follow are disabled (until true, or the next tick)
- `arc_ui_sound_settings` - Master/Music/Effects/Interface/Voices sliders; true when one changed
- `arc_ui_toggle` - these return true when the value changed
- `arc_ui_slider`
- `arc_ui_slider_int`
- `arc_ui_select` - < item >: click a side or LEFT/RIGHT
- `arc_ui_list` - one row per item
- `arc_ui_text` - click to type; true when ENTER finishes
- `arc_ui_progress`
- `arc_ui_inspector` - every arc_var as a slider (also ` with dev keys on, or the `inspector` command)
- `arc_ui_wants_mouse` - the pointer is over a panel: don't also fire the gun
- `arc_ui_wants_keyboard` - typing in a text field, or moving the focus with keys
- `arc_ui_theme` _(type)_
- `arc_ui_set_theme` - NULL = the default dark theme
- `arc_ui_get_theme`
- `arc_ui_set_font` - {0} = the built-in font
- `arc_ui_set_skin`
- `arc_ui_set_nav` - 0.7, all off by default: Navigation: arc_ui_set_nav(true) shows the focus ring from the s…
- `arc_ui_back_pressed`
- `arc_ui_focused` - the label (up to "##") with the focus ring last tick, "" if none
- `arc_ui_set_scale`
- `arc_ui_scale`
- `arc_rectf` _(type)_ - 0.10.1 layout that fits any window (MechBattleArc's request).
- `arc_ui_scale_auto` - returns the scale now
- `arc_safe_area`
- `arc_ui_begin_anchored`
- `arc_orientation`
- `arc_ui_require_landscape`
- `arc_ui_set_rich`

## [UX: easing, screen transitions, captions, colour blindness, localisation](api/ux.md)

- `arc_ease_kind` _(type)_ - Easing (0.7): t is clamped to 0..1; every curve gives 0 at 0 and 1 at 1 (back and elastic…
- `arc_ease`
- `arc_ease_in_quad`
- `arc_ease_out_quad`
- `arc_ease_inout_quad`
- `arc_ease_in_cubic`
- `arc_ease_out_cubic`
- `arc_ease_inout_cubic`
- `arc_ease_in_back`
- `arc_ease_out_back` - overshoots ~10% then settles: pop-ups
- `arc_ease_inout_back`
- `arc_ease_out_elastic` - springs past and wobbles
- `arc_ease_out_bounce`
- `arc_ease_inout_sine`
- `arc_tween` - from + (to - from) * arc_ease(kind, t)
- `arc_transition`
- `arc_transition_covered` - the one tick the screen is fully covered
- `arc_transition_active`
- `arc_transition_amount` - how much is covered, 0..1
- `arc_caption` - up to 16 waiting; '\n' for two lines
- `arc_caption_clear`
- `arc_caption_current` - the line showing now ("" if none)
- `arc_caption_style` - scale x the UI text (0 = 1); backdrop 0 = 0xB0000000
- `arc_palette_cb`
- `arc_cvd_simulate`
- `arc_cvd_daltonize`
- `arc_set_cvd`
- `arc_get_cvd`
- `arc_cvd` - keeps the alpha byte
- `arc_cvd_lut`
- `arc_tr_load` - Localisation (0.7): string tables, one UTF-8 text file per language, `key = text` per lin…
- `arc_tr_load_memory`
- `arc_tr_set_language`
- `arc_tr_language`
- `arc_tr_set_fallback`
- `arc_tr`
- `arc_trf` - the text as a printf format, into the frame arena
- `arc_tr_has`
- `arc_tr_languages` - loaded language names, in load order
- `arc_tr_clear`

## [Collision: ray tests, a static collision world, a character controller](api/collision.md)

- `arc_hit` _(type)_ - Rays: o + d * t for t in [0, max_t] (d need not be unit length; t is in units of d).
- `arc_ray_sphere`
- `arc_ray_box`
- `arc_ray_triangle`
- `arc_ray_mesh` - any mesh, every triangle (picking)
- `arc_cworld` _(type)_ - A collision world: static level geometry (meshes, boxes, triangles) in a bounding volume…
- `arc_cworld_create`
- `arc_cworld_destroy`
- `arc_cworld_clear`
- `arc_cworld_add_mesh`
- `arc_cworld_add_box`
- `arc_cworld_add_triangles` - count = vertices
- `arc_cworld_triangles`
- `arc_cworld_raycast`
- `arc_cworld_overlap_sphere`
- `arc_cworld_push_sphere` - moves the sphere out; returns triangles touched
- `arc_character` _(type)_ - Capsule character: set velocity (x/z from input; y to jump) and call arc_character_move o…
- `arc_character_move`

## [Physics (0.9.4): deterministic rigid bodies](api/physics.md)

- `arc_physics` _(type)_ - Spheres, boxes and capsules that fall, stack, slide, bounce and roll, against each other…
- `arc_physics_desc` _(type)_
- `arc_body_desc` _(type)_
- `arc_physics_hit` _(type)_ - body 0 = static ground
- `arc_physics_event` _(type)_ - a < b; b may be 0 (the ground)
- `arc_physics_create`
- `arc_physics_destroy`
- `arc_physics_add` - the body's number, 0 if full
- `arc_physics_remove`
- `arc_physics_add_plane` - static ground: an infinite plane (points p with dot(normal, p) = d are on it; above is ou…
- `arc_physics_add_triangles` - count = vertices
- `arc_physics_add_mesh`
- `arc_physics_step` - in tick, with arc_dt()
- `arc_physics_pos`
- `arc_physics_rot`
- `arc_physics_matrix` - position and rotation (scale your mesh to the size)
- `arc_physics_vel`
- `arc_physics_ang_vel`
- `arc_physics_sleeping`
- `arc_physics_user`
- `arc_physics_body_count`
- `arc_physics_set_pos` - a teleport (velocity kept)
- `arc_physics_set_vel`
- `arc_physics_impulse` - kg m/s at a world point
- `arc_physics_force` - newtons, for the next step
- `arc_physics_move_kinematic` - kinematic bodies: reach this pose by the end of the next step (moving at the speed that t…
- `arc_physics_wake`
- `arc_physics_raycast` - the first body or ground along o + d * t, t in 0..max_t (d need not be unit length: t is…
- `arc_physics_overlap_sphere` - bodies whose shapes overlap a sphere; returns how many (writes up to max)
- `arc_physics_events` - pairs that started touching in the last step (impacts: for sounds and damage), with the i…
- `arc_physics_save` - the whole simulation state (bodies, contacts carried between steps); static ground is not…
- `arc_physics_load`
- `arc_physics_draw` - debug: shapes as lines, sleeping ones dimmer
- `arc_joint_desc` _(type)_
- `arc_physics_add_joint` - joint number (1 ...); 0 on bad input (logged)
- `arc_physics_remove_joint`
- `arc_physics_joint_angle` - a hinge's turn from angle 0: a's relative to b, about the axis (right hand)
- `arc_physics_joint_motor` - max_torque 0 = off
- `arc_physics_joint_limits`
- `arc_physics_joint_impulse` - the pivot's impulse last step (N s): break a joint above some value
- `arc_physics_joint_count`
- `arc_physics_joint_swing` - 0.9.8 ball: angle between a's axis and the cone's centre
- `arc_ragdoll_desc` _(type)_
- `arc_ragdoll` _(type)_
- `arc_ragdoll_from_pose`
- `arc_ragdoll_humanoid`
- `arc_ragdoll_pose` - the skeleton as the ragdoll lies
- `arc_ragdoll_draw` - each part as `unit` (diameter 1, height 1) scaled
- `arc_ragdoll_remove`

## [Navigation (0.9.2): navmeshes, paths, agents that follow them](api/navigation.md)

- `arc_navmesh` _(type)_ - A navmesh is the walkable surface as triangles.
- `arc_navmesh_desc` _(type)_
- `arc_navmesh_create` - NULL = defaults
- `arc_navmesh_destroy`
- `arc_navmesh_add_triangles` - count = vertices, 3 a triangle
- `arc_navmesh_add_mesh` - the mesh's upward triangles
- `arc_navmesh_add_grid` - a tile grid: every cell where walkable(id) becomes a square cell x cell at height origin.…
- `arc_navmesh_remove_box` - take out the walkable area inside a box (obstacles placed on a floor mesh): triangles who…
- `arc_navmesh_build` - false if nothing walkable
- `arc_navmesh_triangles` - walkable triangles after the build
- `arc_navmesh_islands` - separate walkable areas (not joined to each other)
- `arc_navmesh_path` - Path from `from` to `to` for an agent of `radius`: writes up to max corner points, the fi…
- `arc_navmesh_path_length`
- `arc_navmesh_nearest` - nearest point on the walkable surface within max_dist (0 = any distance); false if none
- `arc_navmesh_walkable_line` - walk a straight line over the surface from `from` toward `to` (in x/z, following its heig…
- `arc_navmesh_connected` - on the same island
- `arc_navmesh_height` - the surface's height under/over p
- `arc_navmesh_random_point` - a random walkable point, spread evenly by area; rng NULL = arc_rand (seeded by --seed)
- `arc_navmesh_draw` - debug drawing (from draw): the triangle edges in rgb, the outline of the walkable area br…
- `ARC_NAV_CORNERS` _(macro)_ - An agent that follows paths: steering, slowing down to arrive, sliding along the edge of…
- `arc_nav_agent` _(type)_
- `arc_nav_agent_init`
- `arc_nav_agent_goto` - false = no path (a->failed)
- `arc_nav_agent_stop`
- `arc_nav_agent_update` - in tick
- `arc_nav_agents_separate` - agents keep `radius` apart: pushes overlapping ones away from each other and back onto th…
- `arc_nav_agent_remaining` - distance left along the stored path
- `arc_nav_agent_draw` - its path, from draw

## [Action (0.10.0): AI brains, senses, shots, camera rigs](api/action.md)

- `ARC_BRAIN_STATES` _(macro)_ - ---- AI brains: state machines ---- Describe the states and transitions once in an arc_br…
- `ARC_BRAIN_TRANSITIONS` _(macro)_
- `ARC_BRAIN_F` _(macro)_
- `ARC_BRAIN_I` _(macro)_
- `ARC_BRAIN_V` _(macro)_
- `ARC_BRAIN_ANY` _(macro)_
- `arc_brain_cond` _(type)_
- `arc_brain_if`
- `ARC_BRAIN_IF` _(macro)_ - (a function: C++ has no compound literals)
- `ARC_BRAIN_NONE` _(macro)_
- `arc_brain` _(type)_
- `arc_brain_def` _(type)_
- `arc_brain_fn` _(type)_ - enter / exit get dt 0
- `arc_brain_test` _(type)_
- `arc_brain_def_state` - the state's number, -1 if full
- `arc_brain_def_transition` - -1 if full
- `arc_brain_def_transition_fn` - on a test of the game's own
- `arc_brain_def_find` - -1 if none
- `arc_brain_init` - blackboard zeroed; `state` entered on the first update
- `arc_brain_update` - in tick
- `arc_brain_trigger` - 0..31, for this update
- `arc_brain_go` - from a callback: switch after this tick (or at the next update)
- `arc_brain_force` - switch at the next update, whatever the transitions say
- `arc_brain_state_name`
- `arc_brain_watch` - dump and the `brain GROUP N STATE` command for an array of agents (brain inside each, `st…
- `ARC_SENSES_AGENTS` _(macro)_ - ---- Senses: sight, hearing, memory, alerts ---- An arc_senses in the game state holds up…
- `ARC_SENSES_TARGETS` _(macro)_
- `ARC_SENSES_NOISES` _(macro)_
- `ARC_SENSES_POINTS` _(macro)_
- `arc_senses_memory` _(type)_
- `arc_senses_agent` _(type)_
- `arc_senses_target` _(type)_
- `arc_senses` _(type)_
- `arc_senses_los_fn` _(type)_ - true = clear
- `arc_senses_init`
- `arc_senses_add` - the agent's number, -1 if full; defaults set
- `arc_senses_remove`
- `arc_senses_set` - every tick it moves
- `arc_senses_noise` - heard at the next update
- `arc_senses_update`
- `arc_senses_knows` - NULL if nothing
- `arc_senses_forget`
- `arc_senses_alert` - agents told
- `arc_senses_search_points` - the first is the nearest to center
- `arc_senses_los_cworld` - user = &your arc_cworld
- `arc_senses_los_physics`
- `arc_senses_draw`
- `arc_senses_watch` - in dump as "senses" (NULL s stops)
- `ARC_SHOTS_MAX` _(macro)_ - projectiles in flight
- `ARC_SHOTS_TARGETS` _(macro)_
- `ARC_SHOTS_HITS` _(macro)_
- `arc_shot_desc` _(type)_
- `arc_shot_hit` _(type)_
- `arc_shot_target` _(type)_ - a sphere (a == b) or a capsule; layer 0 = 1
- `arc_shots_set_world` - any can be {0} / NULL
- `arc_shots_set_grid` - cell (x, z) spans origin + x * cell ..; default 1, 0, 0
- `arc_shots_targets` - this tick's moving targets (copied)
- `arc_shots_fx` - register an impact effect: its number for impact_fx (keep fx alive)
- `arc_shot_fire` - the shot's number (> 0); a hitscan shot's hit is in arc_shots_hits now
- `arc_shot_cast` - the same walk with no shot, looks or state (aim checks)
- `arc_shots_update` - in tick: projectiles move, tracers age
- `arc_shots_hits` - hits made during this tick
- `arc_shots_in_flight`
- `arc_shots_clear`
- `arc_shots_draw`
- `arc_shots_save` - bytes written (or needed when buf is NULL)
- `arc_shots_load`
- `arc_rig_kind` _(type)_ - ---- Camera rigs: top-down, follow, orbit, first person ---- Keep one arc_cam_rig per vie…
- `arc_cam_rig` _(type)_
- `arc_cam_rig_set_world` - for every rig
- `arc_cam_rig_update` - lag, collision, blend and shake
- `arc_cam_rig_camera` - for arc_set_camera
- `arc_cam_rig_blend` - from becomes to, smoothly
- `arc_cam_rig_blending`
- `arc_cam_rig_shake` - trauma 0..1 added; decays by itself
- `arc_cam_rig_snap` - no lag this update (after a teleport)

## [Terrain (0.9.6): height grids, splatted ground, scattered props](api/terrain.md)

- `arc_terrain` _(type)_ - A terrain is a grid of heights drawn as chunks of triangles (lower detail further away, s…
- `arc_terrain_desc` _(type)_
- `arc_terrain_create` - {0} on bad input (logged)
- `arc_terrain_destroy`
- `arc_terrain_draw` - from draw(); chunks cast and take shadows
- `arc_terrain_drawn` - chunks the last arc_terrain_draw drew
- `arc_terrain_chunk_count`
- `arc_terrain_set_culling` - on by default; off draws every chunk
- `arc_marker` _(type)_
- `arc_draw_marker`
- `arc_terrain_height` - on the drawn triangles; outside, the nearest edge
- `arc_terrain_normal` - smooth (as lit), unit length
- `arc_terrain_inside`
- `arc_terrain_raycast` - the first hit along dir (any length) within max_dist: *dist and *point set (either may be…
- `arc_terrain_sculpt` - editing: raise (amount < 0: lower) with a smooth falloff to `radius`; flatten toward a he…
- `arc_terrain_flatten`
- `arc_terrain_set_heights` - samples_x * samples_z
- `arc_terrain_paint` - the splat map: paint a layer in (strength 0..1, smooth falloff; the other layers make roo…
- `arc_splat_rule` _(type)_
- `arc_terrain_splat_rules` - whole map from layer 0; later rules over earlier
- `arc_terrain_layer_weight`
- `arc_terrain_triangles` - full-detail triangles in world space, 3 points each: fills up to `max` points and returns…
- `arc_terrain_splat_map` - RGBA = layer weights (for a minimap)
- `arc_terrain_set_color_map` - copied now: call again after changing tex; {0} = none
- `arc_terrain_set_splat` - splat_res * splat_res RGBA weights, rows along +x from origin
- `arc_terrain_splat_res` - the splat map's side in pixels
- `arc_scatter` _(type)_
- `arc_scatter_desc` _(type)_
- `arc_scatter_create`
- `arc_scatter_destroy`
- `arc_scatter_rebuild` - after sculpting or painting: places from the same seed
- `arc_scatter_draw` - from draw(): copies in view within draw_distance
- `arc_scatter_count` - copies placed (drawn or not)
- `arc_scatter_matrix` - copy i's model matrix (0 ..
- `arc_scatter_drawn` - copies the last arc_scatter_draw drew

## [Profiling](api/profiling.md)

- `arc_prof_begin` - Named CPU timers, summed per frame.
- `arc_prof_end`
- `ARC_PROF` _(macro)_

## [Files](api/files.md)

- `arc_file_read` - Read a whole file into the given arena (NUL-terminated).
- `arc_file_write`
- `arc_file_exists`
- `arc_pack_mount` - Asset packs: one .arcpak holds many files (zlib-compressed where that helps; tools/arcpak…
- `arc_pack_mount_memory`

## [Loading in the background (0.7.1)](api/loading-in-the-background.md)

- `arc_load_group` _(type)_ - {0} = every load queued so far
- `arc_load_info` _(type)_
- `arc_load_begin` - flags: 0 or ARC_LOAD_BACKGROUND; groups do not nest
- `arc_load_end`
- `arc_load_done`
- `arc_load_progress`
- `arc_load_status`
- `arc_load_wait` - finish it now, on this thread (blocks)
- `arc_load_on_done` - at once if done already
- `arc_load_set_budget` - main-thread upload time per frame (default 4)
- `arc_texture_ready` - loaded and usable (true for any texture that was not queued)
- `arc_mesh_ready`
- `arc_sound_ready`
- `arc_music_ready`
- `arc_draw_loading_screen` - ARC's own: text over a bar, centred
- `arc_pack_fetch` - "levels.arcpak" (a URL relative to the page, or a file)
- `arc_pack_state` - ARC_PACK_*: NONE before arc_pack_fetch (or a load) asked for it
- `arc_pack_ready` - mounted (or nothing to fetch: desktop, a single-package build)
- `arc_file_listed` - 0.10.9 arc_file_exists, or a file that a pack still to come holds (listed in arc_packs.tx…

## [Developer tools (0.7.2): tile level editor, replay timeline](api/developer-tools.md)

- `arc_level` _(type)_ - ---- 0.10.8 level files (arc_level): layers of character rows, settings and objects, in a…
- `arc_level_new`
- `arc_level_load` - NULL on failure: arc_level_error()
- `arc_level_parse` - name: for error messages
- `arc_level_error` - the last load / parse / save problem ("" = none)
- `arc_level_save` - path NULL = where it was loaded or last saved
- `arc_level_reload` - read again in place (path NULL = its own)
- `arc_level_text` - the file's text (free() it)
- `arc_level_reloaded` - true once after a hot reload (rebuild what you build from it)
- `arc_level_path`
- `arc_level_free`
- `arc_level_layer_count`
- `arc_level_add_layer` - its index, -1
- `arc_level_remove_layer`
- `arc_level_layer_name`
- `arc_level_set_layer_name`
- `arc_level_width`
- `arc_level_height`
- `arc_level_cells` - the layer's cells: height rows of width chars, each row followed by a '\0' (stride width…
- `arc_level_cell` - 0 outside
- `arc_level_set_cell`
- `arc_level_get` - layer -1: the file's
- `arc_level_get_num`
- `arc_level_set` - NULL removes it
- `arc_level_set_num`
- `arc_level_setting_count`
- `arc_level_setting_key`
- `arc_level_object_count`
- `arc_level_object_add` - its index, -1
- `arc_level_object_remove` - later objects move down one
- `arc_level_object_kind`
- `arc_level_object_x`
- `arc_level_object_y`
- `arc_level_object_move`
- `arc_level_object_get`
- `arc_level_object_get_num`
- `arc_level_object_set` - NULL removes
- `arc_level_object_set_num`
- `arc_level_object_field_count`
- `arc_level_object_field_key`
- `arc_level_object_at` - first index >= from there, -1
- `arc_editor_tile` _(type)_
- `arc_editor_part` _(type)_ - 0.10.3 one mesh of a cell in the editor's own 3D view.
- `ARC_EDITOR_PARTS` _(macro)_
- `arc_editor_field` _(type)_
- `arc_editor_object` _(type)_ - 0.10.8 a kind of object the editor places (a droid spawn, a camera, a lift link)
- `arc_editor_desc` _(type)_
- `arc_editor_begin` - (again to switch grids: the undo history is cleared)
- `arc_editor_end` - closes and forgets the grid
- `arc_editor_open`
- `arc_editor_is_open`
- `arc_editor_set_playing` - the game's tick() runs while the editor is open
- `arc_editor_playing`
- `arc_editor_get` - -1 outside the grid
- `arc_editor_paint` - edits, each one undo step (false / 0 when nothing changed or out of the grid); also what…
- `arc_editor_fill` - a filled rectangle (corners in any order)
- `arc_editor_flood` - the 4-connected area of the same id
- `arc_editor_line`
- `arc_editor_undo`
- `arc_editor_redo`
- `arc_editor_undo_steps` - steps that can be undone now
- `arc_editor_save` - calls desc.save; clears the "changed" mark
- `arc_editor_load`
- `arc_editor_dirty` - edited since the last save or load
- `arc_editor_message` - the last validate message ("" = fine)
- `arc_editor_hover` - the cell under the pointer last tick
- `arc_editor_layer` - 0.10.3 the layer being edited (0 without layers)
- `arc_editor_set_layer`
- `arc_editor_set_view` - 0.10.3 ARC_EDITOR_VIEW_*; one the desc can't show is ignored
- `arc_editor_view`
- `arc_editor_level` - 0.10.8 objects, settings, copy and paste, onion skin, test-play: each edit one undo step,…
- `arc_editor_object_add` - its index (fields get their defaults), -1
- `arc_editor_object_set` - value NULL removes the field
- `arc_editor_object_move`
- `arc_editor_object_delete`
- `arc_editor_object_selected` - -1 = none
- `arc_editor_set_setting` - the current layer's (NULL removes)
- `arc_editor_set_layer_name`
- `arc_editor_select` - a rectangle (corners in any order); x0 < 0 = none
- `arc_editor_copy` - the selection's cells and objects to the clipboard
- `arc_editor_paste` - the clipboard's top-left at x, y
- `arc_editor_clear_selection` - erase tile + no objects in the selection
- `arc_editor_rotate_clipboard` - 90 degrees clockwise (FACING fields turn with it)
- `arc_editor_flip_clipboard` - left-right (or top-bottom)
- `arc_editor_onion` - the layer above (-1: layer - 1), below (+1) or none (0), faint in 2D
- `arc_editor_playtest` - calls desc.playtest (false without it)
- `arc_editor_launch_file` - --editor FILE's file ("" without)
- `arc_timeline_show` - ---- Replay timeline (scrubber) ---- A bar along the bottom of the screen over a running…
- `arc_timeline_shown`
- `arc_timeline_seek` - false (and an "ARC error" line) if it can't get there
- `arc_timeline_set_paused`
- `arc_timeline_paused`
- `arc_timeline_set_speed` - clamped to 0.25..8
- `arc_timeline_length` - the replay's length, or the newest tick of a live game
- `arc_timeline_mark_count`
- `arc_timeline_mark`

## [Multiplayer (0.8): deterministic maths, turn-based LAN sessions](api/multiplayer.md)

- `arc_sinf` - ---- Deterministic maths ---- The C library's sinf, expf, powf...
- `arc_cosf`
- `arc_tanf`
- `arc_asinf`
- `arc_acosf`
- `arc_atanf`
- `arc_atan2f`
- `arc_sqrtf`
- `arc_hypotf`
- `arc_expf`
- `arc_exp2f`
- `arc_logf`
- `arc_log2f`
- `arc_log10f`
- `arc_powf`
- `arc_floorf`
- `arc_ceilf`
- `arc_roundf` - halfway cases away from zero
- `arc_truncf`
- `arc_fmodf`
- `arc_fabsf`
- `ARC_NET_PORT` _(macro)_ - ---- Sessions: turn-based multiplayer over a LAN, hosted by a player's own game ---- The…
- `ARC_NET_MAX_PLAYERS` _(macro)_
- `arc_net_desc` _(type)_
- `arc_net_setup`
- `arc_net_host`
- `arc_net_join`
- `arc_net_leave`
- `arc_net_status` - ARC_NET_OFFLINE / HOSTING / JOINING / CONNECTED / LOST / REFUSED / UNAVAILABLE
- `arc_net_status_name` - "offline", "hosting"...
- `arc_net_message` - why the last refusal or loss happened ("" if none)
- `arc_net_desc_get` - the host's settings (client: the name, game and version it joined)
- `arc_net_is_host`
- `arc_net_player` _(type)_
- `arc_net_players` - players who ever joined, by index; returns the count
- `arc_net_local_player` - this machine's player index (0 offline, -1 before a join is accepted)
- `arc_net_set_name` - before or during a session (sequenced: everyone sees it change)
- `arc_net_name`
- `arc_net_set_ready` - lobby ready flag (sequenced)
- `arc_net_start` - host: start the match (everyone starts at the same sequence point)
- `arc_net_started` - the start has been applied here
- `arc_net_seed` - the match seed (0 before the start)
- `arc_net_command` - send a game command (see above); false = refused here at once
- `arc_net_command_ex`
- `arc_net_command_from` - inside command(): the sending player, -1 when not a net command
- `arc_net_applying` - inside command(): a net command is being applied
- `arc_net_catching_up` - applying a backlog (a rejoin) or re-simulating (seek): no effects
- `arc_net_turn_owner` - whose turn it is (-1 before the start; 0 offline)
- `arc_net_turn` - the turn number, 1 at the start (offline: 1 + turns ended)
- `arc_net_my_turn` - started (or offline) and it is the local player's turn
- `arc_net_turn_end` - end the local player's turn (sequenced like a command)
- `arc_net_seq` - the sequence number of the last command applied here
- `arc_net_event`
- `arc_net_game` _(type)_ - LAN games: arc_net_find() broadcasts a question and hosts answer over about a second (cal…
- `arc_net_find`
- `arc_net_finding`
- `arc_net_games` - answers so far (all games: compare .game yourself)
- `arc_net_log_save` - The command log: every sequenced command of the session, in order (text lines; also `net…
- `arc_net_resume`
- `arc_net_log_count`
- `arc_net_log_line` - "seq from kind flags text", into the frame arena (NULL past the end)
- `arc_net_lobby`
- `arc_net_sim` - simulate a bad network (also --net-sim): "lag=80,jitter=40,loss=0.05,drop_at=600,rejoin_a…
- `arc_net_host_online` - ask the relay for a room (the setup's desc); false = no relay / refused
- `arc_net_join_online` - "K7QF", "k7qf", "K7-QF" or "ws://relay:47880/K7QF"
- `arc_net_room` - the room code ("" when not in a relay room)
- `arc_net_relay_available` - a relay this build can speak is set (the lobby shows the online buttons)
- `ARC_NET_EVERYONE` _(macro)_ - arc_net_turn_owner() and the TURN event's player, simultaneous turns
- `arc_net_turn_waiting` - bit i = player i still to end this turn (alternating: the owner's bit)
- `ARC_NET_NO_DELAY` _(macro)_
- `ARC_NET_INPUT_MAX` _(macro)_ - bytes of one player's input for one tick
- `ARC_NET_MAX_ROLLBACK` _(macro)_
- `ARC_NET_ROLLBACK_PLAYERS` _(macro)_
- `arc_net_rollback` - a rollback match is running here (live, replayed or synctest)
- `arc_net_match_tick` - the match tick being simulated (0 at the start); -1 outside a match
- `arc_net_input` - inside tick(): a player's input for this tick (confirmed, or predicted until it arrives);…
- `arc_net_input_predicted` - that input is a guess so far (presentation only: never in rules)
- `arc_net_input_dropped` - the player has left the match as of this tick (input all zeros)
- `arc_net_key_down` - ARC's default input (games without arc_app.net_input): every key and pad button, and the…
- `arc_net_key_pressed` - down this tick, not the tick before
- `arc_net_axis` - -1..1 (0..1 triggers), in 1/127 steps
- `arc_net_resimulating` - re-running ticks after a correction: no side effects outside the state
- `arc_net_set_input_delay` - 0..10, this machine, at any time (also `net delay N`)
- `arc_net_input_delay`
- `arc_net_waiting_for` - the player the match waits for (-1 = running)
- `arc_net_rb_stats` _(type)_
- `arc_net_stats`
