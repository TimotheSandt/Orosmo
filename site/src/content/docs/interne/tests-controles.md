---
title: "Tests, contrôles et images de référence"
description: "Lancer les tests (unitaires et GPU), les sanitizers et les contrôles du dépôt, produire et comparer des images de référence, et ce que fait la CI."
sidebar:
  order: 8
---

Une modification est terminée quand la compilation passe sans avertissement (`-Werror`), que les tests et les sanitizers
sont verts, que les couches de validation graphique ne signalent rien et que `make check` passe. Cette page donne les
commandes de chaque étape.

## Tests

Les tests utilisent [doctest](https://github.com/doctest/doctest) et sont rangés par module dans `tests/<module>/`
(`tests/core/`, `tests/rhi/`, `tests/renderer/`, `tests/ui/`...). Ils sont compilés dans un seul exécutable, avec les
archives release du moteur.

| Commande | Effet |
| --- | --- |
| `make demo-test` | Tous les tests. |
| `make demo-test-unit` | Seulement ceux qui n'ont besoin ni de GPU ni de fenêtre. |
| `make demo-test-gpu` | Seulement l'intégration graphique (suites de `GPU_TEST_SUITES`). |
| `make demo-test SANITIZE=address,undefined` | Sous ASan et UBSan ; partir d'un arbre propre (`make fclean`). |
| `make cui-test` | Compilateur `.cui` : `cargo test`, entrées invalides, erreurs C++ ramenées au `.cui`. |
| `python3 -m unittest discover -s tests/tools` | Outils Python (`compare_png.py`, barre de progression...). |

Les suites GPU sont listées dans `GPU_TEST_SUITES` du `Makefile` : `rhi/opengl*`, `rhi/vulkan*`, `rhi/backend*`,
`renderer/*`, `platform/window`, `ui/render`. Une nouvelle suite qui ouvre un périphérique doit y être ajoutée.

Les options de doctest passent par `TEST_ARGS`, par exemple pour lancer une seule suite :

```bash
make demo-test TEST_ARGS='--test-suite=core/config'
```

| Variable | Effet |
| --- | --- |
| `ENGINE_TEST_BACKENDS=opengl,vulkan` | Backends testés par les suites communes ; sans elle, tous ceux qui sont compilés. |
| `ENGINE_TEST_REQUIRE_GPU=1` | Un test GPU sans pilote échoue au lieu d'être sauté (utilisé en CI). |
| `LIBGL_ALWAYS_SOFTWARE=1` | Force le rendu logiciel de Mesa (llvmpipe), comme en CI. |

Sous Linux sans affichage, les tests GPU tournent sous Xvfb :

```bash
LIBGL_ALWAYS_SOFTWARE=1 ENGINE_TEST_REQUIRE_GPU=1 xvfb-run -a -s "-screen 0 1280x720x24" make demo-test
```

### Écrire un test

Un test vérifie un comportement observable et, pour le GPU, finit par `CHECK(device->validationErrorCount() == 0)`. Les
aides de `tests/support/` évitent les comportements indéfinis sur un `Result` :

```cpp
#include "support/result_support.hpp"

auto r = config.parseArgs({"--window.width", "wide"});
CHECK((tests::errorOf(r) == ErrorCode::kParse));        // échoue proprement si r est un succès
auto value = tests::valueOf(config.get<int>("window.width"));   // REQUIRE avec le message d'erreur
```

Un test GPU propre à un backend se saute si le backend n'est pas compilé ou n'a pas de pilote
(voir [Ajouter un backend](/interne/ajouter-un-backend/#tester)). Une capacité optionnelle absente (calcul, minuteries)
saute aussi le test qui en dépend, en le disant.

## Contrôles du dépôt

```bash
make check
```

Lance `check-repo` (binaires suivis, collisions de casse, CRLF), `check-layers` (dépendances entre couches), `check-api`
(isolation des API graphiques) et `check-headers` (chaque en-tête public se compile seul). Détail :
[Couches et modules](/interne/couches/#contrôles-automatiques).

Formatage : `sh tools/format_sources.sh` (clang-format requis).

## Images de référence

La démo sait capturer ses images sans intervention : `scene.png` (la scène 3D) et `ui.png` (l'image finale avec
l'interface).

```bash
apps/demo/bin/debug/main --api opengl --headless --capture /tmp/out --capture-frames 5
python3 tools/compare_png.py docs/BASELINE/reference/scene-opengl-demo-1280x720.png /tmp/out/scene.png \
    --tolerance 8 --max-diff-percent 2 --diff-out /tmp/out/diff.png
```

| Option de `compare_png.py` | Défaut | Effet |
| --- | --- | --- |
| `--tolerance T` | 0 | Écart maximal par canal (0-255) ; un pixel diffère si un canal dépasse `T`. |
| `--max-diff-pixels N` | 0 | Nombre de pixels différents toléré. |
| `--max-diff-percent P` | — | Même limite, en pourcentage. |
| `--diff-out FICHIER` | — | Image des écarts : noir = identique, rouge = écart amplifié. |

Le script sort avec 1 si les tailles diffèrent ou si la limite est dépassée (ligne `DIFFERENT`), 0 sinon. Les références
sont dans `docs/BASELINE/reference/` (scène de la démo, ombres, post-traitements, scène mixte). La tolérance 8 / 2 % est
celle de llvmpipe contre un GPU matériel.

| Commande | Effet |
| --- | --- |
| `make demo-capture` | Captures sans fenêtre de la démo (HUD, chaque onglet, thèmes, menus, post-traitements, scène animée) ; compare les post-traitements, la scène mixte et l'ombre tramée à leurs références. |
| `make demo-vulkan-check` | `tools/demo_parity.py` : chaque cas en OpenGL puis en Vulkan, `scene.png` et `ui.png` comparés, et 0 message de validation Vulkan exigé. `FULL=1` ajoute les cas de `demo-capture`. |

Les mesures de performance comparées se font avec `tools/perf_compare.py` ([Mesurer](/guides/noyau/profilage/)).

## Intégration continue

`.github/workflows/ci.yml` à chaque poussée :

| Tâche | Contenu |
| --- | --- |
| Linux x64 | `make check`, build debug `-Werror`, `make demo-test` (Mesa llvmpipe et lavapipe sous Xvfb), `make cui-test`, capture et comparaison à la référence, capture sans fenêtre identique à la capture fenêtrée, tests Python, `make demo-capture`, `make e2e-init` (paquet, `orosmo init`, compilation, lancement). |
| Linux arm64 | Build debug et `make demo-test`. |
| Linux x64 ASan + UBSan | `make demo-test SANITIZE=address,undefined`. |
| Windows x64 (MinGW), macOS arm64 | Build debug et `make demo-test` par vcpkg, `make e2e-init` sans lancement graphique (`E2E_RUN_GRAPHICS=0`) ; bloquants. Sans `ENGINE_TEST_REQUIRE_GPU`, un test GPU sans pilote y est sauté. |

Les pilotes logiciels de la CI n'ont pas toutes les capacités d'un GPU : un test qui ne passe qu'en CI se reproduit avec la
même version de Mesa et `LIBGL_ALWAYS_SOFTWARE=1`. Le GPU, le pilote et les mesures de référence sont notés dans
`docs/BASELINE/`.

## Limites

- RHI-14 (Partiel) : la comparaison OpenGL/Vulkan est automatisée (`make demo-vulkan-check`), mais une seule référence
  OpenGL sert aux deux backends et la comparaison n'est ni dans `make demo-test` ni en CI (`docs/GAPS.md`, G143).
- BLD-01 et BLD-02 restent Partiel dans `docs/02` : les tests graphiques et les captures ne tournent en CI que sous Linux.
- Metal n'est pas testé (backend absent).

## Voir aussi

- [Ajouter un backend](/interne/ajouter-un-backend/)
- [Couches et modules](/interne/couches/)
- [Make, engine.mk et CMake](/reference/construction/)
