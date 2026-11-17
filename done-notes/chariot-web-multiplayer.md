# Request from Operation Chariot: multiplayer in the browser, and over the internet

**What.** ARC 0.8's turn-based sessions, playable from the web build (itch.io HTML5) and between machines that are not
on the same LAN. Today the web build reports `ARC_NET_UNAVAILABLE` ("browsers cannot accept connections"), and desktop
sessions are LAN only (MULTIPLAYER.md: "no internet play through routers").

**Why.** Operation Chariot (the St Nazaire raid, being ported onto the Moonbase Raid tactics core) is meant to be played
co-operatively: the raid is eight commando parties plus the launches, each with its own job, and each player commands
some of them while the German AI plays the other side. Its rules already run on commands and are deterministic (the
port is checked against the JS game dice for dice), so 0.8's model of commands, a sequence and hashes fits as it is.
What it lacks is a way for players to reach each other: Allan's games go out on itch.io, mostly played in the browser,
by people who are not on one LAN.

## What would help

1. **A relay server instead of a player-hosted host.** A small program (`tools/arc_relay`, or `arc_relay.c` built like
   the samples; no game code and no rules) that does what a host does now: it numbers commands, passes them to every
   player in the room, refuses commands out of turn, sequences joins, leaves, rejoins and turn ends, and compares the
   players' state hashes. It holds rooms by a short code (`arc_net_host` gives the room a code; `arc_net_join("CODE")` or
   `"relay.example.com/CODE"` joins). It runs as one process on a cheap VPS or as a Node / Cloudflare Worker script (a
   Worker with a Durable Object per room would cost nothing at this scale). Either is fine; plain C to match ARC is
   probably the most ARC-like.
2. **WebSocket transport.** The web build connects to the relay over WebSocket (`wss://` from an https itch page).
   Desktop builds use the same relay over WebSocket too, so a browser player and a Windows player can share a room and
   internet play works for both. LAN play (a player hosting, UDP discovery) stays as it is.
3. **The lobby.** `arc_net_lobby` gains "host online / join by code" next to the LAN list.
4. **Optional: a simultaneous-turn mode.** Chariot's co-op needs every player to give orders to their own parties in
   the same minute, then "next minute" runs when everyone is ready. I believe this works on 0.8.1 already: orders sent
   with `ARC_NET_FREE`, the game refusing (deterministically, in `command()`, by `arc_net_command_from()`) orders for a
   party the sender doesn't hold, and NEXT MINUTE applied once every player's ready command has come through. If ARC
   offered it (`arc_net_desc.turns = ARC_NET_TURNS_SIMULTANEOUS`: any player may send; `arc_net_turn_end()` from every
   connected player ends the turn), games wouldn't each write that bookkeeping. Not needed to start.

## Suggested API (additions only; 0.8 games unchanged)

```c
/* arc_net_desc: new fields at the end */
const char *relay;          /* "wss://relay.example.com" (NULL: LAN only, as now) */

bool arc_net_host_online(void);              /* asks the relay for a room; ARC_NET_EV_ROOM gives its code */
const char *arc_net_room(void);              /* the room code, e.g. "K7QF" ("" if none) */
bool arc_net_join_online(const char *code);  /* join a room on the relay */
/* arc_net_status(): ARC_NET_UNAVAILABLE on the web only when no relay is set */
```

Agent commands to match the existing ones: `net host_online`, `net join_online CODE`, and `--net-relay URL` so the
headless tests can run a local relay.

## Meanwhile

Nothing is blocked: Chariot's port is at Phase 3 of 6 (`D:\Projects\Operation Chariot\ARC_PORT_PLAN.md`); multiplayer
comes after the raid layer is in C++ (Phase 4). Chariot will use 0.8 LAN sessions for its first co-op tests, and
desktop players can use a VPN that looks like a LAN.

## Test cases

- `tests/net_test.py` again with the relay in the middle: a local `arc_relay` on 127.0.0.1, two or three headless
  `netturns` clients joining by code, the same scenarios (turns, refusals, hashes, a drop-out and rejoin, an injected
  desync and its reports).
- The web build of `netturns` in headless Chromium joining the same room as a Linux headless client: both reach the
  same hash after 50 moves.
- Simultaneous mode (if added): three players each send two free orders and a turn end in a scrambled order; every
  machine applies them in the same sequence and the turn advances once, after the third turn end.

## Reply from ARC 0.8.5 (phase G1): done

All four points, as asked, plus a test relay run on every change:

1. **Relay**: `tools/arc_relay/arc_relay.c` (`./build.sh relay` / `build.bat relay`, Linux and Windows), plain C, no game
   code. It does a host's job for rooms with codes like `K7QF` (no 0/O or 1/I/L): numbers and broadcasts commands,
   refuses out-of-turn ones, sequences joins / leaves / rejoins / turn ends, compares hashes, keeps the log for rejoins.
   The relay and a LAN host now share one rules core (`src/arc_net_core.h`). One port speaks WebSocket and ARC's raw
   TCP; limits, idle expiry and a clean shutdown are in; if it restarts mid-match the players reconnect and the room is
   rebuilt from their logs. `docs/RELAY.md`: a small VPS behind Caddy or nginx (`wss://` for itch.io), a systemd unit,
   and what a Cloudflare Worker + Durable Object version would take (not built).
2. **WebSocket**: the web build uses the browser's WebSocket; desktop builds speak `ws://` (or `tcp://`) to the relay's
   own port. Desktop TLS is not done (it needs a library or SChannel; see RELAY.md), so give `relay` both addresses:
   `"wss://relay.example.com ws://relay.example.com:47880"`; each build takes the first it can use.
3. **API**: `arc_net_desc.relay`, `arc_net_host_online()` (ARC_NET_EV_ROOM brings the code), `arc_net_room()`,
   `arc_net_join_online("K7QF")`, `net host_online` / `net join_online CODE` / `--net-relay URL`. The room's creator is
   its host (HOSTING, starts the match). On the web, `arc_net_host` / `arc_net_join(code)` go online when a relay is
   set. Rollback matches stay LAN only (TCP and browsers make them stall; RELAY.md explains).
4. **Lobby**: Host online, Room code + Join by code (keyboard; a letter grid for gamepads and touch).
5. **Simultaneous turns**: `arc_net_desc.turns = ARC_NET_TURNS_SIMULTANEOUS`. Anyone may send during a turn; each player
   ends it once with `arc_net_turn_end()`; `ARC_NET_EV_TURN_DONE` per player ("waiting for 1 3",
   `arc_net_turn_waiting()`), then `ARC_NET_EV_TURN` (player `ARC_NET_EVERYONE`) where "next minute" runs. A dropped
   player doesn't hold the turn: if the drop leaves only finished players, the turn ends right after its LEFT. Your
   own check in `command()` that the sender holds that party stays as you planned.

Your test cases are `tests/relay_test.py`: the net_test scenarios through a local relay (turns, refusals, hashes,
drop-out and rejoin, an injected desync with reports, plus version, wrong code, full room, relay restarts); the web build
of netturns in headless Chromium and a Linux client (and a Windows one under Wine) in one room reaching the same hash
after 50+ game commands; and simultaneous mode with three players each sending two orders and a turn end in a scrambled
order: the same sequence everywhere, and the turn advances once, after the third end (on the relay and on a LAN host).
How to use it from Chariot: `docs/MULTIPLAYER.md`, "Online play".
