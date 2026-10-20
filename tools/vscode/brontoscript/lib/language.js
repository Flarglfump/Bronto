// BrontoScript language knowledge for the VS Code extension: what is in a
// file, what to complete, what a name means. Plain JavaScript with no
// dependency on VS Code, so it can be tested with node (test/).
'use strict';

const data = require('./builtins.json');

const BUILTINS = new Map(data.builtins.map(b => [b.name, b]));
const CALLBACKS = new Map(data.callbacks.map(c => [c.name, c]));
const KEYWORDS = data.keywords;
const COMPONENTS = data.components;
const TYPES = new Map((data.types || []).map(t => [t.name, t]));
const TYPE_RE = '[A-Za-z_]\\w*(?:\\s*\\[\\s*[A-Za-z_]\\w*\\s*\\])?';

// "amount: int" -> { name: 'amount', type: 'int' }
function splitParam(p) {
    const m = /^\s*([A-Za-z_]\w*)\s*(?::\s*(.+?))?\s*$/.exec(p);
    return m ? { name: m[1], type: m[2] ? m[2].replace(/\s+/g, '') : '' } : { name: p.trim(), type: '' };
}
function paramText(p) { return p.type ? p.name + ': ' + p.type : p.name; }
function funcHead(f) { return `func ${f.name}(${f.typedParams.map(paramText).join(', ')})` + (f.returns ? ' -> ' + f.returns : ''); }

// ---- the file's own declarations ------------------------------------------

// Top-level vars and funcs, with the comment lines just above each (their docs).
function parseDocument(text) {
    const lines = text.split(/\r?\n/);
    const vars = [], funcs = [];
    let depth = 0;
    for (let i = 0; i < lines.length; ++i) {
        const line = stripComment(lines[i]);
        if (depth === 0) {
            let m = new RegExp('^\\s*var\\s+([A-Za-z_]\\w*)\\s*(?::\\s*(' + TYPE_RE + '))?\\s*(?:(:?=)\\s*(.*))?$').exec(line);
            if (m) vars.push({
                name: m[1], line: i, value: (m[4] || '').trim(), type: m[2] ? m[2].replace(/\s+/g, '') : '', inferred: m[3] === ':=',
                doc: commentAbove(lines, i) || trailingComment(lines[i]),
            });
            m = new RegExp('^\\s*func\\s+([A-Za-z_]\\w*)\\s*\\(([^)]*)\\)\\s*(?:->\\s*(' + TYPE_RE + '))?').exec(line);
            if (m) {
                const typed = m[2].split(',').map(s => s.trim()).filter(Boolean).map(splitParam);
                funcs.push({
                    name: m[1], line: i, doc: commentAbove(lines, i),
                    params: typed.map(p => p.name), typedParams: typed, returns: m[3] ? m[3].replace(/\s+/g, '') : '',
                });
            }
        }
        for (const ch of stripStrings(line)) {
            if (ch === '{') depth++;
            else if (ch === '}') depth = Math.max(0, depth - 1);
        }
    }
    return { vars, funcs };
}

// Local variables visible at a line inside a function: params and earlier vars.
function localsAt(text, lineNo) {
    const lines = text.split(/\r?\n/);
    let start = -1;
    for (let i = Math.min(lineNo, lines.length - 1); i >= 0; --i)
        if (/^\s*func\s/.test(lines[i])) { start = i; break; }
    if (start < 0) return [];
    const out = [];
    const head = /^\s*func\s+\w+\s*\(([^)]*)\)/.exec(lines[start]);
    if (head) head[1].split(',').map(s => s.trim()).filter(Boolean).map(splitParam)
        .forEach(p => out.push({ name: p.name, kind: 'parameter', type: p.type, line: start }));
    for (let i = start + 1; i <= lineNo && i < lines.length; ++i) {
        const line = stripComment(lines[i]);
        let m;
        const re = /\bvar\s+([A-Za-z_]\w*)(?:\s*:\s*([A-Za-z_]\w*(?:\[\w+\])?))?/g;
        while ((m = re.exec(line))) out.push({ name: m[1], kind: 'local', type: m[2] || '', line: i });
        const f = /\bfor\s+([A-Za-z_]\w*)(?:\s*:\s*([A-Za-z_]\w*))?\s+in\b/.exec(line);
        if (f) out.push({ name: f[1], kind: 'local', type: f[2] || '', line: i });
    }
    return out;
}

function stripComment(line) {
    let quote = null;
    for (let i = 0; i < line.length; ++i) {
        const c = line[i];
        if (quote) { if (c === '\\') ++i; else if (c === quote) quote = null; }
        else if (c === '"' || c === "'") quote = c;
        else if (c === '#') return line.slice(0, i);
    }
    return line;
}

