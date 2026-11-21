# BASELINE : référence de comportement avant nettoyage (M00)

Date : 2026-11-17. État de référence : tag `pre-cleanup` (`899c349`, tête de `dev`), construit sur Linux dans une copie propre (`git archive pre-cleanup`), hors dépôt.

## Résultat

| Élément | Résultat |
| --- | --- |
| Moteur (`make debug`, `make release`) | compile |
| Démo (`make demo-debug`) | compile ; démarre en OpenGL, terrain généré (498 002 triangles), boucle de jeu atteinte (`run-opengl.txt`) |
| Tests (`make test` dans `demo/`, build release) | **102/102** verts : Utilities 10, GraphicsAPI 14, GraphicsCore 24, Renderers 5, ShaderProgram 4, Noise 10, Procedural 8, Profiler 8, Bindings 5, UI 14 (`tests.txt`) |
| Avertissements `-Wall -Wextra` | 40 lignes `warning:` dans la sortie du build des tests (`BindMethods.h`/`BindFunc.h` : « missing initializer ») ; 6 dans le build de vérification final (debug+release+démo) |
| Ressources copiées dans `bin/<type>/res/` | `fonts/`, `public/`, `shader/` ; 8 fichiers de shader |
| Capture d'écran | **non réalisée** : la capture X11 a échoué (`import -window root` : « Ressource temporairement non disponible », session probablement Wayland/XWayland). À faire à la main si tu veux une image de référence. |
| Vulkan | non lancé (la démo démarre par défaut en OpenGL) ; couches de validation non vérifiées |

## Correctifs de portabilité nécessaires pour construire sur Linux

Absents de `pre-cleanup`, appliqués localement pour cette mesure, puis commités (branche `chore/cleanup-m00`) :

1. `include "Utilities.h"` → `"utilities.h"` dans `UI/Core/Container.h` (casse).
2. `-lmsdfgen-core` manquant dans la liste de liens Linux de `engine-sdk/engine.mk`.
3. `<stacktrace>` (Logger, build DEBUG) : `-lstdc++_libbacktrace` (GCC 13) ou `-lstdc++exp` (GCC 14+).
4. Dépendances : `stb_image.h` est dans `/usr/include/stb/` avec apt ; vcpkg le place à la racine des includes. `tools/setup-linux-deps.sh` relie les deux.

## Environnement

| Élément | Valeur |
| --- | --- |
| OS | Linux 7.2.15, Pop!_OS 24.04 LTS, x86_64 |
| Compilateur | g++ 13.3.0 ; clang++ absent |
| GPU / pilote | Intel Arc Graphics (Meteor Lake-P), Mesa 26.1.6 (KMD Xe, support « expérimental ») |
| Vulkan | instance 1.3.280, appareil 1.4.354 |
| OpenGL | rendu accéléré, profil core, Mesa 26.1.6 |
| Dépendances | apt : glfw 3.3.10, glm 0.9.9.8, stb (2023-01), shaderc 2023.8, freetype, vulkan 1.3.280 ; vcpkg (`~/vcpkg`) : glad, msdfgen |
| Système de fichiers | NTFS via `fuseblk` ; `core.ignorecase` remis à `false` |

## Fichiers

- `tests.txt` : résumé de la suite de tests.
- `run-opengl.txt` : début du journal de la démo (40 lignes, horodatage retiré).

## Comparaison après nettoyage

Voir `docs/releases/M00.md` : mêmes 102 tests, mêmes ressources (somme MD5 identique de `res/`), même journal de démarrage.

## Mise à jour M0 (2026-11-17) : image de référence et sorties

