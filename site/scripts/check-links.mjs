// Vérifie les liens internes du site construit (dist/) : chaque href="/..." doit mener à un fichier existant.
// Usage : npm run build && npm run check-links. Code de sortie non nul si un lien est cassé.
import { readdirSync, readFileSync, existsSync, statSync } from 'node:fs';
import { join } from 'node:path';

const root = new URL('../dist/', import.meta.url).pathname;
if (!existsSync(root)) {
  console.error('dist/ absent : lancer npm run build d\'abord');
  process.exit(2);
}

function htmlFiles(dir) {
  return readdirSync(dir).flatMap((name) => {
    const path = join(dir, name);
    if (statSync(path).isDirectory()) return htmlFiles(path);
    return name.endsWith('.html') ? [path] : [];
  });
}

function resolves(href) {
  const clean = decodeURIComponent(href.split('#')[0].split('?')[0]);
  if (clean === '' || clean === '/') return existsSync(join(root, 'index.html'));
  const target = join(root, clean);
  if (clean.endsWith('/')) return existsSync(join(target, 'index.html'));
  return existsSync(target) || existsSync(join(target, 'index.html')) || existsSync(`${target}.html`);
}

const broken = [];
let count = 0;
for (const file of htmlFiles(root)) {
  const html = readFileSync(file, 'utf8');
  for (const match of html.matchAll(/(?:href|src)="(\/[^"]*)"/g)) {
    const href = match[1];
    if (href.startsWith('//')) continue;
    count++;
    if (!resolves(href)) broken.push(`${file.slice(root.length)} -> ${href}`);
  }
}
if (broken.length > 0) {
  console.error(`${broken.length} lien(s) interne(s) cassé(s) :`);
  for (const line of broken) console.error(`  ${line}`);
  process.exit(1);
}
console.log(`check-links : ${count} liens internes vérifiés, aucun cassé`);
