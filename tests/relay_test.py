#!/usr/bin/env python3
"""ARC 0.8.5 online test: a local arc_relay on 127.0.0.1 and headless netturns clients joining rooms by code.

    python3 tests/relay_test.py [--exe build/netturns_headless] [--client-exe X] [--relay build/arc_relay]
                                [--port 47960] [--only NAME] [--keep DIR] [--web] [--wine]

Builds the relay and samples/netturns headless first unless given. Each scenario starts its own relay, a room's host
(`net host_online`, which prints the room code) and players (`net join_online CODE`), each a separate process driven by
`--headless --cmd ...`, then checks their output and final dumps:
  basic     two players over WebSocket with lag, jitter and loss (--net-sim on WebSocket links); the same state and hash
  raw       one player over ws://, one over tcp:// (the relay's raw ARC protocol on the same port)
  three     three players; one's cable is pulled and plugged back in (--net-sim drop_at/rejoin_at): it reconnects to the
            relay, receives what it missed, and the match ends the same everywhere
  crash     a player killed mid-match and started again (a new process): it rejoins the room by name and catches up
  turn      commands out of turn refused (at once locally; by the relay for a command sent after the player's own turn end)
  desync    one machine changes its state locally: the relay sees the hashes differ, every machine writes its report
  version   another game version is refused with the reason on the player's side, and the relay logs it
  codes     a wrong code ("no room ..."), a malformed code (refused locally), a full room, a started match
  restart   the relay is stopped (SIGTERM: it tells everyone) and later killed (SIGKILL: it says nothing) mid-match and
            started again: the players reconnect, the first back restores the room from its log, play carries on
  simul     simultaneous turns: three players each send two orders and a turn end in a scrambled order; every machine
            applies them in the same sequence and the turn advances once, after the third turn end (also on a LAN host)
  limits    room creations per minute, the HTTP status answer, and a clean shutdown
  web       (--web) the web build of netturns in headless Chromium and a Linux headless client in one room; with
            --wine a Windows client (Wine) joins too: all reach the same hash after 50+ game commands
Exit code 0 when everything passes."""
import argparse, json, os, re, shlex, shutil, signal, subprocess, sys, time, urllib.parse, urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
FAILS = []

def check(cond, what):
    print(('ok    ' if cond else 'FAIL  ') + what, flush=True)
    if not cond: FAILS.append(what)

def turns(n, wait=40):
    return ''.join(f'; net wait turn {wait}; ai; net end_turn' for _ in range(n))

class Proc:
    def __init__(self, exe, name, cmd, cwd, extra=(), relay=None, log=None):
        self.name, self.cwd = name, cwd
        args = shlex.split(exe) + ['--headless', '--net-name', name] + (['--net-relay', relay] if relay else []) + ['--cmd', cmd] + list(extra)
        self.logf = os.path.join(cwd, (log or name) + '.log')
        self.log = open(self.logf, 'w')
        self.p = subprocess.Popen(args, cwd=cwd, stdout=self.log, stderr=subprocess.STDOUT)
    def wait(self, t=120):
        try: self.p.wait(timeout=t)
        except subprocess.TimeoutExpired: self.p.kill(); self.p.wait(); print(f'      ({self.name} timed out)')
        self.log.close()
        return self.out()
    def kill(self): self.p.kill(); self.p.wait(); self.log.close()
    def out(self): return open(self.logf, errors='replace').read()
    def room(self, t=15):            # the code the relay gave ("ARC net room K7QF relay=... player=0")
        end = time.time() + t
        while time.time() < end:
            m = re.search(r'ARC net room ([A-Z0-9]{4,6}) ', self.out())
            if m: return m.group(1)
            time.sleep(0.05)
        return 'NONE'