| Fichier | Contenu |
| --- | --- |
| `reference/scene-opengl-seed1-1280x720.png` | Scène 3D OpenGL, graine 1, 1280x720, 10 images, Intel Arc (Mesa 26.1.6). sha256 `8937f52b311633aafddf834a12ca5bf08ae0bb99c963511fc24bc0b09478974f`. **Remplacée le 2026-11-20** (voir plus bas : terrain `engine::world`) |
| `tests-M0.txt` | Résumé doctest de `make demo-test` depuis un clone propre de `dev` : 118 cas, 647 assertions, 0 échec |

## Mise à jour du 2026-11-19 : texte posé sur sa ligne de base

`reference/ui-opengl-fixedtext-1280x720.png` (parité de l'interface héritée) a été corrigée ce jour-là (texte posé sur sa ligne de
base, espacement régulier : `tests/ui/text_tests.cpp`). **Supprimée le 2026-11-20** avec l'interface héritée (`include/UI/`) et
`tests/renderer/parity_tests.cpp` qui l'utilisait : la démo dessine maintenant son panneau avec `engine::ui`, il n'y a plus de rendu
hérité à reproduire. Le panneau est vérifié par `tests/apps/demo_performance_tests.cpp` (contenu, thème, rafraîchissement).

Régénérer et comparer :

```bash
apps/demo/bin/debug/main --api opengl --capture /tmp/capture          # scene.png + ui.png
python3 tools/compare_png.py docs/BASELINE/reference/scene-opengl-seed1-1280x720.png /tmp/capture/scene.png
```

Vérifié : 3 captures consécutives identiques (même sha256), 5 ou 30 images donnent la même image, `--seed 7` donne une autre image, et l'image produite après les correctifs de M0 (PCG défini, tests doctest, `-Werror`) est **identique pixel pour pixel** à la référence (`compare_png.py` : 0 pixel, écart max 0).

Limites : la référence vient d'une seule machine ; un autre GPU ou pilote (ou llvmpipe en CI) peut différer de quelques pixels, d'où la tolérance dans la CI. `ui.png` n'est pas une référence (FPS affichés). Vulkan : capture refusée jusqu'en M4 (voir `docs/GAPS.md` G29). Pour la démo sans capture, `docs/BASELINE/run-opengl.txt` reste le journal de démarrage de référence.

## Mise à jour du 2026-11-20 : la démo sur `engine::world`

La démo ne génère plus son terrain avec l'ancien module `Noise` (grille fixe de 500 x 500 m) mais avec `engine::world`
(tuiles diffusées autour de la caméra par `TileStreamer`, dessinées par `TerrainRenderer`, graine 1 en capture). La caméra part
40 m au-dessus du sol en (0, 0), regard vers -z légèrement plongeant ; la lumière directionnelle garde sa couleur et son
intensité, sa direction est celle des shaders (vers la lumière). L'ancienne image (caméra presque au ras du sol, terrain sombre)
n'a plus de sens : `reference/scene-opengl-seed1-1280x720.png` est remplacée.

| Élément | Valeur |
| --- | --- |
| Commande | `LIBGL_ALWAYS_SOFTWARE=1 apps/demo/bin/debug/main --api opengl --headless --capture <dossier> --capture-frames 5` |
| Rendu | Mesa llvmpipe (LLVM 20.1.2, 256 bits), OpenGL 4.6 core, Mesa 26.1.6 : le même pilote logiciel que la CI (Xvfb) |
| sha256 | `2c4746d3ba0e9cb959da0682562d98482d09b0f228bd7763c92b27489ee8ef14` |

Vérifié : deux captures de 5 images et une de 30 images donnent le même fichier (même sha256) ; `--seed 7` donne une autre image
(73 % des pixels diffèrent) ; la même capture sur le GPU (Intel Arc, Mesa 26.1.6, sans `LIBGL_ALWAYS_SOFTWARE`) reste dans la
tolérance de la CI (`compare_png.py --tolerance 8 --max-diff-percent 2` : 0 pixel au-delà, écart max 3/255). En capture et sans
fenêtre, chaque image attend que toutes les tuiles visibles soient générées : l'image ne dépend pas de la vitesse des threads.

