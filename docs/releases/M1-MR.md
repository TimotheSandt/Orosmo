# M1 : noyau (`dev` → `main`)

## Résumé

Nouveau module `core` sans appel graphique : `Result<T>`, tâches, temps (delta variable et pas fixe), maths avec positions en `double`, handles à génération, système de fichiers virtuel, réflexion + sérialisation JSON/MessagePack versionnées, configuration en couches, observateur de fichiers, profilage (export Perfetto), allocateurs, bus d'événements. Détail et preuves : `docs/releases/M1.md`, `src/core/README.md`.

## Identifiants

`Fait` : CORE-01, 03, 04, 06, 07, 08, 09, 10, 11, 12. `Partiel` : CORE-02 (exceptions héritées), CORE-05 (GPU via RHI), CORE-13 (Windows/macOS non observés), TEST-01 (pas de couverture mesurée). Total : Fait 11, Partiel 44, Absent 94, Incompatible 3.

## Décisions de l'auteur appliquées

A5 : nlohmann-json (`vcpkg.json`). A12 : constructeurs sans graine supprimés, graine explicite et journalisée dans la démo.

## Commande de démo

```bash
make core-demo
```

## Tests

`make demo-test` : 208 cas verts (3076 assertions) ; `make demo-test SANITIZE=address,undefined` : 208 cas verts, aucun rapport ; capture OpenGL identique à la référence.

## À vérifier à la relecture

- Résultats de la CI Windows/macOS (premier passage des tests `core` hors Linux).
- Dépendance ajoutée : `nlohmann-json` via vcpkg (`vcpkg install` à la racine).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
