"use strict";
// Extension VS Code du langage d'interface (.cui) : diagnostics du compilateur `cui` (fichier:ligne:colonne, pendant la frappe),
// complétion (composants, arguments nommés, modificateurs, événements, alignements, propriétés et images des thèmes), aide au
// survol. La description des composants vient du compilateur lui-même (`cui spec`) : l'extension suit le langage sans copie.
const vscode = require("vscode");
const cp = require("child_process");
const path = require("path");
const fs = require("fs");

const DEBOUNCE_MS = 400;

/** @type {vscode.DiagnosticCollection} */ let diagnostics;
/** @type {vscode.OutputChannel} */ let output;
const timers = new Map();
let specCache = null; // { tool, stamp, spec }
let themeCache = null; // { at, tokens: Set, images: Set }

function log(msg) {
    output.appendLine(`[${new Date().toISOString()}] ${msg}`);
}

// ── Compilateur ──────────────────────────────────────────────────────────────────────────────────────────────────────

function exeName() {
    return process.platform === "win32" ? "cui.exe" : "cui";
}

function resolveTool() {
    const explicit = vscode.workspace.getConfiguration("cui").get("toolPath");
    if (explicit) return explicit;
    const candidates = [];
    for (const folder of vscode.workspace.workspaceFolders || []) {
        const root = folder.uri.fsPath;
        candidates.push(
            path.join(root, "tools", exeName()),                       // dépôt du moteur (make cui-tool)
            path.join(root, "tools", "target", "release", exeName()),  // cargo build --release
            path.join(root, "orosmo", "tools", exeName()),             // projet créé par `orosmo init`
            path.join(root, "apps", "demo", "orosmo", "tools", exeName()),
        );
    }
    for (const c of candidates) if (fs.existsSync(c)) return c;
    return exeName(); // dans le PATH
}

function run(tool, args, input) {
    return new Promise(resolve => {
        let proc;
        try {
            proc = cp.spawn(tool, args, { stdio: ["pipe", "pipe", "pipe"] });
        } catch (err) {
            resolve({ code: -1, stdout: "", stderr: String(err) });
            return;
        }
        let stdout = "", stderr = "";
        proc.stdout.on("data", c => (stdout += c));
        proc.stderr.on("data", c => (stderr += c));
        proc.on("error", err => resolve({ code: -1, stdout, stderr: String(err) }));
        proc.on("close", code => resolve({ code, stdout, stderr }));
        if (input !== undefined) proc.stdin.end(input, "utf8");
        else proc.stdin.end();
    });
}

// Spécification mise en cache par chemin ET date de modification du compilateur : une reconstruction au même chemin est relue ;
// un échec n'est pas gardé (nouvel essai au prochain appel, par exemple après « make cui-tool »).
function toolStamp(tool) {
    try {
        return fs.statSync(tool).mtimeMs;
    } catch {
        return -1;
    }
}

async function spec() {
    const tool = resolveTool();
    const stamp = toolStamp(tool);
    if (specCache && specCache.tool === tool && specCache.stamp === stamp) return specCache.spec;
    const r = await run(tool, ["spec"]);
    try {
        specCache = { tool, stamp, spec: JSON.parse(r.stdout) };
        log(`spec: ${specCache.spec.components.length} components from ${tool}`);
        return specCache.spec;
    } catch {
        log(`spec: unavailable from ${tool} (${r.stderr.trim() || "no output"}) - rebuild cui (make cui-tool)`);
        specCache = null;
        return { components: [], modifiers: [], styleProperties: [], alignments: [], sizes: [], wrappers: [] };
    }
}

let warnedMissingTool = false;

async function check(doc) {
    const tool = resolveTool();
    const args = ["check", "--stdin", doc.fileName];
    const version = doc.version;
    const r = await run(tool, args, doc.getText());
    // Un contrôle plus récent a pu finir avant celui-ci, ou le document a été fermé : ce résultat est périmé.
    if (doc.isClosed || doc.version !== version) return;
    if (r.code === -1) {
        if (!warnedMissingTool) {
            warnedMissingTool = true;
            vscode.window.showWarningMessage(`CUI: compiler not found (${tool}). Build it with "make cui-tool" or set cui.toolPath.`);
        }
        log(`check: ${r.stderr}`);
        return;
    }
    let items = [];
    try {
        items = JSON.parse(r.stdout.trim() || "[]");
    } catch {
        log(`check: unreadable output: ${r.stdout.trim()} ${r.stderr.trim()}`);
        return;
    }
    diagnostics.set(
        doc.uri,
        items.map(d => {
            const range = new vscode.Range(
                Math.max(0, (d.line || 1) - 1),
                Math.max(0, (d.col || 1) - 1),
                Math.max(0, (d.endLine || d.line || 1) - 1),
                Math.max(0, (d.endCol || (d.col || 1) + 1) - 1),
            );
            const diag = new vscode.Diagnostic(
                range,
                d.message,
                d.severity === "warning" ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error,
            );
            diag.source = "cui";
            return diag;
        }),
    );
}

