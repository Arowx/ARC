# arc_relay: online play for ARC's turn-based sessions (0.8.5) and real-time matches (0.10.4)

`arc_relay` lets ARC games play turn-based sessions between machines that are not on one LAN, and from the web build
(itch.io HTML5). It is a small stand-alone C program (`tools/arc_relay/arc_relay.c`, no libraries, no game code) that
does what a game's LAN host does, for many rooms at once:

- it numbers every command and sends it to everyone in the room (the sender too), refuses commands out of turn, and
  sequences joins, leaves, rejoins, ready flags, names, the start and every end of turn (alternating or simultaneous
  turns);
- it compares the players' state hashes after every command (a mismatch: every machine writes its desync report);
- it keeps each room's command log, so a player who drops, or whose game crashed, rejoins and catches up;
- rooms are named by a short code: 4 characters (5 or 6 when there are very many rooms) from
  `23456789ABCDEFGHJKMNPQRSTUVWXYZ` (no 0/O, no 1/I/L), typed in any case, dashes and spaces ignored.

The relay and a game's LAN host share one implementation of these rules: `src/arc_net_core.h` (the wire format, the
session view, who may send what, joining slots, unique names, hash comparison, room codes, the WebSocket pieces).

## Build and run

```sh
./build.sh relay                # build/arc_relay, and build/arc_relay.exe when MinGW is installed
build.bat relay                 # Windows, Visual Studio: build\arc_relay.exe (links ws2_32.lib)
build/arc_relay                 # listens on 0.0.0.0:47880
```

One port speaks both:

- **WebSocket** (RFC 6455, binary frames): the web build's transport, and the desktop builds' default (`ws://`).
- **ARC's raw TCP protocol**: the same messages as a LAN session, for desktop builds given a `tcp://` address.
  (`--no-raw` turns it off.)
- A plain `GET` (no upgrade) answers one line of status: `ARC relay (protocol 1): 3 rooms, 7 connections` (health checks).

Logs go to stdout, one line per event (`relay   12.345 room K7QF created by Ann from 203.0.113.9: chariot 1.2 (ARC 0.8.4), up to 4 players`,
joins, drops, the start, desyncs, refusals, rooms closing). `--verbose` adds every command.

| Option | Default | Meaning |
|---|---|---|
| `--port N` / `--bind ADDR` | 47880 / 0.0.0.0 | where to listen (`--bind 127.0.0.1` behind a proxy on the same machine) |
| `--max-rooms N` | 1000 | rooms at once |
| `--max-conns N` / `--max-conns-per-ip N` | 1000 / 32 | connections in all (at most FD_SETSIZE - 16) / from one address |
| `--creates-per-min N` | 20 | rooms one address may make per minute |
| `--bad-codes-per-min N` | 30 | wrong codes one address may try per minute (then refused for the rest of the minute) |
| `--rate-msgs N` / `--rate-bytes N` | 200 / 524288 | messages / bytes per second per connection (bursts of 10x / 8x); over it the link is closed |
| `--max-log N` / `--max-log-mb N` | 1000000 / 16 | a room's log: entries / MB of command text; beyond, commands are refused ("the room's log is full") |
| `--room-idle S` | 900 | an empty room is kept this long (for players to come back), then closed |
| `--room-quiet S` | 21600 | a room where nothing was sequenced for this long is closed, players or not |
| `--restore-wait S` | 2 | after a restart: how long to wait for a room's players to come back before asking one for the log |
| `--code-len N` | 4 | room code length (4..6) |
| `--seed N` | random | fixed room codes and session ids (tests) |

Ctrl+C, SIGTERM (`systemctl stop`) or Ctrl+Break on Windows shuts it down cleanly: every player is told the relay is
going away and asked to come back; their games reconnect by themselves for 30 seconds.

**Restarts.** The relay keeps rooms in memory only. When it restarts (an update, a crash, a reboot), every game in a room
reconnects by itself (for 30 s) and says which room it was in and how many commands it holds. The relay waits
`--restore-wait` seconds for a room's players to come back, asks the one with the longest log to send it, rebuilds the
room from it (everyone marked as left), and lets the players back in by name: the match carries on where it was. Commands
that were on their way to the relay when it stopped are lost (the game sees `ARC_NET_EV_CONNECTED` again and should play
the turn again, as netturns does). `tests/relay_test.py` (scenario `restart`) stops a relay with SIGTERM, then kills it
with SIGKILL, mid-match, and checks the match ends identically on both machines.

## The game side

```c
arc_net_desc d = {0};
d.game = "chariot"; d.version = "1.2"; d.max_players = 4;
d.relay = "wss://relay.example.com ws://relay.example.com:47880";   /* web: wss (https pages); desktop: ws */
arc_net_setup(&d);
...
arc_net_host_online();          /* ARC_NET_EV_ROOM gives the code (arc_net_room()); show it */
arc_net_join_online("K7QF");    /* a friend types it */
```