class Relay:
    def __init__(self, a, d, port, name='relay', extra=()):
        self.logf = os.path.join(d, name + '.log'); self.log = open(self.logf, 'a')
        self.p = subprocess.Popen(shlex.split(a.relay) + ['--port', str(port)] + list(extra), cwd=d, stdout=self.log, stderr=subprocess.STDOUT)
        self.url = f'ws://127.0.0.1:{port}'; self.port = port
        end = time.time() + 5
        while time.time() < end and 'listening' not in self.out(): time.sleep(0.02)
    def out(self): return open(self.logf, errors='replace').read()
    def stop(self, sig=signal.SIGTERM):
        if self.p.poll() is None:
            self.p.send_signal(sig)
            try: self.p.wait(timeout=10)
            except subprocess.TimeoutExpired: self.p.kill(); self.p.wait()
        self.log.close()

def dump(cwd, f):
    try: return json.load(open(os.path.join(cwd, f)))
    except Exception as e: return {'error': str(e)}

def same_game(a, b, label):
    ga, gb = a.get('game'), b.get('game')
    check(ga is not None and ga == gb, f'{label}: game state identical ({ga.get("rules_hash") if ga else a})')
    na, nb = a.get('net', {}), b.get('net', {})
    check(na.get('seq') == nb.get('seq') and na.get('last_hash') == nb.get('last_hash') and na.get('last_hash'), f'{label}: same seq {na.get("seq")} and hash {na.get("last_hash")} / {nb.get("seq")} {nb.get("last_hash")}')

def scen_basic(a, d):
    r = Relay(a, d, a.port)
    sim = ['--net-sim', 'lag=40,jitter=40,loss=0.03,seed=5']
    h = Proc(a.exe, 'Ann', 'net host_online; net wait connected 10; net wait players 2 30; net start; net wait started' + turns(10) + '; net wait turn_number 21 60; net sync; dump h.json; net wait players 1 30; quit', d, sim, r.url)
    code = h.room()
    c = Proc(a.cexe, 'Bob', f'net join_online {code}; net wait started 30' + turns(10) + '; net wait turn_number 21 60; net sync; dump c.json; net wait time 1.5; net leave; quit', d, sim + ['--seed', '9'], r.url)
    co, ho = c.wait(), h.wait(); r.stop()
    check(re.fullmatch(r'[2-9A-HJKMNP-Z]{4}', code) is not None, f'basic: the relay gave a 4-character room code ({code})')
    check('status=hosting' in ho or 'ARC net room' in ho, 'basic: the room host saw its room')
    hj, cj = dump(d, 'h.json'), dump(d, 'c.json')
    check(hj.get('net', {}).get('room') == code and cj.get('net', {}).get('room') == code and hj.get('net', {}).get('host') is True and cj.get('net', {}).get('host') is False, 'basic: both dumps name the room; the creator is the host')
    same_game(hj, cj, 'basic')
    g = hj.get('game', {})
    check(g.get('attacks', 0) > 0 and g.get('moves', 0) > 0, f'basic: the match was played (moves {g.get("moves")}, attacks {g.get("attacks")})')
    ro = r.out()
    check(f'room {code} created by Ann' in ro and f'room {code} player 1 joined: Bob' in ro and 'the match starts' in ro, 'basic: the relay logged the room, the join and the start')

def scen_raw(a, d):
    r = Relay(a, d, a.port + 1)
    h = Proc(a.exe, 'Wsy', 'net host_online; net wait connected 10; net wait players 2 30; net start; autoplay 13; net wait turn_number 13 60; net sync; dump h.json; net wait players 1 30; quit', d, (), r.url)
    code = h.room()
    c = Proc(a.cexe, 'Tcp', f'net join_online {code}; net wait connected 10; net status; autoplay 13; net wait turn_number 13 60; net sync; dump c.json; net wait time 1.5; net leave; quit', d, (), f'tcp://127.0.0.1:{a.port + 1}')
    co, ho = c.wait(), h.wait(); r.stop()
    check('relay=tcp://127.0.0.1' in co and 'relay=ws://127.0.0.1' in ho, 'raw: one player over tcp://, the other over ws://')
    same_game(dump(d, 'h.json'), dump(d, 'c.json'), 'raw')

