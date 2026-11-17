# Multiplayer (0.8): deterministic maths, turn-based LAN sessions

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Multiplayer (0.8): deterministic maths, turn-based LAN sessions** · [quick reference](../QUICK.md) · [full API](../API.md)


---- Deterministic maths ----
   The C library's sinf, expf, powf... differ in the last bit between compilers and platforms (MSVC, glibc, MinGW,
   Emscripten), so two machines running the same rules drift apart. These are computed by ARC itself and give the same
   bits everywhere (built with -ffp-contract=off, as build.sh does; never -ffast-math), within 1 ulp of the exact result.
   In a multiplayer game use them (and plain + - * / and arc_sqrtf, which IEEE makes exact) for everything that changes
   game state; drawing can keep using anything. ARC's own helpers (arc_m4_rotate_*, quaternions, arc_v3_len...) still use
   the C library so old games draw exactly as before: don't feed their results into game state of a multiplayer game.

```c
ARC_API float arc_sinf(float x);
ARC_API float arc_cosf(float x);
ARC_API float arc_tanf(float x);
ARC_API float arc_asinf(float x);
ARC_API float arc_acosf(float x);
ARC_API float arc_atanf(float x);
ARC_API float arc_atan2f(float y, float x);
ARC_API float arc_sqrtf(float x);
ARC_API float arc_hypotf(float x, float y);
ARC_API float arc_expf(float x);
ARC_API float arc_exp2f(float x);
ARC_API float arc_logf(float x);
ARC_API float arc_log2f(float x);
ARC_API float arc_log10f(float x);
ARC_API float arc_powf(float x, float y);
ARC_API float arc_floorf(float x);
ARC_API float arc_ceilf(float x);
ARC_API float arc_roundf(float x);    // halfway cases away from zero
ARC_API float arc_truncf(float x);
ARC_API float arc_fmodf(float x, float y);
ARC_API float arc_fabsf(float x);
```

