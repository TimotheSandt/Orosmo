# Utilities

Briques de base : `Logger`, `InputManager`, macros et fonctions de `Utilities.h` (dont `GET_RESOURCE_PATH`).

- **API publique** : `include/Utilities/`. Le `Logger` utilise `<stacktrace>` en build `DEBUG` : sous GCC 13, il faut lier `-lstdc++_libbacktrace` (GCC 14+ : `-lstdc++exp`) ; c'est fait dans `tools/engine-sdk/engine.mk`.
- **Dépendances** : bibliothèque standard, GLFW (`InputManager`), `engine/core/system.hpp` (largeur de console du `Logger`, chemin de l'exécutable et dossier de données ; l'implémentation par système est dans `src/platform/`).
- En-tête `Utilities.h` et source `Utilities.cpp` : même casse (G12 traité en M1 bis).