def scen_three(a, d):
    r = Relay(a, d, a.port + 2)
    h = Proc(a.exe, 'Host', 'net host_online; net wait connected 10; net wait players 3 30; net start; autoplay 25; net wait turn_number 25 90; net sync; dump h.json; net wait players 1 60; quit', d, ['--net-sim', 'lag=30,jitter=30,seed=1'], r.url)
    code = h.room()
    c1 = Proc(a.cexe, 'c1', f'net join_online {code}; autoplay 25; net wait turn_number 25 90; net sync; dump c1.json; net wait time 1.5; net leave; quit', d, (), r.url)
    time.sleep(0.4)
    c2 = Proc(a.cexe, 'c2', f'net join_online {code}; autoplay 25; net wait started 30; net wait turn_number 25 90; net sync; dump c2.json; net wait time 1.5; net leave; quit', d,
              ['--net-sim', 'drop_at=120,rejoin_at=400,seed=4'], r.url)
    o2, o1, oh = c2.wait(), c1.wait(), h.wait(); r.stop()
    check('network dropped (simulated)' in o2 and 'rejoining' in o2, 'three: the cable was pulled and plugged back in')
    m2 = re.search(r'ARC net connected player=(\d)', o2); i2 = m2.group(1) if m2 else '?'
    check(f'ARC net left {i2} c2' in oh and f'ARC net rejoined {i2} c2' in oh, f'three: the others saw it leave and rejoin (sequenced, player {i2})')
    hj = dump(d, 'h.json')
    same_game(hj, dump(d, 'c1.json'), 'three host/c1'); same_game(hj, dump(d, 'c2.json'), 'three host/c2 (rejoined)')

def scen_crash(a, d):
    r = Relay(a, d, a.port + 3)
    h = Proc(a.exe, 'Host', 'net host_online; net wait connected 10; net wait players 2 30; net start; net wait started' + turns(9) + '; net wait turn_number 19 60; net sync; dump h.json; net wait players 1 60; quit', d, (), r.url)
    code = h.room()
    c1 = Proc(a.cexe, 'crashy', f'net join_online {code}; net wait started 30' + turns(3) + '; net wait turn_number 8 30; step 100000', d, (), r.url)
    for _ in range(300):
        if 'ARC net turn 8' in c1.out(): break
        time.sleep(0.05)
    c1.kill()
    c2 = Proc(a.cexe, 'crashy', f'net join_online {code}; net wait started 30' + turns(6) + '; net wait turn_number 19 60; net sync; dump c.json; net wait time 1.5; net leave; quit', d, (), r.url)
    o2, oh = c2.wait(), h.wait(); r.stop()
    check('rejoined' in r.out() and 'ARC net rejoined 1' in oh, 'crash: the restarted game rejoined by name')
    same_game(dump(d, 'h.json'), dump(d, 'c.json'), 'crash')

def scen_turn(a, d):
    r = Relay(a, d, a.port + 4)
    h = Proc(a.exe, 'Host', 'net host_online; net wait connected 10; net wait players 2 30; net start; net wait seq 4 30; net end_turn; net cmd move 0 1 0; net wait players 1 30; dump h.json; quit', d, (), r.url)
    code = h.room()
    c = Proc(a.cexe, 'eager', f'net join_online {code}; net wait started 30; net cmd move 2 7 4; net free chat early; net wait turn 30; net end_turn; net cmd move 3 6 5; net free chat hello; net sync; dump c.json; net wait time 1.5; net leave; quit', d,
             ['--net-sim', 'lag=50,seed=2'], r.url)
    co, ho = c.wait(), h.wait(); r.stop()
    check('ARC error net refused (not your turn): move 0 1 0' in ho, 'turn: the room host refused itself locally (not its turn)')
    check('ARC error net refused (not your turn): move 2 7 4' in co, 'turn: the player refused its own early command locally')
    check('ARC error net refused (not your turn): move 3 6 5' in co, 'turn: the relay refused a command sent after the turn ended')
    hj, cj = dump(d, 'h.json'), dump(d, 'c.json')
    check(hj.get('game') is not None and hj.get('game') == cj.get('game'), 'turn: game state identical')
    check(hj.get('game', {}).get('chat') == '1: hello', 'turn: a free command (chat) went through out of turn')

