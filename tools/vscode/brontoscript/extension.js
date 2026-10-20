// BrontoScript for VS Code: completion, hover docs, signature help, go to
// definition, outline, and error checking with the engine's own checker.
// No dependencies beyond VS Code itself.
'use strict';

const vscode = require('vscode');
const cp = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const lang = require('./lib/language');

const LANG = 'brontoscript';
const KINDS = {
    function: vscode.CompletionItemKind.Function, method: vscode.CompletionItemKind.Method,
    variable: vscode.CompletionItemKind.Variable, property: vscode.CompletionItemKind.Property,
    field: vscode.CompletionItemKind.Field, class: vscode.CompletionItemKind.Class,
    keyword: vscode.CompletionItemKind.Keyword, snippet: vscode.CompletionItemKind.Snippet,
};

let diagnostics;
let otherScripts = [];            // { name, vars, funcs } of the workspace's other .bs files
let checkerWarned = false;
const timers = new Map();

function activate(context) {
    diagnostics = vscode.languages.createDiagnosticCollection(LANG);
    context.subscriptions.push(diagnostics);
    const sel = { language: LANG };

    context.subscriptions.push(vscode.languages.registerCompletionItemProvider(sel, {
        provideCompletionItems(doc, pos) {
            const others = otherScripts.filter(s => s.file !== doc.uri.fsPath);
            return lang.completions(doc.getText(), pos.line, pos.character, others).map(toItem);
        },
    }, '.'));

    context.subscriptions.push(vscode.languages.registerHoverProvider(sel, {
        provideHover(doc, pos) {
            const md = lang.hover(doc.getText(), pos.line, pos.character);
            return md ? new vscode.Hover(new vscode.MarkdownString(md)) : undefined;
        },
    }));

    context.subscriptions.push(vscode.languages.registerSignatureHelpProvider(sel, {
        provideSignatureHelp(doc, pos) {
            const s = lang.signatureAt(doc.getText(), pos.line, pos.character);
            if (!s) return undefined;
            const info = new vscode.SignatureInformation(s.signature, new vscode.MarkdownString(s.doc || ''));
            info.parameters = s.params.map(p => new vscode.ParameterInformation(p));
            const help = new vscode.SignatureHelp();
            help.signatures = [info];
            help.activeSignature = 0;
            help.activeParameter = Math.min(s.active, Math.max(0, s.params.length - 1));
            return help;
        },
    }, '(', ','));

    context.subscriptions.push(vscode.languages.registerDefinitionProvider(sel, {
        provideDefinition(doc, pos) {
            const d = lang.definition(doc.getText(), pos.line, pos.character);
            if (d) return new vscode.Location(doc.uri, new vscode.Position(d.line, d.character));
            const w = lang.wordAt(doc.lineAt(pos.line).text, pos.character);      // a function in another script
            if (!w) return undefined;
            for (const s of otherScripts) {
                const f = s.funcs.find(x => x.name === w.word);
                if (f) return new vscode.Location(vscode.Uri.file(s.file), new vscode.Position(f.line, 0));
            }
            return undefined;
        },
    }));

    context.subscriptions.push(vscode.languages.registerDocumentSymbolProvider(sel, {
        provideDocumentSymbols(doc) {
            const p = lang.parseDocument(doc.getText());
            const sym = (name, detail, kind, line) => {
                const r = doc.lineAt(line).range;
                return new vscode.DocumentSymbol(name, detail, kind, r, r);
            };
            return [
                ...p.vars.map(v => sym(v.name, v.value ? '= ' + v.value : 'var', vscode.SymbolKind.Variable, v.line)),
                ...p.funcs.map(f => sym(f.name, '(' + f.params.join(', ') + ')', vscode.SymbolKind.Function, f.line)),
            ];
        },
    }));

    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(d => schedule(d, 0)),
        vscode.workspace.onDidChangeTextDocument(e => schedule(e.document, 400)),
        vscode.workspace.onDidSaveTextDocument(d => { schedule(d, 0); if (d.languageId === LANG) indexWorkspace(); }),
        vscode.workspace.onDidCloseTextDocument(d => diagnostics.delete(d.uri)),
        vscode.workspace.onDidChangeConfiguration(e => {
            if (e.affectsConfiguration(LANG)) { checkerWarned = false; vscode.workspace.textDocuments.forEach(d => schedule(d, 0)); }
        }),
        vscode.commands.registerCommand('brontoscript.checkFile', () => {
            const ed = vscode.window.activeTextEditor;
            if (ed) schedule(ed.document, 0);
        }),
    );
    vscode.workspace.textDocuments.forEach(d => schedule(d, 0));
    indexWorkspace();
}

