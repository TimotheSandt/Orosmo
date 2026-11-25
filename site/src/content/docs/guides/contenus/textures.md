---
title: "Textures : PNG, KTX2, cache et rechargement"
description: "Charger des textures couleur ou données, compressées (KTX2) ou non, partagées par un cache et rechargées quand le fichier change."
sidebar:
  order: 2
---

Les noms des variables (`device`, `renderer`…) et les chemins de ressources des extraits sont expliqués dans les [conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples).

`assets::TextureCache` charge une image depuis un fichier, en fait une `renderer::Texture` avec ses mips, et rend toujours la même texture quand on la redemande avec les mêmes réglages. Le format est choisi selon l'usage : une couleur est stockée en sRGB, une donnée (normale, métal/rugosité, occlusion) en linéaire.

## Exemple minimal

```cpp
#include <orosmo/assets/texture_cache.hpp>

namespace as = orosmo::assets;

as::TextureCache textures(device);   // device : voir « Lire la boucle d'une application »

auto albedo = textures.load(GET_RESOURCE_PATH("textures/brick.png"));   // couleur : sRGB, mips générés
if (!albedo) { /* albedo.error().message */ }

auto normal = textures.load(GET_RESOURCE_PATH("textures/brick_n.png"), {.role = as::TextureRole::kNormal});   // linéaire
```

`load` rend un `core::Result<std::shared_ptr<renderer::Texture>>`. Pour l'utiliser dans un matériau PBR, on la range dans `PbrMaterialDesc::maps` (voir [Matériaux PBR](/guides/rendu-3d/materiaux-pbr/)) ; un `RenderModel` le fait tout seul pour les textures d'un glTF.

Adapté de `src/assets/README.md` et de `tests/assets/texture_loading_tests.cpp`.

## Choisir l'usage et le filtre

Le troisième paramètre de `load` est un `TextureRequest`. Les trois champs font partie de la clé du cache : la même image demandée avec deux réglages différents donne deux textures.

| Option | Type | Défaut | Effet |
|---|---|---|---|
| `role` | `TextureRole` | `kColor` | `kColor` : format `kRGBA8Srgb` (albédo, émission, interface). `kData` et `kNormal` : format `kRGBA8Unorm` (déjà linéaire). Pour un fichier PNG, JPG, TGA ou BMP seulement : un KTX2 garde le format du fichier (un KTX2 sRGB reste sRGB même avec `kNormal`), donc encodez-le avec le bon rôle (`Ktx2EncodeOptions::role` : `kData` ou `kNormal` pour du linéaire). |
| `filter` | `renderer::FilterMode` | `kLinear` | `kLinear` : filtrage linéaire avec mips (trilinéaire). `kNearest` : plus proche voisin, sans mips. `kLinearNoMipmaps` : linéaire, sans mips. |
| `address` | `rhi::AddressMode` | `kRepeat` | Adressage hors de `[0, 1]`. |

```cpp
// Pixel art : plus proche voisin, pas de mips.
auto sprite = textures.load(GET_RESOURCE_PATH("textures/hero.png"), {.role = as::TextureRole::kColor, .filter = orosmo::renderer::FilterMode::kNearest});
```

