---
title: "Configuration, réflexion, sérialisation"
description: "Décrire une structure une fois avec Reflect<T>, puis la lire depuis JSON ou la ligne de commande avec Config, la sauvegarder en JSON ou MessagePack, migrer ses versions."
sidebar:
  order: 2
---

On décrit les champs d'une structure une seule fois, dans une spécialisation de `orosmo::core::Reflect<T>`. La
sérialisation (JSON, MessagePack), la configuration (`Config::applyTo`) et le panneau d'interface automatique s'en
servent. Il n'y a ni macro ni code généré.

En-têtes : `orosmo/core/reflection.hpp`, `orosmo/core/serialization.hpp`, `orosmo/core/config.hpp`.

## Exemple minimal

```cpp title="main.cpp"
#include <orosmo/core/reflection.hpp>
#include <orosmo/core/serialization.hpp>

#include <cstdint>
#include <cstdio>
#include <string>

struct AppSummary
{
    std::string name = "minimal";
    std::uint32_t tasksCompleted = 0;
    std::uint64_t sum = 0;
};

template <> struct orosmo::core::Reflect<AppSummary>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        return std::make_tuple(field("name", &AppSummary::name), field("tasksCompleted", &AppSummary::tasksCompleted),
                               field("sum", &AppSummary::sum));
    }
};

int main()
{
    AppSummary summary;
    summary.sum = 42;
    const std::string json = orosmo::core::dumpJson(orosmo::core::saveDocument(summary), /*indent=*/-1);
    std::printf("%s\n", json.c_str());   // {"data":{"name":"minimal","sum":42,"tasksCompleted":0},"version":1}
}
```

Adapté de [`tools/engine-sdk/examples/minimal/main.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tools/engine-sdk/examples/minimal/main.cpp).

## Décrire une structure

La spécialisation doit être écrite dans l'espace de noms global ou dans `orosmo::core`, avec le nom complet
`orosmo::core::Reflect<T>`. `fields()` renvoie un `std::tuple` de `field(...)` :

```cpp
field(nom, &T::membre, range(min, max), "description")
```

| Argument | Type | Défaut | Effet |
| --- | --- | --- | --- |
| `name` | `const char *` | — | Clé JSON et libellé dans l'interface. |
| `member` | pointeur de membre | — | Le champ lu et écrit. |
| `r` | `Range` (`range(lo, hi)`) | pas de borne | Lecture refusée (`kOutOfRange`) hors de la plage ; bornes du curseur dans l'interface. |
| `description` | `const char *` | `""` | Texte d'aide (interface). |

`kVersion` n'est lu que par `saveDocument` et `loadDocument` ; une structure sans document n'en a pas besoin.

Types de champ reconnus : `bool`, entiers signés et non signés, `float`, `double`, `std::string`, `Vec2`, `Vec3`, `Vec4`
(alias de `glm`), `std::vector<T>`, une énumération (écrite par sa valeur numérique) et toute structure elle-même réfléchie.

Pour qu'une énumération s'affiche en liste déroulante avec des noms, on spécialise `EnumNames<E>` (valeurs continues à
partir de 0) ; le format sauvegardé reste numérique :

```cpp
template <> struct orosmo::core::EnumNames<Basis>
{
    static constexpr std::array<const char *, 2> names{"perlin", "simplex"};
};
```

`describeFields<T>()` donne la liste des champs (nom, `FieldType`, plage, description) et `forEachField(obj, fn)` appelle
`fn(nom, valeur&, plage)` sur chaque champ.

## Sauvegarder et relire en JSON ou MessagePack

| Fonction | Rend | Effet |
| --- | --- | --- |
| `toJsonValue(obj)` | `Json` | Valeur JSON de l'objet (sans version). |
| `fromJsonValue(obj, json, chemin)` | `Result<>` | Remplit l'objet. `chemin` sert aux messages d'erreur (`""` à la racine). |
| `saveDocument(obj)` | `Json` | `{"version": kVersion, "data": {...}}`. |
| `loadDocument(obj, doc, migrations)` | `Result<>` | Vérifie la version, applique les migrations, remplit l'objet. |
| `dumpJson(json, indent)` | `std::string` | Texte ; `indent` vaut 2 par défaut, -1 pour une seule ligne. |
| `parseJson(text)` | `Result<Json>` | `kParse` si le texte n'est pas du JSON. |
| `toBinary(json)` | `std::vector<std::uint8_t>` | MessagePack. |
| `fromBinary(bytes)` | `Result<Json>` | `kParse` si les octets ne sont pas du MessagePack. |

`Json` est `nlohmann::json`.

Règles de lecture, pour qu'un ancien fichier reste lisible :

- un champ absent garde la valeur que l'objet avait avant la lecture ;
- un champ inconnu est ignoré ;
- un type faux donne `kParse`, une valeur hors plage `kOutOfRange` ; le message nomme le champ avec son chemin, par
  exemple `layers[0].weight`. Un entier qui ne tient pas dans le type du champ est refusé, pas tronqué.

```cpp
TerrainParams p;
auto r = loadDocument(p, *parseJson(R"({"version":2,"data":{"layers":[{"weight":3.0}]}})"));
// r.error().code == ErrorCode::kOutOfRange ; r.error().message contient "layers[0].weight"
```

## Versions et migrations

Quand une structure change, on augmente `kVersion` et on écrit une migration de la version `n` vers `n + 1`. La table
`MigrationTable` associe `n` à une fonction qui modifie `data` en place :

```cpp
// Version 1 appelait le champ « freq » ; la version 2 l'appelle « frequency ».
MigrationTable migrations;
migrations[1] = [](Json &data) -> Result<> {
    if (data.contains("freq"))
    {
        data["frequency"] = data["freq"];
        data.erase("freq");
    }
    return {};
};
auto loaded = loadDocument(params, *parseJson(R"({"version":1,"data":{"freq":8.0}})"), migrations);
```

Un document plus récent que `kVersion`, ou une version sans migration, donne `kVersionMismatch`. Une version négative
donne `kParse`.

Source : [`tests/core/serialization_tests.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tests/core/serialization_tests.cpp).

