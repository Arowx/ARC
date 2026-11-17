# Audio: synth effects, sound files, streamed music, 32-voice mixer

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Audio: synth effects, sound files, streamed music, 32-voice mixer** · [quick reference](../QUICK.md) · [full API](../API.md)

```c
typedef enum { ARC_WAVE_SQUARE, ARC_WAVE_SAW, ARC_WAVE_SINE, ARC_WAVE_NOISE, ARC_WAVE_TRIANGLE } arc_wave;
typedef struct { arc_wave wave; float freq, freq_end; float attack, sustain, decay; float volume; float duty; float vibrato_hz; float vibrato_depth; } arc_sfx;
typedef struct { uint32_t id; } arc_sound;
ARC_API arc_sound arc_sound_make(const arc_sfx *s);    // render a synth effect into a sample
ARC_API arc_sound arc_sound_from_pcm(const int16_t *mono, int frames, int rate);
ARC_API arc_sound arc_sound_load(const char *path);    // .mp3, .qoa or 16-bit .wav, mono or stereo, any rate
ARC_API arc_sound arc_sound_from_memory(const uint8_t *data, size_t size);
ARC_API float arc_sound_length(arc_sound s);    // seconds
```

Playing a sound returns a voice: change or stop it while it plays. A voice handle goes stale (and the
   calls below do nothing) once the sound ends or its voice is reused. 32 voices; the oldest is reused.

```c
typedef struct { uint32_t id; } arc_voice;
ARC_API arc_voice arc_play(arc_sound s, float volume, float pan);    // pan -1..1
```

pitch: playback speed (1 = normal, 2 = an octave up); loop: repeat until arc_voice_stop

```c
ARC_API arc_voice arc_play_ex(arc_sound s, float volume, float pan, float pitch, bool loop);
ARC_API void arc_voice_stop(arc_voice v);
ARC_API void arc_voice_set_volume(arc_voice v, float volume);
ARC_API void arc_voice_set_pan(arc_voice v, float pan);
ARC_API void arc_voice_set_pitch(arc_voice v, float pitch);
ARC_API bool arc_voice_playing(arc_voice v);
ARC_API void arc_set_master_volume(float v);
```

Positional sound: place voices in the world and the mixer pans and fades them from where the listener is
   (usually the camera: arc_set_listener(cam.eye, target - eye, up)). Volume falls off from min_dist (full) to
   max_dist (silent); defaults 1 and 40 units, changed for new voices by arc_set_audio_range. Moving voices:
   arc_voice_set_position every tick. Gains update after each tick.

```c
ARC_API void arc_set_listener(arc_v3 pos, arc_v3 forward, arc_v3 up);
ARC_API arc_voice arc_play_3d(arc_sound s, arc_v3 pos, float volume);
ARC_API arc_voice arc_play_3d_ex(arc_sound s, arc_v3 pos, float volume, float pitch, bool loop);
ARC_API void arc_voice_set_position(arc_voice v, arc_v3 pos);
ARC_API void arc_voice_set_range(arc_voice v, float min_dist, float max_dist);
ARC_API void arc_set_audio_range(float min_dist, float max_dist);
```

Filters and space: a low-pass per voice (muffled: behind a wall, under water; hz 0 = off), one for everything
   (arc_set_master_lowpass: pause menus, explosions' deafness), and a room reverb that voices feed (music and
   streams stay dry). arc_set_reverb(0, 0) turns it off; room_size 0..1 (small room .. cavern).

```c
ARC_API void arc_voice_set_lowpass(arc_voice v, float hz);
ARC_API void arc_voice_set_reverb(arc_voice v, float send);    // 0..1 (default 1)
ARC_API void arc_set_reverb(float mix, float room_size);    // mix 0..1
ARC_API void arc_set_master_lowpass(float hz);
```

Streams: PCM the game makes as it goes (synthesised music, speech), played back to back with no gaps.
   Push a chunk whenever arc_stream_queued() drops below what you want buffered (say half a second).

```c
typedef struct { uint32_t id; } arc_stream;
ARC_API arc_stream arc_stream_create(int rate, int channels);    // channels 1 or 2
ARC_API void arc_stream_push(arc_stream s, const int16_t *pcm, int frames);    // interleaved if stereo
ARC_API float arc_stream_queued(arc_stream s);    // seconds not played yet
ARC_API void arc_stream_set_volume(arc_stream s, float volume);
ARC_API void arc_stream_clear(arc_stream s);    // drop what is queued (stop at once)
ARC_API void arc_stream_destroy(arc_stream s);
```

Volume groups ("buses") for a sound-settings menu: every sound plays on one. Sounds default to SFX (set once with
   arc_sound_set_bus, or per play with arc_play_bus); music is on MUSIC; streams default to MUSIC. Bus volumes
   multiply in the mixer at once, even for voices already playing. arc_ui_sound_settings() draws the sliders;
   arc_audio_save_settings / _load_settings keep them with arc_save.

```c
typedef enum { ARC_BUS_SFX = 0, ARC_BUS_MUSIC, ARC_BUS_UI, ARC_BUS_VOICE, ARC_BUS_COUNT } arc_bus;
ARC_API void arc_set_bus_volume(arc_bus bus, float volume);    // 0..1 (up to 2)
ARC_API float arc_bus_volume(arc_bus bus);
ARC_API void arc_sound_set_bus(arc_sound s, arc_bus bus);
ARC_API arc_voice arc_play_bus(arc_sound s, arc_bus bus, float volume, float pan);
ARC_API void arc_stream_set_bus(arc_stream s, arc_bus bus);
ARC_API bool arc_audio_save_settings(void);
ARC_API bool arc_audio_load_settings(void);    // false if nothing was saved
```

