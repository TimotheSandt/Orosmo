# platform

Couche 1 de `docs/03` : ce qui parle au système d'exploitation. Archive : `libplatform.a` (`src/platform/`).

| Fichier | Rôle |
| --- | --- |
| `system.cpp` | Définit les services déclarés par `include/engine/core/system.hpp` : `terminalWidth`, `executablePath`, `userDataDirectory`, `preciseSleep`, `beginHighResolutionTimer` / `endHighResolutionTimer`. Une seule source, `#if` par système (Windows, macOS, Linux/autres). |

- **Pourquoi la déclaration est dans `core`** : `core` est la couche 0 et ne peut rien inclure au-dessus (règle 5). Il *déclare*
  le service dont il a besoin ; `platform` le *définit*. L'édition de liens relie les deux, et une plateforme sans
  implémentation échoue à l'édition de liens au lieu de se dégrader en silence.
- **Seul endroit** (avec les backends graphiques) qui inclut `<windows.h>`, `<mmsystem.h>`, `<shlobj.h>`, `<mach-o/dyld.h>`,
  `<sys/ioctl.h>` : `tools/check_api_isolation.py` le vérifie.
- **Dépendances** : bibliothèque standard et `engine/core/system.hpp`. Aucun appel graphique.
- **Pas encore ici** (M2) : `Window` (`src/Graphics/Window.cpp`) et `InputManager` (`src/Utilities/InputManager.cpp`) sont déjà
  rangés dans la couche `platform` par `tools/layers.json` mais restent dans leurs dossiers d'origine jusqu'à la migration.
- **Windows et macOS** : le code a été déplacé tel quel depuis `Logger`, `Utilities` et `FPSCounter`, sans réécriture ; il n'a
  pas pu être compilé sur Linux et se vérifie à la CI (`docs/GAPS.md`, G28).
- **Tests** : `tests/platform/system_tests.cpp`.
