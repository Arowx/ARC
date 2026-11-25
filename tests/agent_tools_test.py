"""ARC 0.10.16 (tooling request #12): tools/arc_brief.py and tools/arc_report.py on a game made from the tactics
template and on samples/board, as build test runs them. Also that a failing check makes arc_report exit 1, and that
--update-scripts adds the "Start here" note to an older game's AGENTS.md once.

    python3 tests/agent_tools_test.py [--exe build/board_headless]

Prints `ARC ok agent tools` or `ARC error agent tools: ...` and exits 0 / 1."""
import json, os, shutil, subprocess, sys, tempfile, time
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
T = os.path.join(ROOT, 'tools')
bad = []
def check(cond, what):
    if not cond: bad.append(what)
def run(*args, cwd=None):
    r = subprocess.run([sys.executable] + list(args), cwd=cwd, capture_output=True, text=True, errors='replace')
    return r.returncode, r.stdout, r.stderr
t0 = time.time()
d = tempfile.mkdtemp(prefix='arc_agent_tools_')
try:
    g = os.path.join(d, 'briefgame')
    rc, out, err = run(os.path.join(T, 'new_game.py'), g, '--template', 'tactics', '--title', 'Brief Game')
    check(rc == 0, 'new_game: ' + err[-200:])
    check('arc_brief' in open(os.path.join(g, 'AGENTS.md'), encoding='utf-8').read(), "the new game's AGENTS.md does not mention arc_brief")
    # the brief, before any build: everything from the source
    rc, out, err = run(os.path.join(T, 'arc_brief.py'), g, '--json', '--no-net', '--no-run')
    B = json.loads(out) if rc == 0 else {}
    cb = B.get('callbacks', {})
    for k in ('init', 'tick', 'draw', 'dump', 'command'): check(cb.get(k, {}).get('at'), 'brief: no location for ' + k)
    check(B.get('arc', {}).get('version'), 'brief: no ARC version')
    check(B.get('arc', {}).get('calls_need'), 'brief: no "needs ARC" version from the calls')
    check(len(B.get('dump', {}).get('fields', [])) >= 1, 'brief: no dump fields from the source')
    check(any(c['name'] == 'select' for c in B.get('commands', [])), 'brief: no debug commands')
    check(any(t['name'] == 'move' for t in B.get('tunables', [])), 'brief: no tunables')
    check(B.get('template') == 'tactics', 'brief: template not found')
    # the report: builds the game, runs every check, state_check (exit 0)
    rc, out, err = run(os.path.join(T, 'arc_report.py'), g, '--json')
    R = json.loads(out) if out.strip().startswith('{') else {}
    check(rc == 0 and R.get('ok'), 'report: exit %d: %s' % (rc, (out + err)[-400:]))
    check('errors' in R.get('doctor', {}), 'report: no doctor result')
    check('summary' in R.get('warnings', {}) or 'skipped' in R.get('warnings', {}), 'report: no warnings result')
    acc = R.get('acceptance', [])
    check(acc and all(a['ok'] for a in acc) and sum(len(a['blocks']) for a in acc) >= 2, 'report: acceptance blocks missing or failed: %s' % acc)
    check(R.get('state_check', {}).get('ok') and 'dump unchanged' in R.get('state_check', {}).get('summary', ''), 'report: no state_check')
    # the brief again, now with a headless build: the dump from a real run, and --check
    rc, out, err = run(os.path.join(T, 'arc_brief.py'), g, '--json', '--no-net', '--check')
    B = json.loads(out) if out.strip().startswith('{') else {}
    check(B.get('dump', {}).get('from', '').startswith('headless run') and 'units' in B.get('dump', {}).get('fields', []), 'brief: dump not read from a run: %s' % B.get('dump'))
    check(B.get('checks') and all(c['ok'] for c in B['checks']), 'brief --check: %s' % B.get('checks'))
    rc, md, err = run(os.path.join(T, 'arc_brief.py'), g, '--no-net', '--out', os.path.join(d, 'BRIEF.md'))
    check(rc == 0 and md.count('\n') < 150 and os.path.exists(os.path.join(d, 'BRIEF.md')), 'brief markdown: %d lines' % md.count('\n'))
    # a failing check: the report says so and exits 1
    acc_md = os.path.join(g, 'tests', 'acceptance.md')
    open(acc_md, 'a', encoding='utf-8').write('\n## Broken on purpose\n\n```arc-check\ngame .\nexpect game.turn == 99\n```\n')
    rc, out, err = run(os.path.join(T, 'arc_report.py'), g)
    check(rc == 1 and 'FAILED Broken on purpose' in out, 'report with a failing check: exit %d\n%s' % (rc, out[-300:]))
    # a sample in ARC's repo
    rc, out, err = run(os.path.join(T, 'arc_brief.py'), os.path.join(ROOT, 'samples', 'board', 'board.c'), '--json', '--no-net')
    S = json.loads(out) if rc == 0 else {}
    check(all(S.get('callbacks', {}).get(k, {}).get('at') for k in ('init', 'tick', 'draw', 'dump', 'command')), 'brief of samples/board: callbacks %s' % S.get('callbacks'))
    check(len(S.get('dump', {}).get('fields', [])) >= 1, 'brief of samples/board: no dump fields')
    exe = sys.argv[sys.argv.index('--exe') + 1] if '--exe' in sys.argv else None
    rc, out, err = run(os.path.join(T, 'arc_check.py'), os.path.join(ROOT, 'tests', 'agent_tools_check.md'), *(['--exe', exe] if exe else []))
    check(rc == 0, 'tests/agent_tools_check.md: ' + (out.strip().splitlines() or ['?'])[-1])
    # an older game's AGENTS.md gets the note once
    old = os.path.join(d, 'oldgame'); run(os.path.join(T, 'new_game.py'), old)
    ag = os.path.join(old, 'AGENTS.md'); s = open(ag, encoding='utf-8').read()
    open(ag, 'w', encoding='utf-8').write(s[:s.index('**Start here:**')] + s[s.index('Then read:'):])
    run(os.path.join(T, 'new_game.py'), old, '--update-scripts'); run(os.path.join(T, 'new_game.py'), old, '--update-scripts')
    check(open(ag, encoding='utf-8').read().count('## Start here (ARC 0.10.16)') == 1, '--update-scripts: the Start here note is not there exactly once')
finally:
    shutil.rmtree(d, ignore_errors=True)
for b in bad: print('ARC error agent tools: ' + b)
if not bad: print('ARC ok agent tools (%.0f s)' % (time.time() - t0))
sys.exit(1 if bad else 0)
