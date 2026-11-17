#!/usr/bin/env python3
"""ARC 0.8.1 rollback multiplayer test with real UDP: separate headless netarena processes on localhost, driven by agent
scripts, in real time (a rollback match runs on the clock, headless too).

    python3 tests/rollback_test.py [--exe build/netarena_headless] [--client-exe X] [--port 47960] [--only NAME] [--keep DIR]

Builds samples/netarena headless first unless --exe is given. --client-exe runs the clients with another binary (e.g.
"wine build/netarena_headless.exe": a Windows client against a Linux host).
Every match is frozen at a match tick (`net stop_at`) once every input before it is known (`net wait confirmed`), so the
machines' final states can be compared exactly; each machine also records (--record) and its recording is replayed
offline, which must end in the same state with the same events.
Scenarios: lag0, lag50, lag150 (the --net-sim networks, also loss, duplicates and reordering), script (scripted key
presses and `net input` against a bot), three (3 players through the host), desync (one machine changes its state: both
report it at the same tick), stall (a client hangs for 2 s: the host waits, then both recover), drop (a client is killed:
the host drops it and plays on), sounds (a rollback-heavy match: every sound once). Exit code 0 when everything passes."""
import argparse, json, os, re, shlex, shutil, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
FAILS = []
STATS = []

def check(cond, what):
    print(('ok    ' if cond else 'FAIL  ') + what, flush=True)
    if not cond: FAILS.append(what)

class Proc:
    def __init__(self, exe, name, cmd, cwd, extra=()):
        self.name, self.cwd = name, cwd
        args = shlex.split(exe) + ['--headless', '--net-name', name, '--record', name + '.rec', '--cmd', cmd] + list(extra)
        self.log = open(os.path.join(cwd, name + '.log'), 'w')
        self.p = subprocess.Popen(args, cwd=cwd, stdout=self.log, stderr=subprocess.STDOUT)
    def wait(self, t=120):
        try: self.p.wait(timeout=t)
        except subprocess.TimeoutExpired: self.p.kill(); self.p.wait(); print(f'      ({self.name} timed out)')
        self.log.close()
        return self.out()
    def kill(self): self.p.kill(); self.p.wait(); self.log.close()
    def out(self): return open(os.path.join(self.cwd, self.name + '.log'), errors='replace').read()

def dump(cwd, f):
    try: return json.load(open(os.path.join(cwd, f)))
    except Exception as e: return {'error': str(e)}

def events(text):   # the game's events without ARC's tick numbers (machines start their matches at different ARC ticks)
    out = []
    for l in text.splitlines():
        if l.startswith('ARC event '):
            e = json.loads(l[10:]); e.pop('tick', None); e.pop('t', None)
            if e.get('ev') not in ('session_start', 'session_end'): out.append(e)
    return out

def stats_line(text):
    m = [l for l in text.splitlines() if l.startswith('ARC net rollback active=')]
    return m[-1] if m else ''

def host_cmd(port, players, stop, play='bot 1'):
    return (f'net host {port}; net wait players {players} 40; net start; net wait started 20; {play}; net wait tick {stop - 20} 60; '
            f'net stop_at {stop}; net wait confirmed {stop} 30; net stats; dump final.json; net wait alone 30; quit')

def client_cmd(port, stop, play='bot 2'):
    return (f'net join localhost:{port}; net wait started 40; {play}; net wait tick {stop - 20} 60; '
            f'net stop_at {stop}; net wait confirmed {stop} 30; net stats; dump final.json; net leave; quit')