function schedule(doc) {
    const key = doc.uri.toString();
    clearTimeout(timers.get(key));
    timers.set(
        key,
        setTimeout(() => {
            timers.delete(key);
            check(doc);
        }, DEBOUNCE_MS),
    );
}

// ── Thèmes (jetons et images pour .style(...)) ──────────────────────────────────────────────────────────────────────

async function themeNames() {
    if (themeCache && Date.now() - themeCache.at < 10000) return themeCache;
    const tokens = new Set(), images = new Set();
    const files = await vscode.workspace.findFiles("**/themes/*.json", "**/{node_modules,build,dist}/**", 50);
    for (const f of files) {
        try {
            const j = JSON.parse(fs.readFileSync(f.fsPath, "utf8"));
            Object.keys(j.tokens || {}).forEach(t => tokens.add(t));
            Object.keys(j.images || {}).forEach(i => images.add(i));
        } catch {
            /* fichier qui n'est pas un thème */
        }
    }
    themeCache = { at: Date.now(), tokens, images };
    return themeCache;
}

// ── Complétion et survol ────────────────────────────────────────────────────────────────────────────────────────────

// Composant dont on est en train d'écrire les arguments : NOM( ... jusqu'au curseur, parenthèses équilibrées.
function enclosingCall(text) {
    let depth = 0;
    for (let i = text.length - 1; i >= 0; --i) {
        const c = text[i];
        if (c === ")") depth++;
        else if (c === "(") {
            if (depth === 0) {
                const m = /(\.?)([A-Za-z_][A-Za-z0-9_]*)\s*$/.exec(text.slice(0, i));
                return m ? { name: m[2], modifier: m[1] === ".", args: text.slice(i + 1) } : null;
            }
            depth--;
        }
    }
    return null;
}

function snippetFor(c) {
    const required = c.params.filter(p => p.default === null);
    let i = 1;
    const args = required.map((p, k) => (k === 0 && p.kind === "text" ? `"\${${i++}:${p.name}}"` : `${p.name}: \${${i++}}`));
    let s = c.name + (required.length ? `(${args.join(", ")})` : "");
    if (c.children !== "none") s += " {\n\t$0\n}";
    return new vscode.SnippetString(s);
}

function signature(c) {
    const params = c.params.map(p => `${p.name}: ${p.kind}${p.default === null ? "" : " = " + (p.default || "omitted")}`);
    return `${c.name}(${params.join(", ")})`;
}

