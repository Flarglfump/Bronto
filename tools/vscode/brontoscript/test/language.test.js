// Tests for the extension, run with plain node:  node tools/vscode/brontoscript/test/language.test.js
// (no VS Code needed: extension.js is loaded against a small stand-in for its API).
'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const Module = require('module');

const root = path.join(__dirname, '..');
const repo = path.join(root, '..', '..', '..');
const lang = require('../lib/language');
let passed = 0;
function test(name, fn) {
    try { fn(); passed++; console.log('  ok   ' + name); }
    catch (e) { console.log('  FAIL ' + name + '\n       ' + (e.stack || e).toString().split('\n').slice(0, 3).join('\n       ')); process.exitCode = 1; }
}

const player = fs.readFileSync(path.join(repo, 'examples/platformer/scripts/player.bs'), 'utf8');
const lines = player.split('\n');
const at = (needle, offset) => { const l = lines.findIndex(x => x.includes(needle)); return { line: l, character: lines[l].indexOf(needle) + (offset || 0) }; };

test('parseDocument finds vars and funcs with their comments', () => {
    const d = lang.parseDocument(player);
    assert.deepStrictEqual(d.vars.map(v => v.name), ['speed', 'jump', 'spawn_point', 'grounded']);
    assert.deepStrictEqual(d.funcs.map(f => f.name), ['start', 'update', 'on_ground']);
    assert.match(d.funcs[2].doc, /short ray down/);
    assert.deepStrictEqual(d.funcs[1].params, ['dt']);
});

test('completion inside a function: builtins, keywords, vars, locals', () => {
    const p = at('self.velocity.x = dir * speed');
    const items = lang.completions(player, p.line, p.character, []);
    const labels = new Set(items.map(i => i.label));
    for (const n of ['raycast', 'key_down', 'vec2', 'if', 'self', 'speed', 'on_ground', 'dir', 'dt']) assert.ok(labels.has(n), n);
    const rc = items.find(i => i.label === 'raycast');
    assert.strictEqual(rc.snippet, 'raycast(${1:from}, ${2:to})');
});

test('completion after a dot: properties, components, other scripts', () => {
    const text = 'func update(dt) {\n    other.\n}\n';
    const items = lang.completions(text, 1, 10, [{ name: 'ground.bs', vars: [{ name: 'health', line: 0 }], funcs: [{ name: 'hit', params: ['amount'], line: 1 }] }]);
    const labels = new Set(items.map(i => i.label));
    for (const n of ['position', 'global_position', 'parent', 'velocity', 'Shape', 'RigidBody', 'x', 'health', 'hit']) assert.ok(labels.has(n), n);
    assert.ok(!labels.has('raycast'));
    const fields = lang.completions('func f() {\n    self.Collider.\n}\n', 1, 18, []).map(i => i.label);
    assert.deepStrictEqual(fields, ['shape', 'size', 'radius', 'offset', 'friction', 'bounce', 'trigger', 'layer', 'mask']);
});

test('completion at the top level offers the missing callbacks', () => {
    const items = lang.completions(player, lines.length - 1, 0, []);
    const labels = items.map(i => i.label);
    assert.ok(labels.includes('func on_collision') && labels.includes('func on_trigger'));
    assert.ok(!labels.includes('func update'));                 /* already there */
    assert.ok(!labels.includes('raycast'));
    assert.deepStrictEqual(lang.completions('var s = "a.', 0, 11, []), []);   /* nothing inside strings */
});

test('hover: builtins, file functions, vars, properties, components', () => {
    let p = at('raycast(');
    assert.match(lang.hover(player, p.line, p.character + 2), /raycast\(from: vec2, to: vec2, \[ignore: entity \| nil\]\) -> any/);
    p = at('grounded = on_ground()', 12);
    assert.match(lang.hover(player, p.line, p.character + 2), /func on_ground\(\)[\s\S]*short ray/);
    p = at('var speed');
    assert.match(lang.hover(player, p.line, p.character + 5), /var speed: number = 90/);
    assert.match(lang.hover('func f() {\n    self.global_position\n}', 1, 12), /after its parents/);
    assert.match(lang.hover('func f() {\n    self.Shape.color\n}', 1, 11), /Shape\*\* component/);
    p = at('func start');
    assert.match(lang.hover(player, p.line, p.character + 6), /Called by the engine/);
});

test('signature help knows the argument being typed', () => {
    let s = lang.signatureAt('func f() {\n    clamp(1, (2 + 3), \n}', 1, 22);
    assert.strictEqual(s.name, 'clamp');
    assert.strictEqual(s.active, 2);
    s = lang.signatureAt('func g(a, b) { }\nfunc f() {\n    g("x,(", \n}', 2, 13);
    assert.strictEqual(s.name, 'g');
    assert.strictEqual(s.active, 1);
    assert.strictEqual(lang.signatureAt('func f() {\n    x = 1\n}', 1, 9), null);
});