def finish(a, d, procs, label, stop, replay=True):
    """compare every machine's final state, hash, events and sounds; then replay each recording offline"""
    outs = {p.name: p.out() for p in procs}
    for n, o in outs.items():
        check('ARC error' not in o, f'{label}: {n} printed no errors' + ('' if 'ARC error' not in o else ': ' + [l for l in o.splitlines() if 'ARC error' in l][0]))
        check('timed out' not in o, f'{label}: {n} had no waits time out')
    dumps = {n: dump(os.path.join(d, n), 'final.json') for n in outs}
    games = {n: j.get('game') for n, j in dumps.items()}
    rbs = {n: j.get('net', {}).get('rollback', {}) for n, j in dumps.items()}
    names = list(outs)
    g0 = games[names[0]]
    check(g0 is not None and g0.get('tick') == stop, f'{label}: {names[0]} stopped at match tick {stop} ({g0.get("tick") if g0 else None})')
    for n in names[1:]:
        check(games[n] is not None and games[n] == g0, f'{label}: {n} has the same final state as {names[0]} ({games[n].get("rules_hash") if games[n] else None} / {g0.get("rules_hash") if g0 else None})')
        check(rbs[n].get('last_hash') == rbs[names[0]].get('last_hash') and rbs[n].get('last_hash_tick') == stop, f'{label}: {n} has the same confirmed hash at tick {stop} ({rbs[n].get("last_hash")})')
    ev0 = events(outs[names[0]])
    for n in names[1:]: check(events(outs[n]) == ev0, f'{label}: {n} logged the same events ({len(ev0)})')
    for n in names:
        r = rbs[n]
        check(r.get('sounds_played', -1) - r.get('sounds_cancelled', 0) == (games[n] or {}).get('sounds', -2),
              f'{label}: {n} started every sound of the match once (played {r.get("sounds_played")} - stopped {r.get("sounds_cancelled")} = {(games[n] or {}).get("sounds")}; repeats skipped {r.get("sounds_deduped")}, late {r.get("sounds_late")})')
        STATS.append((label, n, r))
        print(f'      {n}: rollbacks {r.get("rollbacks")} avg depth {r.get("avg_depth", 0):.2f} max {r.get("max_depth")} resim {r.get("resim_ticks")} '
              f'predicted {r.get("predicted")} mispredicted {r.get("mispredicted")} skipped {r.get("skipped")} stalled {r.get("stalled_ms")} ms ping {r.get("ping_ms")} '
              f'adv {r.get("frame_advantage", 0):.2f} packets {r.get("packets_sent")}/{r.get("packets_received")}')
    if not replay: return games
    for n in names:
        nd = os.path.join(d, n)
        r = subprocess.run(shlex.split(a.exe) + ['--headless', '--replay', n + '.rec', '--dump-at-exit', 'replay.json'], cwd=nd, capture_output=True, text=True, timeout=120)
        rj = dump(nd, 'replay.json')
        check(rj.get('game') is not None and rj.get('game') == games[n], f'{label}: the offline replay of {n}\'s recording ends in the same state ({(rj.get("game") or {}).get("rules_hash")})')
        check(events(r.stdout) == ev0, f'{label}: ...with the same events')
    return games

def run_pair(a, d, label, port, stop, host_sim=(), client_sim=(), hplay='bot 1', cplay='bot 2', extra_h=(), extra_c=()):
    hd, cd = os.path.join(d, 'host'), os.path.join(d, 'client'); os.makedirs(hd); os.makedirs(cd)
    h = Proc(a.exe, 'host', host_cmd(port, 2, stop, hplay), hd, list(host_sim) + list(extra_h))
    time.sleep(0.3)
    c = Proc(a.cexe, 'client', client_cmd(port, stop, cplay), cd, list(client_sim) + list(extra_c))
    c.wait(); h.wait()
    return finish(a, d, [h, c], label, stop)

def scen_lag0(a, d):
    run_pair(a, d, 'lag0', a.port, 600)

def scen_lag50(a, d):
    sim = ['--net-sim', 'lag=50,jitter=10,loss=0.05,seed=3']
    run_pair(a, d, 'lag50', a.port + 1, 600, sim, sim)

def scen_lag150(a, d):
    run_pair(a, d, 'lag150', a.port + 2, 600, ['--net-sim', 'lag=150,jitter=30,loss=0.1,dup=0.05,seed=4'], ['--net-sim', 'lag=150,jitter=30,loss=0.1,dup=0.05,seed=7'])

