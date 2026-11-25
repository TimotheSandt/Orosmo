---
title: "Formats de fichiers"
description: "Formats lus et écrits par le moteur : glTF, images, KTX2, .omesh, LUT .cube, avec ce qui est pris en charge et ce qui est refusé."
sidebar:
  order: 7
---

Cette page liste ce que le moteur lit et écrit, et ses limites. Les guides correspondants : [glTF](/guides/contenus/gltf/), [textures](/guides/contenus/textures/), [cuisson](/guides/contenus/cuisson/). Source de référence : `docs/FORMATS.md`.

## glTF 2.0

Lecture : `assets::loadGltf` (`.gltf` avec tampons et images externes ou en data URI, `.glb`) et `assets::parseGltf` (octets en mémoire), par cgltf. Écriture : `assets::saveGltf` (`.glb` en un fichier, ou `.gltf` et `<nom>.bin`).

| Pris en charge | Détail |
|---|---|
| Géométrie | Primitives triangles (bandes et éventails convertis en listes), indexées ou non. Attributs `POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD_0`, `TEXCOORD_1`, `COLOR_0`, `JOINTS_0`, `WEIGHTS_0`. Accesseurs normalisés et quantifiés (`KHR_mesh_quantization`). |
| Matériaux | Métal/rugosité : facteurs et cartes (couleur de base, métal/rugosité, normale, occlusion, émission). Modes alpha `OPAQUE`, `MASK`, `BLEND`. Double face. `KHR_materials_unlit`, `KHR_materials_emissive_strength`. |
| Textures | Images PNG ou JPG incluses ; images externes (chemin rendu absolu, lues par le `TextureCache`) ; une image KTX2 référencée par `KHR_texture_basisu` si l'extension n'est pas « requise ». Échantillonneurs : adressage et filtre plus proche voisin. |
| Scène | Nœuds TRS (les matrices sont décomposées), hiérarchie, scènes et scène par défaut. Caméras perspective et orthographique. `KHR_lights_punctual` (directionnelle, ponctuelle, spot). |
| Animation | Peaux (articulations, matrices de liaison inverses). Translation, rotation, échelle ; interpolations `STEP`, `LINEAR` (slerp) et `CUBICSPLINE`. |

| Non pris en charge | Comportement |
|---|---|
| Extension inconnue listée dans `extensionsRequired` (dont Draco, `EXT_meshopt_compression`, `KHR_texture_basisu` requis) | Refus : `kUnsupported`. |
| Cibles de forme (morph targets), pistes `weights` | Ignorées (pas encore prévues). Supprimées à l'export. |
| `KHR_texture_transform`, autres extensions de matériau (clearcoat, transmission, volume, sheen...) | Ignorées. |
| Plus de 4 influences par sommet (`JOINTS_1`, `WEIGHTS_1`) | Ignorées, avec un avertissement. |
| Primitives de points et de lignes | Ignorées, avec un avertissement. |
| Rendu : `TEXCOORD_1` pour une carte | Lue avec `TEXCOORD_0`, avec un avertissement (un seul jeu d'UV). |
| Rendu : `wrapT` différent de `wrapS` | Un seul mode d'adressage par texture, celui de `wrapS`. |
| Rendu : tangentes absentes | Calculées par triangle (pas MikkTSpace). Les transparents `BLEND` ne sont pas triés. |
| Rechargement à chaud d'un modèle | Non : seules les textures sont rechargées. |

Les erreurs de lecture : `kIo` (fichier), `kParse` (contenu invalide), `kUnsupported` (extension requise inconnue).

## Images

PNG, JPG, TGA et BMP sont décodés (stb) en RGBA8, première ligne en haut ; un PNG 16 bits est réduit à 8 bits. L'usage (`TextureRole`) décide du format : couleur en `kRGBA8Srgb`, données et normales en `kRGBA8Unorm`. Les mips sont générés sur le GPU. Pas d'image HDR ici. Écriture : PNG par `renderer::savePng` (utilisé par les captures).

## KTX2

| Contenu du fichier | Chargé comme |
|---|---|
| Basis Universal (UASTC ou ETC1S) | BC7 si `Capabilities::bc7Textures`, sinon ASTC 4x4 si `astcTextures`, sinon RGBA8 ; variante sRGB pour une couleur. |
| BC7, ASTC 4x4 | Envoyé tel quel si le périphérique l'échantillonne, sinon `kUnsupported`. |
| RGBA8 | Envoyé tel quel. |
| Autres formats | `kUnsupported`. |

Écriture : `assets::encodeKtx2` (UASTC niveau 0 à 4 ou RGBA8, supercompression zstd, mips filtrés sur le CPU), déterministe. Limites : KTX2 2D seulement (pas de tableaux, cubes ni 3D) ; replis ASTC et RGBA8 vérifiés sur CPU seulement ; textures compressées non relisibles et sans mips générés.

## .omesh

Format binaire du moteur, produit par `bakePrimitive` et `writeBakedMesh`, relu par `readBakedMesh`. Petit-boutiste.

| Zone | Contenu |
|---|---|
| En-tête | Signature `OMSH`, version `u32` (1), nombre de primitives `u32`. |
| Par primitive | Matériau (`i32`), bornes (6 `float32`), nombre de sommets, masque d'attributs, liste des LOD (nombre d'indices de chacun), flux de sommets, indices `u32` de chaque LOD. |

| Attribut | Quantifié (défaut) | Non quantifié |
|---|---|---|
| Positions | 3 x `float32` | 3 x `float32` |
| Normales, tangentes | 4 x `snorm16` (`w` des tangentes = signe) | `float32` |
| UV | 2 x `half` | 2 x `float32` |
| Couleurs | 4 x `unorm8` | 4 x `unorm8` |
| Articulations, poids | 4 x `u16`, 4 x `unorm16` | idem |

La lecture vérifie les bornes partout : `kParse` (tronqué, mauvaise signature, indices hors bornes), `kVersionMismatch` (version inconnue). Limites : bibliothèque seulement, pas d'exécutable de cuisson ; LOD guidés par les positions et partageant les sommets du LOD 0 ; peaux non reconstruites ; flottants IEEE supposés.

## LUT .cube

`renderer::parseCubeLut(text)` lit le format Adobe/Resolve `.cube` : `LUT_3D_SIZE N` (entre 2 et 256), puis N³ lignes `r g b`. `TITLE`, `DOMAIN_MIN`/`DOMAIN_MAX` (0 et 1 seulement) et les commentaires `#` sont acceptés. Une LUT 1D (`LUT_1D_SIZE`) est refusée (`kUnsupported`) ; une taille manquante ou hors bornes, une ligne illisible ou un mauvais nombre de couleurs donnent `kInvalidArgument`. Voir [HDR et post-traitement](/guides/rendu-3d/post-traitement/).

## Autres formats

| Format | Où |
|---|---|
| Vues `.cui` | [Langage .cui](/reference/langage-cui/) |
| Thèmes JSON | [Thèmes : jetons et propriétés](/reference/themes-json/) |
| OBJ | Absent. FBX : seulement par conversion externe, hors du moteur. |

## Voir aussi

- [Importer un modèle glTF](/guides/contenus/gltf/)
- [Textures : PNG, KTX2, cache et rechargement](/guides/contenus/textures/)
- [Cuire les maillages](/guides/contenus/cuisson/)
