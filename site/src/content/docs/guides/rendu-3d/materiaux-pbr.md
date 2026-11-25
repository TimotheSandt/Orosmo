---
title: "Matériaux PBR"
description: "Créer un PbrMaterial (couleur, métal, rugosité, normale, occlusion, émission), ses variantes de shader et ses instances."
sidebar:
  order: 2
  label: Matériaux PBR
---

Un `rd::PbrMaterial` décrit une surface au modèle métal/rugosité de glTF : couleur de base, métal, rugosité, carte de
normales, occlusion et émission. Il possède son bloc de paramètres et son groupe de liaison (groupe 1) ; il se lie à un
`rd::Mesh` aux sommets PBR et se dessine par `SceneRenderer::drawMesh`. Les modèles glTF créent leurs matériaux tout seuls
(`assets::RenderModel`) ; cette page sert à les créer à la main ou à les modifier.

Dans le code : `namespace rd = orosmo::renderer;`, `namespace as = orosmo::assets;`.

## Exemple minimal

```cpp
auto defaults = rd::PbrDefaults::create(**device);            // textures neutres, une fois pour tous les matériaux
rd::PbrShaderCache pbrShaders(shaders, shaderDir);            // un programme par variante
as::TextureCache textures(**device);

auto albedo = textures.load("textures/brique.png", {.role = as::TextureRole::kColor}); // couleur : sRGB
if (!defaults || !albedo) { /* erreur */ }

rd::PbrMaterialDesc desc;
desc.name = "brique";
desc.params.metallic = 0.0f;
desc.params.roughness = 0.8f;
desc.maps[static_cast<std::size_t>(rd::PbrMap::kBaseColor)] = *albedo;

auto material = rd::PbrMaterial::create(**device, desc, *defaults);
if (!material) { /* erreur */ }
auto program = pbrShaders.program((*material)->features());   // variante : « carte de couleur présente »
if (!program) { /* erreur */ }

// `mesh` : rd::MeshData avec attributeSizes = kPbrVertexAttributeSizes (16 flottants par sommet)
mesh.setProgram(*program);
mesh.setMaterialGroup((*material)->group());                  // non possédé : garder `material` vivant
mesh.setAovMaterial({(*material)->blend(), {}});
```