def scen_script(a, d):
    # the host plays by hand (scripted key presses at uneven times), the client from `net input` bytes and a bot
    hplay = 'press W; step 23; press D; step 9; release D; press SPACE; step 31; release SPACE; release W; step 17; press A; press SPACE; step 40; release A; release SPACE'
    cplay = 'net input 0c; step 37; net input 05; step 11; net input 08; step 53; net input off; bot 3'
    sim = ['--net-sim', 'lag=40,jitter=40,loss=0.05,seed=9']
    g = run_pair(a, d, 'script', a.port + 3, 480, sim, sim, hplay, cplay)
    g = g.get('host') or {}
    check(g.get('shots', 0) > 3 and any(s['x'] != 0 for s in g.get('ships', [])), f'script: the scripted inputs played ({g.get("shots")} shots)')

def scen_three(a, d):
    port = a.port + 4; stop = 480
    procs = []
    for n in ('host', 'c1', 'c2'): os.makedirs(os.path.join(d, n))
    procs.append(Proc(a.exe, 'host', host_cmd(port, 3, stop, 'bot 1'), os.path.join(d, 'host'), ['--net-sim', 'lag=20,jitter=10,seed=1']))
    time.sleep(0.3)
    procs.append(Proc(a.cexe, 'c1', client_cmd(port, stop, 'bot 2'), os.path.join(d, 'c1'), ['--net-sim', 'lag=40,jitter=20,loss=0.05,seed=2']))
    time.sleep(0.2)
    procs.append(Proc(a.cexe, 'c2', client_cmd(port, stop, 'bot 3'), os.path.join(d, 'c2'), ['--net-sim', 'lag=70,jitter=30,loss=0.05,dup=0.05,seed=5']))
    for p in procs[::-1]: p.wait()
    finish(a, d, procs, 'three', stop)

def scen_desync(a, d):
    port = a.port + 5; rep = os.path.join(d, 'rep')
    hd, cd = os.path.join(d, 'host'), os.path.join(d, 'client'); os.makedirs(hd); os.makedirs(cd)
    h = Proc(a.exe, 'host', f'net host {port}; net wait players 2 30; net start; net wait started; bot 1; net wait desync 30; dump final.json; net wait alone 30; quit', hd, ['--net-report', rep])
    time.sleep(0.3)
    c = Proc(a.cexe, 'client', f'net join localhost:{port}; net wait started 30; bot 2; net wait tick 200 30; cheat 0; net wait desync 30; net leave; quit', cd, ['--net-report', rep])
    co, ho = c.wait(), h.wait()
    ht = re.search(r'ARC error desync tick=(\d+)', ho); ct = re.search(r'ARC error desync tick=(\d+)', co)
    check(ht is not None and ct is not None, 'desync: both machines print ARC error desync')
    check(ht and ct and ht.group(1) == ct.group(1) and int(ht.group(1)) >= 200, f'desync: at the same match tick on both ({ht.group(1) if ht else None} / {ct.group(1) if ct else None})')
    dirs = sorted(os.listdir(rep)) if os.path.isdir(rep) else []
    check(len(dirs) == 2 and all(os.path.exists(os.path.join(rep, x, f)) for x in dirs for f in ('dump.json', 'info.json', 'state.bin', 'inputs.txt')), f'desync: two report folders with dump, info, state, inputs ({dirs})')
    if len(dirs) == 2:
        i0, i1 = (json.load(open(os.path.join(rep, x, 'info.json'))) for x in dirs)
        s0, s1 = (open(os.path.join(rep, x, 'state.bin'), 'rb').read() for x in dirs)
        check(i0['desync_tick'] == i1['desync_tick'] and i0['my_hash'] != i1['my_hash'] and len(s0) == len(s1) and s0 != s1, f'desync: the reports hold different states of the same tick ({len(s0)} bytes)')

def scen_stall(a, d):
    # the client hangs for 2 s (no ticks, no packets) at its ARC tick 250: the host waits, says so, and both carry on
    g = run_pair(a, d, 'stall', a.port + 6, 600, [], ['--net-sim', 'freeze_at=250,freeze_ms=2000,lag=20,seed=2'])
    ho = open(os.path.join(d, 'host', 'host.log')).read(); co = open(os.path.join(d, 'client', 'client.log')).read()
    check('frozen for 2000 ms' in co, 'stall: the client hung')
    check('ARC net waiting for player 1' in ho and 'ARC net resumed' in ho, 'stall: the host waited for it (waiting / resumed)')
    m = re.search(r'resumed at tick \d+ after (\d+) ms', ho)
    check(m is not None and 1200 <= int(m.group(1)) <= 2600, f'stall: ...for about the hang ({m.group(1) if m else None} ms)')