Music: an .mp3 or .qoa file kept compressed in memory and decoded while it plays. MP3 loops gaplessly when
   the file has a LAME/Info tag (LAME and FFmpeg write one).

```c
typedef struct { uint32_t id; } arc_music;
ARC_API arc_music arc_music_load(const char *path);
ARC_API arc_music arc_music_from_memory(const uint8_t *data, size_t size);
ARC_API void arc_music_play(arc_music m, bool loop);    // from the start
ARC_API void arc_music_stop(arc_music m);
ARC_API void arc_music_pause(arc_music m, bool paused);
ARC_API void arc_music_set_volume(arc_music m, float volume);
ARC_API bool arc_music_playing(arc_music m);
ARC_API float arc_music_length(arc_music m);    // seconds
```

Variety and limits. A sound can vary each time it plays: pitch by up to +-semitones and volume down by up to
   volume_range (0.2 = 80..100%), drawn from the audio system's own generator (arc_audio_seed), never arc_rand_*, so
   the game's random stream is untouched. Limits: at most max_voices of a sound (or on a bus) at once; a play over
   the limit takes one of them (oldest or quietest) or, with ARC_STEAL_NONE, is refused (returns a stale voice).
   Priority: a play only takes voices of its own or lower priority (lowest first), for limits and when all 32
   voices are busy; finding none, it is refused. Everything 0 = as before. Stolen voices stop at once.

```c
typedef enum { ARC_STEAL_OLDEST, ARC_STEAL_QUIETEST, ARC_STEAL_NONE } arc_steal;
ARC_API void arc_sound_set_variation(arc_sound s, float semitones, float volume_range);
ARC_API void arc_sound_set_limit(arc_sound s, int max_voices, arc_steal steal);    // 0 = no limit
ARC_API void arc_sound_set_priority(arc_sound s, int priority);    // default 0; higher keeps its voice
ARC_API void arc_bus_set_limit(arc_bus bus, int max_voices, arc_steal steal);    // voices on the bus; 0 = no limit
ARC_API void arc_audio_seed(uint64_t seed);    // the variation generator (default seed 1)
ARC_API float arc_audio_random(float lo, float hi);    // from that generator: per-play variety of your own
```

Ducking: a bus drops by db while a voice, stream or music on any of trigger_buses (a mask: 1 << ARC_BUS_VOICE)
   plays, e.g. music under speech: arc_bus_duck(ARC_BUS_MUSIC, 1 << ARC_BUS_VOICE, 9, 0.05f, 0.5f). attack and
   release: seconds to go all the way down and back up (straight lines in dB). db 0 = off.

```c
ARC_API void arc_bus_duck(arc_bus bus, unsigned trigger_buses, float db, float attack, float release);
ARC_API float arc_bus_ducked(arc_bus bus);    // dB it is lowered by now
```

Occlusion: how much is between a positional voice and the listener, 0 (clear) .. 1 (behind a wall). The hook is
   asked when a positional voice starts and after every tick (keep it cheap: one ray, or a cached answer); or set
   an amount on any voice yourself (the hook overrides it on positional voices). It is heard as a gain and a
   low-pass that glide over 50 ms (no zipper noise); fully occluded = arc_set_occlusion_filter (0.4, 800 Hz).

```c
typedef float (*arc_occlusion_fn)(arc_v3 from, arc_v3 to, void *user);    // from: the sound, to: the listener
ARC_API void arc_set_occlusion(arc_occlusion_fn fn, void *user);    // NULL = off
ARC_API void arc_voice_set_occlusion(arc_voice v, float amount);
ARC_API void arc_set_occlusion_filter(float gain, float lowpass_hz);
```

Adaptive music. Stems: tracks of one piece (same rate and length) linked to a lead play, pause, stop and loop
   with it, locked to the sample (link them before playing the lead; their own play/stop/pause is ignored). Every
   track has a fade level under its volume (arc_music_fade glides it, 1 at load). arc_music_set_intensity drives the
   stems: the lead always plays and stem k of n (in link order) fades in as intensity goes from (k-1)/n to k/n.
   Tempo (bpm, beats per bar, first beat in seconds) lets changes wait for the next beat or bar of a track (loops
   start the grid over). arc_music_switch starts `to` (and its stems) from the top on the next beat / bar of `from`,
   crossfading over fade seconds (0 = a cut); `from` stops when faded out. A stinger is a one-shot on the music bus
   that starts exactly on the next beat / bar. With no tempo, or the clock track not playing: at once.

```c
typedef enum { ARC_SYNC_NOW, ARC_SYNC_BEAT, ARC_SYNC_BAR } arc_sync;
ARC_API bool arc_music_link(arc_music stem, arc_music lead);    // lead {0}: unlink; false if rates differ
ARC_API void arc_music_fade(arc_music m, float level, float seconds);    // level 0..1 (up to 4)
ARC_API void arc_music_set_intensity(arc_music lead, float intensity, float seconds, arc_sync when);
ARC_API void arc_music_set_tempo(arc_music m, float bpm, int beats_per_bar, float first_beat);
ARC_API float arc_music_position(arc_music m);    // seconds into the track, updated by the mixer
ARC_API float arc_music_beat(arc_music m);    // beats since the first beat (the fraction is the phase)
ARC_API void arc_music_switch(arc_music from, arc_music to, bool loop, arc_sync when, float fade);
ARC_API arc_voice arc_music_stinger(arc_music clock, arc_sound s, float volume, arc_sync when);
```