function stripStrings(line) { return line.replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g, '""'); }

function commentAbove(lines, i) {
    const doc = [];
    for (let k = i - 1; k >= 0 && /^\s*#/.test(lines[k]); --k) doc.unshift(lines[k].replace(/^\s*#\s?/, ''));
    return doc.join('\n');
}

function trailingComment(line) {
    const code = stripComment(line);
    return code.length < line.length ? line.slice(code.length + 1).trim() : '';
}

// ---- completion ------------------------------------------------------------

// What comes before the cursor on its line decides what to offer.
// Returns items { label, kind, detail, doc, insert?, snippet? }.
function completions(text, line, character, otherScripts) {
    const lines = text.split(/\r?\n/);
    const before = (lines[line] || '').slice(0, character);
    const items = [];
    if (/#/.test(stripStrings(before)) || inString(before)) return items;

    // var x: |   func f(a: |) -> |   for i: |  ->  the types
    if (/(?:(?:^|[^:]):(?!=)|->)\s*\w*$/.test(before) || /\blist\s*\[\s*\w*$/.test(before)) {
        const returning = /->\s*\w*$/.test(before);
        for (const t of TYPES.values()) if (returning || t.name !== 'void') items.push({ label: t.name, kind: 'class', detail: 'type', doc: t.doc });
        return items;
    }
    if (/^\s*@\w*$/.test(before)) {
        items.push({ label: '@strict', kind: 'keyword', doc: KEYWORDS['@strict'], insert: 'strict', filter: 'strict' });
        return items;
    }

    // self.Shape.|  ->  the component's fields
    let m = /\.\s*([A-Z]\w*)\s*\.\s*\w*$/.exec(before);
    if (m && COMPONENTS[m[1]]) {
        for (const f of COMPONENTS[m[1]]) items.push({ label: f, kind: 'field', detail: m[1] + '.' + f });
        return items;
    }
    // something.|  ->  entity properties, components, vec2 members, other scripts' vars and funcs
    if (/\.\s*\w*$/.test(before)) {
        for (const p of data.properties) items.push({ label: p.name, kind: 'property', detail: p.type, doc: p.doc });
        for (const c of Object.keys(COMPONENTS)) items.push({ label: c, kind: 'class', detail: 'component', doc: 'Fields: ' + COMPONENTS[c].join(', ') });
        for (const v of data.vec2) items.push({ label: v.name, kind: 'property', detail: 'vec2', doc: v.doc });
        const seen = new Set(items.map(i => i.label));
        for (const s of [{ name: 'this file', ...parseDocument(text) }, ...(otherScripts || [])]) {
            for (const v of s.vars) if (!seen.has(v.name)) { seen.add(v.name); items.push({ label: v.name, kind: 'variable', detail: 'var in ' + s.name, doc: v.doc }); }
            for (const f of s.funcs) if (!seen.has(f.name) && !CALLBACKS.has(f.name)) {
                seen.add(f.name);
                items.push({ label: f.name, kind: 'method', detail: (f.typedParams ? funcHead(f).slice(5) : `${f.name}(${f.params.join(', ')})`) + ` in ${s.name}`, doc: f.doc, snippet: `${f.name}(${placeholders(f.params)})` });
            }
        }
        return items;
    }
    // At the top level, a new callback is the likely thing to write.
    const doc = parseDocument(text);
    const atTop = depthAt(lines, line) === 0;
    if (atTop) {
        for (const c of data.callbacks)
            if (!doc.funcs.some(f => f.name === c.name))
                items.push({ label: 'func ' + c.name, kind: 'snippet', detail: c.signature, doc: c.doc, snippet: c.snippet, filter: c.name });
        items.push({ label: 'var', kind: 'keyword', doc: KEYWORDS.var }, { label: 'func', kind: 'keyword', doc: KEYWORDS.func });
        if (!/@strict/.test(text)) items.push({ label: '@strict', kind: 'keyword', doc: KEYWORDS['@strict'] });
        return items;
    }
    for (const [k, d] of Object.entries(KEYWORDS)) if (k !== 'func' && k[0] !== '@') items.push({ label: k, kind: 'keyword', doc: d });
    for (const b of data.builtins)                 /* the snippet fills in the arguments it needs */
        items.push({ label: b.name, kind: 'function', detail: builtinSignature(b), doc: b.doc, snippet: `${b.name}(${placeholders(b.params.slice(0, b.min))})` });
    for (const v of doc.vars) items.push({ label: v.name, kind: 'variable', detail: varHead(v), doc: v.doc });
    for (const f of doc.funcs) items.push({ label: f.name, kind: 'function', detail: funcHead(f), doc: f.doc, snippet: `${f.name}(${placeholders(f.params)})` });
    for (const l of localsAt(text, line)) if (!items.some(i => i.label === l.name)) items.push({ label: l.name, kind: 'variable', detail: l.kind + (l.type ? ': ' + l.type : '') });
    return items;
}

function varHead(v) { return 'var ' + v.name + (v.type ? ': ' + v.type : '') + (v.value ? (v.inferred ? ' := ' : ' = ') + v.value : ''); }

// "distance(a, b)" -> "distance(a: vec2, b: vec2) -> number"
function builtinSignature(b) {
    if (!b.param_types) return b.signature;
    const opt = b.params.map((p, i) => (i >= b.min ? '[' : '') + p + ': ' + b.param_types[i] + (i >= b.min ? ']' : ''));
    const more = /\.\.\./.test(b.signature) ? ', ...' : '';
    return `${b.name}(${opt.join(', ')}${more}) -> ${b.returns}`;
}

function placeholders(params) { return params.map((p, i) => '${' + (i + 1) + ':' + p + '}').join(', '); }

function inString(before) {
    let quote = null;
    for (let i = 0; i < before.length; ++i) {
        const c = before[i];
        if (quote) { if (c === '\\') ++i; else if (c === quote) quote = null; }
        else if (c === '"' || c === "'") quote = c;
        else if (c === '#') return false;
    }
    return quote !== null;
}

function depthAt(lines, lineNo) {
    let depth = 0;
    for (let i = 0; i < lineNo && i < lines.length; ++i)
        for (const ch of stripStrings(stripComment(lines[i]))) {
            if (ch === '{') depth++;
            else if (ch === '}') depth = Math.max(0, depth - 1);
        }
    return depth;
}

// ---- hover, signatures, definitions ---------------------------------------------

function wordAt(lineText, character) {
    const re = /[A-Za-z_]\w*/g;
    let m;
    while ((m = re.exec(lineText))) if (character >= m.index && character <= m.index + m[0].length) return { word: m[0], start: m.index, end: m.index + m[0].length };
    return null;
}

// Markdown describing the word under the cursor, or null.
function hover(text, line, character) {
    const lines = text.split(/\r?\n/);
    const lt = lines[line] || '';
    const w = wordAt(lt, character);
    if (!w) return null;
    const afterDot = /\.\s*$/.test(lt.slice(0, w.start));
    const doc = parseDocument(text);
    if (afterDot) {
        const comp = /\.\s*([A-Z]\w*)\s*\.\s*$/.exec(lt.slice(0, w.start));
        if (comp && COMPONENTS[comp[1]]) return '`' + comp[1] + '.' + w.word + '` (a field)';
        const p = data.properties.find(x => x.name === w.word);
        if (p) return '`.' + p.name + ': ' + p.type + '`  \n' + p.doc;
        if (COMPONENTS[w.word]) return '**' + w.word + '** component  \nFields: ' + COMPONENTS[w.word].join(', ') + '  \n(nil if the entity doesn\'t have it)';
    }
    const before = lt.slice(0, w.start);
    if (TYPES.has(w.word) && /(?::|->|\[)\s*$/.test(before)) return '**' + w.word + '** (a type)  \n' + TYPES.get(w.word).doc;
    if (w.word === 'strict' && /@$/.test(before)) return '**@strict**  \n' + KEYWORDS['@strict'];
    const local = localsAt(text, line).find(l => l.name === w.word);
    if (local && !afterDot) return '`' + w.word + (local.type ? ': ' + local.type : '') + '` (' + local.kind + ')';
    const f = doc.funcs.find(x => x.name === w.word);
    if (f) return '```brontoscript\n' + funcHead(f) + '\n```' + (f.doc ? '\n' + f.doc : '') +
        (CALLBACKS.has(f.name) ? '\n\n*Called by the engine:* ' + CALLBACKS.get(f.name).doc : '');
    const v = doc.vars.find(x => x.name === w.word);
    if (v) return '```brontoscript\n' + varHead(v) + '\n```' + (v.doc ? '\n' + v.doc : '') + '\n\nOne per entity; Script.vars can set its starting value.';
    if (BUILTINS.has(w.word)) { const b = BUILTINS.get(w.word); return '```brontoscript\n' + builtinSignature(b) + '\n```\n' + b.doc; }
    if (KEYWORDS[w.word]) return '**' + w.word + '**  \n' + KEYWORDS[w.word];
    return null;
}

// The call the cursor is in: { name, signature, params, active, doc } or null.
function signatureAt(text, line, character) {
    const lines = text.split(/\r?\n/);
    let s = lines.slice(0, line).join('\n') + (line > 0 ? '\n' : '') + (lines[line] || '').slice(0, character);
    s = s.replace(/"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'/g, '""').replace(/#[^\n]*/g, '');
    let depth = 0, commas = 0;
    for (let i = s.length - 1; i >= 0; --i) {
        const c = s[i];
        if (c === ')' || c === ']') depth++;
        else if (c === '[') { if (depth === 0) return null; depth--; }
        else if (c === '(') {
            if (depth === 0) {
                const m = /([A-Za-z_]\w*)\s*$/.exec(s.slice(0, i));
                if (!m) return null;
                const doc = parseDocument(text);
                const f = doc.funcs.find(x => x.name === m[1]);
                if (f) return { name: f.name, signature: funcHead(f).slice(5), params: f.typedParams.map(paramText), active: commas, doc: f.doc };
                const b = BUILTINS.get(m[1]);
                if (b) return { name: b.name, signature: builtinSignature(b), params: b.params.map((p, k) => b.param_types ? p + ': ' + b.param_types[k] : p), active: commas, doc: b.doc };
                return null;
            }
            depth--;
        } else if (c === ',' && depth === 0) commas++;
        else if (c === '{' || c === '}') { if (depth === 0) return null; }
    }
    return null;
}

// Where a name is declared in this file: { line, character } or null.
function definition(text, line, character) {
    const lines = text.split(/\r?\n/);
    const w = wordAt(lines[line] || '', character);
    if (!w) return null;
    const local = localsAt(text, line).filter(l => l.name === w.word).pop();
    if (local && local.line !== line) return { line: local.line, character: Math.max(0, lines[local.line].indexOf(w.word)) };
    const doc = parseDocument(text);
    const d = doc.funcs.find(x => x.name === w.word) || doc.vars.find(x => x.name === w.word);
    if (d) return { line: d.line, character: Math.max(0, lines[d.line].indexOf(w.word)) };
    return null;
}

// ---- checks that need no compiler ---------------------------------------------------
// The engine's own checker (bscheck) is the real one; these catch the common
// slips while typing, and are all it can show if bscheck isn't found.
function quickCheck(text) {
    const problems = [];
    const lines = text.split(/\r?\n/);
    const doc = parseDocument(text);
    const known = new Set([...BUILTINS.keys(), ...doc.funcs.map(f => f.name)]);
    const stack = [];
    for (let i = 0; i < lines.length; ++i) {
        const code = stripStrings(stripComment(lines[i]));
        for (let k = 0; k < code.length; ++k) {
            const c = code[k];
            if (c === '{' || c === '(' || c === '[') stack.push({ c, line: i, character: k });
            else if (c === '}' || c === ')' || c === ']') {
                const open = stack.pop();
                const want = { '}': '{', ')': '(', ']': '[' }[c];
                if (!open || open.c !== want) { problems.push({ line: i, character: k, length: 1, message: `this '${c}' doesn't close anything` }); if (open) stack.push(open); }
            }
        }
        const re = /(^|[^.\w])([A-Za-z_]\w*)\s*\(/g;
        let m;
        while ((m = re.exec(code))) {
            const name = m[2];
            if (KEYWORDS[name] || /^\s*func\s+$/.test(code.slice(0, m.index + m[1].length))) continue;
            if (!known.has(name)) problems.push({ line: i, character: m.index + m[1].length, length: name.length, message: `unknown function '${name}'` });
        }
        const top = /^\s*(?!var\b|func\b|@strict\b|#|$|\}|\{)(\S+)/.exec(lines[i]);
        if (top && depthBefore(lines, i) === 0) problems.push({ line: i, character: lines[i].indexOf(top[1]), length: top[1].length, message: "only 'var' and 'func' can be at the top of a script (put code inside start())" });
    }
    for (const open of stack) problems.push({ line: open.line, character: open.character, length: 1, message: `this '${open.c}' is never closed` });
    return problems;
}

function depthBefore(lines, lineNo) { return depthAt(lines, lineNo); }

// "player.bs:12: unknown name 'x'" lines from bscheck -> problems (line is 0-based).
function parseCheckerOutput(output) {
    const out = [];
    for (const line of output.split(/\r?\n/)) {
        const m = /^(.*?):(\d+): (.*)$/.exec(line.trim());
        if (m) out.push({ file: m[1], line: Math.max(0, parseInt(m[2], 10) - 1), message: m[3] });
    }
    return out;
}

module.exports = {
    data, builtinSignature, parseDocument, localsAt, completions, hover, signatureAt, definition, quickCheck, parseCheckerOutput, wordAt,
};
