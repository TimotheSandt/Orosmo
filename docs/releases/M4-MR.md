# M4 : backend Vulkan (`dev` → `main`)

## Résumé

Jalon M4, version **`v0.4.0`**. Le RHI a un **second backend, Vulkan 1.3** (`src/rhi/vulkan/` : chargé à l'exécution par volk, mémoire par
VMA, rendu dynamique, barrières déduites, cache de pipelines sur disque) qui se comporte comme le backend OpenGL de référence : les
mêmes tests tournent sur les deux, avec les couches de validation à 0 message. La démo se lance avec `--backend=vulkan` ou
`--backend=opengl` ; si Vulkan ne démarre pas, elle retombe sur OpenGL (`--strict-backend` l'interdit). **Sur la démo, les deux backends
donnent la même image** (`scene.png` identique bit à bit sur 15 captures, `ui.png` ne diffère que par le nom du pilote affiché) et
Vulkan est plus rapide de 12 à 20 % en headless (`docs/PERF_OPENGL_VS_VULKAN.md`). Guide `docs/ADD_A_BACKEND.md`.

Dans le même jalon, sur décision de l'auteur : l'API de `platform` est renommée selon `docs/03` §7 (A31) ; le code procédural
(`world`, `TerrainRenderer`, `terrain_gen`, `terrain_viewer`) est **retiré du dépôt et archivé** dans la branche
`archive/procedural-world`, et il ne reste qu'**une démo** (`apps/demo` : scène 3D statique + vitrine de l'interface) (A37) ; le plan
est réorganisé : M7 supprimé, effets en modules préinstallés (M13), registre d'addons (M12), M11 limité au mécanisme (A38, A39).
Détail : `docs/releases/M4.md`.

## Identifiants

`RHI-03` et `RHI-06` passent à **Fait** ; `RHI-04` et `RHI-14` restent **Partiel** (manques écrits). `TER-*` et `TOOL-*` : Archivé ou
Hors moteur ; `MOD-05` à `MOD-09` ajoutés. Totaux (161 identifiants) : Fait 25, Partiel 40, Absent 77, Archivé 11, Hors moteur 7,
Incompatible 1 (`docs/STATUS.md`).

## Changements incompatibles

- **API de `platform`** : fonctions en `camelCase`, énumérations en `kPascalCase`, `WindowState` en `enum class`, tout dans
  `orosmo::platform` (table dans `docs/releases/M4.md`). Comportement inchangé.
- `createDevice(Backend::kVulkan)` crée un périphérique Vulkan (au lieu de `kUnsupported`).
- Dans le dépôt : `world`, `TerrainRenderer`, `terrain_gen`, `terrain_viewer`, `ui_gallery`, `core_demo`, `rhi_demo`, `--seed` et
  leurs cibles `make` disparaissent ; `make demo-capture` remplace `viewer-capture` et `gallery-capture`. Le paquet de release n'avait
  déjà plus `world` depuis `v0.3.1`.

## Décisions

- Appliquées : A31 (renommage de `platform`), A37 (démo unique, procédural archivé), A38 (registre : M12), A39 (M7 supprimé, effets
  préinstallés : M13), A40 (modèles `orosmo init`, `MOD-09`).
- Closes : A7, A8, A13 (le compilateur `cui` reste en Rust), A15, A41. A14 (CMake) reportée à M9. A10 : singleton
  `InputManager::getInstance` gardé.
- Versions visées : M5 `v0.5.0`, M6 `v0.6.0`, M8 `v0.7.0`, M9 `v0.8.0`, **M10 `v1.0.0`**, M11 `v1.1.0`, M12 `v1.2.0`, M13 `v1.3.0`.
- À confirmer par l'auteur : A21 (le batch 2D de M5 laisse UI-02 « Partiel » jusqu'en M5 ; proposée close).

## Commande de démo

```bash
make all-debug
apps/demo/bin/debug/main --backend vulkan        # ou --backend opengl ; --strict-backend : pas de repli
make demo-vulkan-check                           # mêmes images dans la tolérance, validation Vulkan à 0
```

## Tests

PREUVES_MR

## Non prouvé

Windows, macOS et Linux arm64 pour Vulkan (la CI le dira) ; un seul GPU mesuré (Intel Arc, Mesa ANV) ; présentation Vulkan avec
fenêtre vérifiée par deux captures seulement ; barrières conservatrices ; `make demo-vulkan-check` hors de la CI (`docs/GAPS.md`,
G129 à G143).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
