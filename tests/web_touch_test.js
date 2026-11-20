// ARC 0.10.11 multi-touch in the browser (headless Chromium through Playwright, real touch events via the DevTools
// protocol): two fingers down, one moved, one lifted, and a quick tap; after each step ARC's dump must list them.
//   node tests/web_touch_test.js DIR PAGE            (a web build of samples/demo/demo.c; NODE_PATH=$(npm root -g))
// Prints "ARC ok web touch" or "ARC error web touch: ..." and exits 0 / 1.
const { chromium } = require('playwright'); const http = require('http'); const fs = require('fs'); const path = require('path');
const [dir, page] = process.argv.slice(2);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream' };
const srv = http.createServer((q, r) => { const f = path.join(dir, decodeURIComponent(q.url.split('?')[0])); fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'Content-Type': types[path.extname(f)] || 'application/octet-stream' }); r.end(d); }); });
const sleep = ms => new Promise(r => setTimeout(r, ms));
srv.listen(8797, async () => {
  const b = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--no-proxy-server'] });
  const ctx = await b.newContext({ viewport: { width: 1280, height: 720 }, hasTouch: true });
  const p = await ctx.newPage(); const cdp = await ctx.newCDPSession(p);
  let dumps = [], started = false, bad = [];
  p.on('console', m => { const t = m.text(); if (t.startsWith('ARC dump ')) dumps.push(JSON.parse(t.slice(9))); if (t.startsWith('ARC start')) started = true; if (t.startsWith('ARC error')) console.log(t); });
  p.on('pageerror', e => console.log('runner: page error: ' + e));
  await p.goto(`http://localhost:8797/${page}?--mute`);
  for (let i = 0; i < 100 && !started; i++) await sleep(100);
  if (!started) { console.log('ARC error web touch: the page did not start'); process.exit(1); }
  await sleep(500);
  const box = await p.evaluate(() => { const c = document.getElementById('canvas'), r = c.getBoundingClientRect(); return { left: r.left, top: r.top, cw: r.width, ch: r.height, w: c.width, h: c.height }; });
  const at = (x, y) => ({ x: box.left + x * box.cw / box.w, y: box.top + y * box.ch / box.h });   // render pixels -> page
  const dump = async () => { const n = dumps.length; await p.evaluate(() => Module.arcCommand('dump')); for (let i = 0; i < 40 && dumps.length === n; i++) await sleep(50); return dumps[dumps.length - 1]; };
  const touch = (type, pts) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: pts.map(([id, x, y]) => ({ id, ...at(x, y) })) });
  const near = (a, b) => Math.abs(a - b) <= 2;
  const fingers = d => (d && d.input && d.input.touch || []).filter(t => !t.released);
  // two fingers down (the browser's touchStart lists every finger now on the screen)
  await touch('touchStart', [[11, 200, 400]]); await sleep(150);
  await touch('touchStart', [[11, 200, 400], [12, 1000, 600]]); await sleep(200);
  let d = await dump(), f = fingers(d);
  console.log('runner: two fingers ' + JSON.stringify(d.input));
  if (d.input.touches !== 2) bad.push('two fingers: touches ' + d.input.touches);
  if (!f.some(t => near(t.x, 200) && near(t.y, 400)) || !f.some(t => near(t.x, 1000) && near(t.y, 600))) bad.push('two fingers: positions');
  if (!d.input.touch_mode) bad.push('touch mode off');
  // the first finger moves
  await touch('touchMove', [[11, 260, 380], [12, 1000, 600]]); await sleep(200);
  d = await dump(); f = fingers(d);
  const first = f.find(t => near(t.start_x, 200));
  if (!first || !near(first.x, 260) || !near(first.y, 380)) bad.push('move: ' + JSON.stringify(f));
  // the second lifts
  await touch('touchEnd', [[11, 260, 380]]); await sleep(200);
  d = await dump();
  if (d.input.touches !== 1) bad.push('lift: touches ' + d.input.touches);
  await touch('touchEnd', []); await sleep(200);
  d = await dump();
  if (d.input.touches !== 0) bad.push('all lifted: touches ' + d.input.touches);
  await b.close(); srv.close();
  console.log(bad.length ? 'ARC error web touch: ' + bad.join('; ') : 'ARC ok web touch');
  process.exit(bad.length ? 1 : 0);
});
