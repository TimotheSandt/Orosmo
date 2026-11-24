# Formats pris en charge (M6)

Ce que le module `assets` (`include/orosmo/assets/`, `src/assets/README.md`) lit et écrit, et ses limites. Les preuves sont les
tests de `tests/assets/` et `tests/anim/`, et la démo `--scene anim` (`apps/demo/src/Scene/anim_scene.cpp`).

## glTF 2.0 (`model.hpp`, `render_model.hpp`)

Lecture : `loadGltf` (`.gltf` avec tampons et images externes ou en data URI, `.glb` binaire), `parseGltf` (octets en mémoire),
par cgltf. Écriture : `saveGltf` (`.glb` en un fichier, ou `.gltf` + `<nom>.bin` ; les images incluses vont dans le `.bin`, les
images externes restent des URI relatives).

| Pris en charge | Détail |
| --- | --- |
| Géométrie | primitives triangles (bandes et éventails convertis en listes), indexées ou non ; `POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD_0/1`, `COLOR_0`, `JOINTS_0`, `WEIGHTS_0` ; accesseurs normalisés et quantifiés (`KHR_mesh_quantization`) |
| Matériaux | métal/rugosité (facteurs et cartes : couleur de base, métal/rugosité, normale, occlusion, émission) ; modes alpha `OPAQUE`, `MASK`, `BLEND` ; double face ; `KHR_materials_unlit` ; `KHR_materials_emissive_strength` |
| Textures | images PNG ou JPG incluses, images externes (chemin rendu absolu, lues par le `TextureCache`) ; une image KTX2 référencée par `KHR_texture_basisu` est lue si l'extension n'est pas « requise » ; échantillonneurs (adressage, filtre plus proche) |
| Scène | nœuds TRS (les matrices sont décomposées), hiérarchie, scènes et scène par défaut ; caméras perspective et orthographique ; `KHR_lights_punctual` (directionnelle, ponctuelle, spot) |
| Animation | peaux (articulations, matrices de liaison inverses) ; animations de translation, rotation et échelle, interpolations `STEP`, `LINEAR` (slerp) et `CUBICSPLINE` ; skinning GPU (`anim::SkinnedModel`, compute) et foules (`anim::CrowdRenderer`, vertex shader) |

| Non pris en charge | Comportement |
| --- | --- |
| Extension inconnue listée dans `extensionsRequired` (dont Draco, `EXT_meshopt_compression`, `KHR_texture_basisu` requis) | refus `kUnsupported` |
| Cibles de forme (morph targets, pistes `weights`) | ignorées (ANIM-05, G198) |
| `KHR_texture_transform`, autres extensions de matériau (clearcoat, transmission, volume, sheen…) | ignorées |
| Plus de 4 influences par sommet (`JOINTS_1` / `WEIGHTS_1`) | ignorées, avec un avertissement |
| Primitives de points et de lignes | ignorées, avec un avertissement |
| Rendu : `TEXCOORD_1` pour une carte | lue avec `TEXCOORD_0`, avec un avertissement (un seul jeu d'UV, G195) |
| Rendu : `wrapT` différent de `wrapS` | un seul mode d'adressage par texture (celui de `wrapS`, G195) |
| Rendu : tangentes absentes | calculées par triangle (pas MikkTSpace, G195) ; transparents `BLEND` non triés |
| Rechargement à chaud d'un modèle | non (textures seulement, G200) |

## Images (`image_loader.hpp`, `texture_cache.hpp`)

PNG, JPG, TGA, BMP décodés par stb en RGBA8 (première ligne = haut, A26) ; PNG 16 bits réduit à 8 bits. Usage (`TextureRole`) :
couleur en `kRGBA8Srgb`, données et normales en `kRGBA8Unorm` ; mips générés sur le GPU (filtrés dans l'espace linéaire). Pas
d'image HDR ici (les environnements de l'IBL viennent d'ailleurs, G197). Écriture : PNG (`renderer::savePng`, captures).

Rechargement (ASSET-05) : `TextureCache::reloadChanged()` relit les fichiers dont la date ou la taille a changé. Même taille et même
nombre de niveaux : envoi en place (handle conservé) ; sinon la texture est recréée et les matériaux qui la lient reconstruisent
leur groupe (la démo `--scene anim` reconstruit alors ses modèles). Un fichier illisible garde l'ancienne texture.

## KTX2 (`ktx2.hpp`, libktx)

| Contenu du fichier | Chargé comme |
| --- | --- |
| Basis Universal (UASTC ou ETC1S) | transcodé selon `Capabilities` : BC7 (`bc7Textures`), sinon ASTC 4x4 (`astcTextures`), sinon RGBA8 ; variante sRGB pour la couleur |
| BC7, ASTC 4x4 | envoyé tel quel si le périphérique l'échantillonne, sinon `kUnsupported` |
| RGBA8 | envoyé tel quel |
| Autres formats | `kUnsupported` |

Écriture : `encodeKtx2` (UASTC niveau 0 à 4 ou RGBA8, supercompression zstd, mips filtrés sur le CPU), déterministe.
Limites (G200) : KTX2 2D seulement (pas de tableaux, cubes ni 3D) ; replis ASTC et RGBA8 vérifiés sur CPU seulement ; textures
compressées non relisibles (`readTexture`) et sans mips générés (ceux du fichier sont utilisés).

## `.omesh` : maillages cuits (`bake.hpp`, meshoptimizer)

Format binaire du moteur produit par `bakePrimitive` + `writeBakedMesh`, relu par `readBakedMesh` (bornes vérifiées partout).
Petit-boutiste : « OMSH », version u32 (= 1), nombre de primitives u32 ; puis par primitive : matériau (i32), bornes (6 float32),
nombre de sommets, masque d'attributs, liste des LOD (nombre d'indices de chacun), flux de sommets, indices u32 de chaque LOD.

Sommets quantifiés : positions 3 x float32, normales et tangentes 4 x snorm16, UV 2 x half, couleurs 4 x unorm8, articulations
4 x u16, poids 4 x unorm16 (sans quantification : normales, tangentes et UV en float32). Étapes : dédoublonnage, ordre pour le cache
de sommets, surdessin, ordre mémoire, LOD par simplification. Erreurs : `kParse` (tronqué, magie, indices hors bornes),
`kVersionMismatch` (version inconnue).

Limites (G200) : bibliothèque seulement (pas d'exécutable `orosmo-bake`) ; LOD guidés par les positions (coutures d'UV ou de
normales possibles) et partageant les sommets du LOD 0 ; peaux non reconstruites ; flottants IEEE supposés.

## Limites liées

G195 (PBR : un jeu d'UV, un adressage par texture, chemins instanciés hors PBR), G196 (pas de constantes de spécialisation),
G197 (sondes et environnement), G198 (animation), G199 (foules), G200 (KTX2, cuisson, rechargement) : voir `docs/GAPS.md`.
