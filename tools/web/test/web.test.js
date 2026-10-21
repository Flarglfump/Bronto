#!/usr/bin/env node
/*
 * Tests for web builds (make web-test runs them after the test suite):
 *
 *   - the web player draws the same frames as the desktop player, and the
 *     C examples play the same games, picture for picture and sound for sound
 *   - pages made by bronto_web hold a valid program and pack
 *   - in a stand-in browser (a fake canvas and window), a page's game starts,
 *     draws, takes key presses, and ends when stopped
 *   - files a game saves are there the next time it runs
 *
 *   node tools/web/test/web.test.js <build folder>
 */
'use strict';
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync } = require('child_process');
const Bronto = require('../bronto.js');

const build = path.resolve(process.argv[2] || 'build');
const web = path.join(build, 'web');
const repo = path.resolve(__dirname, '..', '..', '..');
let passed = 0, failed = 0;
const pending = [];
function test(name, fn) { pending.push([name, fn]); }

function bmpPixels(file) {                                 /* a 32-bit BMP's pixel bytes */
    const b = fs.readFileSync(file);
    return b.subarray(b.readUInt32LE(10));
}

test('the web player draws what the desktop player draws', async () => {
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'bronto-web-'));
    const native = path.join(tmp, 'native.bmp');
    execFileSync(path.join(build, 'bronto_player'), [path.join(repo, 'examples/platformer'), '--headless', '--frames', '150', '--screenshot', native],
                 { stdio: 'pipe' });
    const saved = new Map();
    const files = [];
    (function add(dir, rel) {
        for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
            if (e.isDirectory()) add(path.join(dir, e.name), rel + '/' + e.name);
            else files.push({ path: rel + '/' + e.name, data: fs.readFileSync(path.join(dir, e.name)) });
        }
    })(path.join(repo, 'examples/platformer'), 'platformer');
    const out = [];
    const game = await Bronto.start({
        wasm: fs.readFileSync(path.join(web, 'bronto_player.wasm')),
        files, args: ['platformer', '--headless', '--frames', '150', '--screenshot', 'shot.bmp'],
        storage: { list: () => [], save: (p, bytes) => saved.set(p, bytes), remove: (p) => saved.delete(p) },
        write: (s, bytes) => out.push(Buffer.from(bytes).toString()),
    });
    assert.strictEqual(await game.exited, 0, out.join(''));
    assert.match(out.join(''), /150 frames of main\.scene, 0 script errors/);
    assert.ok(saved.has('shot.bmp'), 'the screenshot is saved');
    fs.writeFileSync(path.join(tmp, 'web.bmp'), saved.get('shot.bmp'));
    const a = bmpPixels(native), b = bmpPixels(path.join(tmp, 'web.bmp'));
    assert.strictEqual(a.length, b.length);
    let differ = 0;
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) differ++;
    assert.strictEqual(differ, 0, differ + ' bytes differ');
    fs.rmSync(tmp, { recursive: true, force: true });
});

test('the C examples play the very same games as WebAssembly (pictures and sound)', async () => {
    for (const name of ['snake', 'towerdefense', 'bounce']) {
        const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'bronto-web-'));
        const nativeOut = execFileSync(path.join(build, 'examples', name), ['--headless'], { cwd: tmp, stdio: 'pipe' }).toString();
        const saved = new Map();
        let webOut = '';
        const game = await Bronto.start({
            wasm: fs.readFileSync(path.join(web, name + '.wasm')), args: ['--headless'],
            storage: { list: () => [], save: (p, bytes) => saved.set(p, bytes), remove: (p) => saved.delete(p) },
            write: (st, bytes) => { if (st === 1) webOut += Buffer.from(bytes).toString(); },
        });
        await game.exited;
        const summary = (t) => t.split('\n').filter((l) => l && !l.startsWith('[')).join('\n');
        assert.strictEqual(summary(webOut), summary(nativeOut), name + ': the same game');
        const outputs = fs.readdirSync(tmp).filter((f) => /\.(bmp|wav)$/.test(f));
        assert.ok(outputs.length >= 2, name + ' wrote its pictures');
        for (const f of outputs) {
            assert.ok(saved.has(f), name + ': ' + f + ' written');
            assert.ok(Buffer.from(saved.get(f)).equals(fs.readFileSync(path.join(tmp, f))), name + ': ' + f + ' is the same');
        }
        fs.rmSync(tmp, { recursive: true, force: true });
    }
});

