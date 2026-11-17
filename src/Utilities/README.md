# Utilities

Briques de base : `Logger`, `InputManager`, macros et fonctions de `utilities.h` (dont `GET_RESOURCE_PATH`).

- **API publique** : `include/Utilities/`. Le `Logger` utilise `<stacktrace>` en build `DEBUG` : sous GCC 13, il faut lier `-lstdc++_libbacktrace` (GCC 14+ : `-lstdc++exp`) ; c'est fait dans `tools/engine-sdk/engine.mk`.
- **Dépendances** : bibliothèque standard, GLFW (`InputManager`).
- Le fichier d'en-tête est `utilities.h` (minuscule) alors que la source est `Utilities.cpp` : à unifier plus tard.