def scen_desync(a, d):
    r = Relay(a, d, a.port + 5)
    rep = os.path.join(d, 'rep')
    h = Proc(a.exe, 'Host', 'net host_online; net wait connected 10; net wait players 2 30; net start; net wait started' + turns(3) + '; net wait desync 30; dump h.json; net wait players 1 30; quit', d, ['--net-report', rep], r.url)
    code = h.room()
    c = Proc(a.cexe, 'cheater', f'net join_online {code}; net wait started 30' + turns(1) + '; cheat 0 3' + turns(2) + '; net wait desync 30; net leave; quit', d, ['--net-report', rep], r.url)
    co, ho = c.wait(), h.wait(); r.stop()
    check('ARC error desync seq=' in ho and 'ARC error desync seq=' in co, 'desync: both machines print ARC error desync')
    hs = ho.split('ARC error desync seq=')[1].split()[0] if 'ARC error desync seq=' in ho else '?'
    cs = co.split('ARC error desync seq=')[1].split()[0] if 'ARC error desync seq=' in co else '!'
    check(hs == cs and f'DESYNC at seq {hs}' in r.out(), f'desync: the same sequence number on both, logged by the relay ({hs})')
    dirs = sorted(os.listdir(rep)) if os.path.isdir(rep) else []
    check(len(dirs) == 2 and all(os.path.exists(os.path.join(rep, x, f)) for x in dirs for f in ('dump.json', 'log.txt', 'info.json', 'state.bin')), f'desync: two report folders with dump, log, info, state ({dirs})')
    if len(dirs) == 2:
        i0, i1 = (json.load(open(os.path.join(rep, x, 'info.json'))) for x in dirs)
        check(i0['report_seq'] == i1['report_seq'] and len(set(i0['hashes'].values())) == 2, f'desync: reports at the same seq ({i0["report_seq"]}) with different hashes')

def scen_version(a, d):
    r = Relay(a, d, a.port + 6)
    h = Proc(a.exe, 'Host', 'net host_online; net wait connected 10; net wait players 2 5; quit', d, (), r.url)
    code = h.room()
    c = Proc(a.cexe, 'old', f'net join_online {code}; net wait status refused 10; net status; quit', d, ['--net-version', '0.9'], r.url)
    co, ho = c.wait(), h.wait(); r.stop()
    check('ARC error net refused: version mismatch' in co and 'netturns 1.0' in co and '0.9' in co, 'version: the player was told why')
    check('refused old: version mismatch' in r.out(), 'version: the relay logged it')

