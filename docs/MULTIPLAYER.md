# Multiplayer in ARC (0.8): turn-based LAN games, real-time rollback matches (0.8.1), and online play (0.8.5)

This is the guide for adding multiplayer to an ARC game whose rules run on text commands, as HEATLINE's do
(`loadout I FRAME W0..W3 M0..M3 ST SL RAD PF PL PR PB COAT`, `move ...`, `fire ...`). The API is in `include/arc.h`
under "Multiplayer (0.8)"; `samples/netturns/netturns.c` is a complete small game that uses all of it.

## How it works, in one page

- **A command is the network message.** Your game already changes its rules state only in `command()` (that is how
  agents play it). In multiplayer, where you used to apply an order, you call `arc_net_command("move 3 5 7")` instead.
- **The host relays.** One player's game hosts (no server). It gives every command the next sequence number and sends it
  to every machine, the sender and itself included.
- **Every machine applies every command, in order, through `command()`**, at the start of a tick (after input is read,
  before `tick()`). Commands reach machines at different ticks, so the rules must not care *when*: only the order matters.
- **Turns alternate.** Player 0 (the host) first, then by player index, skipping players who are not connected. The host
  refuses commands from a player whose turn it isn't, except those sent with `ARC_NET_FREE` (chat, ready, lobby settings).
- **Everything that affects who plays is in the same sequence:** joins, leaves, rejoins, ready flags, names, the start
  and every end of turn. Every machine sees them at the same point.
- **Desyncs are caught.** After each command, each machine hashes its rules state and sends the hash to the host; on a
  mismatch every machine prints `ARC error desync seq=N tick=T` and writes a report folder for an agent to diff.
- **Offline, nothing changes.** Without a session `arc_net_command` applies locally (next tick, through `command()`), the
  local player is 0 and it is always its turn. Single-player code needs no `if (online)`.
- **On a LAN, player-hosted.** TCP for the game (port 47800), UDP broadcast for finding games (47801). Windows loads
  `ws2_32.dll` at run time: nothing to add to your build.
- **Online, through a relay (0.8.5).** Over the internet, and from the web build, the host's part is played by
  `arc_relay`, a small server with no game code, in rooms with short codes ("K7QF"); everything below works the same.
  See "Online play" at the end and `docs/RELAY.md`. Without a relay the web build runs offline only
  (`ARC_NET_UNAVAILABLE`: browsers can't accept connections).

## Step by step (a game like HEATLINE)

### 1. Describe the game once

```c
static void init(void) {
    arc_net_desc d = { 0 };
    d.game = "HEATLINE";          /* joining another game is refused */
    d.version = HEATLINE_VERSION; /* bump it whenever the rules change: a client with another one is refused */
    d.max_players = 2;
    arc_net_setup(&d);            /* used by arc_net_host(NULL), arc_net_join, the lobby and the `net` agent commands */
}
```

### 2. Send commands instead of applying them

Wherever the UI (or the AI) used to call the rules directly, send the command text:

```c
/* before */  rules_move(mech, x, y);
/* after  */  arc_net_command(arc_fmt("move %d %d %d", mech, x, y));
```

`arc_net_command` returns false when it is refused on the spot (not your turn, match not started, link down, longer
than 4095 characters or with a line break). Otherwise the command comes back through `command()` one or more ticks later,
on every machine. Commands are words separated by spaces (at most 64, as for agents); send integers rather than floats
where you can (if you must send a float, print it with `%.9g` so it reads back to the same bits).

### 3. Apply them in `command()`, and only there

```c
static bool command(const char *name, int argc, const char **argv) {
    int from = arc_net_command_from();     /* the player who sent it; -1 when an agent typed it on this machine */
    if (!strcmp(name, "move") && argc == 3) {
        int m = atoi(argv[0]);
        if (from >= 0 && G.mech[m].owner != side_of(from)) return true;   /* the host checks turns, not your rules */
        rules_move(m, atoi(argv[1]), atoi(argv[2]));
        return true;
    }
    if (!strcmp(name, "fire") && argc == 2) {
        int roll = arc_rand_range(1, 100);  /* inside command(): the match's own generator, the same on every machine */
        ...
    }
    return false;
}
```

Rules:

- **Change rules state only in `command()` and in the sequenced events (below).** Never in `tick()`, never in `draw()`.
  `tick()` may animate, interpolate, play sounds: none of that is rules state.
- **Random numbers:** `arc_rand_*()` called *inside* `command()` draw from the match generator (seeded from the host's
  seed), whatever `tick()` drew elsewhere. Don't keep your own `arc_rng` for rules unless you seed it from
  `arc_net_seed()` and only advance it in `command()`.
- **Maths:** use `arc_sinf`, `arc_cosf`, `arc_tanf`, `arc_atan2f`, `arc_asinf`, `arc_acosf`, `arc_atanf`, `arc_expf`,
  `arc_logf`, `arc_powf`, `arc_floorf`, `arc_roundf`, `arc_fmodf`, `arc_hypotf`... for anything that ends up in rules
  state. The C library's versions differ between MSVC, glibc, MinGW and Emscripten in the last bit, and a last bit is a
  desync three turns later. ARC's are bit-identical everywhere and within 1 ulp of exact. `+ - * /` and `sqrtf` are exact
  already (build with `-ffp-contract=off`, as `build.sh` does; never `-ffast-math`).
  ARC's own vector helpers that only add, multiply and take square roots (`arc_v3_add`, `arc_v3_dot`, `arc_v3_len`,
  `arc_v3_norm`, `arc_v3_cross`, `arc_lerpf`...) and `arc_wrap_angle` (fmod is exact) are safe. Anything built on
  `sinf`/`cosf`/`tanf`/`acosf` is **not**: `arc_m4_rotate_*`, `arc_m4_trs_y`, `arc_quat_axis_angle`, `arc_quat_slerp`,
  `arc_m4_perspective`. Use them for drawing only.
