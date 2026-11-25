---
title: "Cuire les maillages (.omesh)"
description: "Optimiser, quantifier et simplifier un maillage hors ligne, l'écrire au format .omesh et le relire."
sidebar:
  order: 3
---

La cuisson transforme une primitive de maillage (`assets::MeshPrimitive`, celle d'un glTF importé) en une version plus compacte et plus rapide à dessiner, avec des niveaux de détail (LOD). Le résultat s'écrit dans un fichier `.omesh`. La cuisson est une bibliothèque (`include/orosmo/assets/bake.hpp`, meshoptimizer) : il n'existe pas d'exécutable en ligne de commande, on l'appelle depuis son propre outil.

## Exemple minimal

```cpp
#include <orosmo/assets/bake.hpp>
#include <orosmo/assets/model.hpp>

namespace as = orosmo::assets;

auto model = as::loadGltf(GET_RESOURCE_PATH("models/rocher.glb"));
if (!model) { /* ... */ }

std::vector<as::BakedPrimitive> baked;
for (const as::MeshData &mesh : model->meshes) {
    for (const as::MeshPrimitive &primitive : mesh.primitives) {
        auto b = as::bakePrimitive(primitive);          // options par défaut
        if (!b) { /* b.error().message */ }
        baked.push_back(std::move(*b));
    }
}

auto bytes = as::writeBakedMesh(baked);                 // std::vector<std::byte>
if (!bytes) { /* ... */ }
// écrire *bytes dans un fichier .omesh avec std::ofstream
```

Relire :

```cpp
auto back = as::readBakedMesh(*bytes);                  // ou les octets lus du fichier
if (!back) { /* kParse ou kVersionMismatch */ }
auto primitive = as::unbake((*back)[0], 0);             // retour vers une MeshPrimitive, LOD 0
```

Adapté de `tests/assets/bake_tests.cpp`.

## Options de bakePrimitive

| Option (`BakeOptions`) | Type | Défaut | Effet |
|---|---|---|---|
| `optimize` | `bool` | `true` | Dédoublonne les sommets identiques, ordonne les triangles pour le cache de sommets, réduit le surdessin, ordonne les sommets pour la lecture mémoire. |
| `quantize` | `bool` | `true` | Compresse normales et tangentes (snorm16), UV (half). Positions, couleurs, articulations et poids gardent leur format. |
| `lodRatios` | `std::vector<float>` | `{0.5f, 0.25f}` | Part des indices du LOD 0 visée par LOD supplémentaire, décroissante. Vide : pas de LOD. |
| `lodError` | `float` | `0.01f` | Erreur maximale d'un LOD, relative à l'étendue du maillage. |

```cpp
as::BakeOptions options;
options.lodRatios = {0.5f, 0.2f, 0.05f};   // trois LOD en plus du LOD 0
options.quantize = false;                  // garder normales, tangentes et UV en float32
auto b = as::bakePrimitive(primitive, options);
```

`BakedPrimitive::lods[0]` est le maillage complet (optimisé), puis les LOD de plus en plus grossiers. Tous partagent le même tableau de sommets. Avec `quantize = true`, les attributs sont dans `normals`, `tangents`, `uv0`, `uv1` ; avec `false`, dans `normalsF`, `tangentsF`, `uv0F`, `uv1F`. `quantized` dit lequel est rempli.

Erreurs de `bakePrimitive` : `kInvalidArgument` pour une primitive vide, des indices qui ne sont pas un multiple de 3 ou hors bornes, un attribut de mauvaise longueur, un ratio de LOD non fini, ou des UV hors de la plage `binary16` avec `quantize`.

## Écrire et lire un .omesh

| Fonction | Effet |
|---|---|
| `writeBakedMesh(span<const BakedPrimitive>)` | Rend les octets du conteneur. `kInvalidArgument` si une primitive est incohérente. |
| `readBakedMesh(span<const std::byte>)` | Relit un conteneur. `kParse` (tronqué, mauvaise signature, indices hors bornes), `kVersionMismatch` (version inconnue). Les bornes sont vérifiées partout. |
| `unbake(baked, lod)` | Retour vers une `MeshPrimitive` avec les indices du LOD demandé (tous les sommets sont gardés). `kOutOfRange` si le LOD n'existe pas. |

Le résultat est déterministe : mêmes entrées et mêmes options, mêmes octets. La disposition du format est dans [Formats de fichiers](/reference/formats/).

## Cuire les textures

Les textures se cuisent avec `encodeKtx2`, pas avec `bake.hpp` : voir [Textures : PNG, KTX2, cache et rechargement](/guides/contenus/textures/).

## Limites

- La cuisson est une bibliothèque : pas d'exécutable de cuisson.
- Les LOD sont guidés par les positions seules : les coutures d'UV ou de normales ne sont pas protégées. Ils partagent les sommets du LOD 0 (les sommets que le LOD n'utilise plus ne sont pas retirés).
- Les peaux ne sont pas reconstruites ; les cibles de forme ne sont pas gérées.
- Le moteur ne charge pas encore un `.omesh` directement pour le dessiner : `RenderModel` part d'un `Model` glTF. Le fichier `.omesh` se relit en `BakedPrimitive` ou en `MeshPrimitive` (`unbake`), à passer à `renderer::Mesh` soi-même.
- Le format suppose des flottants IEEE.

## Voir aussi

- [Importer un modèle glTF](/guides/contenus/gltf/)
- [Niveaux de détail (LOD)](/guides/rendu-3d/lod/), pour l'usage des LOD à l'exécution
- [Formats de fichiers](/reference/formats/)