def scen_drop(a, d):
    port = a.port + 7
    hd, cd = os.path.join(d, 'host'), os.path.join(d, 'client'); os.makedirs(hd); os.makedirs(cd)
    h = Proc(a.exe, 'host', f'net host {port}; net wait players 2 30; net start; net wait started; bot 1; net wait dropped 40; net wait tick 700 30; net stats; dump final.json; quit', hd)
    time.sleep(0.3)
    c = Proc(a.cexe, 'client', f'net join localhost:{port}; net wait started 30; bot 2; net wait tick 300 30; net wait seq 999 60', cd)
    deadline = time.time() + 40
    while time.time() < deadline and 'ARC ok net wait tick' not in c.out(): time.sleep(0.05)
    c.kill()
    ho = h.wait()
    check(re.search(r'ARC net dropped 1 client at tick \d+', ho) is not None, 'drop: the host dropped the killed client')
    j = dump(hd, 'final.json'); r = j.get('net', {}).get('rollback', {})
    check(r.get('tick', 0) >= 700 and r.get('dropped') == 1 and 'ARC error' not in ho, f'drop: and played on alone (tick {r.get("tick")})')

def scen_sounds(a, d):
    # a rollback-heavy match (deep corrections) with lots of shooting: no sound twice, every sound of the match once
    sim = ['--net-sim', 'lag=110,jitter=60,loss=0.08,seed=11']
    run_pair(a, d, 'sounds', a.port + 8, 600, sim, sim, 'bot 5', 'bot 6', ['--net-rollback', '12'], ['--net-rollback', '12'])
    for n in ('host', 'client'):
        r = dump(os.path.join(d, n), 'final.json').get('net', {}).get('rollback', {})
        check(r.get('sounds_deduped', 0) > 0 and r.get('rollbacks', 0) > 50 and r.get('max_rollback') == 12, f'sounds: {n} rolled back {r.get("rollbacks")} times (max {r.get("max_depth")}) and skipped {r.get("sounds_deduped")} repeats')

SCENARIOS = { 'lag0': scen_lag0, 'lag50': scen_lag50, 'lag150': scen_lag150, 'script': scen_script, 'three': scen_three,
              'desync': scen_desync, 'stall': scen_stall, 'drop': scen_drop, 'sounds': scen_sounds }

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe'); ap.add_argument('--client-exe'); ap.add_argument('--port', type=int, default=47960)
    ap.add_argument('--only', action='append'); ap.add_argument('--keep')
    a = ap.parse_args()
    if not a.exe:
        exe = os.path.join(ROOT, 'build', 'netarena_headless')
        subprocess.check_call(['cc', '-std=c11', '-Wall', '-Wextra', '-O2', '-ffp-contract=off', '-D_GNU_SOURCE', '-DARC_HEADLESS_ONLY',
                               'samples/netarena/netarena.c', 'src/arc.c', '-o', exe, '-lm'], cwd=ROOT)
        a.exe = exe
    a.exe = os.path.abspath(a.exe) if os.path.exists(a.exe) else a.exe
    a.cexe = a.client_exe or a.exe
    base = os.path.abspath(a.keep or os.path.join(ROOT, 'build', 'rollback_test'))
    shutil.rmtree(base, ignore_errors=True)
    for name in (a.only or list(SCENARIOS)):
        d = os.path.join(base, name); os.makedirs(d)
        print(f'--- {name}', flush=True)
        t = time.time(); SCENARIOS[name](a, d); print(f'      ({time.time() - t:.1f} s)')
    print(f'ARC rollback test {"FAILED" if FAILS else "OK"}: {len(FAILS)} failed')
    for f in FAILS: print('  ' + f)
    sys.exit(1 if FAILS else 0)

if __name__ == '__main__':
    main()