test('go to definition: functions, vars, locals', () => {
    let p = at('grounded = on_ground()', 12);
    assert.strictEqual(lang.definition(player, p.line, p.character + 2).line, lines.findIndex(l => l.startsWith('func on_ground')));
    p = at('dir * speed', 7);
    assert.strictEqual(lang.definition(player, p.line, p.character + 1).line, lines.findIndex(l => l.startsWith('var speed')));
    p = at('dir * speed');
    assert.strictEqual(lang.definition(player, p.line, p.character + 1).line, lines.findIndex(l => l.includes('var dir := 0')));
});

test('quick check: no false alarms on real scripts, catches slips', () => {
    const scripts = [];
    (function walk(dir) {
        for (const f of fs.readdirSync(dir)) {
            const full = path.join(dir, f);
            if (fs.statSync(full).isDirectory()) { if (!/^(build|node_modules|\.git)/.test(f)) walk(full); }
            else if (f.endsWith('.bs')) scripts.push(full);
        }
    })(path.join(repo, 'examples'));
    assert.ok(scripts.length >= 3);
    for (const f of scripts) assert.deepStrictEqual(lang.quickCheck(fs.readFileSync(f, 'utf8')), [], f);
    const bad = lang.quickCheck('func f() {\n    jmup()\n    if x {\n}\nprint(1)\n');
    assert.ok(bad.some(p => p.line === 1 && /unknown function 'jmup'/.test(p.message)));
    assert.ok(bad.some(p => /never closed/.test(p.message)));
    assert.ok(lang.quickCheck('func f() {\n}\nprint(1)\n').some(p => p.line === 2 && /top of a script/.test(p.message)));
    assert.deepStrictEqual(lang.quickCheck('func f() {\n    print("jmup()") # nope()\n    self.hit(1)\n}\n'), []);
    assert.deepStrictEqual(lang.quickCheck('var names = ["A",\n             "B"]\nvar n = max(1,\n    2)\n'), []);   // lists and calls over lines
    assert.deepStrictEqual(lang.quickCheck('func f() {\n    if button_pressed("a") or axis("lt") > 0.5 { rumble(1, 0.2) }\n}\n'), []);
});

test('types: declarations, completion after : and ->, hover, signatures', () => {
    const src = '@strict\n# hit points\nvar hp: int = 3\nvar spot := vec2(1, 2)\nvar bag: list[number]\n' +
        'func hit(amount: int, from: entity) -> bool {\n    var left: int = hp - amount\n    for i: int in 3 { }\n    return left <= 0\n}\n';
    const d = lang.parseDocument(src);
    assert.deepStrictEqual(d.vars.map(v => [v.name, v.type, v.inferred]), [['hp', 'int', false], ['spot', '', true], ['bag', 'list[number]', false]]);
    assert.deepStrictEqual(d.funcs[0].params, ['amount', 'from']);
    assert.strictEqual(d.funcs[0].returns, 'bool');
    assert.deepStrictEqual(lang.quickCheck(src), []);                       /* @strict and types are fine */
    let labels = lang.completions('var x: ', 0, 7, []).map(i => i.label);
    assert.ok(labels.includes('int') && labels.includes('vec2') && !labels.includes('void') && !labels.includes('raycast'));
    labels = lang.completions('func f() -> ', 0, 12, []).map(i => i.label);
    assert.ok(labels.includes('void'));
    assert.ok(lang.completions('var bag: list[', 0, 14, []).some(i => i.label === 'number'));
    assert.ok(!lang.completions('func f() {\n    var x := ', 1, 13, []).some(i => i.label === 'int'));   /* := takes a value */
    assert.ok(lang.completions('@', 0, 1, []).some(i => i.label === '@strict'));
    assert.match(lang.hover(src, 2, 5), /var hp: int = 3/);
    assert.match(lang.hover(src, 5, 7), /func hit\(amount: int, from: entity\) -> bool/);
    assert.match(lang.hover(src, 2, 9), /\*\*int\*\* \(a type\)/);
    assert.match(lang.hover(src, 6, 10), /left: int/);
    const s = lang.signatureAt(src + 'func g() {\n    hit(1, \n}\n', 11, 11);
    assert.strictEqual(s.signature, 'hit(amount: int, from: entity) -> bool');
    assert.deepStrictEqual(s.params, ['amount: int', 'from: entity']);
    assert.strictEqual(s.active, 1);
    const cb = lang.completions('', 0, 0, []).find(i => i.label === 'func update');
    assert.strictEqual(cb.snippet, 'func update(dt: number) -> void {\n\t$0\n}');
});

test('checker output is read into problems', () => {
    const out = lang.parseCheckerOutput('/tmp/x/player.bs:12: unknown name \'speeed\' (declare it with var)\nC:\\a\\b.bs:3: x\n');
    assert.deepStrictEqual(out[0], { file: '/tmp/x/player.bs', line: 11, message: "unknown name 'speeed' (declare it with var)" });
    assert.strictEqual(out[1].line, 2);
});