`arc_net_lobby()` shows **Host online** and **Room code** / **Join by code** next to the LAN part whenever a relay this
build can speak is set (`arc_net_relay_available()`). See `docs/MULTIPLAYER.md` ("Online") and `include/arc.h`.

**Several addresses.** `relay` takes several URLs separated by spaces; each build uses the first it can speak: the web
build `wss://` (or `ws://` when the page itself is plain http, such as a local test), desktop builds `ws://` or `tcp://`.
`--net-relay URL` on the command line overrides the game's setting (tests, a private relay).

**Why desktop builds speak plain ws://.** TLS on desktop would need OpenSSL (an external library, not allowed in ARC) or
the operating system's TLS (Windows SChannel through SSPI, macOS Secure Transport): a few hundred lines of fiddly,
platform-specific code with certificate checks to get right, and nothing on Linux without a library. The commands are a
game's moves, not secrets, so ARC 0.8.5 sends desktop traffic in the clear to the relay's own port (47880): open that
port in the firewall next to the proxy's 443. If a desktop build must go through TLS one day, SChannel is the way on
Windows; until then a desktop player behind a proxy that only allows 443 cannot connect.

## Deploying on a small VPS

Any small Linux VPS (1 vCPU, 512 MB) holds hundreds of rooms: the relay uses one thread, `select()`, and about 20 KB per
playing room plus its log. Browsers on https pages (itch.io) may only open `wss://` (TLS) WebSockets, so a TLS proxy
goes in front of the relay for the web build; desktop builds connect to the relay's own port directly.

```
browser (itch.io, https) --wss://relay.example.com--> Caddy / nginx :443 --ws://127.0.0.1:47880--> arc_relay
desktop build --ws://relay.example.com:47880 (or tcp://...)-----------------------------------------^
```

1. Copy `build/arc_relay` to the server (build it there with `cc -std=c11 -O2 tools/arc_relay/arc_relay.c -o arc_relay`,
   or copy the Linux binary), e.g. to `/opt/arc/arc_relay`.
2. Point a DNS name at the server (`relay.example.com`).
3. Open ports 443 (TLS, the web) and 47880 (desktop builds; TCP, and UDP for real-time matches) in the firewall
   (`ufw allow 443/tcp; ufw allow 47880`).

### Caddy (simplest: it gets and renews the certificate by itself)

`/etc/caddy/Caddyfile`:

```
relay.example.com {
    reverse_proxy 127.0.0.1:47880
}
```

Caddy passes WebSocket upgrades through on its own. The relay sees the proxy's address for web players
(`127.0.0.1`), so per-address limits apply to all web players together: raise `--max-conns-per-ip` and
`--creates-per-min` (e.g. 500 and 200) when the relay sits behind a proxy, or run it with `--bind 127.0.0.1` on a second
port only for the proxy and keep the public port for desktop builds.

### nginx

```nginx
server {
    listen 443 ssl;
    server_name relay.example.com;
    ssl_certificate     /etc/letsencrypt/live/relay.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/relay.example.com/privkey.pem;
    location / {
        proxy_pass http://127.0.0.1:47880;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_read_timeout 3600s;          # sessions are long; the relay pings every second anyway
        proxy_send_timeout 3600s;
    }
}
```

(`certbot --nginx -d relay.example.com` gets the certificate.)

### systemd

`/etc/systemd/system/arc-relay.service`:

```ini
[Unit]
Description=ARC relay (online turn-based sessions)
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/opt/arc/arc_relay --port 47880 --max-conns-per-ip 200 --creates-per-min 100
Restart=always
RestartSec=1
DynamicUser=yes
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
LimitNOFILE=4096
KillSignal=SIGTERM
TimeoutStopSec=5

[Install]
WantedBy=multi-user.target
```

```sh
sudo systemctl daemon-reload && sudo systemctl enable --now arc-relay
journalctl -u arc-relay -f          # the log
curl -s https://relay.example.com/  # "ARC relay (protocol 1): 0 rooms, 1 connections"
```

`systemctl restart arc-relay` is safe mid-match: the players reconnect and the rooms are restored (above).

### Windows

`build.bat relay` (or `build/arc_relay.exe` from MinGW) runs the same way in a console; Ctrl+C or Ctrl+Break stops it
cleanly. Put a TLS proxy (Caddy for Windows) in front of it for the web build.

## Security notes

