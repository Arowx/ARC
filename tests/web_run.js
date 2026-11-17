// ARC 0.8.5 web test runner (headless Chromium through Playwright):
//   node tests/web_run.js DIR PAGE [--args "a b c"] [--until TEXT] [--timeout S] [--viewport WxH] [--port N] [--click LABEL] [--shot FILE]
//                                  [--visits N] [--throttle MBITS,LATENCY_MS] [--metrics]                       (0.8.6)
// Serves DIR on localhost, opens PAGE with the args as the page's URL query (each one becomes an argv entry), prints the
// page's "ARC ..." console lines, and exits 0 when a line containing TEXT appears (default "ARC exit"), 1 on time-out.
// --click LABEL: once the page prints "ARC ready-click", asks ARC for its UI widgets (`ui`: JSON), finds LABEL's rectangle in
// render pixels, converts it to page coordinates through the canvas's bounding box, and clicks it with real mouse events
// (move, down, 120 ms, up), as a player would: the test of clicks on a canvas that is not at the page's top-left.
// 0.8.6: --visits N opens the page N times in the same browser profile (the second visit finds what the first cached:
// the Cache API, IndexedDB; the server sends no cache headers, so the HTTP cache does not help); --throttle 20,50 emulates
// a 20 Mbit/s link with 50 ms latency (Chrome DevTools Protocol, Network.emulateNetworkConditions); --metrics prints, per
// visit, "runner: visit N ms=... task_ms=... bytes=..." (wall time to TEXT, main-thread task time from
// Performance.getMetrics TaskDuration, bytes and files the server sent).
// Needs Playwright (NODE_PATH=$(npm root -g) when it is installed globally).
const { chromium } = require('playwright'); const http = require('http'); const fs = require('fs'); const path = require('path');
const argv = process.argv.slice(2); const dir = argv[0], page = argv[1];
const opt = (k, d) => { const i = argv.indexOf(k); return i >= 0 && i + 1 < argv.length ? argv[i + 1] : d; };
const args = opt('--args', ''), until = opt('--until', 'ARC exit'), timeout = parseFloat(opt('--timeout', '120'));
const vp = opt('--viewport', '1280x720').split('x').map(Number), port = parseInt(opt('--port', '8765')), click = opt('--click', null), shot = opt('--shot', null);
const visits = parseInt(opt('--visits', '1')), throttle = opt('--throttle', null), metrics = argv.includes('--metrics');
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream' };
let served = 0, servedFiles = [];
const srv = http.createServer((q, r) => { const f = path.join(dir, decodeURIComponent(q.url.split('?')[0])); fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); r.end(); return; } served += d.length; servedFiles.push(path.basename(f)); r.writeHead(200, { 'Content-Type': types[path.extname(f)] || 'application/octet-stream', 'Content-Length': d.length }); r.end(d); }); });
srv.listen(port, async () => {
  const b = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--no-proxy-server'] });
  const ctx = await b.newContext({ viewport: { width: vp[0], height: vp[1] } });
  let p = await ctx.newPage(), done = false, uiLines = [], wantUi = false, clicked = false;
  let cdp = await ctx.newCDPSession(p);
  const cdpSetup = async () => {
    await cdp.send('Performance.enable');
    if (throttle) {
      const [mbit, lat] = throttle.split(',').map(Number);
      await cdp.send('Network.enable');
      await cdp.send('Network.emulateNetworkConditions', { offline: false, latency: lat || 0, downloadThroughput: mbit * 1e6 / 8, uploadThroughput: mbit * 1e6 / 8 });
    }
  };
  await cdpSetup();
  const taskTime = async () => { const m = await cdp.send('Performance.getMetrics'); const t = m.metrics.find(x => x.name === 'TaskDuration'); return t ? t.value * 1000 : 0; };
  const hook = pg => { pg.on('console', m => {
    const t = m.text();
    if (wantUi && t.startsWith('ARC ui ')) uiLines.push(t);
    if (t.includes('ARC') && !t.includes('ok shot')) console.log(t);
    if (t.includes(until)) done = true;
    if (click && !clicked && t.includes('ARC ready-click')) { clicked = true; doClick().catch(e => console.log('runner: click failed: ' + e)); }
  }); pg.on('pageerror', e => console.log('runner: page error: ' + e)); };
  hook(p);
  async function doClick() {
    wantUi = true; uiLines = [];
    await p.evaluate(() => Module.arcCommand('ui'));
    await new Promise(r => setTimeout(r, 300)); wantUi = false;
    let wd = null;
    for (const l of uiLines) { try { for (const w of JSON.parse(l.slice(7))) if (w.label === click) wd = w; } catch (e) {} }
    if (!wd) { console.log('runner: no widget ' + click + ' in ' + uiLines.length + ' ui lines'); return; }
    const x = wd.x, y = wd.y, w = wd.w, h = wd.h;
    const box = await p.evaluate(() => { const c = document.getElementById('canvas'), r = c.getBoundingClientRect(); return { left: r.left, top: r.top, cw: r.width, ch: r.height, w: c.width, h: c.height }; });
    const px = box.left + (x + w / 2) * box.cw / box.w, py = box.top + (y + h / 2) * box.ch / box.h;
    console.log(`runner: canvas at ${box.left.toFixed(1)},${box.top.toFixed(1)} size ${box.cw.toFixed(1)}x${box.ch.toFixed(1)} css (${box.w}x${box.h} px); clicking ${click} at page ${px.toFixed(1)},${py.toFixed(1)}`);
    await p.mouse.move(px, py); await new Promise(r => setTimeout(r, 100));
    await p.mouse.down(); await new Promise(r => setTimeout(r, 120)); await p.mouse.up();
  }
  let allDone = true;
  for (let v = 1; v <= visits; v++) {
    done = false; served = 0; servedFiles = [];
    if (v > 1) {   /* a later visit: a new tab in the same profile (the old page closed first, so its teardown is not timed) */
      await p.close(); p = await ctx.newPage(); cdp = await ctx.newCDPSession(p); await cdpSetup(); hook(p);
    }
    const tt0 = await taskTime(), t0 = Date.now();
    await p.goto(`http://localhost:${port}/${page}${args ? '?' + args.split(' ').filter(Boolean).join('&') : ''}`);
    while (!done && Date.now() - t0 < timeout * 1000) await new Promise(r => setTimeout(r, 50));
    if (metrics) console.log(`runner: visit ${v} ${done ? 'done' : 'TIMEOUT'} ms=${Date.now() - t0} task_ms=${((await taskTime()) - tt0).toFixed(0)} bytes=${served} files=${servedFiles.join(',')}`);
    allDone = allDone && done;
    if (!done) break;
  }
  if (shot) await p.screenshot({ path: shot });
  await b.close(); srv.close(); process.exit(allDone ? 0 : 1);
});