Le code suit ce que fait `assets::RenderModel::create`
([`src/assets/render_model.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/src/assets/render_model.cpp)).

## Sommets PBR

Un maillage PBR a 16 flottants par sommet, dans cet ordre (`rd::kPbrVertexAttributeSizes` = `{3, 3, 2, 4, 4}`) :
position, normale, uv, tangente (w = signe de la bitangente), couleur. Si vous partez d'une `assets::MeshPrimitive`,
`assets::pbrVertices(primitive)` remplit ce tableau et calcule les normales et tangentes absentes.

## Description et cartes

`PbrMaterialDesc::maps` a une case par `rd::PbrMap`. Une case vide (pointeur nul) reçoit la texture neutre de
`PbrDefaults` : blanc pour la couleur, l'occlusion et le métal/rugosité (seuls les facteurs comptent), normale plate.

| Carte (`PbrMap`) | Liaison | Contenu | Rôle à donner au `TextureCache` |
|---|---|---|---|
| `kBaseColor` | 0 | couleur de base, alpha | `TextureRole::kColor` (sRGB) |
| `kMetallicRoughness` | 1 | B = métal, G = rugosité | `TextureRole::kData` (linéaire) |
| `kNormal` | 2 | normale en espace tangent | `TextureRole::kNormal` |
| `kOcclusion` | 3 | R = occlusion | `TextureRole::kData` |
| `kEmissive` | 4 | émission | `TextureRole::kColor` (sRGB) |

La liaison 5 porte le bloc de paramètres ; 6 et 7 sont réservées aux ombres. Une texture de couleur chargée en linéaire
(ou une carte de données en sRGB) ne provoque aucune erreur : l'image est seulement fausse.

| Option (`PbrMaterialDesc`) | Type | Défaut | Effet |
|---|---|---|---|
| `name` | `std::string` | `"material"` | Nom (journal, debug). |
| `params` | `PbrParams` | voir ci-dessous | Facteurs du matériau. |
| `maps` | `array<shared_ptr<const Texture>, 5>` | vides | Cartes ; vide = texture neutre. |
| `blend` | `BlendMode` | `kOpaque` | `kOpaque`, `kMask` ou `kBlend` (modes alpha de glTF). |
| `doubleSided` | `bool` | `false` | Normale retournée sur les faces arrière. Pensez à `mesh.setCullMode(rhi::CullMode::kNone)`. |
| `unlit` | `bool` | `false` | Couleur de base seule, sans éclairage. |

| Option (`PbrParams`) | Type | Défaut | Effet |
|---|---|---|---|
| `baseColorFactor` | `glm::vec4` | `(1, 1, 1, 1)` | Multiplie la carte de couleur (linéaire). |
| `metallic` | `float` | `1` | Facteur de métal. |
| `roughness` | `float` | `1` | Facteur de rugosité. |
| `normalScale` | `float` | `1` | Force de la carte de normales. |
| `occlusionStrength` | `float` | `1` | Force de l'occlusion. |
| `emissive` | `glm::vec3` | `(0, 0, 0)` | Émission linéaire (facteur × force). |
| `alphaCutoff` | `float` | `0.5` | Seuil de `kMask`. |

Les valeurs par défaut sont celles de glTF (métal 1, rugosité 1) : un matériau créé sans réglage ressemble à du métal brut.

## Variantes de shader

Chaque combinaison de cartes présentes et de modes est une variante : `desc.features()` renvoie des bits `rd::PbrFeature`
(`kPbrBaseColorMap`, `kPbrNormalMap`, `kPbrAlphaMask`, `kPbrDoubleSided`, `kPbrUnlit`, `kPbrShadows`…), et
`rd::pbrDefines(features)` les traduit en macros `OROSMO_PBR_*`. `PbrShaderCache::program(features, extraDefines)` compile
`mesh_pbr.vert` + `mesh_pbr.frag` une fois par couple (variante, macros en plus) et rend ensuite le même programme.

Les macros en plus viennent des autres systèmes, à assembler vous-même avant de compiler :

```cpp
std::vector<rd::Define> extra = forwardPlusOn ? rd::forwardPlusDefines() : std::vector<rd::Define>{};
const auto aov = (*scene)->aovShaderDefines();     // après avoir installé TAA, modes de debug, AOV
extra.insert(extra.end(), aov.begin(), aov.end());
const auto ibl = rd::iblDefines();                 // seulement si un IblFeature est installé
extra.insert(extra.end(), ibl.begin(), ibl.end());
auto program = pbrShaders.program((*material)->features(), extra);
```

Pour qu'un matériau reçoive les ombres, passez le plugin à la création : `rd::PbrMaterial::create(**device, desc,
*defaults, shadowFeature)` ajoute le bit `kPbrShadows` et les liaisons 6 et 7 (voir [Ombres](/guides/rendu-3d/ombres/)).

## Instances

Une instance partage les textures et la variante d'un matériau, avec ses propres paramètres (son tampon, son groupe).
Elle ne coûte pas de nouveau programme.

```cpp
rd::PbrParams params = (*material)->params();
params.baseColorFactor = {0.0f, 0.0f, 1.0f, 1.0f};
auto blue = (*material)->instantiate(params, "brique-bleue");
mesh.setMaterialGroup((*blue)->group());

params.baseColorFactor = {0.0f, 1.0f, 0.0f, 1.0f};
(void)(*blue)->setParams(params);                 // en place : les images en vol gardent l'ancienne valeur
```

Adapté de [`tests/assets/pbr_tests.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tests/assets/pbr_tests.cpp)
(cas « REN-11 instance »).

:::caution
`PbrMaterial::rebuild(shadows)` (après un changement d'atlas d'ombres) détruit l'ancien groupe. Chaque maillage qui l'a
reçu doit recevoir `material->group()` et, si la réception d'ombres change, le programme de `material->features()`
avant son prochain dessin. `RenderModel::rebuildMaterials` le fait pour un modèle glTF.
:::

## Limites

- REN-11 (Partiel) : les variantes sont des macros de préprocesseur, pas des constantes de spécialisation (le RHI n'en a
  pas, G196).
- REN-12 (Absent) : pas d'extensions de matériaux (vernis, tissu, transmission, parallaxe).
- G195 : les objets instanciés (`InstanceRenderer`, `ObjectSet`) et pilotés par GPU n'utilisent pas `PbrMaterial` ; un
  seul jeu d'UV ; tangentes par triangle ; primitives `kBlend` d'un glTF non triées.
- Le `Mesh` ne garde pas son matériau : détruire le matériau rend le dessin invalide sans erreur à l'appel.

État détaillé : [État du projet](/projet/etat/).

## Voir aussi

- [Charger un glTF](/guides/contenus/gltf/)
- [Textures](/guides/contenus/textures/)
- [Éclairage d'environnement](/guides/rendu-3d/environnement-ibl/)
- [HDR et post-traitement](/guides/rendu-3d/post-traitement/) (pourquoi un modèle PBR paraît sombre sans tonemapping)
