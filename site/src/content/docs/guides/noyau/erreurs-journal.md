---
title: "Erreurs et journal"
description: "Renvoyer et traiter une erreur avec Result<T> (std::expected) sans exception, écrire dans le journal, poser une assertion."
sidebar:
  order: 1
---

Une fonction du moteur qui peut échouer renvoie `orosmo::core::Result<T>`, c'est-à-dire `std::expected<T, Error>`. Il n'y a
pas d'exception dans le flux normal. Le journal (`Logger` et macros `LOG_*`) affiche les messages dans la console et les
écrit dans un fichier.

En-têtes : `orosmo/core/error.hpp` et `orosmo/core/log.hpp`.

## Exemple minimal

```cpp title="main.cpp"
#include <orosmo/core/error.hpp>

using namespace orosmo::core;

Result<int> parsePositive(int v)
{
    if (v < 0)
        return makeError(ErrorCode::kOutOfRange, "negative");   // échec : code + message
    return v;                                                    // succès : la valeur
}

Result<> mayFail(bool fail)                                      // Result<> = succès sans valeur
{
    if (fail)
        return makeError(ErrorCode::kIo, "disk");
    return {};
}

int main()
{
    auto r = parsePositive(-1);
    if (!r)
    {
        logError(r.error());     // journal, niveau ERROR : "kOutOfRange: negative (main.cpp:8)"
        return 1;
    }
    return *r;
}
```

