# assets

Couche 4 de `docs/03` §1 : import et préparation des contenus (images, glTF, cuisson, rechargement). Archive : `libassets.a`
(`src/assets/`), API publique `include/orosmo/assets/`. Dépend de `renderer`, `rhi`, `platform` et `core`. Namespace
`orosmo::assets`. Créé en M6 (lots A et B, `docs/05`, A70). Bibliothèques : stb (images), libktx (KTX2), meshoptimizer (cuisson), cgltf (glTF, implémentation dans `src/ThirdParty/Cgltf.cpp`).

| En-tête | Rôle |
| --- | --- |
| `image_loader.hpp` | `decodeImage` (octets en mémoire) et `loadImage` (fichier) : PNG, JPG, TGA, BMP décodés par stb en RGBA8, première ligne = haut (A26) ; `TextureRole` (`kColor`, `kData`, `kNormal`) → `colorSpaceOf`, `textureFormatFor` (couleur en `kRGBA8Srgb`, données en `kRGBA8Unorm`) |
| `ktx2.hpp` | `decodeKtx2` / `loadKtx2` (KTX2 : Basis Universal transcodé vers BC7, ASTC 4x4 ou RGBA8 selon `Capabilities`), `encodeKtx2` (UASTC + zstd, déterministe), `buildMipChain` ; `TextureCache` les charge directement et `reloadChanged` recharge les fichiers modifiés (ASSET-03, ASSET-05) |
| `model.hpp` | `Model` : données CPU d'un glTF 2.0 (maillages et primitives avec positions, normales, tangentes, UV 0 et 1, couleurs, articulations et poids ; matériaux métal/rugosité avec modes alpha, double face, `KHR_materials_unlit`, `KHR_materials_emissive_strength` ; textures, échantillonneurs, images incluses ou externes ; nœuds TRS ; caméras ; lumières `KHR_lights_punctual` ; peaux ; animations ; scènes), `worldMatrices`, `stats`, `validate` ; `loadGltf` / `parseGltf` (cgltf : `.gltf`, `.glb`, data URI ; bandes et éventails convertis en listes ; matrices de nœud décomposées en TRS) ; `saveGltf` (`.glb` ou `.gltf` + `.bin`) |
| `model_loader.hpp` | `ModelLoader` (M8-L12a, A75.2) : possède `TextureCache`, `renderer::PbrShaderCache` et `PbrDefaults` ; `create(context)`, `load(path)` (erreur qui nomme le fichier), `createRenderModel(model, options)`, `reloadChanged` |
| `render_model.hpp` | `RenderModel` : un `Model` prêt à dessiner (une `renderer::Mesh` aux sommets PBR par primitive, un `PbrMaterial` par matériau, plus le matériau par défaut de glTF ; un dessin par nœud et primitive de la scène ; `setRootTransform`, `setNodeMatrices`, `draw`) ; `pbrMaterialDesc`, `pbrVertices` (normales et tangentes calculées si absentes), `blendModeForAlpha` |
| `texture_cache.hpp` | `createTexture` (image décodée → `renderer::Texture` au format de l'usage, mips générés sur le GPU) ; `TextureCache` : une texture partagée par (chemin normalisé, usage, filtre, adressage), `loadFromMemory` pour les images incluses dans un fichier, `purgeUnused` |
| `bake.hpp` | Cuisson de maillages (ASSET-04) : `bakePrimitive` (dédoublonnage, cache de sommets, surdessin, ordre mémoire, quantification normales/tangentes snorm16 et UV half, LOD par simplification), `unbake`, conteneur `.omesh` (`writeBakedMesh`, `readBakedMesh`, bornes vérifiées) ; les textures se cuisent avec `encodeKtx2` |

Exemple :

```cpp
orosmo::assets::TextureCache textures(device);
auto albedo = textures.load("res/brick.png");                                      // sRGB, mips
auto normal = textures.load("res/brick_n.png", {orosmo::assets::TextureRole::kNormal}); // linéaire
```

Mips : générés par le backend (`CommandEncoder::generateMips`) ; une texture sRGB est filtrée dans l'espace linéaire (vérifié sur
OpenGL et Vulkan : `tests/assets/texture_loading_tests.cpp`, damier noir et blanc → mip 188 en sRGB, 128 en linéaire).

Formats pris en charge et limites, en un seul endroit : `docs/FORMATS.md`. Démo : `--scene anim` (personnage glTF généré, importé,
skinné et animé, foule de 1000 agents, texture rechargée à chaud).

Limites de l'import glTF : pas de cibles de forme (morph targets, ANIM-05), pas de Draco ni de meshopt (refusés en `kUnsupported` quand ils sont requis), `KHR_texture_transform` ignoré, au plus 4 influences d'os par sommet (`JOINTS_1` ignoré avec un avertissement), primitives de points et de lignes ignorées avec un avertissement. Les images externes ne sont pas lues à l'import (chemin absolu dans `ImageData::path`).

Limites du rendu (`RenderModel`) : un seul jeu d'UV (une carte qui demande `TEXCOORD_1` est lue avec `TEXCOORD_0`, avec un avertissement) ; un seul mode d'adressage par texture (celui de `wrapS`) ; tangentes calculées par triangle (pas MikkTSpace) ; un nœud à échelle négative inverse le sens des faces (la face avant devient arrière) ; les primitives transparentes (`BLEND`) sont dessinées dans l'ordre du fichier, sans tri.

Limites des textures : images 8 bits par canal seulement (les PNG 16 bits sont réduits à 8 bits par stb) ; pas de HDR ici (IBL, lot D) ; KTX2 2D seulement (`ktx2.hpp`, G200).

Limites de la cuisson : les LOD sont guidés par les positions seules (coutures d'UV ou de normales non protégées) et partagent les sommets du LOD 0 ; pas de cibles de forme.