def scen_codes(a, d):
    r = Relay(a, d, a.port + 7)
    w = Proc(a.cexe, 'wrong', 'net join_online ZZZZ; net wait status refused 10; net status; quit', d, (), r.url)
    m = Proc(a.cexe, 'bad', 'net join_online K1; net status; quit', d, (), r.url)
    wo, mo = w.wait(), m.wait()
    check('no room ZZZZ on the relay' in wo and 'status=refused' in wo, 'codes: a wrong code is refused by the relay with the reason')
    check("'K1' is not a room code" in mo and 'status=offline' in mo, 'codes: a malformed code is refused at once, locally')
    h = Proc(a.exe, 'Host', 'net host_online; net wait connected 10; net wait players 4 30; net wait seq 99 6; quit', d, (), r.url)
    code = h.room()
    lower = code.lower()[:2] + '-' + code.lower()[2:]
    ps = [Proc(a.cexe, f'p{i}', f'net join_online {lower if i == 1 else code}; net wait players 4 20; net wait seq 99 4; quit', d, (), r.url) for i in range(1, 4)]
    time.sleep(1.5)
    f = Proc(a.cexe, 'fifth', f'net join_online {code}; net wait status refused 10; net status; quit', d, (), r.url)
    fo = f.wait(); outs = [p.wait() for p in ps]; h.wait()
    check(all('ARC net connected player=' in o for o in outs), f'codes: three players joined (one typed "{lower}")')
    check('the game is full' in fo, 'codes: a fifth player is refused (the room is full)')
    h2 = Proc(a.exe, 'Host', 'net host_online; net wait connected 10; net wait players 2 30; net start; net wait started; net wait seq 99 5; quit', d, (), r.url)
    code2 = h2.room()
    p2 = Proc(a.cexe, 'joiner', f'net join_online {code2}; net wait started 20; net wait seq 99 4; quit', d, (), r.url)
    time.sleep(1.5)
    late = Proc(a.cexe, 'late', f'net join_online {code2}; net wait status refused 10; quit', d, (), r.url)
    lo = late.wait()
    imp = Proc(a.cexe, 'joiner', f'net join_online {code2}; net wait status refused 10; quit', d, (), r.url, log='impostor')   # (same name, another game)
    io = imp.wait(); p2.wait(); h2.wait(); r.stop()
    check('already started' in lo, 'codes: a newcomer cannot join a started match')
    check('a player named joiner is already playing' in io, 'codes: a live player is not replaced by someone using its name')

def scen_restart(a, d):
    port = a.port + 8
    r = Relay(a, d, port)
    h = Proc(a.exe, 'Host', 'net host_online; net wait connected 10; net wait players 2 30; net start; autoplay 40; net wait turn_number 40 120; net sync; dump h.json; net wait players 1 60; quit', d, ['--set', 'netturns.hp=40'], r.url)
    code = h.room()
    c = Proc(a.cexe, 'Guest', f'net join_online {code}; autoplay 40; net wait turn_number 40 120; net sync; dump c.json; net wait time 1.5; net leave; quit', d, ['--set', 'netturns.hp=40'], r.url)
    def turn_of(p):
        t = re.findall(r'ARC net turn (\d+) ', p.out()); return int(t[-1]) if t else 0
    for _ in range(400):
        if turn_of(h) >= 8: break
        time.sleep(0.02)
    t_stop = turn_of(h)
    r.stop(signal.SIGTERM)                               # a clean stop: the relay says goodbye (players come back)
    time.sleep(1.0)
    r = Relay(a, d, port)
    for _ in range(600):
        if turn_of(h) >= t_stop + 8: break
        time.sleep(0.02)
    t_kill = turn_of(h)
    r.p.kill(); r.p.wait(); r.log.close()               # a crash: no goodbye (the links just close)
    time.sleep(0.5)
    r = Relay(a, d, port)
    co, ho = c.wait(150), h.wait(150); r.stop()
    ro = open(os.path.join(d, 'relay.log'), errors='replace').read()
    check('shutting down' in ro and 'players told' in ro, f'restart: the relay stopped cleanly at turn {t_stop} and told the players')
    check(ro.count('restored from a player') >= 2, f'restart: the room was restored from a returning player\'s log after the stop and after the kill (turn {t_kill})')
    check('relay is shutting down' in ho and 'restoring room' in (ho + co), 'restart: the players saw it go and gave the log back')
    hj, cj = dump(d, 'h.json'), dump(d, 'c.json')
    check(hj.get('net', {}).get('turn', 0) >= 40, f'restart: the match went on to turn 40 ({hj.get("net", {}).get("turn")})')
    same_game(hj, cj, 'restart')

