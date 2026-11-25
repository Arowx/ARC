#!/usr/bin/env python3
"""Serve an ARC web build over http and open it in the browser (ARC 0.10.19). Browsers won't run a page's .wasm and
.data from file://, so a web build needs a small local server; this is the one build.bat web / build.sh web offer to
start.

    python tools/web_serve.py build/web                    # serves on http://localhost:8000/, opens index.html
    python tools/web_serve.py build/web --open demo.html   # another page
    python tools/web_serve.py build/web --port 8080 --no-open

The port goes up by one until a free one is found. Files are sent with no-cache headers, so a rebuild shows on reload,
and .wasm as application/wasm (older Pythons don't know it). Ctrl+C stops it. Standard library only.
"""
import argparse, functools, http.server, os, socket, sys, threading, webbrowser


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = dict(http.server.SimpleHTTPRequestHandler.extensions_map,
                          **{'.wasm': 'application/wasm', '.data': 'application/octet-stream', '.js': 'text/javascript',
                             '.arcpak': 'application/octet-stream', '': 'application/octet-stream'})

    def end_headers(self):
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        super().end_headers()

    def log_message(self, fmt, *args):   # quiet: only errors
        if len(args) > 1 and str(args[1])[:1] in '45':
            sys.stderr.write('ARC web_serve %s\n' % (fmt % args))


def free_port(start, tries=50):
    for p in range(start, start + tries):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            try:
                s.bind(('127.0.0.1', p)); return p
            except OSError:
                continue
    raise SystemExit('ARC error web_serve: no free port from %d to %d' % (start, start + tries - 1))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('folder', nargs='?', default='build/web', help='the web build folder (default build/web)')
    ap.add_argument('--port', type=int, default=8000, help='first port to try (default 8000)')
    ap.add_argument('--open', default='', help='the page to open (default: index.html, or the only .html in the folder)')
    ap.add_argument('--no-open', action='store_true', help="don't open a browser")
    ap.add_argument('--once', action='store_true', help='serve until one page has been fetched, then stop (tests)')
    a = ap.parse_args(argv)
    folder = os.path.abspath(a.folder)
    if not os.path.isdir(folder):
        print('ARC error web_serve: no folder %s - build the web version first' % a.folder); return 1
    page = a.open
    if not page:
        pages = sorted(f for f in os.listdir(folder) if f.endswith('.html'))
        page = 'index.html' if 'index.html' in pages else (pages[0] if len(pages) == 1 else '')
    if page and not os.path.exists(os.path.join(folder, page)):
        print('ARC error web_serve: %s has no %s' % (a.folder, page)); return 1
    port = free_port(a.port)
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', port), functools.partial(Handler, directory=folder))
    url = 'http://localhost:%d/%s' % (port, page)
    print('ARC web_serve %s at %s  (Ctrl+C stops it)' % (a.folder, url), flush=True)
    if a.once:
        served = threading.Event()
        orig = Handler.do_GET
        def once(self):
            orig(self); served.set()
        Handler.do_GET = once
        threading.Thread(target=lambda: (served.wait(30), srv.shutdown()), daemon=True).start()
    if not a.no_open:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print('\nARC web_serve stopped')
    finally:
        srv.server_close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
