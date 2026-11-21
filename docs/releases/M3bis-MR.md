# M3 bis : release cœur générique et confort de build (`dev` → `main`)

## Résumé

Release intermédiaire entre M3 et M4 (version `v0.3.1`, A35). Le paquet de release ne contient plus que le **moteur
générique** : `world`, `TerrainRenderer`, les shaders `terrain.*` et les exemples terrain restent dans le dépôt pour les démos
internes (`ENGINE_EXTRA_MODULES`) mais sortent de `orosmo-pkg-<plateforme>.zip` (décision de l'auteur, A34). `orosmo init` crée un
projet générique (fenêtre, RHI, cube éclairé, interface `.cui`), l'exemple `minimal` n'utilise plus que `orosmo::core`. Le plan
gagne le jalon **M11 · Système de modules et d'addons**. Côté build : barre de progression de `make` dans un terminal,
`sync-demo` qui ne fait plus recompiler la démo, la CI d'une PR n'est plus relancée en double, et la CI garde ses compilations
(ccache) et compile les tests en parallèle. Détail : `docs/releases/M3bis.md`.

M3 (PR #8) est fusionné dans `main` ; les derniers retours de CodeRabbit sur M3 sont reportés.

## Identifiants

`MOD-01` à `MOD-04` ajoutés (Absent). Totaux : Fait 31, Partiel 43, Absent 81, Incompatible 1 (`docs/STATUS.md`).

## Changements incompatibles

Paquet sans `orosmo/world/`, `terrain_renderer.hpp`, `libworld.a`, `libterrain_renderer.a`, `shaders/rhi/terrain.*`, exemples
`terrain_gen` / `terrain_viewer` ; `orosmo init` ne copie plus la démo terrain. Aucun projet extérieur concerné.

## Décisions

- Appliquées : A34 (release cœur générique, jalon M11).
- À trancher : A33 (`generateRegion` hors d'int64 : `Result` à M4 ?).
- Tranchée : A35 (version `v0.3.1`).

## Commande de démo

```bash
make all-debug          # dans un terminal : barre de progression ; relancé sans changement : rien à refaire
make e2e-init           # paquet générique vérifié, orosmo init, compilation, lancement, exemples
```

## Tests

Depuis un clone propre (Linux x64) : `make demo-test` 421 / 421, ASan + UBSan 421 / 421 sans signalement, `make check`,
`make cui-test`, `make terrain-gen-check`, `make e2e-init`, `make viewer-capture`, `make gallery-capture`, tests Python (11),
capture de la démo identique à la référence.

## Non prouvé

Windows et macOS : branche PowerShell de `make dist` / `install-demo` jamais exécutée (G126 ; vérifiée par `build-engine.yml` après
la fusion dans `main`) ; barre de progression vérifiée sous Linux seulement (G127).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