## Configuration : défauts, fichier, ligne de commande

`Config` empile trois couches ; la plus haute gagne : valeurs par défaut déclarées, puis fichier JSON, puis ligne de
commande. Les clés sont pointées (`terrain.octaves`) et correspondent à des objets JSON imbriqués.

```cpp
#include <orosmo/core/config.hpp>

orosmo::core::Config config;
config.declare("window.width", 1280, "window width in pixels");
config.declare("terrain.octaves", 4, "noise octaves");
config.declare("debug", false, "debug overlay");

if (auto r = config.loadFile("settings.json"); !r) { /* fichier absent ou invalide : les valeurs restent */ }
auto positional = config.parseArgs(argc - 1, argv + 1);   // sans argv[0]
if (!positional) { std::puts(config.describe().c_str()); return 1; }

const int width = config.getOr<int>("window.width", 1280);
```

| Méthode | Rend | Effet |
| --- | --- | --- |
| `declare(key, default, description)` | — | Déclare un réglage. Le type JSON de la valeur par défaut fixe le type attendu. |
| `get(key)` | `Result<Json>` | Valeur effective ; `kNotFound` si la clé n'existe dans aucune couche. |
| `get<T>(key)` | `Result<T>` | Idem, convertie ; `kParse` si le type ne convient pas. |
| `getOr<T>(key, fallback)` | `T` | `fallback` en cas d'erreur. |
| `set(key, value)` | — | Écrit dans la couche fichier ; une valeur de la ligne de commande reste prioritaire. |
| `loadFile(path)` | `Result<std::vector<std::string>>` | Remplace la couche fichier et rend les clés dont la valeur effective a changé. En cas d'erreur, l'ancien contenu est gardé. |
| `saveFile(path)` | `Result<>` | Écrit la couche fichier ; `kIo` si l'écriture échoue. |
| `parseArgs(argc, argv)` ou `parseArgs(args)` | `Result<std::vector<std::string>>` | Lit la ligne de commande et rend les arguments positionnels. |
| `describe()` | `std::string` | Aide de la ligne de commande, une ligne par clé, triée : `--window.width (default 1280)  window width in pixels` ; ` = valeur` s'ajoute quand la valeur effective diffère du défaut. |
| `applyTo(obj, prefix)` | `Result<>` | Remplit une structure réfléchie depuis la section `prefix` de la vue fusionnée. |
| `effective()` | `Json` | Vue fusionnée des trois couches. |

Syntaxe de la ligne de commande :

| Forme | Effet |
| --- | --- |
| `--terrain.octaves 6` ou `--terrain.octaves=6` | Valeur du réglage. |
| `--debug` | Booléen vrai. |
| `--no-debug` | Booléen faux ; refusé (`kInvalidArgument`) pour un réglage qui n'est pas booléen. |
| `scene.json` | Argument positionnel, rendu tel quel. |

Une valeur du mauvais type (`--window.width wide`, ou `2.5` pour un entier) donne `kParse` ; une option sans valeur donne
`kInvalidArgument`. Un entier est accepté pour un réglage flottant. Une chaîne déclarée reste une chaîne même si elle
ressemble à un nombre.

### Rechargement à chaud

`loadFile` rend la liste des clés qui ont changé. On peut donc relire le fichier quand un
[`FileWatcher`](/guides/noyau/fichiers-ressources/) le signale, puis réappliquer seulement ce qui a bougé :

```cpp
if (auto changed = config.loadFile(path); changed && !changed->empty())
    (void)config.applyTo(terrain, "terrain");
```

Un fichier cassé pendant l'édition ne remet rien à zéro : l'erreur est rendue et les valeurs d'avant restent.

## Limites

- La démo n'utilise pas encore `Config` pour sa ligne de commande : elle a son propre analyseur.
- `Config`, `Json` et les fonctions de sérialisation ne sont pas thread-safe : un seul propriétaire, ou un verrou extérieur.

## Voir aussi

- [Erreurs et journal](/guides/noyau/erreurs-journal/)
- [Fichiers, ressources, rechargement](/guides/noyau/fichiers-ressources/)
- [Catalogue des widgets](/guides/interface/widgets/) : panneau généré depuis une structure réfléchie.
