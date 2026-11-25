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

Les exemples suivent les noms de [Dessiner une scène](/guides/rendu-3d/scene/#noms-utilisés-dans-les-exemples) (`device`, `renderer`, `shaders`, `pipelines`, `shaderDir`, `lights`), plus `namespace as = orosmo::assets;`.

## Exemple minimal

```cpp
auto defaults = rd::PbrDefaults::create(device);            // textures neutres, une fois pour tous les matériaux
rd::PbrShaderCache pbr(shaders, shaderDir);            // un programme par variante
as::TextureCache textures(device);

auto albedo = textures.load(GET_RESOURCE_PATH("textures/brique.png"), {.role = as::TextureRole::kColor}); // couleur : sRGB
if (!defaults || !albedo) { /* erreur */ }

rd::PbrMaterialDesc desc;
desc.name = "brique";
desc.params.metallic = 0.0f;
desc.params.roughness = 0.8f;
desc.maps[static_cast<std::size_t>(rd::PbrMap::kBaseColor)] = *albedo;

auto material = rd::PbrMaterial::create(device, desc, *defaults);
if (!material) { /* erreur */ }
auto program = pbr.program((*material)->features());   // variante : « carte de couleur présente »
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
const auto aov = renderer.aovShaderDefines();     // après avoir installé TAA, modes de debug, AOV
extra.insert(extra.end(), aov.begin(), aov.end());
const auto ibl = rd::iblDefines();                 // seulement si un IblFeature est installé
extra.insert(extra.end(), ibl.begin(), ibl.end());
auto program = pbr.program((*material)->features(), extra);
```

Pour qu'un matériau reçoive les ombres, passez le plugin à la création : `rd::PbrMaterial::create(device, desc,
*defaults, shadows)` ajoute le bit `kPbrShadows` et les liaisons 6 et 7 (voir [Ombres](/guides/rendu-3d/ombres/)).

## Métal sans environnement

Une lumière ambiante n'éclaire que la part diffuse : `couleur × (1 − métal) × occlusion`. Un métal pur (`metallic = 1`,
la valeur par défaut de glTF) ne renvoie donc rien sous l'ambiante seule ; il n'a que les reflets des lumières
directionnelles, ponctuelles et projecteurs, et paraît presque noir ailleurs (vérifié par `tests/assets/pbr_tests.cpp`).

Pour qu'un métal ait des reflets partout, ajoutez un éclairage d'environnement, même uniforme, et compilez les matériaux
avec `rd::iblDefines()` :

```cpp
auto created = rd::IblFeature::create(device, shaders, pipelines, shaderDir);   // demande le compute
if (created) {
    auto added = renderer.addFeature(std::move(*created));
    auto *ibl = added ? static_cast<rd::IblFeature *>(*added) : nullptr;
    if (ibl) (void)ibl->setEnvironment(rd::uniformEquirect(glm::vec3(0.3f)));       // ciel gris uniforme
}
```

Voir [Éclairage d'environnement et sondes](/guides/rendu-3d/environnement-ibl/).

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
(cas des instances).

:::caution
`PbrMaterial::rebuild(shadows)` (après un changement d'atlas d'ombres) détruit l'ancien groupe. Chaque maillage qui l'a
reçu doit recevoir `material->group()` et, si la réception d'ombres change, le programme de `material->features()`
avant son prochain dessin. `RenderModel::rebuildMaterials` le fait pour un modèle glTF.
:::

## Limites

- Les variantes sont des macros de préprocesseur compilées une par une : le RHI n'a pas de constantes de spécialisation.
- Pas d'extensions de matériaux (vernis, tissu, transmission, parallaxe).
- Les objets instanciés (`InstanceRenderer`, `ObjectSet`) et le rendu piloté par le GPU n'utilisent pas `PbrMaterial`.
- Un seul jeu d'UV ; tangentes calculées par triangle ; les primitives glTF transparentes ne sont pas triées.
- Le `Mesh` ne garde pas son matériau : détruire le matériau rend le dessin invalide sans erreur à l'appel.

Détail et suivi : [État du projet](/projet/etat/).

## Voir aussi

- [Charger un glTF](/guides/contenus/gltf/)
- [Textures](/guides/contenus/textures/)
- [Éclairage d'environnement](/guides/rendu-3d/environnement-ibl/)
- [HDR et post-traitement](/guides/rendu-3d/post-traitement/) (pourquoi un modèle PBR paraît sombre sans tonemapping)
