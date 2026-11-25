---
title: "Conventions GLSL et définitions"
description: "Repère, modèle de liaison (groupes, push constants), bloc FrameData, définitions OROSMO_* du préprocesseur et fichiers d'inclusion des shaders du moteur."
sidebar:
  order: 8
---

Cette page liste ce qu'un shader du moteur peut supposer. Pour écrire et charger un shader, voir
[Écrire et recharger des shaders](/guides/avance/shaders/).

## Repère

| Élément | Convention |
|---|---|
| Origine des images | En haut à gauche : la ligne 0 d'une texture est son haut (envoi, lecture, rendu). |
| NDC | x vers la droite, y = +1 en haut. |
| Profondeur | [0, 1], en reverse-Z : 1 au plan proche, 0 au loin (plan lointain infini par défaut). Effacement à 0, test « plus grand ». |
| Face avant | Triangle anti-horaire en NDC (`FrontFace::kCounterClockwise`). |
| UV | v = 0 est la première ligne de la texture (le haut). |

Ces règles sont les mêmes sur Vulkan et OpenGL (décision de l'auteur) : pas de correction de repère dans vos shaders. Attention, dans
`coords.glsl`, `uvToNdc(uv)` vaut `uv * 2 - 1` sans retourner y : `uv.y = 0` donne `ndc.y = -1`, alors que y = +1 est le haut en NDC.
Retournez y vous-même si vous convertissez une position d'écran en NDC.

## Modèle de liaison

Trois groupes de huit liaisons au plus (`kGroupCount` = 3, `kBindingsPerGroup` = 8).

| Groupe | Contenu |
|---|---|
| 0 | Image : caméra (0), lumières (1), `FrameData` (2), puis liaisons 3 à 7 ajoutées par les plugins (`addFrameBinding`). |
| 1 | Matériau ou passe : textures du maillage (liaisons 0, 1, 2...), bloc PBR, récepteurs d'ombres. |
| 2 | Objet : bloc du modèle (matrice, matrice des normales, modèle précédent, identifiant), choisi par un décalage dynamique. |

Liaisons du groupe 0 prises par les plugins du moteur : clusters forward+ (3 et 4), cube préfiltré IBL (5), table BRDF (6),
bloc `IblBlock` (7).

| Règle | Détail |
|---|---|
| Écriture | `layout(set = N, binding = M)` ; une liaison hors des 3 groupes x 8 est refusée. |
| Push constants | `layout(push_constant)`, 128 octets au plus. Sous OpenGL, ils sont émulés par l'UBO d'emplacement 24 (`kGroupCount * kBindingsPerGroup`). |
| OpenGL | Emplacement plat = groupe x 8 + binding, pour les UBO, SSBO, unités de texture et d'image. |
| Textures | Combinées (`sampler2D`) : textures et échantillonneurs séparés sont refusés. Un tableau `sampler2D t[N]` occupe N liaisons consécutives qui doivent tenir dans leur groupe. |
| Stockage | OpenGL 4.3 ne garantit que 8 emplacements de tampons de stockage : la limite réelle est `Capabilities::maxStorageBufferBindings`. Le groupe 0 est le choix portable pour les images de stockage. |

Dans un programme de maillage, le premier entier des push constants est le drapeau fil de fer de `mesh.frag`.

## Bloc FrameData

`#include "frame_data.glsl"` (dans `shaders/rhi/`) déclare `frame` au groupe 0, liaison 2, en std140. Même disposition que
`orosmo::renderer::FrameData` (640 octets).

| Champ | Contenu |
|---|---|
| `view`, `proj`, `viewProj` | Matrices de la caméra. |
| `invView`, `invProj`, `invViewProj` | Inverses. |
| `prevViewProj` | Image précédente (mouvement, TAA) ; égale à `viewProj` à la première image. |
| `cameraRelativeOrigin` | `xyz` : position de la caméra vue par les shaders (0 en rendu relatif à la caméra). |
| `cameraWorldHigh`, `cameraWorldLow` | Position monde en double, scindée : high + low. |
| `jitter` | `xy` courant, `zw` précédent. |
| `viewport` | Largeur, hauteur, 1/largeur, 1/hauteur (résolution de rendu). |
| `time` | Secondes cumulées, delta, numéro d'image, graine de bruit dans [0, 1). |
| `sunDirection`, `sunColor` | Lumière principale (`w` = 1 si elle existe) ; couleur x intensité. |
| `planet`, `scatteringRayleigh`, `scatteringMie` | Paramètres d'environnement (`FrameEnvironment`), sans sens imposé. |
| `exposure` | `x` : exposition. |

## Définitions OROSMO_*

Les définitions sont passées à `loadGraphics` ou `loadCompute` (paires nom, valeur). Les macros qui sélectionnent une variante
valent `"1"`. Colonne « Fixée par » : ce qui les passe ; sinon c'est à l'application.

| Définition | Shader | Effet | Fixée par |
|---|---|---|---|
| `OROSMO_INSTANCED` | `mesh.frag`, `mesh_instanced.vert` | Variante instanciée : tramage de Bayer 4x4 du LOD. | `InstanceRenderer`, `ShadowFeature` |
| `OROSMO_TRANSPARENT` | `mesh.frag` | Variante des objets transparents. | `TransparentFeature` |
| `OROSMO_TEXTURED` | `mesh.frag`, `mesh_textured.vert`, `debug_view.frag` | Maillage à uv et groupe de textures. | application |
| `OROSMO_SHADOWS` | `mesh.frag`, `mesh_pbr.frag` | Lecture des ombres (groupe 1, liaisons 6 et 7). | `ShadowFeature`, `PbrMaterial` |
| `OROSMO_FORWARD_PLUS` | `mesh.frag`, `mesh_pbr.frag`, `clusters.glsl` | Lumières lues par cluster. | `forwardPlusDefines()` |
| `OROSMO_CLUSTER_DEBUG` | `mesh.frag` | Avec forward+ : affiche le nombre de lumières par cluster. | `forwardPlusDefines(true)` |
| `OROSMO_IBL` | `mesh_pbr.frag`, `ibl.glsl` | Éclairage d'environnement. | `iblDefines()` |
| `OROSMO_AOV_<NOM>` | `mesh*.vert/frag`, `aov.glsl` | Emplacement de sortie d'un AOV : `MOTION`, `NORMALS`, `MATERIAL`, `OBJECT_ID`, `LINEAR_DEPTH`, `EMISSION`, `ALBEDO`. | `SceneRenderer::aovShaderDefines()` |
| `OROSMO_PBR_BASE_COLOR_MAP`, `_METALLIC_ROUGHNESS_MAP`, `_NORMAL_MAP`, `_OCCLUSION_MAP`, `_EMISSIVE_MAP` | `mesh_pbr.frag` | La texture correspondante existe. | `PbrMaterialDesc::features` |
| `OROSMO_PBR_ALPHA_MASK`, `_ALPHA_BLEND`, `_DOUBLE_SIDED`, `_UNLIT` | `mesh_pbr.frag` | Mode alpha, double face, sans éclairage. | `PbrMaterialDesc::features` |
| `OROSMO_SHADOW_ALPHA`, `OROSMO_SHADOW_COPY` | `shadow_caster.*`, `shadow_tile.frag` | Casters à alpha ; recopie d'une tuile. | `ShadowFeature` |
| `OROSMO_DEBUG_VIEW` | `debug_view.frag` | `1` albédo, `2` surdessin, `3` fil de fer. | `SceneRenderer::setDebugView` |
| `OROSMO_VIEW` | `debug_view_aov.frag` | `1` normales, `2` mouvement. | `SceneRenderer::setDebugView` |
| `OROSMO_HIZ` | `gpu_cull.comp` | Occlusion contre la Hi-Z. | `GpuDrivenFeature` |
| `OROSMO_HIZ_FROM_DEPTH` | `hiz_build.comp` | Niveau 0 de la Hi-Z lu depuis la profondeur de la scène. | `GpuDrivenFeature` |

Constantes définies dans les fichiers, pas par le C++ : `OROSMO_MAX_LIGHT_PROBES` (16, `ibl.glsl`), `OROSMO_SHADOW_CASCADES` (4) et
`OROSMO_SHADOW_SPOTS` (8) (`shadow_receiver.glsl`). `OROSMO_CLUSTER_ACCESS` vaut `readonly` par défaut et est vide dans le compute
qui remplit les clusters.

Chaque combinaison de définitions donne un programme différent. Configurez le renderer (plugins, AOV, vue de debug) avant de
compiler les programmes de maillage.

## Fichiers du moteur

| Dossier | Contenu |
|---|---|
| `shaders/rhi/` | Shaders des maillages, ombres, clusters, culling GPU, 2D, interface ; `frame_data.glsl`, `aov.glsl`, `clusters.glsl`, `ibl.glsl`, `shadow_receiver.glsl`, `light_block.glsl` ; `post/` pour les post-traitements. |
| `shaders/include/orosmo/` | Boîte à outils : `math`, `coords`, `depth`, `noise`, `pbr`, `lights`, `shadow` (`.glsl`). |

Deux triangles plein écran de référence : `blit.vert` (uv avec v = 0 en haut) et le sommet `gl_VertexIndex` de
[Écrire un plugin de rendu](/guides/avance/plugin-de-rendu/).

## Voir aussi

- [Écrire et recharger des shaders](/guides/avance/shaders/)
- [Pipeline de shaders](/interne/shaders/)
