#!/usr/bin/env python3
"""ARC 0.8 multiplayer test with real sockets: separate headless game processes on localhost, driven by agent scripts.

    python3 tests/net_test.py [--exe build/netturns_headless] [--client-exe X] [--port 47930] [--only NAME] [--keep DIR]

Builds samples/netturns headless first unless --exe is given. Each scenario starts a host and clients (each its own
process, `--headless --cmd ...`), then checks their output and their final dumps. --client-exe runs the clients with
another binary (e.g. "wine build/netturns_headless.exe": a Windows client against a Linux host).
Scenarios: basic (lag + jitter + loss), three (3 players, one cable pulled and plugged back in), crash (a client killed
and restarted mid-match: it rejoins by name and catches up), version (refused on both sides), turn (a command from a
player whose turn it isn't is refused by the host), desync (one machine changes its state locally: detected, reports
written), resume (the host saves the log, everyone quits, the match resumes from it), find (LAN discovery), replay
(a client's recording replays offline to the same state). Exit code 0 when everything passes."""
import argparse, json, os, shlex, shutil, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
FAILS = []

def check(cond, what):
    print(('ok    ' if cond else 'FAIL  ') + what, flush=True)
    if not cond: FAILS.append(what)

def turns(n, wait=40):
    return ''.join(f'; net wait turn {wait}; ai; net end_turn' for _ in range(n))

class Proc:
    def __init__(self, exe, name, cmd, cwd, extra=()):
        self.name, self.cwd = name, cwd
        args = shlex.split(exe) + ['--headless', '--net-name', name, '--cmd', cmd] + list(extra)
        self.log = open(os.path.join(cwd, name + '.log'), 'w')
        self.p = subprocess.Popen(args, cwd=cwd, stdout=self.log, stderr=subprocess.STDOUT)
    def wait(self, t=90):
        try: self.p.wait(timeout=t)
        except subprocess.TimeoutExpired: self.p.kill(); self.p.wait(); print(f'      ({self.name} timed out)')
        self.log.close()
        return self.out()
    def kill(self): self.p.kill(); self.p.wait(); self.log.close()
    def out(self): return open(os.path.join(self.cwd, self.name + '.log')).read()

def dump(cwd, f):
    try: return json.load(open(os.path.join(cwd, f)))
    except Exception as e: return {'error': str(e)}

def same_game(a, b, label):
    ga, gb = a.get('game'), b.get('game')
    check(ga is not None and ga == gb, f'{label}: game state identical ({ga.get("rules_hash") if ga else a})')
    na, nb = a.get('net', {}), b.get('net', {})
    check(na.get('seq') == nb.get('seq') and na.get('last_hash') == nb.get('last_hash'), f'{label}: same seq {na.get("seq")} and hash {na.get("last_hash")} / {nb.get("seq")} {nb.get("last_hash")}')

def scen_basic(a, d):
    sim = ['--net-sim', 'lag=60,jitter=40,loss=0.05,seed=3']
    h = Proc(a.exe, 'host', f'net host {a.port}; net wait players 2 30; net start; net wait started' + turns(10) + '; net wait turn_number 21 40; net sync; dump h.json; net log h.log; net wait players 1 30; quit', d, sim)
    time.sleep(0.3)
    c = Proc(a.cexe, 'client', f'net join localhost:{a.port}; net wait started 30' + turns(10) + '; net wait turn_number 21 40; net sync; dump c.json; net log c.log; net leave; quit', d, sim + ['--seed', '9'])
    co, ho = c.wait(), h.wait()
    check('ARC error' not in ho and 'ARC error' not in co, 'basic: no errors')
    check('timed out' not in ho + co, 'basic: no waits timed out')
    same_game(dump(d, 'h.json'), dump(d, 'c.json'), 'basic')
    hl = open(os.path.join(d, 'h.log')).read().split('---\n')[1] if os.path.exists(os.path.join(d, 'h.log')) else 'x'
    cl = open(os.path.join(d, 'c.log')).read().split('---\n')[1] if os.path.exists(os.path.join(d, 'c.log')) else 'y'
    check(hl == cl and hl.count('\n') > 20, f'basic: command logs identical ({hl.count(chr(10))} commands)')
    g = dump(d, 'h.json').get('game', {})
    check(g.get('attacks', 0) > 0 and g.get('moves', 0) > 0, f'basic: the match was played (moves {g.get("moves")}, attacks {g.get("attacks")}, winner {g.get("winner")})')

