#!/usr/bin/env python3
"""ARC 0.10.4 real-time (rollback) matches online: a local arc_relay on 127.0.0.1 and headless netarena players in a
room, joined by code, playing over UDP through the relay (or straight to each other once hole punching works).

    python3 tests/relay_rollback_test.py [--exe build/netarena_headless] [--client-exe X] [--relay build/arc_relay]
                                         [--port 47970] [--ticks 3000] [--only NAME] [--keep DIR]

Builds the relay and samples/netarena headless first unless given. --client-exe runs the joining players with another
binary (e.g. "wine /abs/path/netarena_headless.exe": Windows players against a Linux hub). Every player runs with
--net-lag 80 --net-loss 5 --net-jitter 20. Each match is held at a match tick (`net stop_at`) once every input before it
is known (`net wait confirmed`), so the final states can be compared exactly; along the way the players compare state
hashes every 10 ticks (a mismatch is a desync, reported on every machine).
Scenarios:
  direct    two players: hole punching works on one machine, so they talk directly after the first moments
  relayed   two players with --net-relay-only: every datagram goes through the relay
  three     three players, relayed: the hub (the lowest player) passes each one's input to the others
  drop      the joining player is killed mid-match: the hub drops it, reports it and plays on alone; it ends cleanly
  hubdrop   the hub is killed mid-match: the other player is told (the relay says it left) and is alone
Checks: the same final state hash (and game dump) on every machine, no desync, no rollback deeper than the window,
the path each player used (direct / relay). Exit code 0 when everything passes."""
import argparse, json, os, re, shlex, shutil, subprocess, sys, tempfile, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
FAILS = []
SIM = ['--net-lag', '80', '--net-loss', '5', '--net-jitter', '20']

def check(cond, what):
    print(('ok    ' if cond else 'FAIL  ') + what, flush=True)
    if not cond: FAILS.append(what)

class Proc:
    def __init__(self, exe, name, cmd, cwd, extra=()):
        self.name, self.cwd = name, cwd
        self.log = open(os.path.join(cwd, name + '.log'), 'w')
        self.p = subprocess.Popen(shlex.split(exe) + ['--headless', '--net-name', name, '--cmd', cmd] + list(extra), cwd=cwd, stdout=self.log, stderr=subprocess.STDOUT)
    def wait(self, t=240):
        try: self.p.wait(timeout=t)
        except subprocess.TimeoutExpired: self.p.kill(); self.p.wait(); print(f'      ({self.name} timed out)')
        self.log.close(); return self.out()
    def kill(self): self.p.kill(); self.p.wait(); self.log.close()
    def out(self): return open(os.path.join(self.cwd, self.name + '.log'), errors='replace').read()

def dump(cwd, f):
    try: return json.load(open(os.path.join(cwd, f)))
    except Exception: return None

def room_code(proc, t=20):
    end = time.time() + t
    while time.time() < end:
        m = re.search(r'room ([0-9A-Z]{4,6})', proc.out())
        if m: return m.group(1)
        time.sleep(0.1)
    return None

