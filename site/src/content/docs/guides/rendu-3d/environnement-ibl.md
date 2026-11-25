---
title: "Éclairage d'environnement et sondes"
description: "Éclairer les matériaux PBR par une image d'environnement (IBL) et par des sondes de lumière locales."
sidebar:
  order: 4
  label: Environnement (IBL)
---

Les noms des variables (`device`, `renderer`…) et les chemins de ressources des extraits sont expliqués dans les [conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples).

`rd::IblFeature` éclaire les matériaux PBR par une image d'environnement : irradiance diffuse en harmoniques sphériques,
reflets par un cube préfiltré (un niveau de mip par rugosité) et une table BRDF. Des sondes placées dans le monde
remplacent l'environnement près d'elles, pour les objets mobiles. Seuls les matériaux PBR compilés avec `rd::iblDefines()`
le lisent.

Les exemples suivent les noms de [Dessiner une scène](/guides/rendu-3d/scene/#noms-utilisés-dans-les-exemples) (`device`, `renderer`, `shaders`, `pipelines`, `shaderDir`, `lights`), plus `namespace as = orosmo::assets;`.

## Exemple minimal

```cpp
auto created = rd::IblFeature::create(device, shaders, pipelines, shaderDir);   // kUnsupported sans compute
if (!created) { /* garder l'éclairage sans IBL */ }
auto added = renderer.addFeature(std::move(*created));
if (!added) { /* erreur */ }
auto *ibl = static_cast<rd::IblFeature *>(*added);

if (auto r = ibl->setEnvironment(rd::uniformEquirect(glm::vec3(0.5f), 32, 16)); !r) { /* erreur */ }

// Matériaux : la variante IBL, en macros en plus (gltfModel : as::Model rendu par as::loadGltf)
as::RenderModelOptions options;
options.extraDefines = rd::iblDefines();
auto model = as::RenderModel::create(device, gltfModel, textures, pbr, *defaults, options);
```

Adapté de [`tests/renderer/ibl_tests.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tests/renderer/ibl_tests.cpp).
La démo n'utilise pas l'IBL : il n'y a pas de commande de démo pour cette page.

Pour un matériau créé à la main : `pbr.program(material->features(), rd::iblDefines())` (voir
[Matériaux PBR](/guides/rendu-3d/materiaux-pbr/)).

## Fournir l'image d'environnement

`rd::EquirectImage` est une image équirectangulaire de radiance linéaire, en flottants : `width`, `height` et `texels`
(`width × height` valeurs `glm::vec3`, première ligne en haut, +Y). Le moteur ne charge pas de fichier HDR (`.hdr`, `.exr`) :
l'image se remplit par le code ou par un chargeur de votre choix.

```cpp
rd::EquirectImage sky;
sky.width = 64;
sky.height = 32;
sky.texels.resize(std::size_t{sky.width} * sky.height);
for (std::uint32_t y = 0; y < sky.height; ++y)
    for (std::uint32_t x = 0; x < sky.width; ++x) {
        const glm::vec3 dir = rd::equirectDirection({(x + 0.5f) / sky.width, (y + 0.5f) / sky.height});
        sky.texels[std::size_t{y} * sky.width + x] = dir.y > 0.0f ? glm::vec3(0.6f, 0.75f, 1.0f) : glm::vec3(0.1f);
    }
if (auto r = ibl->setEnvironment(sky); !r) { /* kInvalidArgument : image vide ou trop petite (1 × 1) */ }
```

`rd::equirectUv(direction)` et `rd::equirectDirection(uv)` font la correspondance (u = 0,5 regarde vers -Z, v = 0 en haut).
`setEnvironment` envoie l'image, lance le préfiltrage en compute (soumis tout de suite) et calcule l'irradiance sur le CPU.

## Sondes

```cpp
rd::ProbeSet probes;
if (auto r = probes.add({.position = {0.0f, 1.0f, -1.0f}, .radius = 20.0f,
                         .irradiance = rd::ShIrradiance::uniform({1.0f, 0.0f, 0.0f})}); !r) {
    /* kInvalidArgument : plus de 16 sondes ou rayon <= 0 */
}
ibl->setProbes(probes);                                    // copiées ; appliquées à l'image suivante
```

Chaque sonde pèse `(1 - d / rayon)²` au point éclairé. Si la somme des poids dépasse 1, les sondes sont moyennées ; sinon
l'environnement complète avec le poids restant. Une sonde se remplit à la main (`ShIrradiance::uniform`) ou depuis une image
(`rd::projectEquirect(image)`). Au plus `rd::kMaxLightProbes` (16) sondes.

## Réglages

| Option | Type | Défaut | Effet |
|---|---|---|---|
| `IblSettings::cubeSize` | `uint32_t` | `128` | Côté du niveau 0 du cube préfiltré. |
| `IblSettings::mipLevels` | `uint32_t` | `6` | Niveaux de rugosité. |
| `IblSettings::samples` | `uint32_t` | `64` | Échantillons GGX par texel du préfiltrage. |
| `IblSettings::brdfLutSize` | `uint32_t` | `64` | Côté de la table BRDF. |
| `IblSettings::diffuseIntensity` | `float` | `1` | Part diffuse. |
| `IblSettings::specularIntensity` | `float` | `1` | Part spéculaire. |
| `setIntensity(diffuse, specular)` | `float, float` | `1, 1` | Change les deux intensités à l'image suivante ; `0, 0` coupe l'IBL. |

Les réglages se passent en dernier argument de `IblFeature::create`. Sans `setEnvironment`, l'environnement est noir.
Unités : une surface blanche mate dans un environnement uniforme de radiance L renvoie L, comme pour les lumières.

## Limites

- Les sondes ne sont pas capturées depuis la scène : on les remplit à la main ou depuis une image.
- Pas de ciel dessiné (skybox) ni de rotation de l'environnement ; le préfiltrage est fait en une fois à chaque
  `setEnvironment`.
- L'IBL n'atteint que les matériaux PBR dessinés par `drawMesh`, pas les objets instanciés ni `mesh.frag`.
- Pas de chargement d'image HDR dans `assets` (images 8 bits seulement).
- Pas d'occlusion ambiante écran, de réflexions écran ni d'éclairage global.

Détail et suivi : [État du projet](/projet/etat/).

## Voir aussi

- [Matériaux PBR](/guides/rendu-3d/materiaux-pbr/)
- [Lumières](/guides/rendu-3d/lumieres/)
- [HDR et post-traitement](/guides/rendu-3d/post-traitement/)
