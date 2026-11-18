# Utilities

Briques de base : `Logger`, `InputManager`, macros et fonctions de `Utilities.h` (dont `GET_RESOURCE_PATH`).

- **API publique** : `include/Utilities/`. Le `Logger` utilise `<stacktrace>` en build `DEBUG` : sous GCC 13, il faut lier `-lstdc++_libbacktrace` (GCC 14+ : `-lstdc++exp`) ; c'est fait dans `tools/engine-sdk/engine.mk`.
- **Dépendances** : bibliothèque standard, GLFW (`InputManager`).
- En-tête `Utilities.h` et source `Utilities.cpp` : même casse (G12 traité en M1 bis).
