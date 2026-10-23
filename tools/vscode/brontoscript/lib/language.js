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
const STRING_ARGS = data.string_args || { lists: {}, args: {} };
const TYPE_RE = '(?:unsigned\\s+)?[A-Za-z_]\\w*(?:\\s*\\[\\s*(?:unsigned\\s+)?[A-Za-z_]\\w*\\s*\\])?';
const MODIFIERS_RE = '(?:(?:public|private|protected|static|override)\\s+)*';
// Words that only declare things at the top of a file or in a struct or class.
const DECLARING = new Set(['func', 'class', 'struct', 'public', 'private', 'protected', 'static', 'override', 'unsigned']);

// "list [ unsigned  int ]" -> "list[unsigned int]"
function normType(t) { return t ? t.trim().replace(/\s*\[\s*/g, '[').replace(/\s*\]/g, ']').replace(/\s+/g, ' ') : ''; }

// "amount: int" -> { name: 'amount', type: 'int' }
function splitParam(p) {
    const m = /^\s*([A-Za-z_]\w*)\s*(?::\s*(.+?))?\s*$/.exec(p);
    return m ? { name: m[1], type: normType(m[2]) } : { name: p.trim(), type: '' };
}
function paramText(p) { return p.type ? p.name + ': ' + p.type : p.name; }
function funcHead(f) { return `func ${f.name}(${f.typedParams.map(paramText).join(', ')})` + (f.returns ? ' -> ' + f.returns : ''); }

// ---- the file's own declarations ------------------------------------------

// Top-level vars, consts, funcs, structs and classes (with their members),
// with the comment lines just above each (their docs).
function parseDocument(text) {
    const lines = text.split(/\r?\n/);
    const vars = [], funcs = [], classes = [];
    let depth = 0, cls = null;
    const varRe = new RegExp('^\\s*(var|const(?:\\s+var)?)\\s+([A-Za-z_]\\w*)\\s*(?::\\s*(' + TYPE_RE + '))?\\s*(?:(:?=)\\s*(.*))?$');
    const funcRe = new RegExp('^\\s*func\\s+([A-Za-z_]\\w*)\\s*\\(([^)]*)\\)\\s*(?:->\\s*(' + TYPE_RE + '))?');
    for (let i = 0; i < lines.length; ++i) {
        const line = stripComment(lines[i]);
        if (depth === 0) {
            cls = null;
            let m = varRe.exec(line);
            if (m) vars.push({
                name: m[2], line: i, value: (m[5] || '').trim(), type: normType(m[3]), inferred: m[4] === ':=', isConst: m[1] !== 'var',
                doc: commentAbove(lines, i) || trailingComment(lines[i]),
            });
            m = funcRe.exec(line);
            if (m) {
                const typed = m[2].split(',').map(s => s.trim()).filter(Boolean).map(splitParam);
                funcs.push({
                    name: m[1], line: i, doc: commentAbove(lines, i),
                    params: typed.map(p => p.name), typedParams: typed, returns: normType(m[3]),
                });
            }
            m = /^\s*(class|struct)\s+([A-Za-z_]\w*)\s*(?::\s*([A-Za-z_]\w*))?/.exec(line);
            if (m) {
                cls = { name: m[2], kind: m[1], parent: m[3] || '', line: i, doc: commentAbove(lines, i), members: [] };
                classes.push(cls);
            }
        } else if (depth === 1 && cls) {                    // a member: [modifiers] var / const / func
            const m = new RegExp('^\\s*(' + MODIFIERS_RE + ')(var|const(?:\\s+var)?|func)\\s+([A-Za-z_]\\w*)(.*)$').exec(line);
            if (m) {
                const mods = m[1].trim().split(/\s+/).filter(Boolean);
                const member = {
                    name: m[3], line: i, owner: cls.name, doc: commentAbove(lines, i) || trailingComment(lines[i]),
                    access: mods.find(x => /^(public|private|protected)$/.test(x)) || 'public',
                    isStatic: mods.includes('static'), isOverride: mods.includes('override'),
                };
                if (m[2] === 'func') {
                    const f = new RegExp('^\\s*\\(([^)]*)\\)\\s*(?:->\\s*(' + TYPE_RE + '))?').exec(m[4]) || ['', '', ''];
                    const typed = f[1].split(',').map(x => x.trim()).filter(Boolean).map(splitParam);
                    Object.assign(member, { kind: 'method', params: typed.map(p => p.name), typedParams: typed, returns: normType(f[2]) });
                } else {
                    const v = new RegExp('^\\s*(?::\\s*(' + TYPE_RE + '))?\\s*(?:(:?=)\\s*(.*))?$').exec(m[4]) || [];
                    Object.assign(member, { kind: 'field', isConst: m[2] !== 'var', type: normType(v[1]), value: (v[3] || '').trim(), inferred: v[2] === ':=' });
                }
                cls.members.push(member);
            }
        }
        for (const ch of stripStrings(line)) {
            if (ch === '{') depth++;
            else if (ch === '}') depth = Math.max(0, depth - 1);
        }
    }
    return { vars, funcs, classes };
}