def scen_simul(a, d):
    # three players, simultaneous turns: each sends two orders and its end of turn, with different lags and pauses, so
    # the relay receives them interleaved; every machine must apply the same sequence and the turn advances only after
    # the third end of turn
    for where in ('relay', 'lan'):
        sub = os.path.join(d, where); os.makedirs(sub)
        port = a.port + 9
        r = Relay(a, sub, port) if where == 'relay' else None
        url = r.url if r else None
        start = 'net host_online; net wait connected 10' if r else f'net host {a.port + 20}'
        join = (lambda: f'net join_online {code}') if r else (lambda: f'net join localhost:{a.port + 20}')
        tail = '; net wait turn_number 2 30; net sync; dump {n}.json; net log {n}.net.log; net wait time 1.5'
        flags = ['--simultaneous']
        # the schedule (seconds after the start): B b1 0.0, A a1 0.15, C c1 0.3, B b2 0.45, A a2 0.6, C c2 0.75,
        # B end 0.9 (+ b3 at once: refused), A end 1.05, C end 1.2 - interleaved, on links of different lag
        h = Proc(a.exe, 'A', start + '; net wait players 3 30; net start; net wait started; net wait time 0.15; net cmd order a1; net wait time 0.45; net cmd order a2; net wait time 0.45; net end_turn' + tail.format(n='A') + '; net wait players 1 30; quit', sub,
                 flags + ['--net-sim', 'lag=35,jitter=20,seed=11'], url)
        code = h.room() if r else ''
        b = Proc(a.cexe, 'B', join() + '; net wait started 30; net cmd order b1; net wait time 0.45; net cmd order b2; net wait time 0.45; net end_turn; net cmd order b3; net status' + tail.format(n='B') + '; net leave; quit', sub,
                 flags + ['--net-sim', 'lag=5,seed=12'], url)
        time.sleep(0.3)
        c = Proc(a.cexe, 'C', join() + '; net wait started 30; net wait time 0.3; net cmd order c1; net wait time 0.45; net cmd order c2; net wait time 0.45; net end_turn' + tail.format(n='C') + '; net leave; quit', sub,
                 flags + ['--net-sim', 'lag=60,jitter=30,seed=13'], url)
        oc, ob, oa = c.wait(), b.wait(), h.wait()
        if r: r.stop()
        js = [dump(sub, f'{n}.json') for n in 'ABC']
        logs = [open(os.path.join(sub, f'{n}.net.log')).read().split('---', 1)[-1] if os.path.exists(os.path.join(sub, f'{n}.net.log')) else n for n in 'ABC']
        g = js[0].get('game', {})
        check(g.get('orders') == 6 and all(j.get('game') == js[0].get('game') for j in js), f'simul {where}: every machine applied the same orders in the same order ({g.get("order_log")})')
        check('ARC error net refused (you have already ended this turn): order b3' in ob, f'simul {where}: an order after the player\'s own turn end is refused')
        check(logs[0] == logs[1] == logs[2], f'simul {where}: identical command logs on the three machines')
        ent = [l.split() for l in logs[0].strip().splitlines()]
        kinds = [(e[1], e[2]) for e in ent if len(e) > 2 and e[2] in ('turn', 'cmd') ]
        ends = [i for i, k in enumerate(kinds) if k[1] == 'turn']
        check(len(ends) == 3 and sorted(k[0] for k in kinds if k[1] == 'turn') == ['0', '1', '2'], f'simul {where}: three turn ends, one from each player ({kinds})')
        tn = [re.findall(r'ARC net turn (\d+) owner=(-?\d+) seq=(\d+)', o) for o in (oa, ob, oc)]
        td = [re.findall(r'ARC net turn_done (\d+) turn=1 seq=(\d+)', o) for o in (oa, ob, oc)]
        ok = all(len(t) == 1 and t[0][0] == '2' and t[0][1] == '-2' for t in tn) and all(len(x) == 3 for x in td) and \
             all(int(tn[i][0][2]) == max(int(s) for _, s in td[i]) and (oa, ob, oc)[i].index('ARC net turn 2 ') > (oa, ob, oc)[i].rindex('ARC net turn_done') for i in range(3))
        check(ok, f'simul {where}: the turn advanced once (to 2, owner everyone), at the third turn end, on every machine (turn {tn[0]}, ends {td[0]})')
        check(all(j.get('net', {}).get('turns') == 'simultaneous' for j in js), f'simul {where}: the dumps say simultaneous')