- **No time in the rules:** not `arc_tick_count()`, not `arc_time()`, not `arc_real_time()`. Commands land on different
  ticks on different machines.
- **No pointers, addresses, hash-map iteration order or uninitialised bytes** in anything the rules depend on or that
  `save_state` writes (the hash covers every byte: `memset` structs before filling them).

### 4. Turns

```c
if (player_clicked_end_turn) arc_net_turn_end();     /* sequenced like a command */
if (arc_net_my_turn()) { /* enable the controls */ }
```

`arc_net_turn_owner()` is the player index whose turn it is, `arc_net_turn()` the turn number (1 at the start). If the
player whose turn it is drops out, the turn waits for them (they can rejoin and carry on); the host may end it for them
with `arc_net_turn_end()` (the host's own call is allowed while that player is away). Players who are away are skipped
when the turn passes.

### 5. The match start and other events

The match starts when the host calls `arc_net_start()`. Every machine gets `ARC_NET_EV_START` at the same point in the
sequence. Set the match up there, from `arc_net_seed()`:

```c
static void net_event(const arc_net_ev *ev) {      /* arc_app.net_event */
    switch (ev->type) {
    case ARC_NET_EV_START: match_setup(arc_net_seed()); break;          /* map, deployment... */
    case ARC_NET_EV_TURN:  rules_turn_begins(ev->player); break;         /* heat dissipation, ammo... (sequenced) */
    case ARC_NET_EV_LEFT:  show_toast("%s left", ev->text); break;
    case ARC_NET_EV_DESYNC: show_toast("Out of sync!"); break;
    default: break;
    }
}
arc_app arc_main(int argc, char **argv) {
    return (arc_app){ ..., .command = command, .save_state = save_state, .load_state = load_state, .net_event = net_event };
}
```

Sequenced events (START, JOINED, LEFT, REJOINED, TURN, READY, NAME) arrive inside the tick, before `tick()`, at their
exact place among the commands: rules work done there (end-of-turn heat, new-turn resets) is deterministic. The others
(CONNECTED, DESYNC, HOST_LOST, REFUSED, VERSION) arrive when they happen: use them for messages only. You can also poll
`arc_net_event(&ev)` in `tick()`; a game that only polls gets each sequenced event before any later command, because ARC
then stops applying commands for that tick after each sequenced event.

HEATLINE's pre-match setup (loadouts) fits the lobby: each player sends `arc_net_command_ex("loadout ...",
ARC_NET_FREE)` before the start; `command()` stores it per player (`arc_net_command_from()`); START builds the match from
what was stored. Everything happened in the same order on every machine, so every machine builds the same match.

### 6. `save_state`, `load_state` and `net_state`

Give the game `save_state`/`load_state` (they also power `seek`, `rewind` and `state_check` for agents). The desync hash
uses `save_state`'s bytes. If `save_state` also holds things `tick()` changes on its own (animation timers, camera),
those differ between machines and would look like a desync: then also give `arc_app.net_state`, which writes only the
rules state:

```c
static size_t net_state(void *buf, size_t cap) { arc_writer w = { buf, cap }; arc_write(&w, &G.rules, sizeof G.rules); return w.len; }
```

Without either, the hash uses the `"game"` object of your `dump()`.

### 7. The lobby

The quickest lobby is ARC's own, drawn with the UI kit (mouse, keyboard and gamepad):

```c
static void tick(void) {
    if (G.screen == SCREEN_LOBBY) {
        int r = arc_net_lobby(NULL);                 /* name, Host a game, LAN games, join by address, players, ready, start */
        if (r == ARC_NET_LOBBY_STARTED) G.screen = SCREEN_MATCH;
        if (r == ARC_NET_LOBBY_BACK) G.screen = SCREEN_TITLE;
        return;
    }
    ...
}
```

To make your own, the same API is there: `arc_net_host`, `arc_net_find` + `arc_net_games`, `arc_net_join`,
`arc_net_players` (name, connected, ready, ping), `arc_net_set_name`, `arc_net_set_ready`, `arc_net_start`,
`arc_net_status` + `arc_net_message` (why something was refused or lost), `arc_net_leave`.

### 8. Drop-outs, rejoining, resuming

- A player whose game is still running and loses the link reconnects by itself for 30 seconds (or `arc_net_join` again),
  receives what it missed and carries on.
- A player whose game crashed starts it again and joins with the same name: it receives the whole match and catches up
  in one tick. `arc_net_catching_up()` is true while those commands are applied: skip sounds, effects and camera moves.
  If the host has not noticed yet that the old game is gone (a crash or a power cut sends nothing, and the timeout is
  10 s), the new connection replaces the old one: the same running game is recognised by a token, a restarted one (once
  the match has started) by its name. Names are not passwords: this is for a LAN of people who trust each other.
- The host can keep the match safe: `arc_net_log_save("match.log")` at every end of turn. After a host crash: start the
  game, `arc_net_host`, `arc_net_resume("match.log")` (before anyone joins); the players rejoin by name.
- `arc_net_my_turn()` is false while the link is down and while a rejoin's backlog is still being applied.

## Testing with agents (headless, no window)

Every command is also an agent command. Run two headless processes on one machine and script them:

```sh
./build.sh headless samples/netturns/netturns.c
build/netturns_headless --headless --net-name Host --cmd "net host 47900; net wait players 2; net start; autoplay 21; net wait turn_number 21; net sync; dump host.json; net wait players 1; quit" &
build/netturns_headless --headless --net-name Guest --cmd "net join localhost:47900; autoplay 21; net wait turn_number 21; net sync; dump guest.json; net leave; quit"
```

then compare the `"game"` sections of the two dumps (and `net.seq` / `net.last_hash`). Useful pieces:

- `net wait turn` (my turn, nothing of mine in flight), `net wait started`, `net wait turn_number N`, `net wait players N`
  (exactly N connected), `net wait seq N`, `net wait desync`, `net wait status lost`: the script pauses until then (60 s
  timeout, or give seconds as the last word).
- `net sync`: wait until this machine has applied everything the host had numbered when it asked: put it before a final
  `dump` on every machine.
- A per-turn loop: `net wait turn; <your AI or moves>; net end_turn`, repeated. Or give the game an `autoplay N` command
  that plays its own turns from `tick()` (see netturns): like a player clicking, it only *sends* commands.
- `--net-sim lag=80,jitter=40,loss=0.05,seed=3` on any process makes its sends late and uneven (loss stalls the stream as
  TCP retransmission would; enough losses in a row break the link). `drop_at=TICK,rejoin_at=TICK` pulls the cable on a
  client and plugs it back in. `--net-version X` pretends to be another game version (refusal tests).
- `cheat`-style debug commands typed by the agent (not sent) change one machine only: the next command shows the desync,
  and every machine writes `net_desync/seqN_pP/` (`--net-report DIR`): `dump.json`, `log.txt`, `info.json` (the hashes of
  every player at seq N), `state.bin` (the hashed bytes) - all at the same sequence number, so `diff` them.
- `--record FILE` also writes `FILE.net` (the commands with the ticks they were applied on); `--replay FILE` replays the
  match exactly, offline, including `seek` / the timeline.
- `tests/net_test.py` runs all of this (host, clients, a dropout, a crash and restart, version refusal, refused commands,
  an injected desync, resume, LAN discovery, replay) and is a good template for a game's own multiplayer test.
  `--client-exe "wine build/netturns_headless.exe"` plays Windows clients against a Linux host.
- `dump` has a `"net"` object: status, local player, seq, turn, owner, last hash, players with ping.

## Pitfalls

- **Rules in `tick()`.** "If it is my turn, apply the AI's move" in `tick()` changes state on one machine only. Send it.
- **Rules from `draw()` data**: hover, selection and camera are presentation. Send what they decide.
- **Agent commands typed into a session** run at once on that machine only (`arc_net_command_from() == -1`). Use them for
  debugging (they desync on purpose); use `net cmd TEXT` to send one as this machine's player.
- **libm in the rules**: one `sinf` in a hit-chance formula is enough. Grep for `sinf(`, `cosf(`, `powf(`, `expf(`,
  `atan2f(`, `acosf(`... in rules code, and for ARC helpers built on them (`arc_m4_rotate_*`, quaternions).
- **Floating-point build flags**: `-ffast-math` (or MSVC `/fp:fast`) breaks determinism; keep `-ffp-contract=off`.
  32-bit x86 builds using the x87 FPU are not supported for multiplayer.
- **Padding and garbage in `save_state`**: the hash sees every byte. `memset` structs, don't hash pointers.
- **Version strings**: a rules change needs a new `version`, or old and new builds will play together and desync.
- **The host's own commands** go through the same queue: after `arc_net_command` the host also sees the effect next tick.
- **One session per process.** No spectators, no host migration on a LAN (if the host quits, the clients get
  `HOST_LOST`; resume from a saved log). For internet play use the relay (below), where the room outlives every player.

# Real-time games: rollback matches (0.8.1)

Turn-based games send commands; action games send **input**. In a rollback match every machine runs the whole game every
tick from every player's input, GGPO style: `samples/netarena/netarena.c` is a complete example (2-4 ships, bullets,
pickups, sounds, particles). The lobby, LAN discovery, version check, ready flags and the seed are the 0.8 session's; from
the start the match runs over UDP.

## How it works

- **Each tick, each machine packs its own player's input** (a few bytes) and sends it to the others. Every datagram
  repeats every input the receiver has not acknowledged yet, so a lost datagram costs nothing: the next one carries it.
  Clients send to the host, which relays to the other clients (2 players: one hop; up to 4 players).
- **The input is used `input_delay` ticks later** (2 by default, at most 10, set per machine: `arc_net_set_input_delay`,
  `net delay N`, `--net-delay N`). That much lag is hidden completely; the rest is predicted.
- **A remote input that has not arrived is predicted**: the player's last known input repeated. The tick runs at once.
- **When the real input arrives and differs** from the guess a tick used, ARC loads its snapshot from before that tick and
  runs the ticks up to now again, silently, before the next tick. Snapshots are kept for the last `max_rollback` ticks
  (8 by default, up to 15: `desc.max_rollback`, `net rollback N`, `--net-rollback N`); they hold your `save_state` and
  ARC's own state (arc_rand, particles, emitters, decals, UI focus and animation, arc_vars).
- **A machine never runs more than `max_rollback` ticks past the inputs it has**: then it waits. After half a second it
  tells the game (`ARC_NET_EV_WAITING`, `arc_net_waiting_for()`), and `ARC_NET_EV_RESUMED` when it runs again.
- **Time sync:** each machine estimates how many ticks it is ahead of the others (their reported tick, plus half the round
  trip); the one ahead skips a tick now and then until they are level. So corrections stay short on both sides.
- **Desyncs:** every 10 confirmed ticks the machines compare hashes of the state (`arc_app.net_state`, else
  `save_state`'s bytes). A mismatch prints `ARC error desync tick=T ...` on every machine and writes
  `net_desync/tickT_pP/` (`info.json` with the hashes, `state.bin` = the hashed bytes AT tick T on that machine, so the
  two can be diffed directly, `dump.json` now, `inputs.txt` = every player's input up to T, `log.txt`).
- **Drop-outs:** a player whose datagrams stop for 3 s (or whose link closes, or who leaves) is dropped: the host decides
  the tick, and from that tick their input is all zeros on every machine still playing (`arc_net_input_dropped`,
  `ARC_NET_EV_DROPPED` with `ev->seq` = that tick). If the host goes, a client is alone. Rejoining a running rollback
  match is not supported.
- **Recording:** `--record FILE` writes every player's confirmed input into `FILE.net`, so `--replay FILE` plays the
  match offline exactly (seek, rewind and the timeline work on it, but not during a live match).
- **Web:** no sessions in the browser (as in 0.8); offline the same code runs (`arc_net_input(0)` is the local input).

## Making a game rollback-ready

```c
static void init(void) {
    arc_net_desc d = { 0 };
    d.game = "MYGAME"; d.version = MYGAME_VERSION; d.max_players = 2;
    d.mode = ARC_NET_ROLLBACK;                    /* the host's mode decides the match */
    arc_net_setup(&d);
}
static size_t net_input(void *buf, size_t cap) {  /* once per new tick, never while re-simulating */
    uint8_t *b = buf; b[0] = 0;
    if (arc_key_down(ARC_KEY_A)) b[0] |= 1;
    if (arc_key_down(ARC_KEY_D)) b[0] |= 2;
    if (arc_key_down(ARC_KEY_SPACE)) b[0] |= 4;
    b[1] = (uint8_t)(int8_t)(arc_pad_axis(ARC_AXIS_LX) * 127.0f);   /* quantise: it is sent every tick */
    return 2;
}
static void tick(void) {
    for (int p = 0; p < S.players; p++) {
        int n; const uint8_t *in = arc_net_input(p, &n);    /* this tick's input of player p (guessed until it arrives) */
        ship_update(&S.ship[p], in ? in[0] : 0, in ? (int8_t)in[1] : 0);
    }
    ...
}
static void net_event(const arc_net_ev *ev) {
    if (ev->type == ARC_NET_EV_START) match_setup(arc_net_seed());   /* match tick 0, every machine (again after a correction to it) */
}
arc_app arc_main(int argc, char **argv) {
    return (arc_app){ ..., .save_state = save_state, .load_state = load_state, .net_event = net_event, .net_input = net_input };
}
```

### What must be in `save_state`

**Everything `tick()` changes that a later tick reads.** Positions, velocities, timers, cooldowns, health, scores, the
state of every AI, bullets, pickups, the game phase, a countdown, your own `arc_rng` if you keep one, the tick counter
you count with. If the next tick could behave differently because of it, it must be saved. ARC adds its own part:
`arc_rand_*`, particles, emitters, decals, UI focus and animation, arc_vars. What is only drawn (camera smoothing,
interpolation, screen shake you work out in `draw()`) need not be saved.

Test it before going online: `--net-synctest N` (or `net synctest N [PLAYERS]`) runs the game alone, rolls back N ticks
EVERY tick and compares the state hashes of each tick the first and the second time. Something missing from `save_state`
shows at once: `ARC error synctest tick=T ... first differing byte B` and `net_desync/synctest_tickT/state_first.bin`,
`state_again.bin`. `net input P HEX` gives the other players an input meanwhile.

### Input rules

- The rules read **only `arc_net_input(player)`** (or, with ARC's default input, `arc_net_key_down(player, k)`,
  `arc_net_key_pressed`, `arc_net_axis`). Never `arc_key_*`, `arc_mouse`, `arc_pad_axis` in the rules: those are this
  machine's keys, now.
- Pack the input in `arc_app.net_input` (called once per new tick, before `tick()`, never while re-simulating). Keep it
  small (bits and bytes; at most `ARC_NET_INPUT_MAX` = 64): it goes out every tick, repeated until acknowledged. Mouse
  aim: send an angle in a byte or two, not pixels (windows differ in size).
- Without `net_input` ARC sends its default input (39 bytes: every key and pad button, the six pad axes): fine for a
  prototype, wasteful for a release.
- Edges (`pressed`) are worked out from this tick's and last tick's inputs, so they survive corrections.
- `arc_net_input_predicted(p)` says an input is still a guess: for presentation only (never in the rules).
- Use `arc_net_match_tick()` (0 at the start) for time in the rules, not `arc_tick_count()`: machines start their
  matches at different ARC ticks. `arc_rand_*` is reseeded from the match seed at match tick 0.

### Side effects

Ticks run more than once, so anything a tick does outside the game state needs care. ARC handles its own:

| What | During a correction (re-simulated ticks) |
|---|---|
| `arc_play*` (sounds) | Played once per (tick, sound, n-th play of that sound in that tick): a repeat is skipped (`sounds_deduped`); a sound a corrected tick plays for the first time starts now, late (`sounds_late`); one whose tick no longer plays it is stopped (`sounds_cancelled`). The game's own count of sounds equals `played - cancelled`. |
| `arc_music_*` | Ignored while re-simulating (start music from confirmed facts or from `draw()`). |
| particles, emitters, decals | Rolled back with the snapshot and run again: nothing to do. |
| `arc_event` | Held until its tick is confirmed (every input known), then logged once: the session log holds exactly the confirmed match, the same on every machine and in a replay. |
| UI kit | Its state (focus, animation) is in the snapshot. |

Your own side effects (camera shake, rumble, toasts, achievements, analytics, writing saves) must be skipped while
`arc_net_resimulating()` is true, or better, worked out in `draw()` from the state (a shake timer in the state, read by
the camera). Never keep `arc_voice` handles or other engine handles in the rolled-back state.

### Testing with agents

```sh
./build.sh headless samples/netarena/netarena.c
build/netarena_headless --headless --net-name A --net-sim lag=50,jitter=10,loss=0.05 --record a.rec --cmd "net host 47950; net wait players 2; net start; net wait started; bot 1; net wait tick 580 60; net stop_at 600; net wait confirmed 600; dump a.json; net wait alone; quit" &
build/netarena_headless --headless --net-name B --net-sim lag=50,jitter=10,loss=0.05 --record b.rec --cmd "net join localhost:47950; net wait started; bot 2; net wait tick 580 60; net stop_at 600; net wait confirmed 600; dump b.json; net leave; quit"
build/netarena_headless --headless --replay a.rec --dump-at-exit ra.json
```

- A rollback match runs **in real time even headless** (the clock paces the ticks), so `--net-sim lag=...` means what it
  says. `step N` takes N/60 s.
- `net stop_at T` holds the match at match tick T (inputs still flow); `net wait confirmed T` waits until every input
  before T is known here and every other machine has acknowledged this machine's: the states are then final and can be
  compared exactly (`dump`: the `game` sections, `net.rollback.last_hash`). `net wait alone` waits until the others left.
- Inputs: the game's own `press`/`release` (read by `net_input`), `net input HEX` (this machine's input as bytes, until
  `net input off`), or a bot in the game (netarena: `bot N`).
- `net wait tick N|confirmed N|waiting|resumed|dropped|alone|desync`, `net stats` (one `ARC net rollback ...` line:
  rollbacks, depths, re-simulated ticks, predictions and misses, skipped ticks, time waited, ping, frame advantage,
  sounds, packets, the last confirmed hash), `dump` has `net.rollback` with the same, and the FPS overlay (F3, `fps on`)
  shows a line `net d2 rb N/max avg A Pms +adv`.
- `--net-sim` on UDP: `lag`, `jitter` (reorders datagrams), `loss`, `dup` (duplicates), `freeze_at=TICK,freeze_ms=MS`
  (this machine hangs: the others wait, then recover), `drop_at=TICK` (the cable is pulled: the others drop it).
- A typed `cheat` changes one machine only: the next hash comparison reports the desync on every machine.
- `python3 tests/rollback_test.py` runs ARC's own scenarios over real UDP (lag 0/50/150, scripted inputs, 3 players,
  desync, stall, drop, sounds; every final state compared and every recording replayed); `--client-exe "wine
  /abs/path/netarena_headless.exe"` plays Windows clients against a Linux host.

### Pitfalls

- **State outside `save_state`** (a static counter, a cache, an AI's memory, a list kept in a separate pool): fine in a
  replay, wrong after a correction. Run the synctest.
- **Rules reading this machine's keys or mouse.** Read `arc_net_input`.
- **`arc_tick_count()` in the rules.** Use `arc_net_match_tick()`.
- **Side effects in `tick()`** (shake, rumble, toasts, network or file writes): they happen again on every correction.
- **Floats from the C library** (`sinf`, `atan2f`...): as for turns, use ARC's deterministic maths in the rules.
- **Big `save_state`**: it is copied every tick (and on every correction up to max_rollback times): keep it lean (pools
  of plain structs, no textures). Its cost shows in the FPS overlay's tick time.
- **Changing the rules in `command()`** during a rollback match: commands arrive at different ticks on different machines.
  Use them for chat and lobby settings only.
- **Lockstep fallback:** a game without `save_state`/`load_state` plays in lockstep (it waits for every input before each
  tick): correct, but every bit of lag is felt.

## What Droids needs for a head-to-head transfer duel (and 2-player co-op)

Droids' transfer duel is 10 seconds of two players flipping a few circuit inputs; it is a good first rollback target.

1. **Rules state in `save_state`** (being added now): the duel board (every wire, junction, pulse position and timer),
   both sides' selections and cooldowns, the clock, the result; for co-op the whole deck (droids, positions, AI states,
   doors, lifts, alert level, bullets, the player droids' energy and weapons). Then pass `--net-synctest 8` on a duel and
   on a deck before any network work.
2. **Input:** a `net_input` of 1-2 bytes for the duel (up, down, fire, plus the chosen side), a few bytes for co-op
   (movement as a byte direction, fire, transfer, the aim as an angle byte).
3. **Rules read `arc_net_input`** for both players: the duel's AI opponent becomes "player 1's input"; offline it is the
   same code with player 1 driven by the AI (as netarena's offline mode).
4. **Time:** the duel's 10-second clock and all droid timers from `arc_net_match_tick()` or the state, not the ARC tick.
5. **Side effects:** the duel's sounds through `arc_play` are fine; screen flashes, camera moves, the result banner and
   any save of progress must be skipped while `arc_net_resimulating()` (or drawn from the state).
6. **Maths:** the deterministic `arc_sinf`/`arc_atan2f`... in movement, aiming and AI.
7. **Lobby:** `arc_net_lobby()` or Droids' own menus on `arc_net_host`/`arc_net_join`/`arc_net_start`; `ARC_NET_EV_START`
   builds the duel (or deck) from `arc_net_seed()`.
8. **Tests:** the `tests/rollback_test.py` pattern with Droids' headless build: two processes, `--net-sim`, `stop_at`,
   compare dumps, replay both recordings.

# Online play: the relay (0.8.5)

A LAN host is a player's own game, and browsers cannot accept connections, so internet and web play go through
`arc_relay` (`tools/arc_relay`; how to run and deploy it: `docs/RELAY.md`). It does the host's part (numbering commands,
turns, joins and rejoins, hashes, the log) for rooms named by short codes, with the same code as a LAN host
(`src/arc_net_core.h`). For the game almost nothing changes: commands, `command()`, turns, events, `net_state`, desync
reports and rejoining work exactly as on a LAN.

## Setting it up

```c
arc_net_desc d = {0};
d.game = "chariot"; d.version = "1.2"; d.max_players = 4;
d.relay = "wss://relay.example.com ws://relay.example.com:47880";   /* each build takes the first it can speak */
arc_net_setup(&d);
```

- **Hosting:** `arc_net_host_online()`: status `ARC_NET_JOINING`, then `ARC_NET_HOSTING` once the relay has made the room.
  `ARC_NET_EV_ROOM` brings the code (also `arc_net_room()`): show it big, the other players type it. This machine is the
  room's player 0, its host: `arc_net_is_host()` is true, it starts the match (`arc_net_start()`) and may end an absent
  player's turn, as on a LAN.
- **Joining:** `arc_net_join_online("K7QF")` (lower case, spaces and dashes are fine; `"ws://host:port/K7QF"` names the
  relay too): `JOINING`, then `CONNECTED` (and `ARC_NET_EV_ROOM`). A wrong code, another game or game version or ARC
  version, a full room, or a match that has started (except for its own players, by name) is refused: `ARC_NET_REFUSED`
  and `arc_net_message()` says why ("no room ZZZZ on the relay (a wrong code, or the room has closed)").
- **The lobby:** `arc_net_lobby()` shows **Host online**, a **Room code** field and **Join by code** above the LAN part
  whenever a relay this build can speak is set (`arc_net_relay_available()`); in a room it shows the code. With a
  gamepad (or a touch screen) a grid of letter buttons appears under the field: the focus keys walk across it, A types.
  The web build shows only the online part.
- **The web build:** with a relay set, `arc_net_host()` and `arc_net_join(code)` go online by themselves, so a game whose
  lobby calls those works in the browser unchanged. The page must reach the relay with `wss://` when it is served over
  https (itch.io); `ws://` only from a plain http page (a local test).
- **Dropping out:** the room is on the relay, so it survives anyone leaving, the host included. A game whose link
  drops reconnects by itself for 30 seconds and receives what it missed; a crashed game started again rejoins with the
  same name and code and catches up in one tick (`arc_net_catching_up()`). Meanwhile the match waits on an absent
  player's turn, as on a LAN (the room's host may pass it with `arc_net_turn_end()`).
- **The room's host** is the player who made it, for as long as the room lives: if it leaves before the start and does
  not come back, the others cannot start the match (make a new room). There is no host migration.
- **Relay restarts:** the games reconnect by themselves and the first one back gives the relay the room's log; play
  carries on. Commands in flight at that moment are lost: on `ARC_NET_EV_CONNECTED` play the current turn again
  (netturns resets its "played this turn" flag there).
- **Rollback online (0.10.4):** real-time matches play through the relay too, desktop builds only (UDP; see
  `docs/RELAY.md`, "Real-time (rollback) matches"). `arc_net_resume` and LAN discovery are LAN things.
- **Desktop builds** speak `ws://` (no TLS on desktop) or `tcp://` to the relay's own port; open it next to the
  proxy's 443 (`docs/RELAY.md`).

## Simultaneous turns

For games where every player gives orders in the same turn and "next turn" runs when all are ready (Operation Chariot's
co-op minutes):

```c
d.turns = ARC_NET_TURNS_SIMULTANEOUS;        /* the host's setting decides; LAN host and relay alike */
```

- From the start every player of the match may send commands during a turn, in any order and without `ARC_NET_FREE`.
  The host (or relay) numbers them as they arrive, and every machine applies them in that one order.
- Each player ends the turn once with `arc_net_turn_end()`. Commands (except free ones) and a second end of turn after
  that are refused until the next turn. Each end is sequenced: `ARC_NET_EV_TURN_DONE` (player = who finished,
  text = "waiting for 1 3"), and `arc_net_turn_waiting()` is the bit mask of players still to finish.
- After the last end comes `ARC_NET_EV_TURN` (player `ARC_NET_EVERYONE`): run "next minute" there, at the same point on
  every machine. `arc_net_turn_owner()` is `ARC_NET_EVERYONE`; `arc_net_my_turn()` means "I have not ended this turn".
- A player who is not connected does not hold the turn up: when a drop leaves only players who have ended it, the turn
  ends right after that player's `ARC_NET_EV_LEFT`. One who rejoins before the turn ends counts again and must end it.
- The game still checks in `command()` that the sender may order that party (`arc_net_command_from()`), as always.

```c
static void net_event(const arc_net_ev *ev) {
    if (ev->type == ARC_NET_EV_TURN_DONE) G.ready[ev->player] = true;        /* show who is ready */
    if (ev->type == ARC_NET_EV_TURN) { run_minute(); memset(G.ready, 0, sizeof G.ready); }
}
```

## Testing online with agents

Run a relay on localhost and give every process `--net-relay ws://127.0.0.1:PORT` (`tcp://127.0.0.1:PORT` for the raw
protocol). The room's host prints `ARC net room K7QF relay=... player=0`: read the code from its output and start the
others with it.

```sh
./build.sh relay && ./build.sh headless samples/netturns/netturns.c
build/arc_relay --port 47880 &
build/netturns_headless --headless --net-name Ann --net-relay ws://127.0.0.1:47880 --stdin     # then: net host_online
#   -> ARC net room K7QF relay=ws://127.0.0.1:47880/ player=0
build/netturns_headless --headless --net-name Bob --net-relay ws://127.0.0.1:47880 \
    --cmd "net join_online K7QF; autoplay 21; net wait turn_number 21; net sync; dump bob.json; net wait time 1.5; net leave; quit"
```

- `net host_online`, `net join_online CODE`, `net room` (prints the code again); `net status` shows `room=`; `dump` has
  `net.room` and `net.relay`. Simultaneous turns: `net.turns` = "simultaneous" and `net.waiting`.
- Online even the room's host gets its `net sync` answer from the relay, so a player who leaves right after its own final
  `dump` can be in another machine's final dump: wait a moment first (`net wait time 1.5`, real seconds with the network
  running) so every machine dumps at the same sequence number.
- `--net-sim` works on WebSocket and TCP links to the relay as on a LAN (lag, jitter, loss stalls, `drop_at` /
  `rejoin_at`).
- `python3 tests/relay_test.py` runs ARC's online scenarios (turns, refusals, hashes and desync reports, a pulled cable,
  a crash and rejoin, version and code refusals, a full room, relay restarts, simultaneous turns on the relay and on a
  LAN host, limits). `--client-exe "wine /abs/path/netturns_headless.exe"` plays Windows clients; `--web` adds the web
  build in headless Chromium in one room with a Linux client (`--wine` adds a Windows one), 50+ commands, same hash.