// A class's members with the inherited ones (the nearest one of each name first).
function classMembers(doc, name) {
    const out = [], seen = new Set();
    for (let c = doc.classes.find(x => x.name === name), guard = 0; c && guard < 32; c = doc.classes.find(x => x.name === c.parent), ++guard)
        for (const m of c.members) if (!seen.has(m.name)) { seen.add(m.name); out.push(m); }
    return out;
}

// The struct or class whose body a line is in, or null.
function classAt(doc, lines, lineNo) {
    let best = null;
    for (const c of doc.classes) if (c.line <= lineNo && depthAt(lines, lineNo) >= 1 && (!best || c.line > best.line)) best = c;
    if (!best) return null;
    for (let i = best.line + 1; i < lineNo; ++i) if (depthAt(lines, i + 1) === 0) return null;   // it closed before the line
    return best;
}

function memberHead(m) {
    const mods = (m.access !== 'public' ? m.access + ' ' : '') + (m.isStatic ? 'static ' : '') + (m.isOverride ? 'override ' : '');
    if (m.kind === 'method') return mods + funcHead(m);
    return mods + (m.isConst ? 'const ' : 'var ') + m.name + (m.type ? ': ' + m.type : '') + (m.value ? (m.inferred ? ' := ' : ' = ') + m.value : '');
}
function classHead(c) { return c.kind + ' ' + c.name + (c.parent ? ' : ' + c.parent : ''); }

// Point(1, 2): init()'s parameters, or a struct's fields.
function constructorParams(doc, c) {
    const init = classMembers(doc, c.name).find(m => m.kind === 'method' && m.name === 'init');
    if (init) return init.typedParams;
    if (c.kind !== 'struct') return [];
    return c.members.filter(m => m.kind === 'field' && !m.isStatic).map(m => ({ name: m.name, type: m.type }));
}

