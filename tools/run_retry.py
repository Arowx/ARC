#!/usr/bin/env python3
"""Run a program, retrying while Windows App Control / Smart App Control refuses it (0.9.2).

    python tools/run_retry.py build/arc_test_post.exe --headless --frames 200

A freshly built unsigned exe is sometimes refused ("An Application Control policy has blocked this file", shown by
cmd as "blocked by your organization's Device Guard policy") and let through moments later. This waits and tries
again (5 tries over about 20 s), passes the program's output straight through, and exits with its exit code.
"""
import os, subprocess, sys, time

def main():
    if len(sys.argv) < 2: sys.exit(__doc__)
    cmd = sys.argv[1:]
    if os.name == 'nt' and not os.path.dirname(cmd[0]): cmd[0] = os.path.join('.', cmd[0])   # the current folder, as cmd does
    for attempt in range(5):
        try:
            return subprocess.call(cmd)
        except OSError as e:
            blocked = getattr(e, 'winerror', None) in (4551, 1260) or 'Application Control' in str(e)
            if not blocked: raise
            if attempt == 4:
                print('ARC error Windows blocked %s (Smart App Control / App Control: a new unsigned exe). Run the test '
                      'again; if it keeps happening, see Windows Security > App & browser control.' % cmd[0], flush=True)
                return 1
            print('run_retry: Windows blocked %s (App Control); trying again in %d s' % (cmd[0], 2 + 2 * attempt), flush=True)
            time.sleep(2 + 2 * attempt)

if __name__ == '__main__':
    sys.exit(main())