---- Sessions: turn-based multiplayer over a LAN, hosted by a player's own game ----
   The game's rules already run on text commands (its command() callback, as agents type them). In multiplayer a command
   is the network message:
     - The game calls arc_net_command("move 3 5 7") instead of changing its state. The host gives every command the next
       sequence number and sends it to everyone, the sender included; every machine (the host too) applies them strictly
       in sequence order through the game's own command() callback.
     - Where: at the start of a tick, after input is read and before tick() (and before the tile editor): every command
       that has arrived is applied then, in order. A game without arc_app.net_event stops the batch after each sequenced
       event (start, join, leave, turn...), so tick() sees that event (arc_net_event) before any later command.
       A command takes effect one tick after arc_net_command at the soonest; different machines apply it on different
       ticks, so the rules must not depend on when: change game state ONLY in command() (and in the sequenced events),
       never in tick(). tick() may animate, but whatever it changes is not game state (keep it out of net_state).
     - Inside command(), arc_net_command_from() is the player who sent it (-1 for commands typed by an agent or the
       console, which are applied at once on this machine only) and arc_rand_*() draws from the match's own generator
       (seeded from the host's seed), so random rules (hit rolls) agree everywhere whatever tick() drew.
     - Offline (no session) arc_net_command applies locally the same way (next tick, through command()), so
       single-player code needs no branches. Offline the local player is 0, the only player, and it is always its turn.
   Turns alternate: player 0 (the host) first, then by player index, skipping players who are not connected. Commands from
   a player whose turn it isn't are refused by the host (an ARC_NET_EV_REFUSED event for the sender), except those sent
   with ARC_NET_FREE (chat, ready, lobby settings). Before the match starts only free commands are accepted. If the
   player whose turn it is drops out, the turn waits for them (they can rejoin and carry on); the host may end it for them
   with arc_net_turn_end(). Join, leave, rejoin, ready, name, start and turn changes are part of the same sequence, so
   every machine sees them at the same point.
   Joining needs the same game name, game version string and ARC version as the host (else both sides get a clear
   "version mismatch" message). A player who drops out and comes back (same name, or the same running game) gets the
   commands they missed and catches up at once (arc_net_catching_up() is true meanwhile: skip sounds and effects).
   Desyncs: after every applied command each machine hashes its state (arc_app.net_state, else save_state, else the dump's
   "game" object, plus ARC's own session state) and sends the hash to the host. On a mismatch every machine prints
   "ARC error desync seq=N tick=T", gets ARC_NET_EV_DESYNC, and writes a report folder (net_desync/seqN_pP/, or
   --net-report DIR): info.json (hashes), dump.json, log.txt (the command log), state.bin (the hashed bytes) - all taken
   at the same sequence number on every machine, so an agent can diff them.
   Transport: TCP (port ARC_NET_PORT, or desc.port), LAN discovery over UDP (port ARC_NET_PORT + 1), polled once a frame on
   the main thread. Windows loads ws2_32.dll at run time (no extra libraries to link). The web build has no LAN sessions
   (browsers cannot accept connections): without a relay arc_net_host / arc_net_join fail with status
   ARC_NET_UNAVAILABLE; offline arc_net_command still works. With a relay set they go online (0.8.5, "Online" below).
   Agent commands: net host [PORT] | net join ADDR | net find | net games | net leave | net status | net players | net name NAME |
   net ready [0|1] | net start | net cmd TEXT | net free TEXT | net end_turn | net log [FILE] | net resume FILE |
   net sync | net wait started|turn|turn_number N|connected|players N|seq N|desync|status NAME [SECONDS] (scripts pause until
   then; `players N` = exactly N connected; `turn` = my turn with nothing of mine in flight) | net wait time SECONDS (0.8.5:
   real seconds, the network running).
   `dump` gains a "net" object. --net-sim lag=MS,jitter=MS,loss=P,drop_at=TICK,rejoin_at=TICK,timeout=MS,seed=N
   simulates a bad network on this machine's sends (deterministic for a seed). See docs/MULTIPLAYER.md.

```c
#define ARC_NET_PORT 47800
#define ARC_NET_MAX_PLAYERS 8
enum { ARC_NET_OFFLINE, ARC_NET_HOSTING, ARC_NET_JOINING, ARC_NET_CONNECTED, ARC_NET_LOST, ARC_NET_REFUSED, ARC_NET_UNAVAILABLE };
enum { ARC_NET_FREE = 1 };    // arc_net_command_ex flags: allowed whoever's turn it is, and before the match starts
typedef struct { int port; const char *name; const char *game; const char *version; int max_players; uint64_t seed; void *user; int mode; int input_delay; int max_rollback; const char *relay; int turns; } arc_net_desc;
ARC_API void arc_net_setup(const arc_net_desc *d); /* describe this game once (in init): game, version, port, players; used by
arc_net_host(NULL), arc_net_join, the lobby and the `net` agent commands */ ARC_API bool arc_net_host(const arc_net_desc *d);
ARC_API bool arc_net_join(const char *address); /* "192.168.1.20", "pc-name:47800", "localhost"; while ARC_NET_LOST
(or with the same address again) it rejoins and catches up */ ARC_API void arc_net_leave(void);
ARC_API int arc_net_status(void);    // ARC_NET_OFFLINE / HOSTING / JOINING / CONNECTED / LOST / REFUSED / UNAVAILABLE
ARC_API const char *arc_net_status_name(int status);    // "offline", "hosting"...
ARC_API const char *arc_net_message(void);    // why the last refusal or loss happened ("" if none)
ARC_API const arc_net_desc *arc_net_desc_get(void);    // the host's settings (client: the name, game and version it joined)
ARC_API bool arc_net_is_host(void);
typedef struct { int index; char name[32]; bool connected; bool ready; bool in_match; bool local; int ping_ms; } arc_net_player;
ARC_API int arc_net_players(arc_net_player *out, int max);    // players who ever joined, by index; returns the count
ARC_API int arc_net_local_player(void);    // this machine's player index (0 offline, -1 before a join is accepted)
ARC_API void arc_net_set_name(const char *name);    // before or during a session (sequenced: everyone sees it change)
ARC_API const char *arc_net_name(void);
ARC_API void arc_net_set_ready(bool ready);    // lobby ready flag (sequenced)
ARC_API bool arc_net_start(void);    // host: start the match (everyone starts at the same sequence point)
ARC_API bool arc_net_started(void);    // the start has been applied here
ARC_API uint64_t arc_net_seed(void);    // the match seed (0 before the start)
ARC_API bool arc_net_command(const char *text);    // send a game command (see above); false = refused here at once
ARC_API bool arc_net_command_ex(const char *text, int flags);
ARC_API int arc_net_command_from(void);    // inside command(): the sending player, -1 when not a net command
ARC_API bool arc_net_applying(void);    // inside command(): a net command is being applied
ARC_API bool arc_net_catching_up(void);    // applying a backlog (a rejoin) or re-simulating (seek): no effects
ARC_API int arc_net_turn_owner(void);    // whose turn it is (-1 before the start; 0 offline)
ARC_API int arc_net_turn(void);    // the turn number, 1 at the start (offline: 1 + turns ended)
ARC_API bool arc_net_my_turn(void);    // started (or offline) and it is the local player's turn
ARC_API bool arc_net_turn_end(void);    // end the local player's turn (sequenced like a command)
ARC_API uint32_t arc_net_seq(void);    // the sequence number of the last command applied here
```

Events (also given to arc_app.net_event): poll until false, e.g. in tick(). The queue holds the last 64.

```c
enum { ARC_NET_EV_NONE, ARC_NET_EV_START, ARC_NET_EV_JOINED, ARC_NET_EV_LEFT, ARC_NET_EV_REJOINED, ARC_NET_EV_TURN, ARC_NET_EV_READY, ARC_NET_EV_NAME, ARC_NET_EV_CONNECTED, ARC_NET_EV_DESYNC, ARC_NET_EV_HOST_LOST, ARC_NET_EV_REFUSED, ARC_NET_EV_VERSION, ARC_NET_EV_WAITING, ARC_NET_EV_RESUMED, ARC_NET_EV_DROPPED, ARC_NET_EV_ROOM, ARC_NET_EV_TURN_DONE };
struct arc_net_ev { int type; int player; uint32_t seq; bool sequenced; char text[160]; };
ARC_API bool arc_net_event(arc_net_ev *ev);
```

LAN games: arc_net_find() broadcasts a question and hosts answer over about a second (call it again to refresh).

```c
typedef struct { char name[48], game[48], version[32], arc_version[16]; char address[64]; int players, max_players; bool started; } arc_net_game;
ARC_API void arc_net_find(void);
ARC_API bool arc_net_finding(void);
ARC_API int arc_net_games(arc_net_game *out, int max);    // answers so far (all games: compare .game yourself)
```

The command log: every sequenced command of the session, in order (text lines; also `net log`). A host can resume a
   match from a saved log: arc_net_host, then arc_net_resume(path): it re-applies the log, and the players rejoin by name
   and receive it.

```c
ARC_API bool arc_net_log_save(const char *path);
ARC_API bool arc_net_resume(const char *path);
ARC_API int arc_net_log_count(void);
ARC_API const char *arc_net_log_line(int i);    // "seq from kind flags text", into the frame arena (NULL past the end)
```

A ready-made lobby drawn with the UI kit (call it from tick() while the game shows its lobby): name, host, LAN games,
   join by address, the player list with ready flags, start (host) and leave. Mouse, keyboard and gamepad (the UI kit's
   focus keys). desc = how to host (NULL = defaults). Returns ARC_NET_LOBBY_STARTED once the match has started here, or
   ARC_NET_LOBBY_BACK when the player backs out (Back / ESC while offline); otherwise ARC_NET_LOBBY_OPEN.

```c
enum { ARC_NET_LOBBY_OPEN, ARC_NET_LOBBY_STARTED, ARC_NET_LOBBY_BACK };
ARC_API int arc_net_lobby(const arc_net_desc *desc);
```

simulate a bad network (also --net-sim): "lag=80,jitter=40,loss=0.05,drop_at=600,rejoin_at=900,timeout=10000,seed=3";
   NULL or "" turns it off (it applies to LAN links and to WebSocket / TCP links to a relay alike)

```c
ARC_API void arc_net_sim(const char *spec);
```

---- Online: the relay (0.8.5) ----
   Between machines that are not on one LAN, and from the web build, a session goes through arc_relay (tools/arc_relay,
   docs/RELAY.md): a small server with no game code that does what a LAN host does (numbers commands and sends them to
   everyone, refuses commands out of turn, sequences joins / leaves / rejoins / turn ends, compares the state hashes, keeps
   each room's log for rejoins) for rooms named by a short code ("K7QF": 4-6 letters and digits from an alphabet with no
   0/O or 1/I/L). Everything above works the same (commands, turns, events, hashes, desync reports, rejoin, net_sim,
   the `net` agent commands); what differs:
     - arc_net_host_online() asks the relay for a new room: this machine becomes its player 0, the room's host (status
       ARC_NET_JOINING, then ARC_NET_HOSTING and arc_net_is_host(); it starts the match and may end an absent player's
       turn). ARC_NET_EV_ROOM gives the code (also arc_net_room()); show it so the others can type it.
     - arc_net_join_online("K7QF") joins it (JOINING, then CONNECTED and ARC_NET_EV_ROOM). "ws://host:port/K7QF" also
       names the relay. A wrong code is refused (ARC_NET_REFUSED, "no room ... on the relay"); so are another game, game
       version or ARC version (as on a LAN), a full room, and a started match (except its players rejoining by name).
     - The room lives on the relay, not on a player's machine: a player who drops (the host too) reconnects by itself for
       30 s, or rejoins by name with the code; the match goes on meanwhile (an absent player's turn waits, as on a LAN).
       If the relay restarts, the first players back give it the room's log and play carries on (docs/RELAY.md).
     - Transports: the web build uses the browser's WebSocket (wss:// from an https page such as itch.io); desktop builds
       speak ws:// (plain: no TLS on desktop) or tcp:// (ARC's own protocol) to the relay's plain port. The relay
       speaks both on one port; a TLS proxy (Caddy, nginx) in front gives the web its wss://.
     - 0.10.4 rollback (real-time) matches play online too, desktop builds only (UDP): after the room's start, every
       player's datagrams go to the relay's UDP port (the same number as its TCP port) and on to the others, or straight
       to them once hole punching works (it is tried at once and keeps being checked; a path that goes quiet for 1.5 s
       falls back to the relay). The lowest player in the match is the hub, doing the LAN host's part (passing inputs on,
       deciding drops). The same arc_net calls as on a LAN; `dump` shows net.rollback.online (hub, each peer's path).
       --net-relay-only (or `net direct off`) keeps every datagram on the relay. A browser has no UDP: a web build that
       hosts a rollback room is refused, and one that joins plays alone.
     - On the web, arc_net_host / arc_net_join go online by themselves when a relay is set (arc_net_join takes the code),
       and only without a relay do they fail with ARC_NET_UNAVAILABLE (browsers cannot accept connections).
   Agent commands: net host_online | net join_online CODE | net room (prints "ARC net room CODE relay=URL player=N", also
   printed when the relay gives the room); --net-relay URL sets the relay (tests run a local arc_relay).

```c
ARC_API bool arc_net_host_online(void);    // ask the relay for a room (the setup's desc); false = no relay / refused
ARC_API bool arc_net_join_online(const char *code);    // "K7QF", "k7qf", "K7-QF" or "ws://relay:47880/K7QF"
ARC_API const char *arc_net_room(void);    // the room code ("" when not in a relay room)
ARC_API bool arc_net_relay_available(void);    // a relay this build can speak is set (the lobby shows the online buttons)
```

---- Simultaneous turns (0.8.5) ----
   arc_net_desc.turns = ARC_NET_TURNS_SIMULTANEOUS (the host's setting; LAN host and relay alike): from the start every
   player in the match may send commands during a turn (in any order: the host numbers them as they arrive, and every
   machine applies them in that one order), and the turn ends when every connected player of the match has called
   arc_net_turn_end() (once each: commands or another end of turn after it are refused until the next turn). Each end
   of turn is sequenced (ARC_NET_EV_TURN_DONE, with who is still to finish); after the last one comes ARC_NET_EV_TURN
   (player ARC_NET_EVERYONE), so "next minute" runs in the TURN event, at the same point on every machine. A player who is
   not connected does not hold the turn up: if a drop leaves only players who have ended it, the turn ends right after
   that player's LEFT event; one who rejoins before the turn ends must end it too. arc_net_turn_owner() is
   ARC_NET_EVERYONE, arc_net_my_turn() is "I have not ended this turn yet". The game still checks in command() that the
   sender may order that unit (arc_net_command_from()).

```c
enum { ARC_NET_TURNS_ALTERNATE = 0, ARC_NET_TURNS_SIMULTANEOUS = 1 };
#define ARC_NET_EVERYONE (-2)    // arc_net_turn_owner() and the TURN event's player, simultaneous turns
ARC_API unsigned arc_net_turn_waiting(void);    // bit i = player i still to end this turn (alternating: the owner's bit)
```

---- Real-time rollback matches (0.8.1) ----
   For action games: every machine runs the whole simulation every tick from every player's INPUT (not commands), GGPO
   style. Set the match up with the session above (host, join, ready, start: the lobby, LAN discovery and version check
   are the same) and arc_net_desc.mode = ARC_NET_ROLLBACK (the host's mode decides). From the start the match runs over UDP:
     - Every tick each machine packs its local player's input (arc_app.net_input, or ARC's default: keys, pad buttons and
       sticks) and sends it to the others (each datagram repeats every input not yet acknowledged, so a lost one costs
       nothing). The input is used `input_delay` ticks later (2 by default), which hides that much lag completely.
     - tick() reads every player's input for this tick with arc_net_input(player, &size) (or arc_net_key_down(player, k),
       arc_net_axis... with the default input) and nothing else: no arc_key_*, no arc_mouse in the rules.
     - An input that has not arrived yet is PREDICTED (the player's last known input repeated) and the tick runs at once.
       When the real input arrives and differs, ARC loads the snapshot from before that tick (arc_app.save_state plus
       ARC's own state: arc_rand_*, particles, emitters, decals, UI focus, arc_vars) and runs the ticks again up to now,
       silently, before the next tick (arc_net_resimulating() is true meanwhile). So save_state / load_state are required,
       and must hold EVERYTHING tick() changes that a later tick reads.
     - A machine never runs more than max_rollback ticks (8) past the inputs it has: then it waits (and after half a
       second ARC_NET_EV_WAITING names the player it waits for; ARC_NET_EV_RESUMED when it runs again). Machines keep
       their clocks together: the one that is ahead skips a tick now and then (time sync), so rollbacks stay short.
     - Side effects: sounds started in tick() are played once: a sound a re-simulated tick plays again (the same sound,
       the n-th time in that tick) is not repeated, one that a corrected tick plays for the first time starts late, and
       one whose tick no longer plays it is stopped. Music calls are ignored while re-simulating. Particles, emitters and
       decals are rolled back with the snapshot. arc_event lines are held until their tick is confirmed (every input
       known), so the log holds exactly the confirmed match, identical on every machine. Anything else tick() does
       outside the game state (camera shake, UI toasts, rumble) must be skipped while arc_net_resimulating(), or be
       worked out in draw() from the state.
     - arc_rand_* is reseeded from the match seed at match tick 0; arc_net_match_tick() counts ticks from there (use it,
       not arc_tick_count(), in the rules: machines start at different ARC ticks). The START event arrives at match tick 0,
       before tick(): set the match up there (from arc_net_seed()). Deterministic maths and the other rules above apply.
     - Desyncs: every 10 confirmed ticks each machine hashes its state (arc_app.net_state, else save_state's bytes) and
       the hashes are compared: on a mismatch "ARC error desync tick=T ..." and a report folder (net_desync/tickT_pP/:
       info.json, state.bin at tick T on every machine, dump.json, inputs.txt).
     - A player whose packets stop for 3 s (or whose link closes) is dropped: ARC_NET_EV_DROPPED, and from the tick the
       host decides their input is all zeros everywhere (arc_net_input_dropped). Rejoining a rollback match is not supported.
     - Up to 4 players (the host relays inputs between clients). --record records every player's confirmed input, so
       --replay reproduces the match offline exactly (with seek and the timeline). Offline (no match) arc_net_input(0)
       is this machine's input with no delay, so single-player code needs no branches.
   Agent commands: net mode rollback|turns | net delay N | net rollback N | net input HEX|off | net input P HEX (synctest) |
   net stop_at T | net stats | net synctest N [PLAYERS] | net wait tick N|confirmed N|waiting|resumed|dropped.
   --net-sim also takes dup=P (duplicate datagrams) and freeze_at=TICK,freeze_ms=MS (this machine hangs); lag, jitter (which
   reorders datagrams) and loss apply to every datagram. --net-synctest N checks a game offline: every tick it rolls back N
   ticks and compares the state hashes (an "ARC error synctest" + report folder when save_state misses something).
   See docs/MULTIPLAYER.md ("Real-time games").

```c
enum { ARC_NET_TURNS = 0, ARC_NET_ROLLBACK = 1 };
#define ARC_NET_NO_DELAY (-1)
#define ARC_NET_INPUT_MAX 64    // bytes of one player's input for one tick
#define ARC_NET_MAX_ROLLBACK 15
#define ARC_NET_ROLLBACK_PLAYERS 4
```

arc_app.net_input (optional): pack this machine's input for one tick into buf (at most cap = ARC_NET_INPUT_MAX bytes)
   and return its size. Called once per new tick, before tick(), never while re-simulating; read arc_key_*, arc_mouse,
   arc_pad_axis there. Keep it small and quantised (bits, bytes): it is sent every tick.

```c
ARC_API bool arc_net_rollback(void);    // a rollback match is running here (live, replayed or synctest)
ARC_API int64_t arc_net_match_tick(void);    // the match tick being simulated (0 at the start); -1 outside a match
```

inside tick(): a player's input for this tick (confirmed, or predicted until it arrives); size gets its byte count.
   Bytes past size read as zeros (up to ARC_NET_INPUT_MAX). NULL for a player not in the match. Offline: player 0 is this
   machine's input for this tick.

```c
ARC_API const void *arc_net_input(int player, int *size);
ARC_API bool arc_net_input_predicted(int player);    // that input is a guess so far (presentation only: never in rules)
ARC_API bool arc_net_input_dropped(int player);    // the player has left the match as of this tick (input all zeros)
```

ARC's default input (games without arc_app.net_input): every key and pad button, and the pad's axes

```c
ARC_API bool arc_net_key_down(int player, arc_key k);
ARC_API bool arc_net_key_pressed(int player, arc_key k);    // down this tick, not the tick before
ARC_API float arc_net_axis(int player, arc_axis a);    // -1..1 (0..1 triggers), in 1/127 steps
ARC_API bool arc_net_resimulating(void);    // re-running ticks after a correction: no side effects outside the state
ARC_API void arc_net_set_input_delay(int ticks);    // 0..10, this machine, at any time (also `net delay N`)
ARC_API int arc_net_input_delay(void);
ARC_API int arc_net_waiting_for(void);    // the player the match waits for (-1 = running)
typedef struct { bool active; int64_t tick; int64_t confirmed; int input_delay, max_rollback, players, local; int rollbacks; int max_depth; float avg_depth; int64_t resim_ticks; int64_t predicted; int64_t mispredicted; int skipped; int64_t stalled_ms; int ping_ms; float frame_advantage; int sounds_played, sounds_deduped, sounds_late, sounds_cancelled; int64_t packets_sent, packets_received; int64_t last_hash_tick; uint64_t last_hash; int64_t desync_tick; } arc_net_rb_stats;
ARC_API arc_net_rb_stats arc_net_stats(void);
} #endif #endif
```
