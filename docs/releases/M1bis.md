# M1 bis : remise en ordre des couches (`dev` → `main`)

Aucune fonctionnalité nouvelle : les dépendances sont remises dans le bon sens pour que M2 (RHI) démarre sur des couches propres. Le comportement ne change pas, sauf un cas limite voulu (G62).

## Résultat

| Contrôle | Avant | Après |
| --- | --- | --- |
| `python3 tools/check_layers.py --strict` | 5 violations (`Profiler` → RHI, `Window` → RHI ×4) | **0** ; `tools/baseline/layers.txt` vide |
| `python3 tools/check_headers.py --strict` | 1 en-tête non autonome (`IFrameGenerationMode.h`) | **0** |
| `<windows.h>` dans `core` | `Logger`, `Utilities`, `FPSCounter` | **aucun** (seulement `src/platform/system.cpp`, `Window.cpp`, `InputManager.cpp`, backends) |
| `tools/check_api_isolation.py` | 13 fichiers | 11 fichiers (G63, M2) |
| `make demo-test` | 214 cas | **224 cas**, 3348 assertions, 0 échec |
| ASan + UBSan | | 224 cas verts, aucun rapport |
| Capture OpenGL graine 1 | | identique à la référence (écart max 0/255) |

## Ce qui a changé

| Branche | Contenu |
| --- | --- |
| `fix/M1bis-header-autonomous` | `IFrameGenerationMode.h` : déclaration anticipée de `GraphicsCapabilities` |
| `chore/M1bis-naming` | G12 : `utilities.h` → `Utilities.h` ; G13 : `Graphics/Backend/GraphicsAPI.h` → `Backends/` (`git mv` séparés des `#include`) |
| `feat/M1bis-profiler-gpu-timer-port` | le `Profiler` n'inclut plus le RHI : `IGpuTimer` / `IGpuTimerFactory` (`include/Profiler/GpuTimer.h`), installés par `BindGraphicsRuntime` |
| `feat/M1bis-window-graphics-binding` | `Window` n'inclut plus le RHI : `IWindowGraphicsBinding` (`include/Graphics/Presentation/`), installé par `BindGraphicsRuntime` |
| `feat/M1bis-system-port` | `include/engine/core/system.hpp` (déclaré dans `core`) + `src/platform/system.cpp` (nouvelle archive `libplatform.a`) ; `Logger`, `Utilities`, `FPSCounter`, `Vfs` n'appellent plus le système ; G59 |
| `ci/M1bis-strict-checks` | la CI vérifie couches et en-têtes en mode strict |
| `docs/M1bis` | README des modules (`src/platform/README.md` nouveau), STATUS, GAPS (G61-G63), QUESTIONS (A24, A25) |

## Décisions à valider (docs/QUESTIONS.md)

- **A24** : `core` ne peut pas inclure `platform` ; il *déclare* le service (`system.hpp`) et `platform` le *définit*. `std::filesystem` reste dans `core`.
- **A25** : les couches du bas définissent l'interface, le RHI l'installe au `BindGraphicsRuntime`.

## Commande de démo

```bash
make check                       # couches : 0 violation, en-têtes : 0 non autonome, isolation des API : 11 fichiers (baseline)
python3 tools/check_layers.py --strict && python3 tools/check_headers.py --strict   # sortie 0
make all-debug && apps/demo/bin/debug/main --api opengl --capture /chemin/sortie
python3 tools/compare_png.py docs/BASELINE/reference/scene-opengl-seed1-1280x720.png /chemin/sortie/scene.png
```

## Résultat des tests (Linux, g++ 13.3)

```
make demo-test                              -> 224 cas, 3348 assertions, 0 échec (docs/BASELINE/tests-M1bis.txt)
make demo-test SANITIZE=address,undefined   -> 224 cas verts, aucun rapport ASan/UBSan
make all-debug                              -> 0 avertissement (-Wall -Wextra -Wpedantic -Werror)
check_layers --strict / check_headers --strict -> sortie 0
capture OpenGL graine 1                     -> identique à la référence (écart max 0/255)
```

Nouveaux tests : `tests/suites/profiler_gpu_timer_tests.cpp` (3), `tests/suites/window_graphics_binding_tests.cpp` (2), `tests/platform/system_tests.cpp` (5).

## Limites connues

- **Windows et macOS** : le code de `src/platform/system.cpp` a été déplacé tel quel mais n'a été compilé que sous Linux (G61). La CI du push le vérifie.
- **Un changement de comportement** : un `XDG_DATA_HOME` vide compte comme non défini (G62).
- **Isolation des API** : 11 fichiers restent en infraction (GLFW dans `Window`, `InputManager`, `Camera`, `RenderState`, `WindowContext` ; `<windows.h>` dans `Window.cpp` et `InputManager.cpp`) : ils migrent dans `src/platform/` et `src/rhi/` en M2 (G63).
