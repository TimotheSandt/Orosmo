# core

Couche 0 de `docs/03` : ne dépend d'aucun autre module. Code neuf en `include/engine/core/*.hpp`, `namespace engine::core`,
`camelCase`/`kPascalCase` ; le code hérité (`Logger`, `Profiler`, `Utilities`) migre en M1 bis.

| Fichier | ID | Rôle |
| --- | --- | --- |
| `error.hpp` | CORE-02 | `Error`, `Result<T>` (`std::expected`), `makeError`, `logError`, `ENGINE_ASSERT` |

Tests : `tests/core/`. Dépendances : bibliothèque standard, logger existant (`Utilities/Logger.h`).