def scen_limits(a, d):
    r = Relay(a, d, a.port + 10, extra=['--creates-per-min', '2'])
    outs = []
    for i in range(3):
        p = Proc(a.exe, f'mk{i}', 'net host_online; net wait connected 5; net status; quit', d, (), r.url); outs.append(p.wait())
    check(all('status=hosting' in o for o in outs[:2]) and 'too many rooms made from your address' in outs[2], 'limits: room creations per address are limited')
    try: body = urllib.request.urlopen(f'http://127.0.0.1:{a.port + 10}/health', timeout=5).read().decode()
    except Exception as e: body = str(e)
    check(body.startswith('ARC relay (protocol 1)'), f'limits: a plain GET answers the status ({body.strip()})')
    r.stop()
    check('shutting down' in r.out() and 'stopped' in r.out() and r.p.returncode == 0, 'limits: SIGTERM shuts the relay down cleanly')

def scen_web(a, d):
    # the web build in headless Chromium and a Linux headless client (and with --wine a Windows client) in one room
    web = os.path.join(ROOT, 'build', 'webtest_net')
    if not os.path.exists(os.path.join(web, 'netturns.html')):
        check(False, 'web: build/webtest_net/netturns.html missing (run with --web to build it)'); return
    r = Relay(a, d, a.port + 11)
    n_players = 3 if a.wine else 2
    hp = ['--set', 'netturns.hp=60']
    h = Proc(a.exe, 'Linux', f'net host_online; net wait connected 10; net wait players {n_players} 60; net start; autoplay 31; net wait turn_number 31 240; net sync; dump h.json; net wait players 1 60; quit', d, hp, r.url)
    code = h.room()
    w = None
    if a.wine:
        w = Proc(f'wine {os.path.join(ROOT, "build", "netturns_headless.exe")}', 'Windows', f'net join_online {code}; autoplay 31; net wait turn_number 31 240; net sync; dump w.json; net wait time 3; net leave; quit', d, hp, r.url)
    cmd = f'net join_online {code}; net wait connected 30; autoplay 31; net wait turn_number 31 240; net sync; dump; net wait time 3; net leave; quit'
    args = ['--net-relay', r.url, '--net-name', 'Browser', '--set', 'netturns.hp=60', '--mute', '--cmd', cmd]
    q = ' '.join(urllib.parse.quote(x, safe='') for x in args)
    env = dict(os.environ, NODE_PATH=subprocess.run(['npm', 'root', '-g'], capture_output=True, text=True).stdout.strip())
    t = time.time()
    wp = subprocess.run(['node', os.path.join(ROOT, 'tests', 'web_run.js'), web, 'netturns.html', '--args', q, '--until', 'ARC exit', '--timeout', '300', '--port', str(a.port + 30)],
                        capture_output=True, text=True, env=env, timeout=400)
    open(os.path.join(d, 'web.log'), 'w').write(wp.stdout + wp.stderr)
    ho = h.wait(120); wo = w.wait(120) if w else ''
    r.stop()
    m = re.search(r'ARC dump (\{.*\})', wp.stdout)
    wj = json.loads(m.group(1)) if m else {'error': 'no dump from the page'}
    hj = dump(d, 'h.json')
    check(re.search(r'ARC net connected player=[12] ', wp.stdout) is not None and f'ARC net room {code}' in wp.stdout, f'web: the browser joined room {code} over WebSocket ({time.time() - t:.0f} s)')
    g = hj.get('game', {})
    check(g.get('moves', 0) + g.get('attacks', 0) >= 50, f'web: 50+ game commands were played (moves {g.get("moves")}, attacks {g.get("attacks")})')
    same_game(hj, wj, 'web: Linux vs browser')
    if w: same_game(hj, dump(d, 'w.json'), 'web: Linux vs Windows (Wine)')