test('the grammar is valid and its patterns compile', () => {
    const g = JSON.parse(fs.readFileSync(path.join(root, 'syntaxes/brontoscript.tmLanguage.json'), 'utf8'));
    let count = 0;
    (function visit(o) {
        if (Array.isArray(o)) return o.forEach(visit);
        if (o && typeof o === 'object') {
            for (const k of ['match', 'begin', 'end']) if (typeof o[k] === 'string') { new RegExp(o[k]); count++; }
            Object.values(o).forEach(visit);
        }
    })(g);
    assert.ok(count > 15);
    const builtin = g.repository.call.patterns[0].match;
    assert.ok(new RegExp(builtin).test('raycast('));
    JSON.parse(fs.readFileSync(path.join(root, 'snippets/brontoscript.json'), 'utf8'));
    JSON.parse(fs.readFileSync(path.join(root, 'language-configuration.json'), 'utf8'));
});

test('extension.js activates and its providers answer', () => {
    const registered = {};
    class Pos { constructor(l, c) { this.line = l; this.character = c; } }
    const fake = {
        CompletionItemKind: new Proxy({}, { get: (_, k) => String(k) }),
        SymbolKind: new Proxy({}, { get: (_, k) => String(k) }),
        DiagnosticSeverity: { Error: 0 },
        CompletionItem: class { constructor(l, k) { this.label = l; this.kind = k; } },
        SnippetString: class { constructor(v) { this.value = v; } },
        MarkdownString: class { constructor(v) { this.value = v; } },
        Hover: class { constructor(c) { this.contents = c; } },
        SignatureHelp: class {}, SignatureInformation: class { constructor(l) { this.label = l; } },
        ParameterInformation: class { constructor(l) { this.label = l; } },
        Location: class { constructor(u, p) { this.uri = u; this.range = p; } },
        Position: Pos, Range: class {}, Diagnostic: class { constructor(r, m) { this.message = m; } },
        DocumentSymbol: class { constructor(n) { this.name = n; } },
        Uri: { file: f => ({ fsPath: f }) },
        languages: {
            createDiagnosticCollection: () => ({ set() {}, delete() {}, dispose() {} }),
            registerCompletionItemProvider: (s, p) => (registered.completion = p, {}),
            registerHoverProvider: (s, p) => (registered.hover = p, {}),
            registerSignatureHelpProvider: (s, p) => (registered.signature = p, {}),
            registerDefinitionProvider: (s, p) => (registered.definition = p, {}),
            registerDocumentSymbolProvider: (s, p) => (registered.symbols = p, {}),
        },
        workspace: {
            onDidOpenTextDocument: () => ({}), onDidChangeTextDocument: () => ({}), onDidSaveTextDocument: () => ({}),
            onDidCloseTextDocument: () => ({}), onDidChangeConfiguration: () => ({}), textDocuments: [],
            findFiles: async () => [], workspaceFolders: [], getConfiguration: () => ({ get: (k, d) => d }),
        },
        commands: { registerCommand: () => ({}), executeCommand() {} },
        window: { showInformationMessage: async () => undefined, activeTextEditor: undefined },
    };
    const load = Module._load;
    Module._load = function (req, ...rest) { return req === 'vscode' ? fake : load.call(this, req, ...rest); };
    try {
        const ext = require('../extension');
        ext.activate({ subscriptions: [] });
    } finally { Module._load = load; }
    const doc = { getText: () => player, uri: { fsPath: '/p/player.bs' }, lineAt: l => ({ text: lines[l], range: {} }) };
    const p = at('raycast(');
    const items = registered.completion.provideCompletionItems(doc, new Pos(p.line, p.character));
    assert.ok(items.some(i => i.label === 'raycast' && i.insertText.value.startsWith('raycast(')));
    assert.match(registered.hover.provideHover(doc, new Pos(p.line, p.character + 2)).contents.value, /raycast/);
    assert.deepStrictEqual(registered.symbols.provideDocumentSymbols(doc).map(s => s.name), ['speed', 'jump', 'spawn_point', 'grounded', 'start', 'update', 'on_ground']);
});

test('the extension icon is the editor app icon: a 256x256 PNG', () => {
    const pkg = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8'));
    assert.strictEqual(pkg.icon, 'images/icon.png');
    const png = fs.readFileSync(path.join(root, pkg.icon));
    assert.strictEqual(png.slice(0, 8).toString('hex'), '89504e470d0a1a0a');
    assert.strictEqual(png.slice(12, 16).toString('ascii'), 'IHDR');
    assert.strictEqual(png.readUInt32BE(16), 256);
    assert.strictEqual(png.readUInt32BE(20), 256);
    assert.strictEqual(pkg.contributes.languages[0].icon.dark, './images/icon.png');
});

console.log(passed + ' extension tests passed' + (process.exitCode ? ', some FAILED' : ''));
