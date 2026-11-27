---
title: "Sauvegarder et charger une scène"
description: "Écrire un scene::World en JSON ou en MessagePack, le relire par nom de type, migrer d'anciennes versions de composants et comparer deux états par leur empreinte."
sidebar:
  order: 2
---

Un monde (`scene::World`) s'écrit en JSON ou en MessagePack et se relit **par nom de type et de champ**, jamais par
octets bruts : un composant qui a gagné un champ se relit encore, et un document plus ancien passe par des migrations.
L'empreinte d'un monde (un nombre de 64 bits) permet de vérifier que deux états sont identiques.

En-têtes : `orosmo/scene/scene_file.hpp`, `orosmo/scene/fingerprint.hpp`. Pour l'état complet d'une simulation (monde,
générateur, pas, entrées), voir [Simulation à pas fixe](/guides/scene/simulation/#capturer-sauvegarder-rejouer).

## Exemple minimal

```cpp
#include <orosmo/scene/scene_file.hpp>
namespace scene = orosmo::scene;

if (auto saved = scene::saveSceneFile(world, "save/level1.json"); !saved)   // .json : JSON ; autre extension : MessagePack
    return std::unexpected(saved.error());

scene::World loaded;
if (auto velocity = loaded.registerComponent<Velocity>("Velocity"); !velocity) // les types doivent être enregistrés avant
    return std::unexpected(velocity.error());
auto report = scene::loadSceneFile(loaded, "save/level1.json");
if (!report)
    return std::unexpected(report.error());
// report->entities, report->components : ce qui a été chargé ; report->warnings : champs inconnus, types non
// enregistrés (déjà journalisés)
```

## Fonctions

| Fonction | Effet |
| --- | --- |
| `saveScene(world)` / `loadScene(world, json, migrations)` | document `core::Json` en mémoire |
| `saveSceneBinary(world)` / `loadSceneBinary(world, octets, migrations)` | MessagePack, même contenu |
| `saveSceneFile(world, chemin)` / `loadSceneFile(world, chemin, migrations)` | fichier : JSON si l'extension est `.json`, MessagePack sinon ; une erreur d'écriture (disque plein) rend `kIo` |
| `componentToJson(desc, données)` / `componentFromJson(desc, données, json, avertissements)` | un seul composant |
| `fingerprint(world)` | empreinte FNV-1a 64 bits des entités, de la hiérarchie et des champs décrits |

## Ce qui est gardé

- La table des entités entière : mêmes identifiants après le chargement, donc les champs de type `Entity` restent valides,
  et les `create()` suivants rendent les mêmes entités.
- La hiérarchie avec l'ordre des enfants, et l'ordre de stockage de chaque type (l'ordre de parcours ne change pas).
- Les valeurs, champ par champ, d'après la description du type : réels au plus court qui se relit à l'identique, NaN et
  infinis en `"nan"`, `"inf"`, `"-inf"`, quaternions en `[w, x, y, z]`, énumérations en entier. Le cache monde des
  `Transform` n'est pas écrit : il est recalculé au chargement.

## Chargement et versions

Le chargement est **tout ou rien** : il se fait dans une copie du monde, échangée seulement en cas de succès.

| Cas | Résultat |
| --- | --- |
| champ absent du document | valeur par défaut du type |
| champ inconnu, type non enregistré | ignoré, avertissement (journal et `SceneLoadReport::warnings`) |
| valeur hors de la plage (`range`) d'un champ | erreur |
| type plus ancien que `ComponentDesc::version` | migrations `SceneMigrations::components[nom][v]` appliquées, de v à v + 1 |
| type plus récent | `kVersionMismatch` |
| format du document plus ancien | migrations `SceneMigrations::format` |

```cpp
scene::SceneMigrations migrations;
migrations.components["Velocity"][1] = [](orosmo::core::Json &value) -> orosmo::core::Result<> {
    if (value.contains("speed"))         // version 1 -> 2 : champ renommé
    {
        value["value"] = value["speed"];
        value.erase("speed");
    }
    return {};
};
auto report = scene::loadSceneFile(loaded, "save/old.json", migrations);
```

La table de migration est celle du noyau (`core::MigrationTable`, clé = version de départ,
[Configuration](/guides/noyau/configuration/)).

## Empreinte

`fingerprint(world)` hache la table des entités, la hiérarchie et les champs décrits des types non vides, par nom de type,
dans l'ordre de stockage. Elle est stable après un aller-retour JSON (y compris pour les NaN) et sert aux tests de
rejouabilité. Deux mondes de même contenu mais d'ordre de stockage différent ont des empreintes différentes : c'est voulu,
car l'ordre de parcours change le résultat d'une simulation.

## Limites

- Un type non enregistré au chargement est perdu si la scène est ensuite resauvegardée (G283).
- La charge et le signe d'un NaN ne sont pas gardés (G282) ; une structure non réfléchie est écrite en octets.
- Mémoire doublée pendant le chargement, pas de chargement partiel ni en flux (G286).
- Les poignées des composants de rendu (`MeshRenderer::mesh`…) sont sauvegardées mais n'ont pas de sens d'une exécution à
  l'autre (G351).

## Voir aussi

- [Entités, composants et transformations](/guides/scene/entites-composants/)
- [Formats de fichiers](/reference/formats/)
