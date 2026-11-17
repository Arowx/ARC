# Request from Droids: real-time online play (rollback over the internet, not just the LAN)

**Why.** ARC 0.8.1 rollback works on a LAN (UDP). ARC 0.8.5's relay takes games online, but it is turn-based only,
and `docs/RELAY.md` says rollback stays LAN-only: over WebSocket (TCP) a lost packet holds up everything behind it,
and browsers have no UDP.

Droids is ready for real-time play:
- `save_state` / `load_state` / `net_state` (`src/snapshot.h`);
- `tests/smoke.py` checks that seeking and rewinding give the same state as a straight run;
- ARC 0.9.7 checks determinism across machines.

The two-player modes that make sense are real-time:
- a head-to-head transfer duel: both players fire into the same cylinder for 10 seconds;
- two droids co-operating to clear a deck.

They need rollback between two homes, and in the browser for itch.io.

**Ask.**
1. **Desktop over the internet:**
   - rollback through the relay over UDP: the relay forwards datagrams between the room's players after the same
     room-code handshake;
   - NAT hole punching first where it works, falling back to relaying;
   - the same `arc_net` rollback calls as on the LAN, with `relay` set.
2. **Browser:** WebRTC data channels in unreliable, unordered mode, with the relay as the signalling server and
   STUN only. That is the only UDP-like path a browser has. If WebRTC is too big for "no external libraries" in the
   web build, say so and we'll settle for desktop-only.
3. **Agent and debugging:**
   - `--net-lag MS`, `--net-loss PCT` and `--net-jitter MS` to simulate bad connections locally;
   - the rollback count and the largest rollback in `dump`, as the LAN rollback already reports.
- **Must not change:** LAN rollback and the turn-based relay.

**Game side (to do once this lands).** Droids would add:
- **Online duel:** a lobby entry. Both players share one transfer board with mirrored sides and the 10-second
  clock; inputs are emitter position and fire.
- **Co-op deck:** later. Two droids on one deck. The enemy droids are already deterministic.

## Acceptance

In words:
- Two clients through a local relay with 80 ms latency, 5% loss and 20 ms jitter play 3000 ticks of
  `samples/netarena`. They reach the same state hash every 60 ticks, and no rollback is longer than the configured
  window.
- The same with one browser client in headless Chromium (if WebRTC is done).
- A dropped player is reported and the match ends cleanly.

## Done in ARC 0.10.4

`#if defined(ARC_HAS_ONLINE_ROLLBACK)`. Item 1 and item 3 are in; item 2 (browser) is not; see below.

**1. Desktop over the internet:**
- Rollback through the relay over UDP, after the usual room-code handshake: each player registers a UDP socket with
  the relay's UDP port (same number as its TCP port, 47880) using its slot and HELLO token; the relay forwards
  datagrams between the room's players.
- NAT hole punching is tried first: the relay tells each player the other's public address, they send punch datagrams
  straight to each other, and once one is answered the match goes direct. If it doesn't work (strict NATs), everything
  stays on the relay. A direct path that goes quiet for 1.5 s falls back to the relay.
- The same `arc_net` calls as on the LAN: `d.mode = ARC_NET_ROLLBACK`, `relay` set, `arc_net_host_online()` /
  `arc_net_join_online(code)`, `arc_net_start()`. The lowest player in the match acts as the LAN host does (passes
  inputs on, decides drops).
- Server: open UDP 47880 next to TCP 47880. Caddy/nginx only carry the TCP side; UDP goes straight to the relay.

**2. Browser (WebRTC): not done.** Browsers have WebRTC built in, but the other end (the relay and every desktop build)
would need DTLS + SCTP + ICE to talk to it; without external libraries that is far too big. So, as you offered:
desktop-only. A web build that hosts a rollback room is refused with that reason; one that joins one plays alone.

**3. Agent and debugging:**
- `--net-lag MS`, `--net-loss PCT`, `--net-jitter MS` (also `--net-sim lag=..,loss=..,jitter=..` as before).
- `--net-relay-only` / `net direct off`: no hole punching (on one machine punching always works, so use this to test
  the relayed path).
- `dump` → `net.rollback`: `rollbacks` and `max_depth` (as on the LAN), plus `online`: the hub and each peer's
  `path` (`direct` / `relay`) with datagram counts.

**Acceptance** (`python3 tests/relay_rollback_test.py`):
- Two clients through a local relay at 80 ms / 5 % / 20 ms play 3000 ticks of `samples/netarena`, directly and
  relayed: the same final hash and game dump, no desync (the players compare state hashes every 10 ticks, which covers
  your "every 60"), deepest rollback 6 within the window of 8.
- Three players, relayed: same; the deepest rollback reached the window (8 of 8) but never beyond.
- A Windows client (MinGW build under Wine) against a Linux hub: same result.
- A dropped player is reported by the hub, which plays on and ends cleanly; a dropped hub leaves the other player
  alone, told why.
- LAN rollback and the turn-based relay tests still pass.

**For Droids' duel:** host with `d.mode = ARC_NET_ROLLBACK` in a relay room exactly as on the LAN. Test with two
headless processes, `--net-relay ws://127.0.0.1:47880 --net-lag 80 --net-loss 5 --net-jitter 20`, as the test script does.
