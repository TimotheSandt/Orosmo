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
| `reference/scene-opengl-seed1-1280x720.png` | Scène 3D OpenGL, graine 1, 1280x720, 10 images, Intel Arc (Mesa 26.1.6). sha256 `8937f52b311633aafddf834a12ca5bf08ae0bb99c963511fc24bc0b09478974f` |
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