def scen_three(a, d):
    # c2 pulls its cable at tick 150 and plugs it back in at tick 400; meanwhile its turns are skipped (or, if it was its
    # turn, the match waits for it). Everyone plays by itself (autoplay) until turn 25.
    port = a.port + 1
    h = Proc(a.exe, 'host', f'net host {port}; net wait players 3 30; net start; autoplay 25; net wait turn_number 25 90; net sync; dump h.json; net wait players 1 60; quit', d, ['--net-sim', 'lag=30,jitter=30,seed=1'])
    time.sleep(0.3)
    c1 = Proc(a.cexe, 'c1', f'net join localhost:{port}; autoplay 25; net wait turn_number 25 90; net sync; dump c1.json; net leave; quit', d)
    time.sleep(0.2)
    c2 = Proc(a.cexe, 'c2', f'net join localhost:{port}; autoplay 25; net wait started 30; net wait turn_number 25 90; net sync; dump c2.json; net leave; quit', d,
              ['--net-sim', 'drop_at=150,rejoin_at=400,lag=20,seed=5'])
    o2, o1, ho = c2.wait(120), c1.wait(120), h.wait(120)
    check('network dropped (simulated)' in o2 and 'rejoining' in o2, 'three: c2 dropped and rejoined')
    check('ARC net left 2' in ho and 'ARC net rejoined 2' in ho, 'three: the host saw c2 leave and rejoin (sequenced)')
    check('timed out' not in ho + o1 + o2 and 'ARC error' not in ho + o1 + o2, 'three: no errors, no waits timed out')
    hj, j1, j2 = dump(d, 'h.json'), dump(d, 'c1.json'), dump(d, 'c2.json')
    same_game(hj, j1, 'three host/c1'); same_game(hj, j2, 'three host/c2')

def scen_crash(a, d):
    port = a.port + 2
    h = Proc(a.exe, 'host', f'net host {port}; net wait players 2 30; net start; net wait started' + turns(9) + '; net wait turn_number 19 60; net sync; dump h.json; net wait players 1 60; quit', d)
    time.sleep(0.3)
    c = Proc(a.cexe, 'crashy', f'net join localhost:{port}; net wait started 30' + turns(3) + '; net wait seq 1000 60', d)   # plays 3 turns then hangs
    deadline = time.time() + 30
    while time.time() < deadline and c.out().count('ARC ok net end_turn') < 3: time.sleep(0.05)
    c.kill()                                                   # the process dies; its successor is there at once (often before the host has noticed)
    c2 = Proc(a.cexe, 'crashy', f'net join localhost:{port}; net wait started 30' + turns(6) + '; net wait turn_number 19 60; net sync; dump c.json; net leave; quit', d)
    o2, ho = c2.wait(), h.wait(120)
    check('ARC net left 1' in ho and 'ARC net rejoined 1' in ho, 'crash: host saw the player leave (timeout/close) and rejoin by name')
    check('timed out' not in ho + o2, 'crash: no waits timed out')
    same_game(dump(d, 'h.json'), dump(d, 'c.json'), 'crash (restarted client caught up)')

def scen_version(a, d):
    port = a.port + 3
    h = Proc(a.exe, 'host', f'net host {port}; net wait players 2 4; quit', d)
    time.sleep(0.3)
    c = Proc(a.cexe, 'old', f'net join localhost:{port}; net wait status refused 10; net status; quit', d, ['--net-version', '0.9'])
    co, ho = c.wait(), h.wait()
    check('ARC error net refused: version mismatch' in co and 'netturns 1.0' in co and '0.9' in co, 'version: the client was told why')
    check('refused old: version mismatch' in ho, 'version: the host logged it')

