#!/usr/bin/env node
/*
 * Runs a Bronto WebAssembly program in Node, without a browser - for tests
 * and tools (make web-test uses it to run the engine's test suite).
 *
 *   node tools/web/run_node.js program.wasm [options] [-- program arguments]
 *
 *   --file name=path   put a file into the program's file system first (repeatable)
 *   --dir name=path    put a folder there, with everything in it (repeatable)
 *   --mkdir name       make an empty folder there (repeatable)
 *   --out folder       files the program writes are copied into this folder
 *
 * Output goes to stdout / stderr as the program writes it; the exit status
 * is the program's.
 */
'use strict';
const fs = require('fs');
const path = require('path');
const Bronto = require('./bronto.js');

const argv = process.argv.slice(2);
let wasmPath = null, outDir = null;
const files = [], dirs = [], args = [];
function addTree(name, from) {
    for (const entry of fs.readdirSync(from, { withFileTypes: true })) {
        const n = name + '/' + entry.name, f = path.join(from, entry.name);
        if (entry.isDirectory()) addTree(n, f);
        else if (entry.isFile()) files.push({ path: n, data: fs.readFileSync(f) });
    }
    dirs.push(name);
}
for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--') { args.push(...argv.slice(i + 1)); break; }
    if (a === '--file' && i + 1 < argv.length) {
        const spec = argv[++i], eq = spec.indexOf('=');
        if (eq < 0) { console.error('run_node: --file wants name=path'); process.exit(2); }
        files.push({ path: spec.slice(0, eq), data: fs.readFileSync(spec.slice(eq + 1)) });
    } else if (a === '--dir' && i + 1 < argv.length) {
        const spec = argv[++i], eq = spec.indexOf('=');
        if (eq < 0) { console.error('run_node: --dir wants name=path'); process.exit(2); }
        addTree(spec.slice(0, eq), spec.slice(eq + 1));
    } else if (a === '--mkdir' && i + 1 < argv.length) dirs.push(argv[++i]);
    else if (a === '--out' && i + 1 < argv.length) outDir = argv[++i];
    else if (!wasmPath) wasmPath = a;
    else args.push(a);
}
if (!wasmPath) {
    console.error('usage: node tools/web/run_node.js program.wasm [--file name=path] [--out folder] [-- args]');
    process.exit(2);
}

const storage = {
    list: () => [],
    save(name, bytes) {
        if (!outDir) return;
        const to = path.join(outDir, name);
        fs.mkdirSync(path.dirname(to), { recursive: true });
        fs.writeFileSync(to, bytes);
    },
    remove(name) { if (outDir) { try { fs.unlinkSync(path.join(outDir, name)); } catch (e) { /* not there */ } } },
};

(async () => {
    const game = await Bronto.start({
        wasm: fs.readFileSync(wasmPath),
        program: path.basename(wasmPath, '.wasm'),
        files, dirs, args, storage,
        write: (stream, bytes) => (stream === 2 ? process.stderr : process.stdout).write(bytes),
        requestFrame: (f) => setImmediate(f),
    });
    const status = await game.exited;
    process.exitCode = typeof status === 'number' && status >= 0 ? status : 1;
})().catch((e) => { console.error(e); process.exitCode = 1; });