function pagePayload(html, id) {
    const m = html.match(new RegExp('<script id="' + id + '" type="application/octet-stream">([^<]*)</script>'));
    assert.ok(m, 'the page has ' + id);
    return Bronto.fromBase64(m[1].trim());
}

test('a project\'s page holds the web player and the project\'s pack', async () => {
    const html = fs.readFileSync(path.join(web, 'platformer', 'index.html'), 'utf8');
    assert.match(html, /<title>Platformer<\/title>/);
    assert.ok(!html.includes('{{'), 'every part filled in');
    const wasm = pagePayload(html, 'bronto-wasm'), pack = pagePayload(html, 'bronto-pack');
    assert.ok(WebAssembly.validate(wasm), 'the program is valid WebAssembly');
    assert.deepStrictEqual(Buffer.from(wasm), fs.readFileSync(path.join(web, 'bronto_player.wasm')));
    assert.strictEqual(Buffer.from(pack.subarray(pack.length - 32, pack.length - 24)).toString(), 'BRONTOPK', 'the pack ends in its trailer');
    assert.ok(Buffer.from(pack).includes('scripts/player.bs'), 'the project\'s scripts are packed');
});

test('the C examples\' pages hold their programs and no pack', async () => {
    for (const name of ['snake', 'towerdefense', 'bounce']) {
        const html = fs.readFileSync(path.join(web, name, 'index.html'), 'utf8');
        assert.ok(WebAssembly.validate(pagePayload(html, 'bronto-wasm')), name);
        assert.strictEqual(pagePayload(html, 'bronto-pack').length, 0, name);
    }
});

/* ---- a stand-in browser: just enough DOM for bronto.js ---- */
function fakeBrowser() {
    const listeners = {};
    const target = (name) => ({
        addEventListener: (type, fn) => { (listeners[name + ':' + type] = listeners[name + ':' + type] || []).push(fn); },
        removeEventListener: (type, fn) => { const l = listeners[name + ':' + type] || []; const i = l.indexOf(fn); if (i >= 0) l.splice(i, 1); },
    });
    const frames = [];
    const ctx2d = () => ({ putImageData: (img) => frames.push(img), drawImage() {}, fillRect() {}, set fillStyle(v) {}, set imageSmoothingEnabled(v) {} });
    const canvas = Object.assign(target('canvas'), {
        width: 0, height: 0, style: {},
        getBoundingClientRect: () => ({ left: 0, top: 0, width: 640, height: 360 }),
        getContext: ctx2d, focus() {},
    });
    const doc = Object.assign(target('document'), {
        title: '', fullscreenElement: null,
        createElement: () => ({ width: 0, height: 0, getContext: ctx2d, toDataURL: () => 'data:,' }),
        querySelector: () => null, head: { appendChild() {} },
    });
    const win = Object.assign(target('window'), { devicePixelRatio: 2 });
    global.ImageData = class { constructor(data, w, h) { this.data = data; this.width = w; this.height = h; } };
    global.document = doc;
    global.window = win;
    const fire = (name, type, event) => (listeners[name + ':' + type] || []).forEach((fn) => fn(Object.assign({ preventDefault() {} }, event)));
    return { canvas, doc, win, frames, fire, listeners };
}