Une couleur chargée avec `kData` (ou l'inverse) donne une image fausse sans message d'erreur (couleurs trop claires ou trop sombres).

Les mips sont générés sur le GPU. Une texture sRGB est moyennée dans l'espace linéaire (vérifié par les tests sur OpenGL et Vulkan).

## Images en mémoire et décodage seul

| Fonction | Effet |
|---|---|
| `TextureCache::load(path, request)` | Fichier PNG, JPG, TGA, BMP, ou KTX2 (reconnu à son en-tête). Clé : chemin absolu normalisé + les trois champs de `TextureRequest`. |
| `TextureCache::loadFromMemory(key, bytes, request)` | Image encodée en mémoire (par exemple incluse dans un `.glb`). `key` l'identifie dans le cache. |
| `decodeImage(bytes, name)` | Décode PNG, JPG, TGA ou BMP en `DecodedImage` (RGBA8, lignes serrées, première ligne = haut). `kInvalidArgument` si les octets sont vides, `kParse` si illisibles. |
| `loadImage(path)` | Lit un fichier puis `decodeImage`. `kIo` si le fichier ne s'ouvre pas. |
| `createTexture(device, image, request, debugName)` | Texture depuis une `DecodedImage` (hors cache). Une variante prend des `TextureLevels` (KTX2). |

`DecodedImage` a `width`, `height`, `sourceChannels` (canaux du fichier avant l'expansion en RGBA) et `rgba8`. Les images ont 8 bits par canal : un PNG 16 bits est réduit à 8 bits.

## KTX2 : textures compressées

Un fichier `.ktx2` se charge comme une image, avec le même appel `load`. Ce qui est fait dépend du contenu et du matériel (`rhi::Capabilities`) :

| Contenu du fichier | Résultat |
|---|---|
| Basis Universal (UASTC ou ETC1S) | Transcodé en BC7 si `bc7Textures`, sinon ASTC 4x4 si `astcTextures`, sinon RGBA8. |
| BC7, ASTC 4x4 | Envoyé tel quel si le périphérique l'échantillonne, sinon `kUnsupported`. |
| RGBA8 | Envoyé tel quel. |
| Autre | `kUnsupported`. |

Les fonctions de `ktx2.hpp` permettent de décoder à la main : `loadKtx2(path, caps)` et `decodeKtx2(bytes, caps, name)` rendent des `TextureLevels` (format, taille, un tableau d'octets par mip) ; `isKtx2(bytes)` reconnaît l'en-tête ; `transcodeTargetFor(caps, srgb)` donne le format choisi.

Pour produire un fichier KTX2, `encodeKtx2(image, options)` encode une `DecodedImage` ; le résultat est déterministe (mêmes entrées, mêmes octets).

| Option (`Ktx2EncodeOptions`) | Type | Défaut | Effet |
|---|---|---|---|
| `role` | `TextureRole` | `kColor` | Couleur : sRGB. Données et normales : linéaire. |
| `mips` | `bool` | `true` | Écrit la chaîne complète de mips, filtrée sur le CPU (boîte 2x2). |
| `basis` | `bool` | `true` | UASTC, transcodable partout. `false` : RGBA8 non compressé. |
| `uastcLevel` | `uint32_t` | `1` | 0 (le plus rapide) à 4 (le plus lent, meilleure qualité). |
| `zstd` | `bool` | `true` | Supercompression Zstandard (niveau 18). |

```cpp
auto image = as::loadImage(GET_RESOURCE_PATH("textures/brick.png"));
if (!image) { /* ... */ }

auto ktx = as::encodeKtx2(*image, {.role = as::TextureRole::kColor, .uastcLevel = 2});
if (ktx) {
    // ktx->data() : std::vector<std::byte>, à écrire dans un fichier .ktx2
}
```

`buildMipChain(image, space)` rend la chaîne de mips d'une image (niveau 0 compris) pour une `ColorSpace` (`kSrgb` ou `kLinear`), si on veut les mips sans passer par un fichier. Il n'existe pas d'exécutable de conversion : on écrit un petit programme qui appelle `loadImage` puis `encodeKtx2`.

## Recharger quand le fichier change

`TextureCache::reloadChanged()` relit les fichiers dont la date ou la taille a changé depuis l'appel précédent et rend le nombre de textures rechargées. C'est à l'application de l'appeler ; la démo le fait toutes les 500 ms (`--scene anim`) :

```cpp
// Dans la boucle, par exemple une fois par demi-seconde.
std::size_t n = textures.reloadChanged();
```

Deux cas :

- Même taille, même format et même nombre de niveaux de mip : le contenu est envoyé sur la texture existante. Le handle ne change pas, les matériaux voient la nouvelle image sans rien refaire.
- Taille, format (un KTX2 peut changer de format sans changer de taille) ou nombre de niveaux de mip différents (un KTX2 peut garder ses dimensions et changer de nombre de niveaux) : la texture partagée est recréée. Les matériaux qui la lient doivent reconstruire leur groupe (`PbrMaterial::rebuild`, ou recréer le `RenderModel` comme le fait `AnimScene::reloadChanged`).

Un fichier illisible garde l'ancienne texture et l'erreur est journalisée.

## Libérer

| Appel | Effet |
|---|---|
| `size()` | Nombre d'entrées du cache. |
| `purgeUnused()` | Oublie les textures que plus personne ne tient (seul le cache les garde) ; rend le nombre d'entrées retirées. |
| `clear()` | Vide le cache. Les textures encore tenues ailleurs restent valides. |

## Limites

- Seules les textures sont rechargées, pas les modèles glTF, et la démo est le seul code qui appelle `reloadChanged`.
- 8 bits par canal. Pas d'image HDR ici : les environnements de l'IBL viennent d'une autre fonction (voir [Éclairage d'environnement](/guides/rendu-3d/environnement-ibl/)).
- KTX2 2D seulement : pas de tableaux, cubes ni 3D.
- Les replis ASTC et RGBA8 sont vérifiés sur le CPU seulement (la machine de test prend BC7). Une texture compressée n'est pas relisible (`readTexture` refusé) et n'a pas de mips générés : ceux du fichier sont utilisés.
- Pas d'exécutable de cuisson de textures.

## Voir aussi

- [Importer un modèle glTF](/guides/contenus/gltf/)
- [Cuire les maillages](/guides/contenus/cuisson/)
- [Formats de fichiers](/reference/formats/)