`reference/ui-opengl-fixedtext-1280x720.png` n'est plus vérifiée : son test (`tests/renderer/parity_tests.cpp`, parité de
l'interface héritée) est supprimé. Le fichier reste pour l'historique (`docs/releases/M2-B5.md`, `M3.md`).


## Mise à jour du 2026-11-21 : démo unique (diorama 3D statique, A37)

Le module procédural est archivé dans `archive/procedural-world` et remplacé par une unique démo générique (`apps/demo/`, diorama
3D statique en C++ + vitrine complète d'`orosmo::ui`). `reference/scene-opengl-seed1-1280x720.png` est remplacée par
`reference/scene-opengl-demo-1280x720.png`.

| Élément | Valeur |
| --- | --- |
| Commande | `LIBGL_ALWAYS_SOFTWARE=1 apps/demo/bin/debug/main --api opengl --headless --ui off --capture <dossier> --capture-frames 5` |
| Rendu | Mesa llvmpipe (OpenGL 4.6 core, Mesa 26.1.6 ; identique à la CI Xvfb) ; GPU local testé : Intel Arc Graphics (MTL), Mesa 26.1.6 |
| sha256 | `c020d4a1b89b4d079e8946afeef5ae8c2028c628a15394ea2ad28f7fb05bcb7e` |

Vérifié : deux captures consécutives sur llvmpipe produisent exactement le même sha256 (`c020d4a1b89b4d079e8946afeef5ae8c2028c628a15394ea2ad28f7fb05bcb7e`), deux captures consécutives sur
GPU produisent exactement le même sha256 (`8791b770970b6d3144f92a8ea8ccb0893c329c681ce45dc397b04e2bbd3f3b74`), la capture fenêtrée est identique pixel pour pixel à la capture `--headless`
(0 pixel d'écart), et la comparaison llvmpipe vs GPU reste dans la tolérance CI (`--tolerance 8 --max-diff-percent 2`).

## Mise à jour du 2026-11-21 (M4) : parité d'image OpenGL / Vulkan de la démo

Pas de seconde image de référence : la **référence reste OpenGL**, et Vulkan est comparé à OpenGL à chaque exécution.

```bash
make demo-vulkan-check            # 5 cas (off, hud, dark/light widgets, dark scene) ; FULL=1 : tous les onglets et fenêtres de demo-capture
# équivalent à : python3 tools/demo_parity.py --bin apps/demo/bin/debug/main --out build/demo-parity
```

- Tolérance : `--tolerance 8 --max-diff-percent 2` (celle de llvmpipe contre un GPU matériel), appliquée à `scene.png` et `ui.png`.
- Résultat sur Intel Arc (Mesa 26.1.6, ANV contre Iris) : `scene.png` **identique bit à bit** (0 pixel, écart max 0) dans toutes les captures ; `ui.png` identique sauf les captures qui affichent le nom du pilote (onglet scène, dialogue) : ~5 300 pixels (0,58 %) qui sont le texte « Vulkan 1.4... » contre « 4.6 (Core Profile)... », pas du rendu.
- Résultat sur lavapipe (`ENGINE_VULKAN_DEVICE=llvmpipe`, contre OpenGL Iris) : 38 pixels (0,004 %, arêtes de lignes de debug, écart max 237) hors tolérance 8 sur la scène ; ui : 0,003 % hors le texte du pilote.
- La cible exige aussi que le journal Vulkan de chaque exécution contienne `Vulkan validation: 0 error/warning message(s)` (validation de synchronisation active, binaire debug).
- `docs/BASELINE/reference/scene-opengl-demo-1280x720.png` : la capture OpenGL actuelle en diffère de 332 pixels (0,036 %, écart max 237 : arêtes) ; la référence est peut-être à régénérer (à vérifier, non modifiée ici).
- Performances comparées : `docs/PERF_OPENGL_VS_VULKAN.md`.
