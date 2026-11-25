# M6 : assets, matériaux PBR, IBL et animation (`milestone/M6` → `main`)

## Résumé

Jalon M6, version **`v0.6.0`**. Le moteur lit et prépare des contenus, les rend en PBR et les anime, par **deux modules neufs** : `assets` (`libassets.a`) et `anim` (`libanim.a`), et trois dépendances vcpkg (`cgltf`, `ktx`, `meshoptimizer`). **Import et export glTF 2.0** en données CPU ; **textures** sRGB ou linéaires selon l'usage, **KTX2** (BC7 ou ASTC selon `Capabilities`, repli RGBA8, transcodage Basis) ; **cuisson** de maillages (meshoptimizer, LOD, quantification, `.omesh`) ; **rechargement à chaud** des textures ; **matériaux PBR** métal/rugosité avec variantes et instances ; **IBL** (harmoniques sphériques, cube préfiltré en compute, table BRDF, sondes) ; **skinning GPU**, clips, mélange, machine à états, animation de propriétés ; **foules** instanciées (1000 agents en un draw call par primitive skinnée). Le RHI gagne les formats compressés `kBC7*` et `kASTC4x4*` (`Capabilities::bc7Textures`, `astcTextures`). Les images par défaut ne changent pas ; la nouveauté s'active par `--scene anim`. Détail : `docs/releases/M6.md`, formats : `docs/FORMATS.md`.

## Identifiants

10 identifiants passent à **Fait** (ASSET-01, 02, 03, 04 ; REN-10, 13 ; ANIM-01, 02, 03, 04). Restent **Partiel**, manque écrit dans la cellule de `docs/02` : REN-11 (pas de constantes de spécialisation, A71) et ASSET-05 (textures seulement). Totaux (167 lignes) : **Fait 52, Partiel 41, Absent 55**, Archivé 11, Hors moteur 7, Incompatible 1 (`docs/STATUS.md`). Avant : Fait 42, Partiel 40, Absent 66.

## Changements incompatibles

- Aucun changement de comportement par défaut. Ajouts au RHI : `Capabilities::bc7Textures`, `astcTextures`, quatre `Format` compressés, `FormatInfo::blockExtent` et `bytesPerBlock`.
- `vcpkg.json` : `cgltf`, `ktx`, `meshoptimizer` (relancer `vcpkg install`) ; `tools/layers.json` : couches `assets` et `anim`.
- Les chemins instancié et piloté par GPU gardent le matériau provisoire `InstanceMaterial` (G195).

## Décisions

- Propositions **à confirmer par l'auteur** : **A70** (huit lots, modules `assets` et `anim`, bibliothèques choisies, modèles de test générés par le code, UI-13 reporté à M8), **A71** (variantes par définitions de préprocesseur, REN-11 reste Partiel ; constantes de spécialisation dans le RHI = décision d'architecture non appliquée), **A72** (la BRDF PBR est multipliée par π ; lumières glTF hors unités physiques).
- Version : M6 `v0.6.0` ; fichiers de version mis à jour dans `chore/M6-release`.

## Commande de démo

```bash
make all-debug
apps/demo/bin/debug/main --scene anim      # personnage glTF skinné et animé, foule de 1000 agents, texture rechargée à chaud
make demo-vulkan-check                     # mêmes images dans la tolérance, validation Vulkan à 0
```

## Tests

Linux x64, g++ 13.3, Mesa 26.1.6, Intel Arc, **clone propre** de `chore/M6-release` (`b54f89d`) : `make all-debug` sans avertissement ; `make demo-test` **679 cas** (678 verts, 1 ignoré, 1 rouge : le cas de fenêtre `PLAT-01` connu instable, la suite relancée seule passe 6 / 6), 122 268 assertions ; ASan + UBSan (après nettoyage complet) : même résultat, aucun signalement ASan, **3 messages UBSan** `platform/window.hpp:34` (booléen non initialisé de `WindowParameters`, code non touché par M6). **Corrigé ensuite** (`fix/M6-window-params`, `feab270`) : membres de `WindowParameters` initialisés, test « G201 » ; relancé dans le clone propre sous ASan + UBSan : **680 cas, 679 verts, 1 ignoré, 1 rouge (le même cas de fenêtre instable), 0 message ASan, 0 message UBSan** ; `make demo-test` dans le dépôt : **680 / 680** (1 ignoré). ; `make check` (0 violation de couche, 0 appel d'API graphique hors du RHI, 100 en-têtes autonomes), `make demo-capture`, `make demo-vulkan-check` (validation silencieuse ; captures de la scène `anim` à 2/255 entre OpenGL et Vulkan), `make cui-test`, tests Python verts.

Mesures (Intel Arc, release, médiane de 30 images) : skinning compute d'un personnage de 10 147 sommets 0,054 ms (Vulkan) ; image avec 1000 agents (10,1 M sommets skinnés, 2 draw calls) 14,7 à 17,8 ms (OpenGL) et 15,1 à 18,5 ms (Vulkan), soit environ 1,5 ns par sommet ; personnage par défaut de la démo avec 1000 agents sous 1,3 ms GPU par image (`docs/PERF_OPENGL_VS_VULKAN.md`).

## Non prouvé

Windows, macOS et Linux arm64 (la CI le dira) ; un seul GPU (Intel Arc, Mesa) ; replis ASTC et RGBA8 de KTX2 vérifiés sur CPU seulement (le GPU de test prend BC7) ; pas d'exécutable de cuisson ; rechargement des textures seulement, pas des modèles glTF ; limites du PBR, de l'IBL, de l'animation et des foules (`docs/GAPS.md`, G195 à G201).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
