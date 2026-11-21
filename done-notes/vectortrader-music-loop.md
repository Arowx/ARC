# Request from Vector Trader: loop a section of a music track

**Now.** `arc_music_play(m, true)` loops the whole file from the start. Vector Trader's title theme (from 1.0) has a
quiet 48-second intro and then the full band; looped from the start it jumps from the loud ending back to near
silence, which sounds as if the music has stopped. 1.0 (WebAudio) looped only the full section, from 47.99 s to the
end, with a short crossfade written into the end.

**Workaround in the game.** The theme is cut offline into `theme_intro.mp3` (played once) and `theme_loop.mp3`
(looped), and tick() polls `arc_music_playing(intro)` to start the loop. It works, but needs two files, an ffmpeg step
whenever the music changes, and a frame of polling that can leave a small gap.

## Ask

```c
/* loop from start_s to end_s (seconds; end_s 0 = the end of the file) when the track loops; the first play still
   starts at 0, so an intro plays once. Seamless, like today's whole-file loop. */
ARC_API void arc_music_set_loop(arc_music m, float start_s, float end_s);
```

Optionally a crossfade length (`arc_music_set_loop_fade(m, seconds)`), since a loop point inside a track rarely lines
up perfectly; 1.0 used 0.08 s.

## Acceptance

```arc-check
game samples/demo/demo.c
do set music_loop_test 1; step 600
expect game.music_position >= 2
expect game.music_position < 6
```

(The demo would play a short test track with `arc_music_set_loop(m, 2, 6)` and report `arc_music_position` in its
dump; after 10 s of play the position is back inside the loop, never before 2 s.)

## Done in ARC 0.10.13

- `arc_music_set_loop(m, start_s, end_s)`: a looping track plays from 0 to `end_s` once, then loops `start_s` to
  `end_s`. This works for MP3 and QOA, sample-exact. (0, 0) loops the whole file again. `arc_music_position` stays
  inside the section.
- `arc_music_set_loop_fade(m, seconds)` (0..2): crossfades the section's end into its start with equal-power gains.
  0 is a hard, sample-exact jump.
- **Headless:** headless runs have no audio mixer, so music never advanced there. The new flag `--audio-sim` runs
  the mixer at the tick rate with no sound card. Add `args --audio-sim` to an arc-check block that checks music
  positions.
- `tests/music_loop_check.md` (demo var `music_loop_test`, `tests/loop_test.qoa`: 8 s, loop 2..6) checks that the
  position is inside 2..6 after 10 s. The self-test compares the mixed samples with the decoded file for QOA and
  MP3, and checks the crossfade has no step at the seam.
- `#if ARC_HAS_MUSIC_LOOP`.
