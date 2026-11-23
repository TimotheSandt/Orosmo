# platform

Couche 1 de `docs/03` : ce qui parle au système d'exploitation. Archive : `libplatform.a` (`src/platform/`).

| Fichier | Rôle |
| --- | --- |
| `window.cpp` / `include/orosmo/platform/window.hpp` | `Window` (PLAT-01) : fenêtre GLFW **cachée** (poignée opaque `void *getWindow()`, `requestClose`, `getFramebufferSize` (gardée par rappel GLFW, sans aller-retour X11), `getContentScale` (DPI), `setCursorMode`, `get/setCursorPosition`, position gardée par rappel GLFW ; fonctions libres `framebufferSizeOf` et `cursorPositionOf` pour le RHI et `InputManager`), plein écran / sans bordure, compteur d'images. Ne connaît aucune API graphique : `init(beforeCreate)` appelle un rappel avant la création (l'application y pose `rhi::prepareWindowHints`), puis le RHI adopte la fenêtre (`SurfaceDesc::nativeWindow`). |
| `input_manager.cpp` / `include/orosmo/platform/input_manager.hpp` | `InputManager` (PLAT-02) : états par image, actions, dispositions de clavier, souris **et molette** (`getMouseScroll`, cumulée par rappel GLFW jusqu'au prochain `update`). `getInstance(void *nativeWindow)`. |
| `system.cpp` | Définit les services déclarés par `include/orosmo/core/system.hpp` : `terminalWidth`, `executablePath`, `userDataDirectory`, `preciseSleep`, `beginHighResolutionTimer` / `endHighResolutionTimer`. Une seule source, `#if` par système (Windows, macOS, Linux/autres). |

- **Pourquoi la déclaration est dans `core`** : `core` est la couche 0 et ne peut rien inclure au-dessus (règle 5). Il *déclare*
  le service dont il a besoin ; `platform` le *définit*. L'édition de liens relie les deux, et une plateforme sans
  implémentation échoue à l'édition de liens au lieu de se dégrader en silence.
- **Seul endroit** (avec les backends graphiques) qui inclut `<windows.h>`, `<mmsystem.h>`, `<shlobj.h>`, `<mach-o/dyld.h>`,
  `<sys/ioctl.h>` : `tools/check_api_isolation.py` le vérifie.
- **Dépendances** : bibliothèque standard et `orosmo/core/system.hpp`. Aucun appel graphique.
- **GLFW** : seul `src/platform/` l'inclut (hors tests) ; aucun en-tête public n'en expose un type. `check_api_isolation.py --strict` est bloquant, sans baseline.
- **Noms** (A31, début de M4) : tous les types publics (`Window`, `WindowParameters`, `WindowState`, `CursorMode`, `InputManager`, `KeyButton`, `MouseButton`, `InputState`, `KeyLayout`, `KeyPress`, `InputEvent`…) sont dans `orosmo::platform` ; fonctions et méthodes en `camelCase` (`Window::newFrame`, `InputManager::getInstance`), valeurs d'énumération en `kPascalCase` (`KeyButton::kEscape`, `WindowState::kWindowed`) ; `WindowState` est une `enum class`.
- **Windows et macOS** : le code a été déplacé tel quel depuis `Logger`, `Utilities` et `FPSCounter`, sans réécriture ; il n'a
  pas pu être compilé sur Linux et se vérifie à la CI (`docs/GAPS.md`, G28).
- **Tests** : `tests/platform/system_tests.cpp`, `tests/platform/window_input_tests.cpp` (sans affichage, ils s'arrêtent avec un avertissement ; CI sous xvfb).
