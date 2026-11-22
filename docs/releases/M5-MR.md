# M5 : cœur du rendu (`dev` → `main`)

## Résumé

Jalon M5, version **`v0.5.0`**. Le moteur a un **cœur de rendu moderne** construit sur le RHI, **sans modifier le RHI ni les backends** : un **graphe de passes** avec **plugins** (`RenderFeature`), le bloc de **données par image** `FrameData` partagé C++/GLSL, l'éclairage **forward+ clusterisé**, les **ombres en cascade avec cache**, le **HDR** et les post-traitements (exposition automatique, bloom, tonemapping, LUT, vignettage), l'**anticrénelage** (FXAA, SMAA 1x, TAA), les **sorties auxiliaires** (AOV) à la demande, l'**instancing** avec **culling** CPU et **GPU** (Hi-Z, multi-draw indirect) et LOD, les **transparents triés** et les modes de fusion, le **batch 2D** et le mélange 2D/3D. L'interface passe par le batch 2D (de 143 à 18 draw calls), n'émet rien pour ce qui est caché ou hors découpage, et le carré d'un `ColorPicker` ouvre la roue de couleur. **Les images par défaut sont identiques à la tolérance 0** avant et après chaque branche, sur OpenGL et Vulkan ; chaque fonction s'active par une option de la démo. Détail : `docs/releases/M5.md`.

## Identifiants

17 identifiants passent à **Fait** (REN-01, 03, 04, 06, 07, 17, 18, 28, 29, 30, 32, 39 ; R2D-01, 02, 03, 05, 06). Restent **Partiel**, manque écrit dans la cellule de `docs/02` : REN-05, 08, 19, 21, 22, 31, 35, 37, 41 et UI-13. Totaux (162 lignes) : Fait 42, Partiel 40, Absent 61, Archivé 11, Hors moteur 7, Incompatible 1 (`docs/STATUS.md`). UI-13 et REN-22 sont Partiel : le cache par couche est reporté, et le critère de REN-22 cite l'atmosphère (module M13).

## Changements incompatibles

- `SceneRenderer::drawMesh` enregistre ; les dessins sont joués à `endFrame` (les tampons doivent rester valides jusque-là) ; image construite par le graphe de passes.
- Groupe 0 : `FrameData` en liaison 2, liaisons 3 à 7 réservées aux plugins ; `ModelBlock` de 128 à 208 octets ; convention de push constants de `mesh.frag` (pré-passe) ; jitter de la perspective de signe corrigé.
- Les ponctuelles et projecteurs sont maintenant rendus ; `shaders/include/` est une nouvelle racine d'inclusion.
- Aucun changement du RHI ni du paquet de release.

## Décisions

- Propositions **à confirmer par l'auteur** : A44 à A56 (`docs/QUESTIONS.md`) ; en particulier **A54** (reporter le cache par couche de l'interface à M6), **A50** (ajouts au RHI : alpha-to-coverage et MSAA, `drawIndexedIndirectCount`, `depthBias`, copie texture à texture, libération différée), A55 (jalon livré en une seule merge request).
- Version : M5 `v0.5.0` ; les fichiers de version restent à mettre à jour par l'auteur.

## Commande de démo

```bash
make all-debug
apps/demo/bin/debug/main --api vulkan --instances 100000 --gpu-driven on    # aussi : --shadows on, --lighting forwardplus --lights 1000, --post full, --aa taa
make demo-vulkan-check                                                      # mêmes images dans la tolérance, validation Vulkan à 0
```

## Tests

Linux x64, g++ 13.3, Mesa, Intel Arc, sur `milestone/M5` : `make demo-test` **545 / 545**, suites GPU en Vulkan **229 / 229** (316 cas hors GPU ignorés), `make check`, `make demo-vulkan-check` (validation à 0 message ; seul écart connu : `dark-scene` / `ui` à 0,58 % des pixels au-delà de la tolérance 8, antérieur à M5, G139), `make demo-capture`. ASan + UBSan (après nettoyage complet) : **545 / 545**, aucun signalement. Clone propre (`df005c7`) : `make all-debug`, `demo-test` 545 / 545, Vulkan 229 / 229, `check`, `demo-vulkan-check`, `demo-capture`, `cui-test`, `e2e-init`, tests Python verts.

Mesures : interface 143 → 18 draw calls ; 10 000 instances en 2 draw calls ; 100 000 objets avec culling GPU, passe `scene` 9,2 → 5,2 ms (OpenGL) et 6,0 → 4,7 ms (Vulkan) ; forward+ avec 1000 lumières, passe `scene` 93,6 → 5,7 ms (OpenGL) et 37,5 → 2,9 ms (Vulkan) (debug -O1, Intel) ; cache d'ombres : 0 redessin de la géométrie fixe après la première image.

## Non prouvé

Windows, macOS et Linux arm64 (la CI le dira) ; un seul GPU (Intel Arc, Mesa) ;  écart d'un pixel entre forward et forward+ ; libération de ressources pendant l'enregistrement (`FullscreenBlit`, `Mesh::setTextures`) ; limites du rendu piloté par GPU, des ombres, du HDR, de l'anticrénelage et du batch 2D ; `clang-format` non installé (`docs/GAPS.md`, G151 à G171).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