def scen_turn(a, d):
    port = a.port + 4
    # seq 1, 2: joins, 3: start, 4: the client's chat (free, out of turn) after it tried a move out of turn
    h = Proc(a.exe, 'host', f'net host {port}; net wait players 2 30; net start; net wait seq 4 30; net end_turn; net cmd move 0 1 0; net wait players 1 30; dump h.json; quit', d)
    time.sleep(0.3)
    # the client's end_turn and the move after it go out together: the client still thinks it is its turn, the host refuses the move
    c = Proc(a.cexe, 'eager', f'net join localhost:{port}; net wait started 30; net cmd move 2 7 4; net free chat early; net wait turn 30; net end_turn; net cmd move 3 6 5; net free chat hello; net sync; dump c.json; net leave; quit', d,
             ['--net-sim', 'lag=50,seed=2'])
    co, ho = c.wait(), h.wait()
    check('ARC error net refused (not your turn): move 0 1 0' in ho, 'turn: the host refused itself locally (not its turn)')
    check('ARC error net refused (not your turn): move 2 7 4' in co, 'turn: the client refused its own early command locally')
    check('ARC log net refused player 1 (not your turn): move 3 6 5' in ho and 'ARC error net refused (not your turn): move 3 6 5' in co, 'turn: the host refused a command sent after the turn ended')
    hj, cj = dump(d, 'h.json'), dump(d, 'c.json')
    check(hj.get('game') is not None and hj.get('game') == cj.get('game'), 'turn: game state identical')
    check(hj.get('game', {}).get('chat') == '1: hello', 'turn: a free command (chat) went through out of turn')

def scen_desync(a, d):
    port = a.port + 5
    rep = os.path.join(d, 'rep')
    h = Proc(a.exe, 'host', f'net host {port}; net wait players 2 30; net start; net wait started' + turns(3) + '; net wait desync 30; dump h.json; net wait players 1 30; quit', d, ['--net-report', rep])
    time.sleep(0.3)
    c = Proc(a.cexe, 'cheater', f'net join localhost:{port}; net wait started 30' + turns(1) + '; cheat 0 3' + turns(2) + '; net wait desync 30; net leave; quit', d, ['--net-report', rep])
    co, ho = c.wait(), h.wait()
    check('ARC error desync seq=' in ho and 'ARC error desync seq=' in co, 'desync: both machines print ARC error desync')
    hs = ho.split('ARC error desync seq=')[1].split()[0] if 'ARC error desync seq=' in ho else '?'
    cs = co.split('ARC error desync seq=')[1].split()[0] if 'ARC error desync seq=' in co else '!'
    check(hs == cs, f'desync: the same sequence number on both ({hs})')
    dirs = sorted(os.listdir(rep)) if os.path.isdir(rep) else []
    check(len(dirs) == 2 and all(os.path.exists(os.path.join(rep, x, f)) for x in dirs for f in ('dump.json', 'log.txt', 'info.json', 'state.bin')), f'desync: two report folders with dump, log, info, state ({dirs})')
    if len(dirs) == 2:
        i0, i1 = (json.load(open(os.path.join(rep, x, 'info.json'))) for x in dirs)
        check(i0['report_seq'] == i1['report_seq'] and len(set(i0['hashes'].values())) == 2, f'desync: reports at the same seq ({i0["report_seq"]}) with different hashes')
        d0, d1 = (json.load(open(os.path.join(rep, x, 'dump.json')))['game'] for x in dirs)
        check(d0['units'][0]['hp'] != d1['units'][0]['hp'], f'desync: the dumps show the difference (unit 0 hp {d0["units"][0]["hp"]} vs {d1["units"][0]["hp"]})')

def scen_resume(a, d):
    port = a.port + 6
    h = Proc(a.exe, 'host', f'net host {port}; net wait players 2 30; net start; net wait started' + turns(3) + '; net wait turn 30; net sync; net log match.log; dump before.json; quit', d)
    time.sleep(0.3)
    c = Proc(a.cexe, 'resumer', f'net join localhost:{port}; net wait started 30' + turns(3) + '; net wait status lost 30; quit', d)
    c.wait(); h.wait()
    h2 = Proc(a.exe, 'host2', f'net host {port}; net resume match.log; net wait players 2 30; net wait turn 30; dump resumed.json' + turns(4) + '; net wait turn_number 15 40; net sync; dump h.json; net wait players 1 30; quit', d)
    time.sleep(0.3)
    c2 = Proc(a.cexe, 'resumer', f'net join localhost:{port}; net wait started 30' + turns(4) + '; net wait turn_number 15 40; net sync; dump c.json; net leave; quit', d)
    o2, ho = c2.wait(), h2.wait()
    check('ARC net resumed' in ho and 'ARC net rejoined 1' in ho, 'resume: the host resumed the log and the player rejoined')
    b, r = dump(d, 'before.json').get('game'), dump(d, 'resumed.json').get('game')
    check(b is not None and b == r, 'resume: the resumed state equals the state when the log was saved')
    same_game(dump(d, 'h.json'), dump(d, 'c.json'), 'resume')

