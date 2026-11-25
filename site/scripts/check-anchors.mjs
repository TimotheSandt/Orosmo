// Vérifie les ancres des liens internes du site construit (dist/) : chaque href="/page/#ancre" (ou "#ancre")
// doit pointer vers un id présent dans la page cible. Usage : npm run build && npm run check-anchors.
import { readdirSync, readFileSync, existsSync, statSync } from 'node:fs';
import { join, relative, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../dist/', import.meta.url));
if (!existsSync(root)) {
  console.error("dist/ absent : lancer npm run build d'abord");
  process.exit(2);
}

function htmlFiles(dir) {
  return readdirSync(dir).flatMap((name) => {
    const path = join(dir, name);
    if (statSync(path).isDirectory()) return htmlFiles(path);
    return name.endsWith('.html') ? [path] : [];
  });
}

function pageFile(pathname) {
  const clean = decodeURIComponent(pathname);
  const target = join(root, clean);
  if (clean.endsWith('/')) return join(target, 'index.html');
  if (existsSync(`${target}.html`)) return `${target}.html`;
  return existsSync(target) && statSync(target).isDirectory() ? join(target, 'index.html') : target;
}

const idCache = new Map();
function idsOf(file) {
  if (!idCache.has(file)) {
    const html = readFileSync(file, 'utf8');
    idCache.set(file, new Set([...html.matchAll(/\sid="([^"]*)"/g)].map((m) => m[1])));
  }
  return idCache.get(file);
}

// URL publique d'une page de dist/ (".../index.html" -> ".../"), base de résolution des liens relatifs.
function publicUrl(file) {
  const rel = relative(root, file).split(sep).join('/');
  return new URL(rel.replace(/(^|\/)index\.html$/, '$1'), 'http://site.local/');
}

const broken = [];
let count = 0;
for (const file of htmlFiles(root)) {
  const html = readFileSync(file, 'utf8');
  for (const match of html.matchAll(/href="([^"]*#[^"]+)"/g)) {
    const href = match[1];
    if (/^[a-z]+:|^\/\//.test(href)) continue;
    const hash = href.slice(href.indexOf('#') + 1);
    const rawPath = href.slice(0, href.indexOf('#')).split('?')[0];
    // Lien relatif (../autre/#a, autre/#a) : résolu depuis l'URL publique de la page source, pas depuis la racine.
    const pathPart = rawPath === '' ? '' : new URL(rawPath, publicUrl(file)).pathname;
    const target = pathPart === '' ? file : pageFile(pathPart);
    if (!existsSync(target) || !target.endsWith('.html')) continue; // pages manquantes : voir check-links
    count++;
    if (!idsOf(target).has(decodeURIComponent(hash))) broken.push(`${file.slice(root.length)} -> ${href}`);
  }
}
if (broken.length > 0) {
  console.error(`${broken.length} ancre(s) cassée(s) :`);
  for (const line of [...new Set(broken)]) console.error(`  ${line}`);
  process.exit(1);
}
console.log(`check-anchors : ${count} liens avec ancre vérifiés, aucun cassé`);
