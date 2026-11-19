# M2 : RHI, OpenGL, couche de rendu et plateforme (`dev` → `main`)

## Résumé

Le moteur n'appelle plus aucune API graphique ni GLFW hors de `src/rhi/` et `src/platform/`. Une interface **RHI** (`engine::rhi`, handles à génération, encodeurs de commandes, modèle de liaison à 3 groupes) avec un backend **OpenGL 4.3** ; un pipeline de shaders GLSL 450 (shaderc → SPIR-V → SPIRV-Cross, cache disque, rechargement à chaud) ; une couche `engine::renderer` (ressources, scène, dessin 2D par listes de dessin) ; la démo et l'interface migrées dessus ; `Window` et `InputManager` dans `platform` avec GLFW caché ; mode `--headless`. L'ancien rendu (OpenGL, Vulkan, Metal) est retiré (−16 800 lignes) et archivé sous l'étiquette `archive/pre-M2-B6-legacy-graphics` ; le backend Vulkan revient en M4. Détail : `docs/releases/M2.md`, `M2-lotA.md`, `M2-B2.md` à `M2-B6.md`, `docs/RHI.md`.

## Identifiants

Voir `docs/STATUS.md` (sections M2). Lot C : PLAT-01 Partiel, PLAT-02 Partiel (molette faite), PLAT-04 Absent → Partiel, RHI-08 Partiel.

## Décisions de l'auteur appliquées

A1 (ancien Vulkan archivé, réécrit en M4), A27 (M2 en lots A/B/C), A28 (réécriture explicite des façades). Question ouverte avant M3 : A26 (convention de coordonnées du RHI).

## Commande de démo

```bash
make all-debug && make run
apps/demo/bin/debug/main --api opengl --headless --capture /tmp/out
```

## Tests

`make demo-test` : voir `docs/BASELINE/tests-M2-lotC.txt` (cas doctest verts) ; ASan + UBSan propres ; `make check` : 0 violation (couches, isolation des API sans baseline, en-têtes) ; capture fenêtrée et sans fenêtre identiques à la référence.

## À vérifier à la relecture

- CI Windows (MinGW), macOS et arm64 : premiers passages des tests de fenêtre et d'entrées (`tests/platform/window_input_tests.cpp`, ils s'arrêtent avec un avertissement sans affichage).
- Le pas de CI « Headless capture » (Linux x64).
- Rien n'est supposé sur Vulkan : `--api vulkan` répond « not available yet (Vulkan: M4) ».

🤖 Generated with [Claude Code](https://claude.com/claude-code)