Adapté de [`tests/core/error_tests.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tests/core/error_tests.cpp).

## Ce que contient une erreur

`Error` a trois champs : `code` (`ErrorCode`), `message` (texte libre, en anglais dans le moteur) et `where`
(`std::source_location` de l'appel à `makeError`, rempli tout seul). `describe()` les met sur une ligne :
`kNotFound: res://a.json (src/x.cpp:12)`. `toString(code)` donne le nom du code seul.

| Code | Sens |
| --- | --- |
| `kUnknown` | Valeur par défaut, à éviter. |
| `kInvalidArgument` | Paramètre refusé (taille nulle, liaison déjà prise, chemin qui remonte avec `..`). |
| `kNotFound` | Fichier, clé ou ressource absente. |
| `kAlreadyExists` | Nom déjà pris (par exemple un plugin de rendu du même nom). |
| `kIo` | Lecture ou écriture du disque qui échoue. |
| `kParse` | Texte, JSON ou type de valeur invalide. |
| `kUnsupported` | Fonction absente sur ce périphérique ou cette plateforme (capacité manquante, backend non compilé). |
| `kOutOfRange` | Valeur hors plage, index périmé. |
| `kCancelled` | Opération annulée (ressource libérée pendant un rechargement). |
| `kBusy` | Rien à faire pour l'instant : `Device::beginFrame` sur une fenêtre réduite ; l'appelant saute l'image. |
| `kVersionMismatch` | Document d'une version plus récente, ou sans migration. |
| `kDeviceLost` | Périphérique graphique perdu (pilote réinitialisé, GPU retiré) : il n'est plus utilisable. |

## Propager une erreur

Une fonction qui reçoit une erreur la renvoie à son appelant. Deux formes : la renvoyer telle quelle avec
`std::unexpected(r.error())`, ou ajouter du contexte avec `makeError`.

```cpp
namespace core = orosmo::core;
namespace rd = orosmo::renderer;

core::Result<rd::Mesh> makeLitCube(orosmo::rhi::Device &device, rd::LightManager &lights, const rd::MeshData &data)
{
    auto mesh = rd::Mesh::create(device, data);
    if (!mesh)
        return std::unexpected(mesh.error());                                  // telle quelle

    if (auto r = lights.upload(); !r)
        return core::makeError(r.error().code, "lights: " + r.error().message);   // avec contexte
    return std::move(*mesh);
}

// En haut de la pile, on journalise une seule fois :
auto cube = makeLitCube(device, lights, makeColoredCube());
if (!cube)
{
    core::logError(cube.error());
    return EXIT_FAILURE;
}
```

`device` et `lights` sont des références (`rhi::Device &`, `rd::LightManager &`), comme dans les
[conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples).

`makeError` enregistre l'endroit où il est appelé. Avec du contexte ajouté, `describe()` montre donc la fonction qui a ajouté
le contexte, pas celle qui a échoué en premier ; le message d'origine reste dans le texte.

Il n'existe pas de macro de propagation (du genre `TRY`) : chaque appel demande son `if`. Les fonctions de
`std::expected` (`and_then`, `transform`, `or_else`, `value_or`) marchent aussi, puisque `Result` en est un.

## Lire la valeur selon le type rendu

Les fabriques du moteur ne rendent pas toutes le même type, d'où des écritures différentes :

| Type rendu | Exemples | Accès |
| --- | --- | --- |
| `Result<T>` (valeur) | `Mesh::create`, `LightManager::create` | `mesh->setProgram(...)`, `std::move(*mesh)` |
| `Result<std::unique_ptr<T>>` | `rhi::createDevice`, `SceneRenderer::create`, `CanvasRenderer::create` | `(*sceneResult)->setLights(...)` ; `**sceneResult` pour la référence |
| `Result<std::shared_ptr<T>>` | `PbrMaterial::create`, `ShaderLibrary::loadGraphics` | `*program` donne le `shared_ptr` |
| `Result<>` | `LightManager::upload`, `Device::present` | tester `!r`, rien à lire |

Un `Result<>` peut être ignoré quand l'échec ne change rien pour l'appelant ; on l'écrit alors `(void)device.present();`
pour que l'intention se voie.

:::caution
Appeler `r.error()` sur un succès, ou `*r` sur un échec, est un comportement indéfini (précondition de `std::expected`).
Toujours tester `r` avant.
:::

## Modules qui n'utilisent pas encore Result

Une partie du code repris des versions précédentes garde son ancien style : `platform::Window::init` rend un `int`
(0 = succès), `Window::newFrame` un `bool`, `ui::TextRenderer::init` et `loadFont` un `bool`, et `core::bench::writeReport`
lance `std::runtime_error` si le fichier ne s'ouvre pas.

## Assertions

`ENGINE_ASSERT(cond, msg)` vérifie un invariant (une erreur de programmation, pas une erreur attendue). Elle est active en
debug et retirée quand `NDEBUG` est défini. `ENGINE_ASSERT_ALWAYS(cond, msg)` reste active partout.

```cpp
ENGINE_ASSERT(index < size, "index out of range");
```

Un échec écrit `assertion failed: index < size — index out of range (fichier:ligne)` au niveau ERROR, appelle le
gestionnaire posé par `setAssertHandler` (aucun par défaut), puis `std::abort()`. Les tests posent un gestionnaire qui lance
une exception pour vérifier qu'une assertion se déclenche.

## Journal

Les macros prennent une liste d'arguments, mis bout à bout avec `operator<<` :

```cpp
#include <orosmo/core/log.hpp>

LOG_INFO("Graphics device: ", name, " (", width, "x", height, ")");
LOG_WARNING("Lighting: forward+ unavailable, using forward: ", reason);
LOG_ERROR(1, "UI: unregistered action ", missing);   // le premier argument est un code entier
```

| Macro | Niveau | Remarque |
| --- | --- | --- |
| `LOG_DEBUGGING(...)` | `L_DEBUGGING` | Seulement en build `DEBUG`, vide sinon. Pour un essai rapide, à ne pas garder. |
| `LOG_DEBUG(...)` | `L_DEBUG` | |
| `LOG_TRACE(...)` | `L_TRACE` | |
| `LOG_INFO(...)` | `L_INFO` | |
| `LOG_WARNING(...)` | `L_WARNING` | Sans code. |
| `LOG_EWARNING(code, ...)` | `L_WARNING` | Avertissement avec code. |
| `LOG_ERROR(code, ...)` | `L_ERROR` | Le message est suivi de `(Error: code)`. |
| `LOG_FATAL(code, ...)` | `L_FATAL` | Termine le programme par `std::exit(1)` juste après. |
| `LOG(level, ...)` | au choix | |

Le code de `LOG_ERROR` est un entier libre ; le moteur met souvent `1`. `logError(error)` passe la valeur de `error.code`.

### Réglages

| Macro | Défaut | Effet |
| --- | --- | --- |
| `SET_LOG_FILE("logs/log.log")` | pas de fichier | Écrit le journal dans ce fichier (dossiers créés au besoin). |
| `SET_LOG_FILE_DEFAULT` | — | Fichier `logs/log_AAAA-MM-JJ.log` dans `Orosmo/` du dossier de données de l'utilisateur (`XDG_DATA_HOME`, `APPDATA`, `Application Support`), sinon dans le dossier courant. |
| `FLUSH_LOG_TO_FILE` | — | Écrit dans le fichier les messages qui n'y sont pas encore. |
| `SET_LOG_LEVEL_INFO` (et `_DEBUG`, `_TRACE`, `_WARNING`, `_ERROR`, `_FATAL`) | `L_DEBUGGING` en debug, `L_INFO` en release | Niveau minimal affiché dans la console. |

La démo et le projet créé par `orosmo init` font ceci au début de `main` :

```cpp
orosmo::core::setWorkingDirectoryToExecutable();
#ifdef DEBUG
SET_LOG_FILE("logs/log.log");
#else
SET_LOG_FILE_DEFAULT;
#endif
// ...
FLUSH_LOG_TO_FILE;   // avant chaque sortie
```

Source : [`apps/demo/src/main.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/main.cpp).

### Comportement

- Les messages sont gardés en mémoire et écrits dans le fichier par `FLUSH_LOG_TO_FILE`, ou quand la mémoire dépasse
  `Logger::kMaxKeptLogs` (8192) messages. Sans `FLUSH_LOG_TO_FILE` avant de quitter, les derniers messages manquent au
  fichier.
- Un message identique à l'un des 64 derniers (`kDuplicateSearchDepth`) émis dans les 3 dernières secondes n'est pas
  répété : son compteur de répétitions augmente.
- Couleur par niveau dans la console. Le `Logger` est utilisable depuis plusieurs threads.
- En build `DEBUG`, chaque message garde le fichier, la ligne et la pile d'appels (`<stacktrace>`). Il faut alors lier
  `-lstdc++_libbacktrace` avec GCC 13, ou `-lstdc++exp` avec GCC 14 et plus ; `tools/engine-sdk/engine.mk` le fait. Avec la
  libc++ d'Apple, la pile est vide.

:::note
`LOG_FATAL` quitte sans écrire le journal dans le fichier. Pour garder la trace d'une erreur fatale, appeler
`FLUSH_LOG_TO_FILE` avant, ou renvoyer l'erreur jusqu'à `main`.
:::

## Limites

- Du code repris des versions précédentes lance encore des exceptions ou renvoie `bool` ou `int` au lieu d'un
  `Result` (liste plus haut).
- Pas de macro de propagation d'erreur.
- Le `Logger` garde ses anciens noms (`Logger::Log`, `L_INFO`) ; il n'a pas été renommé selon les conventions actuelles.

État complet : [État du projet](/projet/etat/).

## Voir aussi

- [Configuration, réflexion, sérialisation](/guides/noyau/configuration/) : erreurs qui nomment le champ fautif.
- [RHI](/interne/rhi/) : erreur d'encodeur renvoyée par `Device::submit`.
- [Couches et modules](/interne/couches/) : conventions du code.