test('in a browser, a game starts, draws, takes keys and ends', async () => {
    const b = fakeBrowser();
    const queue = [];
    const html = fs.readFileSync(path.join(web, 'platformer', 'index.html'), 'utf8');
    try {
        const game = await Bronto.start({
            wasm: pagePayload(html, 'bronto-wasm'), canvas: b.canvas,
            files: [{ path: 'game.brpak', data: pagePayload(html, 'bronto-pack') }],
            requestFrame: (f) => queue.push(f), print: () => {},
        });
        const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
        const step = async (n) => {                        /* (frames come at most 60 a second, by the clock) */
            for (let i = 0; i < n && queue.length; i++) { await sleep(18); queue.shift()(); }
        };
        assert.strictEqual(b.canvas.width, 1280, 'the canvas is sized for the screen\'s pixel ratio');
        assert.strictEqual(b.doc.title, 'Platformer', 'the window title is the page title');
        await step(5);
        assert.ok(b.frames.length >= 1, 'it draws');
        const last = b.frames[b.frames.length - 1];
        assert.deepStrictEqual([last.width, last.height], [320, 180]);
        assert.ok(b.listeners['window:keydown'] && b.listeners['canvas:pointerdown'], 'it listens for input');
        const before = Buffer.from(last.data).toString('base64');
        b.fire('window', 'keydown', { code: 'ArrowRight', key: 'ArrowRight', keyCode: 39 });
        await step(10);
        b.fire('window', 'keyup', { code: 'ArrowRight', key: 'ArrowRight', keyCode: 39 });
        await step(1);
        const after = Buffer.from(b.frames[b.frames.length - 1].data).toString('base64');
        assert.notStrictEqual(after, before, 'holding the right arrow changed the picture');
        b.fire('canvas', 'pointermove', { clientX: 100, clientY: 50 });
        b.fire('canvas', 'wheel', { deltaX: 0, deltaY: 100, deltaMode: 0 });
        game.stop();                                      /* as the window's close button */
        await step(3);
        assert.strictEqual(await game.exited, 0);
        assert.ok(!b.listeners['window:keydown'] || b.listeners['window:keydown'].length === 0, 'it stops listening');
    } finally {
        delete global.document; delete global.window; delete global.ImageData;
    }
});

test('files a game saves are there the next time it runs', async () => {
    const kept = new Map();
    const storage = { list: () => [...kept.entries()], save: (p, bytes) => kept.set(p, bytes), remove: (p) => kept.delete(p) };
    const wasm = fs.readFileSync(path.join(web, 'persist.wasm'));
    for (const want of ['run 1', 'run 2', 'run 3']) {
        let out = '';
        const game = await Bronto.start({ wasm, storage, write: (s, bytes) => { out += Buffer.from(bytes).toString(); } });
        assert.strictEqual(await game.exited, 0);
        assert.strictEqual(out.trim(), want);
    }
    assert.deepStrictEqual([...kept.keys()], ['saves/Persist Test/runs.txt'], 'one file, in the game\'s folder (no half-written copies left over)');
    assert.strictEqual(Buffer.from(kept.get('saves/Persist Test/runs.txt')).toString(), '3\n');
});

test('the console: lines, and exit statuses', async () => {
    const lines = [];
    const game = await Bronto.start({ wasm: fs.readFileSync(path.join(web, 'bronto_player.wasm')), args: ['--nope'],
                                      print: (s, l) => lines.push([s, l]) });
    assert.strictEqual(await game.exited, 1, 'the player says it found no game');
    assert.ok(lines.some(([s, l]) => s === 2 && l.startsWith('bronto_player: ')), 'stderr arrives line by line');
});

let current = null, done = false;
process.on('beforeExit', () => {                           /* a test waiting on something that never comes */
    if (!done) { console.log('  FAIL ' + current + ' (it never finished)'); process.exitCode = 1; done = true; }
});
(async () => {
    for (const [name, fn] of pending) {
        current = name;
        try { await fn(); passed++; console.log('  ok   ' + name); }
        catch (e) { failed++; console.log('  FAIL ' + name + '\n       ' + (e && e.stack ? e.stack.split('\n').slice(0, 3).join('\n       ') : e)); }
    }
    console.log(passed + ' web tests passed' + (failed ? ', ' + failed + ' FAILED' : ''));
    process.exitCode = failed ? 1 : 0;
    done = true;
})();
