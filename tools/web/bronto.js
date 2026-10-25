/*
 * Bronto web runtime - runs a Bronto program compiled to WebAssembly.
 *
 * The other half of src/platform/web (the C library) and
 * src/platform/platform_web.c: it supplies what they import - the clock,
 * the console, maths, the canvas, keyboard / mouse / gamepads, WebAudio,
 * keeping saved files in the browser's storage - and calls their exports.
 *
 *   const game = await Bronto.start({
 *       wasm,                 // the program: an ArrayBuffer or Uint8Array
 *       files: [{ path: 'game.brpak', data: bytes }],   // put in its file system first
 *       dirs: ['saves'],      // folders to make there first
 *       args: ['--flag'],     // its command line (argv[0] is added)
 *       canvas,               // where it draws; leave out to run without a window
 *       storageKey: 'my-game',// where its saved files are kept (null: not kept)
 *   });
 *   const status = await game.exited;   // the exit status, when it ends
 *
 * Works in browsers and in Node (without a canvas: for tests and tools).
 * No dependencies.
 */
(function (global) {
'use strict';

/* KeyboardEvent.code -> br_key (include/bronto/input.h) */
const KEYS = {
    Space: 32, Quote: 39, Comma: 44, Minus: 45, Period: 46, Slash: 47, Semicolon: 59, Equal: 61,
    BracketLeft: 91, Backslash: 92, BracketRight: 93, Backquote: 96, IntlBackslash: 92,
    Escape: 256, Enter: 257, Tab: 258, Backspace: 259, Insert: 260, Delete: 261,
    ArrowRight: 262, ArrowLeft: 263, ArrowDown: 264, ArrowUp: 265, PageUp: 266, PageDown: 267, Home: 268, End: 269,
    CapsLock: 280, ScrollLock: 281, NumLock: 282, PrintScreen: 283, Pause: 284,
    NumpadDecimal: 330, NumpadDivide: 331, NumpadMultiply: 332, NumpadSubtract: 333, NumpadAdd: 334,
    NumpadEnter: 335, NumpadEqual: 336,
    ShiftLeft: 340, ControlLeft: 341, AltLeft: 342, MetaLeft: 343, OSLeft: 343,
    ShiftRight: 344, ControlRight: 345, AltRight: 346, MetaRight: 347, OSRight: 347, ContextMenu: 348,
};
for (let i = 0; i < 26; i++) KEYS['Key' + String.fromCharCode(65 + i)] = 65 + i;
for (let i = 0; i < 10; i++) { KEYS['Digit' + i] = 48 + i; KEYS['Numpad' + i] = 320 + i; }
for (let i = 1; i <= 12; i++) KEYS['F' + i] = 289 + i;

/* Browser shortcuts the page leaves to the browser. */
const BROWSER_KEYS = new Set(['F5', 'F11', 'F12']);
const BROWSER_CHORDS = new Set(['KeyR', 'KeyW', 'KeyT', 'KeyN', 'KeyQ', 'KeyL', 'Tab', 'Digit0', 'Minus', 'Equal']);

/* Gamepad API "standard" button -> bit of br_gamepad_button */
const PAD_BUTTONS = { 0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 8: 6, 9: 7, 16: 8, 10: 9, 11: 10, 12: 11, 15: 12, 13: 13, 14: 14 };

const MATH1 = [Math.sin, Math.cos, Math.tan, Math.asin, Math.acos, Math.atan, Math.sinh, Math.cosh, Math.tanh,
               Math.asinh, Math.acosh, Math.atanh, Math.exp, Math.expm1, Math.log, Math.log2, Math.log10,
               Math.log1p, Math.cbrt];
const MATH2 = [Math.atan2, Math.pow, (x, y) => x % y, Math.hypot];

class BrontoExit extends Error {
    constructor(status) { super('exit(' + status + ')'); this.status = status; }
}

const utf8in = new TextDecoder('utf-8');
const utf8out = new TextEncoder();
const now = typeof performance !== 'undefined' ? () => performance.now() : () => Date.now();

function toBase64(bytes) {
    if (typeof Buffer !== 'undefined') return Buffer.from(bytes).toString('base64');
    let s = '';
    for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return btoa(s);
}
function fromBase64(text) {
    if (typeof Buffer !== 'undefined') return new Uint8Array(Buffer.from(text, 'base64'));
    const s = atob(text), out = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
    return out;
}

/* Saved files: the browser's localStorage, one entry per file. Falls back to
 * memory where there's none (Node, or storage turned off). */
function makeStorage(key) {
    const memory = new Map();
    let ls = null;
    try { if (key && typeof localStorage !== 'undefined') { ls = localStorage; ls.getItem('bronto-test'); } } catch (e) { ls = null; }
    const prefix = 'bronto:' + key + ':';
    return {
        list() {
            if (!ls) return [...memory.entries()];
            const out = [];
            for (let i = 0; i < ls.length; i++) {
                const k = ls.key(i);
                if (k && k.startsWith(prefix)) {
                    try { out.push([k.slice(prefix.length), fromBase64(ls.getItem(k))]); } catch (e) { /* skip a damaged entry */ }
                }
            }
            return out;
        },
        save(path, bytes) {
            if (!key) return;
            if (!ls) { memory.set(path, bytes.slice()); return; }
            try { ls.setItem(prefix + path, toBase64(bytes)); }
            catch (e) { console.warn('Bronto: couldn\'t keep ' + path + ' (storage is full or turned off)'); }
        },
        remove(path) {
            if (!ls) { memory.delete(path); return; }
            try { ls.removeItem(prefix + path); } catch (e) { /* nothing to do */ }
        },
    };
}

async function start(opts) {
    opts = opts || {};
    const canvas = opts.canvas || null;
    const doc = typeof document !== 'undefined' ? document : null;
    const win = typeof window !== 'undefined' ? window : null;
    const storage = opts.storage || makeStorage(opts.storageKey === undefined ? null : opts.storageKey);
    let memory = null, exports = null, ctx = null, off = null, offCtx = null;
    let finished = false, loopWanted = false, ticking = false, cleanup = [];
    let resolveExit;
    const exited = new Promise(r => { resolveExit = r; });
    const pending = ['', ''];                     /* partial console lines (stdout, stderr) */
    /* (the engine's [INFO] and [DEBUG] lines go to stderr, but aren't errors) */
    const print = opts.print || ((stream, line) => (stream === 2 && !/^\[(INFO|DEBUG)/.test(line) ? console.error : console.log)(line));
    const u8 = () => new Uint8Array(memory.buffer);
    const cstr = (p) => { if (!p) return ''; const m = u8(); let e = p; while (m[e]) e++; return utf8in.decode(m.subarray(p, e)); };
    const requestFrame = opts.requestFrame || (win && win.requestAnimationFrame ? (f) => win.requestAnimationFrame(f)
                                                                              : (f) => setTimeout(f, 1000 / 60));
    const decoders = [new TextDecoder(), new TextDecoder()];

    function flushConsole() {
        for (let s = 0; s < 2; s++) if (pending[s]) { print(s + 1, pending[s]); pending[s] = ''; }
    }
    function finish(status, error) {
        if (finished) return;
        finished = true;
        flushConsole();
        for (const f of cleanup) { try { f(); } catch (e) { /* ignore */ } }
        if (opts.onExit) opts.onExit(status, error);
        resolveExit(status);
    }
    /* Runs an export; an exit() or a crash inside it ends the program. */
    function call(fn, ...args) {
        if (finished) return 0;
        try { return fn(...args); }
        catch (e) {
            if (e instanceof BrontoExit) finish(e.status);
            else {
                flushConsole();
                print(2, 'Bronto: the program stopped: ' + (e && e.message ? e.message : e));
                finish(-1, e);
            }
            return 0;
        }
    }

    /* ---- the canvas ---- */
    let ratio = 1, sizeWanted = [0, 0];
    function fitCanvas() {
        if (!canvas) return false;
        ratio = (win && win.devicePixelRatio) || 1;
        const r = canvas.getBoundingClientRect();
        const w = Math.max(1, Math.round((r.width || sizeWanted[0] || 640) * ratio));
        const h = Math.max(1, Math.round((r.height || sizeWanted[1] || 480) * ratio));
        if (canvas.width === w && canvas.height === h) return false;
        canvas.width = w;
        canvas.height = h;
        return true;
    }
    function canvasPoint(e) {
        const r = canvas.getBoundingClientRect();
        return [(e.clientX - r.left) * (canvas.width / (r.width || 1)), (e.clientY - r.top) * (canvas.height / (r.height || 1))];
    }

    /* ---- audio ---- */
    let ac = null, audioRate = 44100, audioLatency = 0.08, audioNext = 0;
    function resumeAudio() { if (ac && ac.state === 'suspended') ac.resume().catch(() => {}); }

    /* ---- gamepads ---- */
    const padSeen = [false, false, false, false];

    const imports = { bronto: {
        /* C library */
        write(stream, p, n) {
            const bytes = u8().slice(p, p + n);
            if (opts.write) { opts.write(stream, bytes); return; }
            const s = stream === 2 ? 1 : 0;
            const text = pending[s] + decoders[s].decode(bytes, { stream: true });
            const lines = text.split('\n');
            pending[s] = lines.pop();
            for (const line of lines) print(s + 1, line);
        },
        exit(status) { throw new BrontoExit(status); },
        now: () => now(),
        date_now: () => Date.now(),
        tz_offset: (ms) => -new Date(ms).getTimezoneOffset(),
        parse_double: (p, n) => Number(utf8in.decode(u8().subarray(p, p + n))),
        math1: (op, x) => MATH1[op](x),
        math2: (op, x, y) => MATH2[op](x, y),
        file_saved(p, data, n) { storage.save(cstr(p).replace(/^\//, ''), u8().slice(data, data + n)); },
        file_removed(p) { storage.remove(cstr(p).replace(/^\//, '')); },

        /* window */
        canvas_width: () => canvas ? canvas.width : (opts.width || 960),
        canvas_height: () => canvas ? canvas.height : (opts.height || 540),
        pixel_ratio: () => canvas ? ratio : 1,
        present(p, w, h, vx, vy, vw, vh) {
            if (!ctx) return;
            if (!off || off.width !== w || off.height !== h) {
                off = doc.createElement('canvas');
                off.width = w;
                off.height = h;
                offCtx = off.getContext('2d');
            }
            offCtx.putImageData(new ImageData(new Uint8ClampedArray(memory.buffer, p, w * h * 4), w, h), 0, 0);
            ctx.fillStyle = '#000';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            ctx.imageSmoothingEnabled = false;
            ctx.drawImage(off, vx, vy, vw, vh);
            if (opts.onFrame) opts.onFrame();
        },
        /* The on-screen touch controls, over the frame just drawn: br_overlay_item[n] (platform.h), 40 bytes each. */
        overlay(p, n) {
            if (!ctx) return;
            const dv = new DataView(memory.buffer), rgba = (v) => `rgba(${v >>> 24},${(v >>> 16) & 255},${(v >>> 8) & 255},${(v & 255) / 255})`;
            ctx.save();
            for (let i = 0; i < n; ++i) {
                const a = p + 40 * i, kind = dv.getInt32(a, true), x = dv.getFloat32(a + 4, true), y = dv.getFloat32(a + 8, true);
                const w = dv.getFloat32(a + 12, true), h = dv.getFloat32(a + 16, true);
                const fill = dv.getUint32(a + 20, true), line = dv.getUint32(a + 24, true), lw = dv.getFloat32(a + 28, true);
                if (kind === 3) {                                    /* text */
                    let end = a + 32;
                    while (end < a + 40 && dv.getUint8(end)) ++end;
                    const text = utf8in.decode(u8().subarray(a + 32, end));   /* (UTF-8: the d-pad's arrows) */
                    ctx.font = `bold ${Math.max(1, Math.round(h))}px system-ui, -apple-system, "Segoe UI", Roboto, sans-serif`;
                    ctx.textAlign = 'center';
                    ctx.textBaseline = 'middle';
                    ctx.fillStyle = rgba(fill);
                    ctx.fillText(text, x, y + h * 0.04);
                    continue;
                }
                ctx.beginPath();
                if (kind === 1) ctx.arc(x, y, w / 2, 0, Math.PI * 2);
                else {                                               /* a pill: a rectangle with round ends */
                    const r = Math.min(w, h) / 2, l = x - w / 2, t = y - h / 2;
                    ctx.moveTo(l + r, t);
                    ctx.arcTo(l + w, t, l + w, t + h, r);
                    ctx.arcTo(l + w, t + h, l, t + h, r);
                    ctx.arcTo(l, t + h, l, t, r);
                    ctx.arcTo(l, t, l + w, t, r);
                    ctx.closePath();
                }
                if (fill & 255) { ctx.fillStyle = rgba(fill); ctx.fill(); }
                if (line & 255) { ctx.strokeStyle = rgba(line); ctx.lineWidth = lw; ctx.stroke(); }
            }
            ctx.restore();
        },
        set_title(p) { if (doc && opts.setTitle !== false) doc.title = cstr(p); },
        set_size(w, h) { sizeWanted = [w, h]; if (opts.onSize) opts.onSize(w, h); },
        set_fullscreen(on) {
            if (!doc || !canvas) return;
            const target = opts.fullscreenElement || canvas;
            try {
                if (on && !doc.fullscreenElement && target.requestFullscreen) target.requestFullscreen().catch(() => {});
                else if (!on && doc.fullscreenElement && doc.exitFullscreen) doc.exitFullscreen().catch(() => {});
            } catch (e) { /* not allowed now */ }
        },
        set_cursor(visible) { if (canvas) canvas.style.cursor = visible ? '' : 'none'; },
        set_icon(p, w, h) {
            if (!doc || opts.setIcon === false) return;
            const c = doc.createElement('canvas');
            c.width = w; c.height = h;
            c.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(memory.buffer.slice(p, p + w * h * 4)), w, h), 0, 0);
            let link = doc.querySelector('link[rel="icon"]');
            if (!link) { link = doc.createElement('link'); link.rel = 'icon'; doc.head.appendChild(link); }
            link.href = c.toDataURL('image/png');
        },
        open_url(p) {
            const url = cstr(p);
            if (!win || !/^(https?:|mailto:)/i.test(url)) return 0;
            win.open(url, '_blank', 'noopener');
            return 1;
        },
        clipboard_set(p) {
            const text = cstr(p);
            try { if (typeof navigator !== 'undefined' && navigator.clipboard) navigator.clipboard.writeText(text).catch(() => {}); }
            catch (e) { /* not allowed */ }
        },

        /* audio */
        audio_open(rate, latencyMs) {
            if (opts.audio === false || !canvas) return 0;
            const AC = win && (win.AudioContext || win.webkitAudioContext);
            if (!AC) return 0;
            try { ac = opts.audioContext || new AC(); } catch (e) { return 0; }
            audioRate = rate;
            audioLatency = Math.max(latencyMs, 60) / 1000;
            audioNext = 0;
            resumeAudio();
            return 1;
        },
        audio_close() { if (ac && !opts.audioContext) ac.close().catch(() => {}); ac = null; },
        audio_wanted() {
            if (!ac || ac.state !== 'running') return 0;
            const t = ac.currentTime;
            if (audioNext < t) audioNext = t + 0.02;          /* ran dry: start again a moment ahead */
            const want = audioLatency - (audioNext - t);
            return want > 0 ? Math.ceil(want * audioRate) : 0;
        },
        audio_submit(p, frames) {
            if (!ac) return;
            const pcm = new Int16Array(memory.buffer, p, frames * 2);
            const buf = ac.createBuffer(2, frames, audioRate);
            const l = buf.getChannelData(0), r = buf.getChannelData(1);
            for (let i = 0; i < frames; i++) { l[i] = pcm[2 * i] / 32768; r[i] = pcm[2 * i + 1] / 32768; }
            const src = ac.createBufferSource();
            src.buffer = buf;
            src.connect(ac.destination);
            if (audioNext < ac.currentTime) audioNext = ac.currentTime;
            src.start(audioNext);
            audioNext += frames / audioRate;
        },

        /* controllers */
        poll_gamepads() {
            if (typeof navigator === 'undefined' || !navigator.getGamepads) return;
            let pads;
            try { pads = navigator.getGamepads(); } catch (e) { return; }
            for (let i = 0; i < 4; i++) {
                const g = pads[i];
                if (!g || !g.connected) {
                    if (padSeen[i]) { padSeen[i] = false; exports.bronto_web_gamepad(i, 0, 0, 0, 0, 0, 0, 0, 0); }
                    continue;
                }
                if (!padSeen[i]) {                     /* its name, for the connected event */
                    const name = utf8out.encode(g.id.replace(/\s*\(.*?Vendor.*\)\s*/i, '').slice(0, 60) || 'Gamepad');
                    const at = exports.bronto_web_gamepad_name(), m = u8();
                    m.set(name.subarray(0, 63), at);
                    m[at + Math.min(name.length, 63)] = 0;
                    padSeen[i] = true;
                }
                let bits = 0;
                for (const k in PAD_BUTTONS) {
                    const b = g.buttons[k];
                    if (b && (b.pressed || b.value > 0.5)) bits |= 1 << PAD_BUTTONS[k];
                }
                const ax = (n) => (g.axes[n] || 0);
                const trig = (n) => (g.buttons[n] ? g.buttons[n].value : 0);
                exports.bronto_web_gamepad(i, 1, bits, ax(0), ax(1), ax(2), ax(3), trig(6), trig(7));
            }
        },
        rumble(pad, low, high) {
            if (typeof navigator === 'undefined' || !navigator.getGamepads) return 0;
            const g = navigator.getGamepads()[pad], act = g && g.vibrationActuator;
            if (!act) return 0;
            try {
                if (low <= 0 && high <= 0) { if (act.reset) act.reset(); }
                else act.playEffect('dual-rumble', { duration: 5000, strongMagnitude: low, weakMagnitude: high }).catch(() => {});
            } catch (e) { return 0; }
            return 1;
        },

        /* the loop */
        run_loop() { loopWanted = true; },
    } };

    /* ---- load and start ---- */
    const bytes = opts.wasm instanceof ArrayBuffer || ArrayBuffer.isView(opts.wasm) ? opts.wasm : await opts.wasm;
    const result = await WebAssembly.instantiate(bytes, imports);
    exports = result.instance.exports;
    memory = exports.memory;

    function putFile(path, data, persist) {
        const at = exports.bronto_web_file(allocString(path), data.length, persist ? 1 : 0);
        if (!at) throw new Error('Bronto: not enough memory for ' + path);
        u8().set(data, at);
    }
    function allocString(s) {
        const b = utf8out.encode(s), p = exports.bronto_alloc(b.length + 1), m = u8();
        m.set(b, p);
        m[p + b.length] = 0;
        return p;
    }
    for (const d of opts.dirs || []) exports.bronto_web_mkdir(allocString(d));
    for (const [path, data] of storage.list()) putFile(path, data, true);   /* what it saved last time */
    for (const f of opts.files || []) putFile(f.path, f.data instanceof Uint8Array ? f.data : new Uint8Array(f.data), !!f.persist);

    if (canvas) {
        ctx = canvas.getContext('2d', { alpha: false });
        fitCanvas();
    }
    const argv = [opts.program || 'bronto'].concat(opts.args || []);
    const argvPtr = exports.bronto_alloc(4 * (argv.length + 1));
    argv.forEach((a, i) => new DataView(memory.buffer).setUint32(argvPtr + 4 * i, allocString(a), true));
    new DataView(memory.buffer).setUint32(argvPtr + 4 * argv.length, 0, true);

    const running = call(exports.bronto_start, argv.length, argvPtr);
    if (!finished && (running !== 1 || !loopWanted)) call(exports.bronto_exit);   /* main returned */
    if (finished) return { exports, memory, exited, stop: () => {} };

    /* The program handed its frames to us. */
    if (canvas && win) listen();
    function tick() {
        if (finished) return;
        if (!call(exports.bronto_web_frame)) { finish(0); return; }
        requestFrame(tick);
    }
    requestFrame(tick);

    function listen() {
        const on = (target, type, fn, options) => { target.addEventListener(type, fn, options); cleanup.push(() => target.removeEventListener(type, fn, options)); };
        const held = new Map();
        const mods = (e) => (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0) | (e.metaKey ? 8 : 0) |
                            (e.getModifierState && e.getModifierState('CapsLock') ? 16 : 0);
        on(win, 'keydown', (e) => {
            resumeAudio();
            const key = KEYS[e.code] || 0;
            held.set(e.code, key);
            call(exports.bronto_web_key, key, e.keyCode || 0, mods(e), 1, e.repeat ? 1 : 0);
            const chars = [...(e.key || '')];
            if (chars.length === 1 && (!(e.ctrlKey || e.metaKey) || (e.ctrlKey && e.altKey))) call(exports.bronto_web_text, chars[0].codePointAt(0));
            const leave = BROWSER_KEYS.has(e.code) || ((e.ctrlKey || e.metaKey) && (BROWSER_CHORDS.has(e.code) || e.code === 'KeyV'));
            if (!leave) e.preventDefault();
        });
        on(win, 'keyup', (e) => {
            held.delete(e.code);
            call(exports.bronto_web_key, KEYS[e.code] || 0, e.keyCode || 0, mods(e), 0, 0);
            e.preventDefault();
        });
        on(win, 'blur', () => {                        /* keys let go while away would stay "down" */
            for (const key of held.values()) call(exports.bronto_web_key, key, 0, 0, 0, 0);
            held.clear();
            call(exports.bronto_web_focus, 0);
        });
        on(win, 'focus', () => call(exports.bronto_web_focus, 1));
        const BUTTONS = [0, 2, 1, 3, 4];               /* DOM button -> br_mouse_button */
        /* Fingers (and pens) are touches: the engine's on-screen controls take
         * them, or turn them into the mouse. The page mustn't scroll or zoom. */
        const fingers = new Set();
        const isTouch = (e) => e.pointerType === 'touch' || e.pointerType === 'pen';
        canvas.style.touchAction = 'none';
        canvas.style.webkitUserSelect = canvas.style.userSelect = 'none';
        canvas.style.webkitTouchCallout = 'none';
        const touchEnd = (e) => {
            if (!fingers.delete(e.pointerId)) return;
            const [x, y] = canvasPoint(e);
            call(exports.bronto_web_touch, 2, e.pointerId, x, y);
            e.preventDefault();
        };
        on(canvas, 'pointermove', (e) => {
            const [x, y] = canvasPoint(e);
            if (isTouch(e)) { if (fingers.has(e.pointerId)) call(exports.bronto_web_touch, 1, e.pointerId, x, y); e.preventDefault(); return; }
            call(exports.bronto_web_mouse_move, x, y);
        });
        on(canvas, 'pointerdown', (e) => {
            resumeAudio();
            canvas.focus && canvas.focus();
            if (canvas.setPointerCapture) { try { canvas.setPointerCapture(e.pointerId); } catch (err) { /* fine */ } }
            const [x, y] = canvasPoint(e);
            if (isTouch(e)) {
                fingers.add(e.pointerId);
                call(exports.bronto_web_touch, 0, e.pointerId, x, y);
            } else {
                call(exports.bronto_web_mouse_move, x, y);
                if (BUTTONS[e.button] !== undefined) call(exports.bronto_web_mouse_button, BUTTONS[e.button], 1, x, y);
            }
            e.preventDefault();
        });
        on(canvas, 'pointerup', (e) => {
            if (isTouch(e)) { touchEnd(e); return; }
            const [x, y] = canvasPoint(e);
            if (BUTTONS[e.button] !== undefined) call(exports.bronto_web_mouse_button, BUTTONS[e.button], 0, x, y);
            e.preventDefault();
        });
        on(canvas, 'pointercancel', (e) => { if (isTouch(e)) touchEnd(e); });
        on(canvas, 'touchstart', (e) => e.preventDefault(), { passive: false });    /* (no double-tap zoom, no long-press menu) */
        on(canvas, 'touchmove', (e) => e.preventDefault(), { passive: false });
        on(canvas, 'pointerenter', (e) => { if (!isTouch(e)) call(exports.bronto_web_mouse_inside, 1); });
        on(canvas, 'pointerleave', (e) => { if (!isTouch(e)) call(exports.bronto_web_mouse_inside, 0); });
        on(canvas, 'contextmenu', (e) => e.preventDefault());
        on(canvas, 'wheel', (e) => {
            const scale = e.deltaMode === 0 ? 1 / 100 : e.deltaMode === 1 ? 1 / 3 : 1;
            call(exports.bronto_web_wheel, e.deltaX * scale, -e.deltaY * scale);
            e.preventDefault();
        }, { passive: false });
        on(doc, 'paste', (e) => {
            const text = e.clipboardData ? e.clipboardData.getData('text/plain') : '';
            if (text) call(exports.bronto_web_paste, allocString(text.replace(/\r\n?/g, '\n')));
        });
        on(doc, 'fullscreenchange', () => call(exports.bronto_web_fullscreen, doc.fullscreenElement ? 1 : 0));
        let lastRatio = win.devicePixelRatio || 1;
        const resized = () => {
            const changed = fitCanvas();
            if ((win.devicePixelRatio || 1) !== lastRatio) { lastRatio = win.devicePixelRatio || 1; call(exports.bronto_web_scale_changed); }
            if (changed) call(exports.bronto_web_resized);
        };
        if (typeof ResizeObserver !== 'undefined') {
            const ro = new ResizeObserver(resized);
            ro.observe(canvas);
            cleanup.push(() => ro.disconnect());
        }
        on(win, 'resize', resized);
    }

    return {
        exports, memory, exited,
        /* Ends the game as its window's close button would. */
        stop() { call(exports.bronto_web_close); },
    };
}

const Bronto = { start, fromBase64, toBase64, KEYS };
if (typeof module === 'object' && module.exports) module.exports = Bronto;
else global.Bronto = Bronto;
})(typeof globalThis !== 'undefined' ? globalThis : this);
