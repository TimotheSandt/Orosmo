# M1 bis : remise en ordre des couches (`dev` → `main`)

## Résumé

Aucune fonctionnalité nouvelle. Les dépendances remontantes sont supprimées pour que M2 (RHI) démarre sur des couches propres : le `Profiler` et la `Window` ne connaissent plus le RHI (ils reçoivent un fournisseur de minuteries GPU et un « binding » graphique installés par `BindGraphicsRuntime`) ; `core` ne contient plus `<windows.h>` ni `getenv` (un port `include/engine/core/system.hpp`, défini dans `src/platform/system.cpp`) ; un en-tête non autonome corrigé ; `utilities.h` → `Utilities.h` et `Backend/` → `Backends/`.

## Contrôles

`check_layers.py --strict` : 5 violations → **0** (baseline vide). `check_headers.py --strict` : 1 → **0**. Isolation des API : 13 → 11 fichiers. La CI passe en mode strict pour couches et en-têtes.

## Décisions à valider

A24 (port système : déclaré dans `core`, défini dans `platform` ; `std::filesystem` reste dans `core`) et A25 (le RHI installe les interfaces définies en bas). Voir `docs/QUESTIONS.md`.

## Commande de démo

```bash
make check
python3 tools/check_layers.py --strict && python3 tools/check_headers.py --strict
```

## Tests

`make demo-test` : 224 cas verts (3348 assertions) ; `make demo-test SANITIZE=address,undefined` : 224 cas verts, aucun rapport ; capture OpenGL identique à la référence (écart max 0/255) ; `make all-debug` : 0 avertissement.

## À vérifier à la relecture

- CI Windows/macOS : première compilation de `src/platform/system.cpp` hors Linux (G61).
- Seul changement de comportement : `XDG_DATA_HOME` vide = non défini (G62).
- Nouvelle archive `libplatform.a` (`mk/engine.mk`, `tools/engine-sdk/engine.mk`).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
