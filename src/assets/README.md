# assets

Couche 4 de `docs/03` §1 : import et préparation des contenus (images, glTF, cuisson, rechargement). Archive : `libassets.a`
(`src/assets/`), API publique `include/orosmo/assets/`. Dépend de `renderer`, `rhi`, `platform` et `core`. Namespace
`orosmo::assets`. Créé en M6 (lots A et B, `docs/05`, A70). Bibliothèques : stb (images), cgltf (glTF, implémentation dans `src/ThirdParty/Cgltf.cpp`).

| En-tête | Rôle |
| --- | --- |
| `image_loader.hpp` | `decodeImage` (octets en mémoire) et `loadImage` (fichier) : PNG, JPG, TGA, BMP décodés par stb en RGBA8, première ligne = haut (A26) ; `TextureRole` (`kColor`, `kData`, `kNormal`) → `colorSpaceOf`, `textureFormatFor` (couleur en `kRGBA8Srgb`, données en `kRGBA8Unorm`) |
| `model.hpp` | `Model` : données CPU d'un glTF 2.0 (maillages et primitives avec positions, normales, tangentes, UV 0 et 1, couleurs, articulations et poids ; matériaux métal/rugosité avec modes alpha, double face, `KHR_materials_unlit`, `KHR_materials_emissive_strength` ; textures, échantillonneurs, images incluses ou externes ; nœuds TRS ; caméras ; lumières `KHR_lights_punctual` ; peaux ; animations ; scènes), `worldMatrices`, `stats`, `validate` ; `loadGltf` / `parseGltf` (cgltf : `.gltf`, `.glb`, data URI ; bandes et éventails convertis en listes ; matrices de nœud décomposées en TRS) ; `saveGltf` (`.glb` ou `.gltf` + `.bin`) |
| `texture_cache.hpp` | `createTexture` (image décodée → `renderer::Texture` au format de l'usage, mips générés sur le GPU) ; `TextureCache` : une texture partagée par (chemin normalisé, usage, filtre, adressage), `loadFromMemory` pour les images incluses dans un fichier, `purgeUnused` |

Exemple :

```cpp
orosmo::assets::TextureCache textures(device);
auto albedo = textures.load("res/brick.png");                                      // sRGB, mips
auto normal = textures.load("res/brick_n.png", {orosmo::assets::TextureRole::kNormal}); // linéaire
```

Mips : générés par le backend (`CommandEncoder::generateMips`) ; une texture sRGB est filtrée dans l'espace linéaire (vérifié sur
OpenGL et Vulkan : `tests/assets/texture_loading_tests.cpp`, damier noir et blanc → mip 188 en sRGB, 128 en linéaire).

Limites de l'import glTF : pas de cibles de forme (morph targets, ANIM-05), pas de Draco ni de meshopt (refusés en `kUnsupported` quand ils sont requis), `KHR_texture_transform` ignoré, au plus 4 influences d'os par sommet (`JOINTS_1` ignoré avec un avertissement), primitives de points et de lignes ignorées avec un avertissement. Les images externes ne sont pas lues à l'import (chemin absolu dans `ImageData::path`).

Limites des textures : images 8 bits par canal seulement (les PNG 16 bits sont réduits à 8 bits par stb) ; pas de HDR ici (IBL, lot D) ni de
KTX2 (lot G).