function toItem(i) {
    const item = new vscode.CompletionItem(i.label, KINDS[i.kind] || vscode.CompletionItemKind.Text);
    if (i.detail) item.detail = i.detail;
    if (i.doc) item.documentation = new vscode.MarkdownString(i.doc);
    if (i.snippet) item.insertText = new vscode.SnippetString(i.snippet);
    if (i.filter) item.filterText = i.filter;
    if (i.kind === 'snippet') item.sortText = '0' + i.label;
    return item;
}

// The other scripts in the workspace, for other.health / other.hit(...) and go to definition.
async function indexWorkspace() {
    try {
        const files = await vscode.workspace.findFiles('**/*.bs', '**/{node_modules,.trash,build}/**', 500);
        const out = [];
        for (const uri of files) {
            try {
                const text = fs.readFileSync(uri.fsPath, 'utf8');
                out.push({ name: path.basename(uri.fsPath), file: uri.fsPath, ...lang.parseDocument(text) });
            } catch (e) { /* unreadable: skip */ }
        }
        otherScripts = out;
    } catch (e) { /* no workspace */ }
}

// ---- diagnostics ---------------------------------------------------------------

function schedule(doc, delay) {
    if (doc.languageId !== LANG) return;
    const key = doc.uri.toString();
    clearTimeout(timers.get(key));
    timers.set(key, setTimeout(() => { timers.delete(key); check(doc); }, delay));
}

function config() { return vscode.workspace.getConfiguration(LANG); }

// The checker: the setting, else bscheck in a build/ folder of the workspace, else on PATH.
function findChecker() {
    const set = config().get('checker.path');
    if (set) return set;
    const exe = process.platform === 'win32' ? 'bscheck.exe' : 'bscheck';
    for (const folder of vscode.workspace.workspaceFolders || []) {
        let dir = folder.uri.fsPath;
        for (let up = 0; up < 4; ++up) {                       // the project may sit inside the engine's tree
            const candidate = path.join(dir, 'build', exe);
            if (fs.existsSync(candidate)) return candidate;
            const parent = path.dirname(dir);
            if (parent === dir) break;
            dir = parent;
        }
    }
    return exe;                                                  // let the system's PATH find it
}

// The project folder a script belongs to (the nearest project.bronto above it): its
// strict_typing setting decides whether the checker requires types.
function projectOf(file) {
    let dir = path.dirname(file);
    for (let up = 0; up < 12; ++up) {
        if (fs.existsSync(path.join(dir, 'project.bronto'))) return dir;
        const parent = path.dirname(dir);
        if (parent === dir) break;
        dir = parent;
    }
    return null;
}

function check(doc) {
    if (doc.isClosed) return;
    const text = doc.getText();
    const quick = lang.quickCheck(text).map(p => problem(doc, p.line, p.character, p.length, p.message, 'quick check'));
    if (!config().get('checker.enable', true)) { diagnostics.set(doc.uri, quick); return; }
    // Check what is in the editor, saved or not: write it to a temporary file with the same name.
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'bscheck-'));
    const tmp = path.join(tmpDir, path.basename(doc.fileName) || 'script.bs');
    fs.writeFileSync(tmp, text);
    const project = doc.fileName ? projectOf(doc.fileName) : null;
    const args = project ? ['--project', project, tmp] : [tmp];
    cp.execFile(findChecker(), args, { timeout: 5000 }, (err, stdout) => {
        fs.rm(tmpDir, { recursive: true, force: true }, () => {});
        if (err && err.code === 'ENOENT') {
            diagnostics.set(doc.uri, quick);                      // no checker: the quick checks alone
            if (!checkerWarned) {
                checkerWarned = true;
                vscode.window.showInformationMessage(
                    'BrontoScript: bscheck wasn\'t found, so only quick checks run. Build it with "make tools" ' +
                    'and set "brontoscript.checker.path" to build/bscheck for full error checking.', 'Open Settings')
                    .then(c => { if (c) vscode.commands.executeCommand('workbench.action.openSettings', 'brontoscript.checker'); });
            }
            return;
        }
        const found = lang.parseCheckerOutput(stdout || '').map(p => {
            const line = Math.min(p.line, doc.lineCount - 1);
            const lt = doc.lineAt(line).text;
            const first = lt.search(/\S/);
            return problem(doc, line, first < 0 ? 0 : first, Math.max(1, lt.length - Math.max(0, first)), p.message, 'bscheck');
        });
        // The checker stops at the first mistake; brace problems from the quick check are still worth showing.
        diagnostics.set(doc.uri, found.length ? found.concat(quick.filter(q => /never closed|doesn't close/.test(q.message))) : []);
    });
}

function problem(doc, line, character, length, message, source) {
    const l = Math.max(0, Math.min(line, doc.lineCount - 1));
    const range = new vscode.Range(l, character, l, character + Math.max(1, length));
    const d = new vscode.Diagnostic(range, message, vscode.DiagnosticSeverity.Error);
    d.source = source;
    return d;
}

function deactivate() {}

module.exports = { activate, deactivate };