def scen_find(a, d):
    port = a.port + 7
    h = Proc(a.exe, 'host', f'net host {port}; net wait players 2 15; quit', d)
    time.sleep(0.3)
    c = Proc(a.cexe, 'finder', 'net find; step 1; net wait seq 99 2; net games; quit', d)
    co = c.wait(); h.kill()
    check(f':{port} name="host\'s game" game="netturns" version="1.0"' in co, 'find: the host answered the LAN search')

def scen_replay(a, d):
    port = a.port + 8
    h = Proc(a.exe, 'host', f'net host {port}; net wait players 2 30; net start; net wait started' + turns(4) + '; net wait turn_number 9 30; net sync; net wait players 1 30; quit', d)
    time.sleep(0.3)
    c = Proc(a.cexe, 'rec', f'net join localhost:{port}; net wait started 30' + turns(4) + '; net wait turn_number 9 30; net sync; step 5; dump live.json; rewind 90; dump back.json; step 90; dump rew.json; quit', d, ['--record', 'rec.rec'])
    c.wait(); h.wait()
    lj = dump(d, 'live.json')
    r = subprocess.run(shlex.split(a.cexe) + ['--headless', '--replay', 'rec.rec', '--dump-at-exit', 'replay.json'], cwd=d, capture_output=True, text=True, timeout=60)
    rj = dump(d, 'replay.json')
    check('net replay:' in r.stdout, 'replay: the recording brought its .net command log')
    check(lj.get('game') is not None and lj.get('game') == rj.get('game'), f'replay: the replay ends in the same game state ({rj.get("game", {}).get("rules_hash")})')
    bj, wj = dump(d, 'back.json'), dump(d, 'rew.json')
    check('tick' in bj and bj['tick'] < lj.get('tick', 0) and bj.get('net', {}).get('seq', 99) < lj.get('net', {}).get('seq', 0), f'replay: rewind in a live session went back (tick {bj.get("tick")} seq {bj.get("net", {}).get("seq")} from tick {lj.get("tick")} seq {lj.get("net", {}).get("seq")})')
    check(wj.get('game') == lj.get('game') and wj.get('net', {}).get('seq') == lj.get('net', {}).get('seq'), 'replay: and forward again to the same state')

SCENARIOS = { 'basic': scen_basic, 'three': scen_three, 'crash': scen_crash, 'version': scen_version, 'turn': scen_turn,
              'desync': scen_desync, 'resume': scen_resume, 'find': scen_find, 'replay': scen_replay }

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe'); ap.add_argument('--client-exe'); ap.add_argument('--port', type=int, default=47930)
    ap.add_argument('--only', action='append'); ap.add_argument('--keep')
    a = ap.parse_args()
    if not a.exe:
        exe = os.path.join(ROOT, 'build', 'netturns_headless')
        subprocess.check_call(['cc', '-std=c11', '-Wall', '-Wextra', '-O2', '-ffp-contract=off', '-D_GNU_SOURCE', '-DARC_HEADLESS_ONLY',
                               'samples/netturns/netturns.c', 'src/arc.c', '-o', exe, '-lm'], cwd=ROOT)
        a.exe = exe
    a.exe = os.path.abspath(a.exe) if os.path.exists(a.exe) else a.exe
    a.cexe = a.client_exe or a.exe
    base = os.path.abspath(a.keep or os.path.join(ROOT, 'build', 'net_test'))
    shutil.rmtree(base, ignore_errors=True)
    for name in (a.only or list(SCENARIOS)):
        d = os.path.join(base, name); os.makedirs(d)
        print(f'--- {name}', flush=True)
        t = time.time(); SCENARIOS[name](a, d); print(f'      ({time.time() - t:.1f} s)')
    print(f'ARC net test {"FAILED" if FAILS else "OK"}: {len(FAILS)} failed')
    for f in FAILS: print('  ' + f)
    sys.exit(1 if FAILS else 0)

if __name__ == '__main__':
    main()
