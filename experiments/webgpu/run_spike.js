// node run_spike.js BUILD_DIR [ARGS]: serve BUILD_DIR, open spike.html in headless Chromium with WebGPU on SwiftShader,
// print SPIKE lines and write the read-back picture to BUILD_DIR/spike.rgba (raw RGBA, 320 x 180)
const { chromium } = require(process.env.PLAYWRIGHT || '/opt/npm-tools/node_modules/playwright'); const http = require('http'); const fs = require('fs'); const path = require('path');
const [dir, args] = process.argv.slice(2);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const srv = http.createServer((q, r) => { const f = path.join(dir, decodeURIComponent(q.url.split('?')[0])); fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'Content-Type': types[path.extname(f)] || 'application/octet-stream' }); r.end(d); }); });
srv.listen(8768, async () => {
  const b = await chromium.launch({ args: ['--enable-unsafe-webgpu', '--use-webgpu-adapter=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const p = await b.newPage(); let done = false, err = false;
  p.on('console', m => { const t = m.text(); console.log(t); if (t.includes('SPIKE done')) done = true; if (t.includes('SPIKE error')) err = true; });
  await p.goto(`http://localhost:8768/spike.html${args ? '?' + args : ''}`);
  for (let i = 0; i < 120 && !done && !err; i++) await new Promise(r => setTimeout(r, 500));
  if (done) fs.writeFileSync(path.join(dir, 'spike' + (args ? '_' + args : '') + '.rgba'), Buffer.from(await p.evaluate(() => window.spikeRGBA)));
  await b.close(); srv.close(); process.exit(done ? 0 : 1);
});
