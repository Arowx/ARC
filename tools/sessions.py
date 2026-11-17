#!/usr/bin/env python3
"""Read the play sessions a game recorded (arc_app.sessions): what happened, what the player flagged, how to replay.

    python tools/sessions.py sessions/                         one line per session, newest last
    python tools/sessions.py sessions/ --marks                 every F8 bookmark, with its screenshot and how to replay to it
    python tools/sessions.py sessions/ --event duel_end --field won        mean / count of a field, per session and overall
    python tools/sessions.py sessions/ --timeline rating --field after     a field's values in order (how a rating moved)
    python tools/sessions.py sessions/20261102-213000           one session in detail (event counts, last events, log)
    python tools/sessions.py sessions/ --exe build/droids.exe  print replay commands for that executable

Each session folder has events.jsonl (one JSON object per line: tick, t (seconds), ev, and the game's fields),
summary.json (written when the game closed normally; missing after a crash), input.rec (+ .saves) and log.txt.
A replay reproduces the session exactly:  GAME --replay sessions/<id>/input.rec
To see a moment:  GAME --headless --replay sessions/<id>/input.rec --cmd "step N; shot moment.png; quit"
"""
import argparse, collections, json, os, statistics, sys


def load_events(d):
    evs = []
    p = os.path.join(d, 'events.jsonl')
    if not os.path.exists(p): return evs
    with open(p, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line: continue
            try: evs.append(json.loads(line))
            except ValueError: evs.append({'ev': '(unreadable line)', 'raw': line[:120]})
    return evs


def load_summary(d):
    try:
        with open(os.path.join(d, 'summary.json'), encoding='utf-8') as f: return json.load(f)
    except (OSError, ValueError): return None


def session_dirs(root):
    if os.path.exists(os.path.join(root, 'events.jsonl')): return [root]
    return sorted(os.path.join(root, n) for n in os.listdir(root) if os.path.isdir(os.path.join(root, n)))


def fmt_secs(s):
    s = int(s); return '%d:%02d' % (s // 60, s % 60) if s < 3600 else '%d:%02d:%02d' % (s // 3600, s // 60 % 60, s % 60)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('path', help='the sessions folder, or one session folder')
    ap.add_argument('--marks', action='store_true', help='list F8 bookmarks')
    ap.add_argument('--event', help='event name to measure with --field')
    ap.add_argument('--timeline', help='event name whose --field values to list in order')
    ap.add_argument('--field', help='field of --event / --timeline')
    ap.add_argument('--last', type=int, default=0, help='only the last N sessions')
    ap.add_argument('--exe', default='GAME', help='the game executable, for replay commands')
    a = ap.parse_args()
    dirs = session_dirs(a.path)
    if a.last: dirs = dirs[-a.last:]
    if not dirs: sys.exit('no sessions in ' + a.path)

    if len(dirs) == 1 and not (a.marks or a.event or a.timeline):     # one session in detail
        d = dirs[0]; evs = load_events(d); sm = load_summary(d)
        print(d)
        if sm: print('  played %s, %d ticks, %d events, %d bookmarks, engine %s, seed %s' % (fmt_secs(sm.get('seconds_played', 0)), sm.get('ticks', 0), sm.get('events', 0), sm.get('bookmarks', 0), sm.get('engine'), sm.get('seed')))
        else: print('  no summary.json: the game did not close normally (crash or killed) - the replay still works up to the end')
        counts = collections.Counter(e.get('ev') for e in evs)
        print('  events: ' + ', '.join('%s %d' % kv for kv in counts.most_common()))
        print('  last events:')
        for e in evs[-12:]: print('    %8.2fs  %s' % (e.get('t', 0), json.dumps({k: v for k, v in e.items() if k not in ('t', 'tick')})))
        print('  replay: %s --replay %s' % (a.exe, os.path.join(d, 'input.rec')))
        log = os.path.join(d, 'log.txt')
        if os.path.exists(log) and os.path.getsize(log):
            print('  log.txt (last lines):')
            with open(log, encoding='utf-8', errors='replace') as f:
                for line in f.read().splitlines()[-8:]: print('    ' + line)
        return

    if a.marks:
        n = 0
        for d in dirs:
            for e in load_events(d):
                if e.get('ev') != 'bookmark': continue
                n += 1
                shot = os.path.join(d, 'mark_%02d.jpg' % e.get('n', 0))
                print('%s  %s  tick %d%s' % (os.path.basename(d), fmt_secs(e.get('t', 0)), e.get('tick', 0), ('  "%s"' % e['note']) if e.get('note') else ''))
                print('    screenshot: %s' % (shot if os.path.exists(shot) else '(none)'))
                print('    replay to it: %s --replay %s --cmd "step %d"' % (a.exe, os.path.join(d, 'input.rec'), max(0, e.get('tick', 0) - 300)))
        if not n: print('no bookmarks')
        return

    if a.event or a.timeline:
        name = a.event or a.timeline
        allv = []
        for d in dirs:
            vals = [e.get(a.field) for e in load_events(d) if e.get('ev') == name and (not a.field or a.field in e)]
            if a.timeline:
                if vals: print('%s  %s' % (os.path.basename(d), ' '.join(str(v) for v in vals)))
                continue
            nums = [float(v) for v in vals if isinstance(v, (int, float))]
            allv += nums
            if vals: print('%s  %-4d %s' % (os.path.basename(d), len(vals), ('mean %.3f' % statistics.fmean(nums)) if nums else ''))
        if a.event and allv: print('all       %-4d mean %.3f  median %.3f  min %g  max %g' % (len(allv), statistics.fmean(allv), statistics.median(allv), min(allv), max(allv)))
        return

    # overview
    print('%-22s %8s %6s %5s  %s' % ('session', 'played', 'events', 'marks', 'most common events'))
    tot = collections.Counter()
    for d in dirs:
        evs = load_events(d); sm = load_summary(d)
        counts = collections.Counter(e.get('ev') for e in evs); tot.update(counts)
        played = sm.get('seconds_played', 0) if sm else (evs[-1].get('t', 0) if evs else 0)
        top = ', '.join('%s %d' % kv for kv in counts.most_common(6) if kv[0] not in ('session_start', 'session_end'))
        print('%-22s %8s %6d %5d  %s%s' % (os.path.basename(d), fmt_secs(played), len(evs), counts.get('bookmark', 0), top, '' if sm else '  [no summary: crashed?]'))
    print('%d sessions; all events: %s' % (len(dirs), ', '.join('%s %d' % kv for kv in tot.most_common(10))))


if __name__ == '__main__':
    main()