- Room codes and player names are not passwords. A code is hard to guess (923,521 four-character codes, and wrong codes
  are limited per address), but anyone who has it can join a room that has not started. Once a match has started only
  its players can come back, by name: a player who has dropped can have its seat taken by someone else who knows the
  code and the name. A player who is still connected is not replaced by a newcomer using its name (only by its own
  running game, or once its link has gone quiet for 5 seconds, as a crashed game's does).
- The relay checks turns, not rules: the games check, in `command()`, that the sender may move that unit
  (`arc_net_command_from()`), as on a LAN. A modified client can still send nonsense that its own rules accept; the others'
  rules refuse it the same way (or the hashes differ and everyone sees a desync).
- Plain `ws://` and `tcp://` traffic (desktop builds) is not encrypted: it carries game moves and player names.
- Any web page can connect to a relay (there is no `Origin` check); the limits above keep one address from flooding it.

## Real-time (rollback) matches (0.10.4)

Rollback matches (0.8.1) send every player's input every tick, repeated until acknowledged, so a lost datagram costs
nothing; over a TCP link one lost packet would hold up everything behind it. So online they use UDP:

- The lobby, the room code, ready flags and the start go over the room's WebSocket / TCP link as for turns.
- From the start each player opens a UDP socket and registers it with the relay's **UDP port** (the same number as its
  TCP port, 47880): a `REG` datagram with its room session, slot and HELLO token, every second (it keeps NAT mappings
  open). The relay answers with the other players' public addresses as it sees them.
- **Hole punching** is tried at once: each player sends `PUNCH` datagrams straight to the other's public address; one
  that comes back answered means the direct path works, and the match's datagrams go that way. Behind most home
  routers this works; behind strict (symmetric) NATs it doesn't, and nothing changes for the game: the datagrams go
  **through the relay** (`FWD` to the relay, which sends them on). A direct path that goes quiet for 1.5 s falls back to
  the relay; punching keeps being tried.
- The lowest player in the match is the **hub**: it does what the LAN host does in a rollback match (passes each
  player's input on to the others, decides drops). With 2 players that is one hop either way.
- **Drops:** a player whose datagrams stop for 3 s, or whom the relay reports as gone, is dropped by the hub (zeros from
  that tick, `ARC_NET_EV_DROPPED` everywhere). If the hub goes, the others are alone. Rejoining a running rollback
  match is not supported (as on a LAN).
- The game code is the LAN code: `d.mode = ARC_NET_ROLLBACK`, `arc_net_host_online()` / `arc_net_join_online(code)`,
  `arc_net_start()`. `dump` adds `net.rollback.online`: the hub and, for each peer, `path` (`direct` / `relay`) and how
  many datagrams went each way.
- **Firewall:** open **UDP** 47880 on the relay's server next to TCP 47880 (`ufw allow 47880`). Behind Caddy / nginx,
  only the TCP side goes through the proxy; UDP goes straight to the relay. The relay forwards at most a few hundred
  datagrams a second per player in a room and only between addresses that registered with the room's tokens.
- **Browsers:** no UDP, and WebRTC's data channels (the browser's only UDP-like path) would need the relay and every
  desktop build to speak DTLS + SCTP + ICE: far too big without libraries. So real-time matches online are desktop only:
  a web build that hosts a rollback room is refused with that reason, and one that joins one plays alone.
- **Testing:** `--net-lag MS`, `--net-loss PCT`, `--net-jitter MS` (or `--net-sim`) on each player; `--net-relay-only`
  (or `net direct off`) keeps every datagram on the relay (on one machine hole punching always works).
  `python3 tests/relay_rollback_test.py` plays 3000-tick netarena matches at 80 ms / 5 % / 20 ms: direct, relayed, three
  players, a dropped player and a dropped hub (`--client-exe "wine .../netarena_headless.exe"` for Windows players).

## As a Cloudflare Worker with a Durable Object (not implemented)

At this scale a Worker would cost nothing and need no server. What it would take:

- **One Durable Object per room** (`idFromName(code)`), holding what `rroom` holds: the session view, tokens, the log
  (in memory, and in the Object's storage every few commands so a restart loses nothing), the hash rows. The Durable
  Object accepts the WebSockets (`state.acceptWebSocket`, the hibernation API, so idle rooms cost nothing) and runs the
  same rules as `room_admit` / `on_live`: a port of `src/arc_net_core.h` to JavaScript (or the core compiled to WebAssembly
  with Emscripten and called from the Object, which keeps one implementation).
- **The Worker** routes `GET /room/K7QF` (upgrade) to that room's Object, and makes new codes (checking the Object is
  empty). HELLO already carries the code, so the client would open `wss://worker.example.workers.dev/` and the Worker
  would read the first message to route it, or the client would put the code in the URL path.
- **TLS is Cloudflare's**: web builds get `wss://` for free; desktop builds would need TLS too (Workers only take https /
  wss), which brings back the desktop TLS question above.
- Limits move to Cloudflare's (requests per day, CPU per message, Object storage); restarts are Cloudflare's business
  (with the log in storage, a room survives them without the players' help).
