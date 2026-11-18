#!/usr/bin/env python3
"""ARC 0.10.10: talk to tools/arc_mcp.py as an MCP client would (JSON-RPC lines on stdin/stdout) and drive a game.

    python3 tests/mcp_test.py EXE [--window]

EXE is a build of tests/target_game.c. --window starts it with its window (needs a display) instead of headless.
Prints `ARC ok mcp protocol` or `ARC error mcp protocol ...` and exits 0 / 1.
"""
import json, os, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    if len(sys.argv) < 2: sys.exit(__doc__)
    exe, window = os.path.abspath(sys.argv[1]), '--window' in sys.argv
    srv = subprocess.Popen([sys.executable, os.path.join(ROOT, 'tools', 'arc_mcp.py'), '--game', ROOT],
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding='utf-8', bufsize=1)
    n = [0]
    def rpc(method, params=None, notify=False):
        msg = {'jsonrpc': '2.0', 'method': method}
        if params is not None: msg['params'] = params
        if not notify: n[0] += 1; msg['id'] = n[0]
        srv.stdin.write(json.dumps(msg) + '\n'); srv.stdin.flush()
        if notify: return None
        r = json.loads(srv.stdout.readline())
        assert r.get('id') == n[0], r
        return r
    def call(name, **args):
        r = rpc('tools/call', {'name': name, 'arguments': args})['result']
        t = ' / '.join(c['text'][:120].replace('\n', ' | ') if c['type'] == 'text' else '<%s %d bytes>' % (c['mimeType'], len(c['data'])) for c in r['content'])
        print('  %-12s %s %s' % (name, 'ERROR' if r['isError'] else 'ok', t))
        return r
    bad = []
    t0 = time.time()
    init = rpc('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {}, 'clientInfo': {'name': 'mcp_test', 'version': '1'}})
    rpc('notifications/initialized', notify=True)
    if init['result']['serverInfo']['name'] != 'arc': bad.append('initialize')
    tools = [t['name'] for t in rpc('tools/list')['result']['tools']]
    for want in ('arc_start', 'arc_step', 'arc_frame', 'arc_shot', 'arc_expect', 'arc_save_check'):
        if want not in tools: bad.append('no ' + want)
    if call('arc_start', exe=exe, window=window, args='--size 640x360')['isError']: bad.append('start')
    if call('arc_step', ticks=4)['isError']: bad.append('step')
    r = call('arc_expect', expect='tick == 4\nframe.markers[shape=ring].label == "!"\nframe.meshes[name=sentry].on_screen == 1')
    if r['isError']: bad.append('expect')
    r = call('arc_shot', max_width=320)
    if r['isError'] or r['content'][0]['type'] != 'image': bad.append('shot')
    if window:
        if 'run=1' not in call('arc_live', run=True)['content'][0]['text']: bad.append('live run')
        time.sleep(0.5)
        if 'run=0' not in call('arc_live', run=False)['content'][0]['text']: bad.append('live hold')
    r = call('arc_save_check', path=os.path.join(ROOT, 'build', 'mcp_test_check.md'))
    if r['isError']: bad.append('save_check')
    call('arc_stop')
    if rpc('ping')['result'] != {}: bad.append('ping')
    srv.stdin.close(); srv.wait(10)
    if not window:   # the saved session replays as a headless acceptance check
        rc = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'arc_check.py'), os.path.join(ROOT, 'build', 'mcp_test_check.md')], capture_output=True, text=True)
        print('  ' + (rc.stdout.strip().splitlines() or ['?'])[-1])
        if rc.returncode: bad.append('saved check')
    print(('ARC ok mcp protocol (%.1f s)' % (time.time() - t0)) if not bad else 'ARC error mcp protocol: ' + ', '.join(bad))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
