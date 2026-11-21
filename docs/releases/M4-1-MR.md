# v0.4.1 : outillage DX et SDK (`dev` → `main`)

## Résumé

Correctif de confort après M4 (`v0.4.0`), sans changement du moteur : `orosmo doctor` (vérifie la chaîne d'outils), `make compile-commands` (génère `compile_commands.json`, dépôt et projets), support **CMake optionnel** (`find_package(Orosmo)`, le Makefile reste la référence, A14), empreinte du compilateur `cui` dans le cache du code généré (G113 corrigé, `cui --version`). Décision : A21 close. Détail : `docs/releases/M4-1.md`.

## Changements incompatibles

Aucun.

## Tests

Linux x64 (g++ 13.3) : `make check`, `make demo-test` **422 / 422**, `cui-test`, `demo-capture`, `demo-vulkan-check`, `e2e-init` (doctor, Make, CMake, `compile_commands.json`, exemple `minimal`), tests Python : verts.

## Non prouvé

`doctor`, CMake et `compile_commands.json` sous Windows et macOS (la CI le dira).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
