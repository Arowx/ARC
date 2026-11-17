#!/usr/bin/env python3
"""Run many agent scripts through a few long-lived game processes (ARC 0.9.8, from MechBattleArc's request).

    python3 tools/arc_batch.py --exe build/game_headless [-j 4] [--args "--no-assets --no-session"] cases/*.txt
    python3 tools/arc_batch.py --exe build/game_headless --cases sweep.txt        (one file, cases split by lines of ---)

Starting a game per test case costs a process start and loading every asset each time; a balance sweep of 300 battles
spends most of its time there. arc_batch keeps N processes running (`--headless --stdin`, -j N, default the CPU count
up to 8) and feeds them one case after another, sending `arc_reset` between cases, so each case starts from the state
right after init (tick 0, the same seed, vars back to their defaults and --set values) exactly as a fresh process would.
Use --no-assets in --args for lighter processes when the cases don't take pictures.

A case is a list of agent commands, one or more per line (`step 60; press D` works), `#` starts a comment; `quit` lines
are left out (the process keeps running). Output: one JSON line per case, in the order given, to stdout or --out FILE:
    {"case": "cases/a.txt", "ok": true, "dumps": [{...game...}], "errors": [], "hints": [], "seconds": 1.2, "worker": 0}
"dumps" are the `game` objects of the case's `dump` commands (--full keeps the whole dump: engine, stats, ui...).
"ok" is false when the case printed an `ARC error` line, timed out (--timeout, default 300 s) or the process died
(it is started again for the next case). Exit code 1 if any case is not ok. Progress goes to stderr.
"""
import argparse, json, os, queue, shlex, subprocess, sys, threading, time


def read_cases(a):
    cases = []
    for path in a.files:
        with open(path) as f: cases.append((path, f.read()))
    if a.cases:
        with open(a.cases) as f: text = f.read()
        part, n = [], 0
        for line in text.splitlines() + ['---']:
            if line.strip() == '---':
                if any(l.strip() and not l.strip().startswith('#') for l in part):
                    n += 1; cases.append(('%s#%d' % (a.cases, n), '\n'.join(part)))
                part = []
            else: part.append(line)
    return cases


def commands(text):
    out = []
    for line in text.splitlines():
        line = line.split('#', 1)[0].strip() if not line.strip().startswith('echo') else line.strip()
        if not line: continue
        parts = [p.strip() for p in line.split(';') if p.strip()]
        out += [p for p in parts if p != 'quit']
    return out


class Worker:
    def __init__(self, idx, argv):
        self.idx, self.argv, self.p, self.q, self.used = idx, argv, None, None, 0

    def start(self):
        self.p = subprocess.Popen(self.argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.q = queue.Queue(); self.used = 0
        def pump(p, q):
            for line in p.stdout: q.put(line.rstrip('\n'))
            q.put(None)
        threading.Thread(target=pump, args=(self.p, self.q), daemon=True).start()

    def alive(self): return self.p is not None and self.p.poll() is None

    def stop(self):
        if self.alive():
            try: self.p.stdin.write('quit\n'); self.p.stdin.flush(); self.p.wait(5)
            except Exception: self.p.kill()

    def run(self, name, cmds, timeout, full, reset_cmd):
        if not self.alive(): self.start()
        marker = '__arc_batch_%d_%d' % (self.idx, self.used)
        lines = ([reset_cmd] if self.used else []) + cmds + ['echo ' + marker]
        self.used += 1
        t0 = time.time(); res = {'case': name, 'ok': True, 'dumps': [], 'errors': [], 'hints': [], 'worker': self.idx}
        try:
            self.p.stdin.write('\n'.join(lines) + '\n'); self.p.stdin.flush()
        except Exception as e:
            res['ok'] = False; res['errors'].append('the process could not take commands: %s' % e); self.p = None
            res['seconds'] = round(time.time() - t0, 3); return res
        while True:
            left = timeout - (time.time() - t0)
            try: line = self.q.get(timeout=max(0.05, left))
            except queue.Empty: line = '__timeout__'
            if line is None or line == '__timeout__':
                res['ok'] = False
                res['errors'].append('timed out after %g s' % timeout if line else 'the process exited (exit code %s)' % self.p.poll())
                if self.alive(): self.p.kill()
                self.p = None; break
            if line == 'ARC echo ' + marker: break
            if line.startswith('ARC dump '):
                try:
                    d = json.loads(line[9:]); res['dumps'].append(d if full else d.get('game', d))
                except ValueError: res['errors'].append('unreadable dump')
            elif line.startswith('ARC error'):
                res['errors'].append(line); res['ok'] = False
            elif line.startswith('ARC hint'):
                res['hints'].append(line)
        res['seconds'] = round(time.time() - t0, 3)
        return res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='*', help='case files (agent commands)')
    ap.add_argument('--exe', required=True, help='the game, built headless (or any ARC build: --headless is added)')
    ap.add_argument('--args', default='', help='more arguments for every process, e.g. "--no-assets --set ai=2"')
    ap.add_argument('--cases', help='one file with many cases, split by lines of ---')
    ap.add_argument('-j', type=int, default=min(8, os.cpu_count() or 1), help='processes (default: CPUs, at most 8)')
    ap.add_argument('--timeout', type=float, default=300, help='seconds per case (default 300)')
    ap.add_argument('--out', help='write the JSON lines here instead of stdout')
    ap.add_argument('--full', action='store_true', help='keep whole dumps, not only their game object')
    ap.add_argument('--reset-cmd', default='arc_reset', help='sent between cases (default arc_reset: ARC\'s, even when the game has its own reset)')
    argv = sys.argv[1:]   # --args "--no-assets ..." starts with dashes: argparse would take it for an option
    for i in range(len(argv) - 1):
        if argv[i] == '--args': argv[i:i + 2] = ['--args=' + argv[i + 1]]; break
    a = ap.parse_args(argv)
    cases = read_cases(a)
    if not cases: ap.error('no cases: give case files or --cases FILE')
    argv = [a.exe, '--headless', '--stdin'] + shlex.split(a.args)
    workers = [Worker(i, argv) for i in range(max(1, min(a.j, len(cases))))]
    results = [None] * len(cases); todo = queue.Queue()
    for i, c in enumerate(cases): todo.put(i)
    lock = threading.Lock(); done = [0]; t0 = time.time()
    def work(w):
        while True:
            try: i = todo.get_nowait()
            except queue.Empty: break
            name, text = cases[i]
            r = w.run(name, commands(text), a.timeout, a.full, a.reset_cmd)
            with lock:
                results[i] = r; done[0] += 1
                sys.stderr.write('[%d/%d] %s %s (%.1f s, worker %d)%s\n' % (done[0], len(cases), 'ok  ' if r['ok'] else 'FAIL', name, r['seconds'], w.idx,
                                                                         ('  ' + r['errors'][0]) if r['errors'] else ''))
        w.stop()
    threads = [threading.Thread(target=work, args=(w,)) for w in workers]
    for t in threads: t.start()
    for t in threads: t.join()
    out = open(a.out, 'w') if a.out else sys.stdout
    for r in results: out.write(json.dumps(r) + '\n')
    if a.out: out.close()
    bad = sum(1 for r in results if not r['ok'])
    sys.stderr.write('arc_batch: %d case(s) in %.1f s with %d process(es), %d failed\n' % (len(cases), time.time() - t0, len(workers), bad))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