// Local variables visible at a line inside a function: params and earlier vars.
function localsAt(text, lineNo) {
    const lines = text.split(/\r?\n/);
    let start = -1;
    for (let i = Math.min(lineNo, lines.length - 1); i >= 0; --i)
        if (new RegExp('^\\s*' + MODIFIERS_RE + 'func\\s').test(lines[i])) { start = i; break; }
    if (start < 0) return [];
    const out = [];
    const head = new RegExp('^\\s*' + MODIFIERS_RE + 'func\\s+\\w+\\s*\\(([^)]*)\\)').exec(lines[start]);
    if (head) head[1].split(',').map(s => s.trim()).filter(Boolean).map(splitParam)
        .forEach(p => out.push({ name: p.name, kind: 'parameter', type: p.type, line: start }));
    for (let i = start + 1; i <= lineNo && i < lines.length; ++i) {
        const line = stripComment(lines[i]);
        let m;
        const re = new RegExp('\\b(?:var|const(?:\\s+var)?)\\s+([A-Za-z_]\\w*)(?:\\s*:\\s*(' + TYPE_RE + '))?', 'g');
        while ((m = re.exec(line))) out.push({ name: m[1], kind: 'local', type: normType(m[2]), line: i });
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
// `assets`: the project's files (paths from its folder), for play_sound("|"), spawn("|")...
function completions(text, line, character, otherScripts, assets) {
    const lines = text.split(/\r?\n/);
    const before = (lines[line] || '').slice(0, character);
    const items = [];
    if (/#/.test(stripStrings(before))) return items;
    if (inString(before)) return stringCompletions(before, character, assets);

    // var x: |   func f(a: |) -> |   for i: |  ->  the types
    if (/(?:(?:^|[^:]):(?!=)|->)\s*\w*$/.test(before) || /\blist\s*\[\s*\w*$/.test(before)) {
        const returning = /->\s*\w*$/.test(before);
        for (const t of TYPES.values()) if (returning || t.name !== 'void') items.push({ label: t.name, kind: 'class', detail: 'type', doc: t.doc });
        for (const c of parseDocument(text).classes) items.push({ label: c.name, kind: 'class', detail: c.kind, doc: c.doc });
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
    // this.|  super.|  Counter.|  ->  a struct's or class's members
    m = /\b(this|super|[A-Z]\w*)\s*\.\s*\w*$/.exec(before);
    if (m) {
        const doc = parseDocument(text);
        const here = classAt(doc, lines, line);
        let members = null;
        if (m[1] === 'this' && here) members = classMembers(doc, here.name);
        else if (m[1] === 'super' && here && here.parent) members = classMembers(doc, here.parent).filter(x => x.kind === 'method' && x.access !== 'private');
        else if (doc.classes.some(c => c.name === m[1])) members = classMembers(doc, m[1]).filter(x => x.isStatic);
        if (members) {
            for (const x of members)
                items.push(x.kind === 'method'
                    ? { label: x.name, kind: 'method', detail: memberHead(x) + ' in ' + x.owner, doc: x.doc, snippet: `${x.name}(${placeholders(x.params)})` }
                    : { label: x.name, kind: 'field', detail: memberHead(x) + ' in ' + x.owner, doc: x.doc });
            return items;
        }
    }
    // something.|  ->  entity properties, components, vec2 members, other scripts' vars and funcs, objects' members
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
        for (const c of parseDocument(text).classes)
            for (const x of c.members) if (!seen.has(x.name) && x.access === 'public' && !x.isStatic && x.name !== 'init') {
                seen.add(x.name);
                items.push(x.kind === 'method'
                    ? { label: x.name, kind: 'method', detail: memberHead(x) + ' in ' + c.name, doc: x.doc, snippet: `${x.name}(${placeholders(x.params)})` }
                    : { label: x.name, kind: 'field', detail: memberHead(x) + ' in ' + c.name, doc: x.doc });
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
        for (const k of ['var', 'const', 'func', 'struct', 'class']) items.push({ label: k, kind: 'keyword', doc: KEYWORDS[k] });
        if (!/@strict/.test(text)) items.push({ label: '@strict', kind: 'keyword', doc: KEYWORDS['@strict'] });
        return items;
    }
    if (depthAt(lines, line) === 1 && classAt(doc, lines, line)) {          // a struct's or class's body: its members
        for (const k of ['var', 'const', 'func', 'public', 'private', 'protected', 'static', 'override'])
            items.push({ label: k, kind: 'keyword', doc: KEYWORDS[k] });
        return items;
    }
    for (const [k, d] of Object.entries(KEYWORDS)) if (!DECLARING.has(k) && k[0] !== '@') items.push({ label: k, kind: 'keyword', doc: d });
    for (const b of data.builtins)                 /* the snippet fills in the arguments it needs */
        items.push({ label: b.name, kind: 'function', detail: builtinSignature(b), doc: b.doc, snippet: `${b.name}(${placeholders(b.params.slice(0, b.min))})` });
    for (const v of doc.vars) items.push({ label: v.name, kind: 'variable', detail: varHead(v), doc: v.doc });
    for (const f of doc.funcs) items.push({ label: f.name, kind: 'function', detail: funcHead(f), doc: f.doc, snippet: `${f.name}(${placeholders(f.params)})` });
    for (const c of doc.classes) {
        const ps = constructorParams(doc, c);
        items.push({ label: c.name, kind: 'class', detail: `${c.name}(${ps.map(paramText).join(', ')})`, doc: c.doc, snippet: `${c.name}(${placeholders(ps.map(p => p.name))})` });
    }
    for (const l of localsAt(text, line)) if (!items.some(i => i.label === l.name)) items.push({ label: l.name, kind: 'variable', detail: l.kind + (l.type ? ': ' + l.type : '') });
    return items;
}

function varHead(v) { return (v.isConst ? 'const ' : 'var ') + v.name + (v.type ? ': ' + v.type : '') + (v.value ? (v.inferred ? ' := ' : ' = ') + v.value : ''); }

// "distance(a, b)" -> "distance(a: vec2, b: vec2) -> number"
function builtinSignature(b) {
    if (!b.param_types) return b.signature;
    const opt = b.params.map((p, i) => (i >= b.min ? '[' : '') + p + ': ' + b.param_types[i] + (i >= b.min ? ']' : ''));
    const more = /\.\.\./.test(b.signature) ? ', ...' : '';
    return `${b.name}(${opt.join(', ')}${more}) -> ${b.returns}`;
}

function placeholders(params) { return params.map((p, i) => '${' + (i + 1) + ':' + p + '}').join(', '); }

// Inside a string that is a call's argument: { func, arg, prefix, from } (from: the column the
// string's text starts at), or null.
function stringArgAt(before) {
    const stack = [];
    let quote = null, start = 0;
    for (let i = 0; i < before.length; ++i) {
        const c = before[i];
        if (quote) {
            if (c === '\\') ++i;
            else if (c === quote) quote = null;
            continue;
        }
        if (c === '"' || c === "'") { quote = c; start = i + 1; }
        else if (c === '#') return null;
        else if (c === '(' || c === '[') {
            const m = c === '(' ? /([A-Za-z_]\w*)\s*$/.exec(before.slice(0, i)) : null;
            stack.push({ func: m ? m[1] : null, arg: 0 });
        } else if (c === ')' || c === ']') stack.pop();
        else if (c === ',' && stack.length) stack[stack.length - 1].arg++;
    }
    if (!quote || !stack.length || !stack[stack.length - 1].func) return null;
    const top = stack[stack.length - 1];
    return { func: top.func, arg: top.arg, prefix: before.slice(start), from: start };
}

// key_down("|"), button_pressed("|"), set_volume("|"), has(e, "|"), play_sound("|")...: the names it takes.
function stringCompletions(before, character, assets) {
    const at = stringArgAt(before);
    const kind = at && (STRING_ARGS.args[at.func] || {})[String(at.arg)];
    if (!kind) return [];
    if (kind[0] === '.') {                              /* files of that kind, from the project's folder */
        return (assets || []).filter(f => f.toLowerCase().endsWith(kind))
            .map(f => ({ label: f, kind: 'file', detail: at.func + '()', replaceFrom: at.from }));
    }
    return (STRING_ARGS.lists[kind] || []).map(v => ({ label: v, kind: 'value', detail: at.func + '()', replaceFrom: at.from }));
}

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
        const owner = /\b(this|super|[A-Z]\w*)\s*\.\s*$/.exec(lt.slice(0, w.start));
        const here = classAt(doc, lines, line);
        const cname = owner && (owner[1] === 'this' ? here && here.name : owner[1] === 'super' ? here && here.parent : owner[1]);
        const x = (cname ? classMembers(doc, cname) : doc.classes.flatMap(c => c.members)).find(y => y.name === w.word);
        if (x) return '```brontoscript\n' + memberHead(x) + '\n```' + (x.doc ? '\n' + x.doc : '') + '\n\n*In ' + x.owner + '*';
    }
    const before = lt.slice(0, w.start);
    const cls = doc.classes.find(c => c.name === w.word);
    if (cls) return '```brontoscript\n' + classHead(cls) + '\n```' + (cls.doc ? '\n' + cls.doc : '') +
        (cls.kind === 'struct' ? '\n\nA struct is a value: storing one stores a copy.' : '\n\nA class object is shared: storing one stores the same object.') +
        '\n\n' + cls.members.map(m => '`' + memberHead(m) + '`').join('  \n');
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
                const pre = s.slice(0, i - m[0].length);
                if (/\.\s*$/.test(pre)) {                    // obj.method(  ->  a struct's or class's function
                    const x = doc.classes.flatMap(c => c.members).find(y => y.kind === 'method' && y.name === m[1]);
                    if (x) return { name: x.name, signature: funcHead(x).slice(5), params: x.typedParams.map(paramText), active: commas, doc: x.doc };
                }
                const c = doc.classes.find(x => x.name === m[1]);
                if (c) {
                    const ps = constructorParams(doc, c);
                    return { name: c.name, signature: `${c.name}(${ps.map(paramText).join(', ')})`, params: ps.map(paramText), active: commas, doc: c.doc };
                }
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
    const d = doc.funcs.find(x => x.name === w.word) || doc.vars.find(x => x.name === w.word) || doc.classes.find(x => x.name === w.word) ||
        doc.classes.flatMap(c => c.members).find(x => x.name === w.word);
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
    const known = new Set([...BUILTINS.keys(), ...doc.funcs.map(f => f.name), ...doc.classes.map(c => c.name)]);
    const stack = [];
    for (let i = 0; i < lines.length; ++i) {
        const code = stripStrings(stripComment(lines[i]));
        const continued = stack.some(o => o.c !== '{');        // (inside a list or call that began on an earlier line)
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
            if (KEYWORDS[name] || /\bfunc\s+$/.test(code.slice(0, m.index + m[1].length))) continue;
            if (!known.has(name)) problems.push({ line: i, character: m.index + m[1].length, length: name.length, message: `unknown function '${name}'` });
        }
        const top = /^\s*(?!var\b|const\b|func\b|class\b|struct\b|@strict\b|#|$|\}|\{)(\S+)/.exec(lines[i]);
        if (top && !continued && depthBefore(lines, i) === 0) problems.push({ line: i, character: lines[i].indexOf(top[1]), length: top[1].length, message: "only var, const, func, struct and class can be at the top of a script (put code inside start())" });
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
    data, builtinSignature, parseDocument, classMembers, localsAt, completions, hover, signatureAt, definition, quickCheck, parseCheckerOutput, wordAt,
    stringArgAt,
};