SCENARIOS = { 'basic': scen_basic, 'raw': scen_raw, 'three': scen_three, 'crash': scen_crash, 'turn': scen_turn, 'desync': scen_desync,
              'version': scen_version, 'codes': scen_codes, 'restart': scen_restart, 'simul': scen_simul, 'limits': scen_limits, 'web': scen_web }

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe'); ap.add_argument('--client-exe'); ap.add_argument('--relay'); ap.add_argument('--port', type=int, default=27960)   # (below the ephemeral range: no clash with a client's local port)
    ap.add_argument('--only', action='append'); ap.add_argument('--keep'); ap.add_argument('--web', action='store_true'); ap.add_argument('--wine', action='store_true')
    a = ap.parse_args()
    if not a.relay:
        subprocess.check_call(['cc', '-std=c11', '-Wall', '-Wextra', '-O2', 'tools/arc_relay/arc_relay.c', '-o', 'build/arc_relay'], cwd=ROOT)
        a.relay = os.path.join(ROOT, 'build', 'arc_relay')
    if not a.exe:
        exe = os.path.join(ROOT, 'build', 'netturns_headless')
        subprocess.check_call(['cc', '-std=c11', '-Wall', '-Wextra', '-O2', '-ffp-contract=off', '-D_GNU_SOURCE', '-DARC_HEADLESS_ONLY',
                               'samples/netturns/netturns.c', 'src/arc.c', '-o', exe, '-lm'], cwd=ROOT)
        a.exe = exe
    if a.wine and not os.path.exists(os.path.join(ROOT, 'build', 'netturns_headless.exe')):
        subprocess.check_call(['x86_64-w64-mingw32-gcc', '-std=c11', '-Wall', '-Wextra', '-O2', '-ffp-contract=off', '-DARC_HEADLESS_ONLY',
                               'samples/netturns/netturns.c', 'src/arc.c', '-o', 'build/netturns_headless.exe', '-static'], cwd=ROOT)
    if a.web:
        web = os.path.join(ROOT, 'build', 'webtest_net'); os.makedirs(web, exist_ok=True)
        subprocess.check_call(['bash', '-c', 'source /home/claude/emsdk/emsdk_env.sh >/dev/null 2>&1; emcc -std=c11 -Iinclude -Wall -Wextra -O2 -ffp-contract=off samples/netturns/netturns.c src/arc.c '
                               '-o build/webtest_net/netturns.html --shell-file web/shell.html -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 '
                               '-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_arc_web_audio_fill,_arc_web_command -sEXPORTED_RUNTIME_METHODS=lengthBytesUTF8,stringToUTF8,HEAPF32'], cwd=ROOT)
    a.exe = os.path.abspath(a.exe) if os.path.exists(a.exe) else a.exe
    a.relay = os.path.abspath(a.relay) if os.path.exists(a.relay) else a.relay
    a.cexe = a.client_exe or a.exe
    base = os.path.abspath(a.keep or os.path.join(ROOT, 'build', 'relay_test'))
    shutil.rmtree(base, ignore_errors=True)
    names = a.only or [n for n in SCENARIOS if n != 'web' or a.web]
    for name in names:
        d = os.path.join(base, name); os.makedirs(d)
        print(f'--- {name}', flush=True)
        t = time.time(); SCENARIOS[name](a, d); print(f'      ({time.time() - t:.1f} s)')
    print(f'ARC relay test {"FAILED" if FAILS else "OK"}: {len(FAILS)} failed')
    for f in FAILS: print('  ' + f)
    sys.exit(1 if FAILS else 0)

if __name__ == '__main__':
    main()