async function provideCompletion(doc, pos) {
    const s = await spec();
    const line = doc.lineAt(pos.line).text.slice(0, pos.character);
    const before = doc.getText(new vscode.Range(new vscode.Position(Math.max(0, pos.line - 30), 0), pos));
    const items = [];
    const K = vscode.CompletionItemKind;

    // Après « . » : un modificateur / événement, ou une valeur énumérée dans les arguments.
    if (/\.\w*$/.test(line)) {
        const call = enclosingCall(before);
        if (call && /\(\s*\.\w*$|[:,]\s*\.\w*$/.test(line)) {
            const values = call.name === "width" || call.name === "height" ? s.sizes : call.name === "overflow" ? s.overflows || [] : s.alignments;
            return values.map(v => new vscode.CompletionItem(v, K.EnumMember));
        }
        return s.modifiers.map(m => {
            const it = new vscode.CompletionItem(m.name, m.name.startsWith("on") ? K.Event : K.Method);
            it.documentation = m.doc;
            it.insertText = new vscode.SnippetString(`${m.name}($1)`);
            return it;
        });
    }

    const call = enclosingCall(before);
    if (call) {
        // Dans .style( : propriétés, puis jetons / images dans la chaîne.
        if (call.modifier && call.name === "style") {
            const themes = await themeNames();
            if (((call.args.match(/"/g) || []).length % 2) === 1) { // dans une chaîne ouverte
                const out = [...themes.tokens].map(t => new vscode.CompletionItem(t, K.Constant));
                [...themes.images].forEach(i => out.push(Object.assign(new vscode.CompletionItem(i, K.File), { detail: "image (nine-slice)" })));
                return out;
            }
            return s.styleProperties.map(p => Object.assign(new vscode.CompletionItem(p, K.Property), { insertText: new vscode.SnippetString(`${p}: "$1"`) }));
        }
        // Arguments nommés d'un composant.
        const comp = s.components.find(c => c.name === call.name);
        if (comp && !call.modifier) {
            return comp.params.map(p => {
                const it = new vscode.CompletionItem(p.name, K.Field);
                it.detail = p.kind;
                it.insertText = new vscode.SnippetString(`${p.name}: $1`);
                return it;
            });
        }
        return items;
    }

    // Début d'instruction : composants, déclarations.
    for (const c of s.components) {
        const it = new vscode.CompletionItem(c.name, K.Class);
        it.detail = signature(c);
        it.documentation = c.doc;
        it.insertText = snippetFor(c);
        items.push(it);
    }
    for (const w of s.wrappers) {
        const it = new vscode.CompletionItem(w, K.Keyword);
        it.insertText = new vscode.SnippetString(`${w} var \${1:name}: \${2:Type}`);
        items.push(it);
    }
    items.push(Object.assign(new vscode.CompletionItem("import \"orosmo/ui\"", K.Module), { insertText: 'import "orosmo/ui"' }));
    items.push(Object.assign(new vscode.CompletionItem("view", K.Keyword), { insertText: new vscode.SnippetString("view ${1:Name} {\n\t$0\n}") }));
    return items;
}

async function provideHover(doc, pos) {
    const range = doc.getWordRangeAtPosition(pos, /[A-Za-z_][A-Za-z0-9_]*/);
    if (!range) return null;
    const word = doc.getText(range);
    const s = await spec();
    const comp = s.components.find(c => c.name === word);
    if (comp) {
        const md = new vscode.MarkdownString();
        md.appendCodeblock(signature(comp), "cui");
        md.appendMarkdown(comp.doc + "\n\n");
        if (comp.events.length) md.appendMarkdown(`Events: ${comp.events.map(e => "`" + e + "`").join(", ")}  \n`);
        md.appendMarkdown(`Children: ${comp.children}`);
        return new vscode.Hover(md, range);
    }
    const prev = range.start.character > 0 ? doc.getText(new vscode.Range(range.start.translate(0, -1), range.start)) : "";
    if (prev === ".") {
        const m = s.modifiers.find(x => x.name === word);
        if (m) return new vscode.Hover(new vscode.MarkdownString(`**.${m.name}** — ${m.doc}`), range);
    }
    return null;
}

// ── Activation ──────────────────────────────────────────────────────────────────────────────────────────────────────

function activate(context) {
    output = vscode.window.createOutputChannel("CUI");
    diagnostics = vscode.languages.createDiagnosticCollection("cui");
    context.subscriptions.push(output, diagnostics);
    log(`activated; compiler: ${resolveTool()}`);

    vscode.workspace.textDocuments.forEach(d => d.languageId === "cui" && check(d));
    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(d => d.languageId === "cui" && check(d)),
        vscode.workspace.onDidChangeTextDocument(e => e.document.languageId === "cui" && schedule(e.document)),
        vscode.workspace.onDidSaveTextDocument(d => d.languageId === "cui" && check(d)),
        vscode.workspace.onDidCloseTextDocument(d => diagnostics.delete(d.uri)),
        vscode.workspace.onDidChangeConfiguration(e => {
            if (e.affectsConfiguration("cui")) {
                specCache = null;
                warnedMissingTool = false;
            }
        }),
        vscode.languages.registerCompletionItemProvider({ language: "cui" }, { provideCompletionItems: provideCompletion }, ".", "(", ",", '"', "@"),
        vscode.languages.registerHoverProvider({ language: "cui" }, { provideHover }),
        vscode.commands.registerCommand("cui.showOutput", () => output.show(true)),
    );
}

function deactivate() {}

module.exports = { activate, deactivate };