def scenario(name, a, base):
    cwd = os.path.join(base, name); os.makedirs(cwd, exist_ok=True)
    port = a.port + {'direct': 0, 'relayed': 1, 'three': 2, 'drop': 3, 'hubdrop': 4}[name]
    relay = subprocess.Popen([a.relay, '--port', str(port), '--seed', '7'], cwd=cwd, stdout=open(os.path.join(cwd, 'relay.log'), 'w'), stderr=subprocess.STDOUT)
    time.sleep(0.4)
    url = f'ws://127.0.0.1:{port}'; T = a.ticks
    extra = ['--net-relay', url] + SIM + (['--net-relay-only'] if name in ('relayed', 'three') else [])
    n = 3 if name == 'three' else 2
    t0 = time.time()
    play = f'net stop_at {T}; net wait confirmed {T} 180'
    if name == 'drop':      hub_cmd = 'net wait tick 900 60; net wait dropped 30; net wait tick 1500 60; dump h.json; quit'
    elif name == 'hubdrop': hub_cmd = 'net wait tick 900 60; net wait tick 999999 600'
    else:                   hub_cmd = f'{play}; dump h.json; net wait alone 30; quit'
    hub = Proc(a.exe, 'hub', f'net host_online; net wait players {n} 30; net start; net wait started; bot 1; ' + hub_cmd, cwd, extra)
    code = room_code(hub)
    check(code is not None, f'{name}: the hub made a room ({code})')
    joins = []
    for k in range(1, n):
        if name == 'drop':      jc = 'net wait tick 900 60; net wait tick 999999 600'
        elif name == 'hubdrop': jc = 'net wait tick 900 60; net wait alone 30; net wait tick 1300 60; dump j1.json; quit'
        else:                   jc = f'{play}; dump j{k}.json; net leave; quit'
        joins.append(Proc(a.client_exe, f'j{k}', f'net join_online {code}; net wait started 30; bot {k + 1}; ' + jc, cwd, extra))
    if name == 'drop':
        wait_line(cwd, 'j1', 'ARC ok net wait tick'); joins[0].kill()
        outs = [hub.wait()]
    elif name == 'hubdrop':
        wait_line(cwd, 'hub', 'ARC ok net wait tick'); hub.kill()
        outs = [joins[0].wait()]
    else:
        outs = [j.wait() for j in joins] + [hub.wait()]
    relay.terminate(); relay.wait()
    secs = time.time() - t0
    rl = open(os.path.join(cwd, 'relay.log')).read()
    if name in ('direct', 'relayed', 'three'):
        ds = [dump(cwd, 'h.json')] + [dump(cwd, f'j{k}.json') for k in range(1, n)]
        check(all(d for d in ds), f'{name}: every machine wrote its dump')
        if all(d for d in ds):
            rbs = [d['net']['rollback'] for d in ds]
            check(all(r['confirmed'] >= T for r in rbs), f'{name}: every machine confirmed {T} ticks ({[r["confirmed"] for r in rbs]})')
            check(len({r['last_hash'] for r in rbs}) == 1 and all(r['last_hash_tick'] == T for r in rbs), f'{name}: the same state hash at tick {T} on every machine ({rbs[0]["last_hash"]})')
            check(len({json.dumps(d.get('game'), sort_keys=True) for d in ds}) == 1, f'{name}: the same game dump on every machine')
            check(all(r['desync_tick'] < 0 for r in rbs), f'{name}: no desync (hashes compared every 10 ticks)')
            check(all(r['max_depth'] <= r['max_rollback'] for r in rbs), f'{name}: no rollback deeper than the window ({[r["max_depth"] for r in rbs]} <= {rbs[0]["max_rollback"]}; rollbacks {[r["rollbacks"] for r in rbs]})')
            paths = [p['path'] for r in rbs for p in r.get('online', {}).get('peers', [])]
            want = 'relay' if name in ('relayed', 'three') else 'direct'
            check(paths and all(p == want for p in paths), f'{name}: every player reached the others {"through the relay" if want == "relay" else "directly"} ({paths})')
        check('datagrams forwarded' in rl and 'UDP from' in rl, f'{name}: the relay saw the players\' UDP')
    elif name == 'drop':
        out = outs[0]; d = dump(cwd, 'h.json')
        check(re.search(r'ARC net dropped 1 j1 at tick \d+', out) is not None, f'drop: the hub reported the dropped player')
        check(d is not None and d['net']['rollback']['dropped'] == 1 and d['net']['rollback']['tick'] >= 1500, 'drop: the hub played on alone to tick 1500 and quit cleanly')
        check('ARC exit' in out, 'drop: the hub exited normally')
    else:
        out = outs[0]; d = dump(cwd, 'j1.json')
        check(re.search(r'ARC net dropped 0 hub at tick \d+ \((left the room|no packets)', out) is not None, 'hubdrop: the player was told the hub has gone')
        check(d is not None and d['net']['rollback']['tick'] >= 1300, 'hubdrop: it played on alone and quit cleanly')
    print(f'      ({secs:.1f} s)', flush=True)

def wait_line(cwd, name, text, t=120):
    end = time.time() + t
    while time.time() < end:
        if text in open(os.path.join(cwd, name + '.log'), errors='replace').read(): return
        time.sleep(0.2)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe'); ap.add_argument('--client-exe'); ap.add_argument('--relay')
    ap.add_argument('--port', type=int, default=47970); ap.add_argument('--ticks', type=int, default=3000)
    ap.add_argument('--only'); ap.add_argument('--keep')
    a = ap.parse_args()
    if not a.exe:
        subprocess.run(['sh', 'build.sh', 'headless', 'samples/netarena/netarena.c'], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        a.exe = os.path.join(ROOT, 'build', 'netarena_headless')
    if not a.relay:
        subprocess.run(['sh', 'build.sh', 'relay'], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        a.relay = os.path.join(ROOT, 'build', 'arc_relay')
    a.client_exe = a.client_exe or a.exe
    base = a.keep or tempfile.mkdtemp(prefix='arc_rrt_')
    if a.keep: shutil.rmtree(base, ignore_errors=True); os.makedirs(base)
    for name in ('direct', 'relayed', 'three', 'drop', 'hubdrop'):
        if a.only and name != a.only: continue
        print(f'== {name}', flush=True)
        scenario(name, a, base)
    if not a.keep: shutil.rmtree(base, ignore_errors=True)
    print(f'ARC relay rollback test {"OK" if not FAILS else "FAILED"}: {len(FAILS)} failed')
    sys.exit(1 if FAILS else 0)

if __name__ == '__main__':
    main()
