---
title: "Écrire un plugin de rendu"
description: "Ajouter ses propres passes au graphe de l'image avec un RenderFeature : déclarer des passes raster ou génériques, lire la scène, écrire la sortie, lire FrameData."
sidebar:
  order: 2
  label: Plugin de rendu
---

Un plugin de rendu (`RenderFeature`) ajoute des passes au graphe de l'image sans modifier le cœur du renderer. Les ombres,
les post-traitements, l'anticrénelage, le 2D mélangé et le rendu piloté par GPU en sont. À chaque `SceneRenderer::endFrame`,
le renderer appelle `prepare` de chaque plugin ; quand le graphe doit être reconstruit, il déclare ses passes (scène, mise à
l'échelle, historique), puis `declarePasses` de chaque plugin dans l'ordre d'enregistrement, puis ses post-traitements. Il
exécute le graphe et appelle enfin `frameEnded`.

## Exemple minimal

Dans les extraits, `device`, `shaders`, `pipelines`, `renderer` et `shaderDir` suivent [Lire la boucle d'une application](/demarrer/premiere-application/) (section « Conventions des exemples »).

Un plugin qui dessine un triangle plein écran par-dessus la sortie, avec un shader qui lit `FrameData`. Adapté de
`tests/renderer/render_graph_tests.cpp` et de
[`apps/demo/src/Game/frame_overlay.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/Game/frame_overlay.cpp).

```cpp
#include <orosmo/renderer/render_feature.hpp>
#include <orosmo/renderer/pipeline_cache.hpp>

namespace rd = orosmo::renderer;
namespace rhi = orosmo::rhi;

class PulseFeature final : public rd::RenderFeature
{
  public:
    PulseFeature(rhi::PipelineHandle pipeline) : pipeline_(pipeline) {}

    std::string name() const override { return "game.pulse"; }

    orosmo::core::Result<> declarePasses(rd::RenderGraphBuilder &builder) override
    {
        const rd::RgTexture output = builder.resources().output;
        builder.graph()
            .addPass("game.pulse", rd::PassType::kRaster,
                     [pipeline = pipeline_](rd::RenderPassContext &ctx) -> orosmo::core::Result<>
                     {
                         ctx.encoder().setPipeline(pipeline);
                         ctx.encoder().setBindGroup(0, ctx.frame()->group);   // groupe 0 : caméra, lumières, FrameData
                         ctx.encoder().draw(3);
                         return {};
                     })
            .writeColor(output, rhi::LoadOp::kLoad);                          // garde la scène déjà dessinée
        return {};
    }

  private:
    rhi::PipelineHandle pipeline_;
};

// Enregistrement, une fois.
auto added = renderer.addFeature(std::make_unique<PulseFeature>(*pipeline));   // pipeline : voir « Écrire des shaders »
if (!added) { /* added.error().code == kUnsupported ou kAlreadyExists */ }
```

La passe est déclarée dans `declarePasses`, pas à l'enregistrement : le graphe est reconstruit à chaque image. Le pipeline se
crée comme dans [Écrire et recharger des shaders](/guides/avance/shaders/), avec le format de la sortie de `endFrame`.
`addFeature` renvoie un `RenderFeature *` non possédé (le renderer garde le plugin) ; `static_cast` vers le type concret pour
l'utiliser ensuite.

## Les méthodes à surcharger

| Méthode | Défaut | Rôle |
|---|---|---|
| `name()` | obligatoire | Nom unique : un deuxième plugin du même nom est refusé (`kAlreadyExists`). |
| `declarePasses(builder)` | obligatoire | Déclare passes et ressources de l'image. |
| `supports(capabilities)` | `true` | Faux : refus à l'enregistrement (`kUnsupported`). Pour les capacités optionnelles. |
| `prepare(frame)` | rien | Avant `declarePasses`, avec `FrameData` : caméra, temps, lumière principale. Préparation CPU. |
| `requiredAovs()` | aucun | AOV que le plugin lit ; ils sont ajoutés à la passe de scène tant qu'il est enregistré. |
| `wantsJitter()` | `false` | Vrai : caméra décalée d'un sous-pixel à chaque image (TAA). |
| `postProcessesScene()` | `false` | Vrai : le plugin modifie ou échantillonne la couleur de scène après la passe de scène. |
| `passSignature()` | aucune | Résumé de ce que `declarePasses` déclarerait à cette image (voir ci-dessous). Sans signature, le graphe est reconstruit à chaque image. |
| `frameEnded()` | rien | Fin de chaque `endFrame`, réussi ou non : oublier ce qui ne valait que pour l'image. |

## Garder le graphe d'une image à l'autre

Le renderer garde le graphe compilé tant que rien de sa structure ne change : plugins, cibles, tailles, et la
`passSignature()` de chaque plugin, lue après `prepare`. Pendant ce temps, `declarePasses` n'est **pas** rappelé : les passes
déjà déclarées s'exécutent de nouveau. Pour qu'un plugin en profite :

- ses passes lisent l'état de l'image au moment de l'exécution (membres du plugin, `ctx.texture(...)`, `ctx.frame()`), jamais
  une valeur de l'image copiée dans la capture du lambda ;
- `declarePasses` ne fait aucun travail par image (envoi de données, création de groupes) : c'est le rôle de `prepare` ou de la
  passe elle-même ;
- la signature change dès que les passes, leurs accès, les ressources importées (et leurs handles) ou les liaisons
  `addFrameBinding` changeraient. `mixPassSignature(graine, valeur)` mêle des entiers et des handles.

```cpp
std::optional<std::uint64_t> passSignature() const override
{
    return orosmo::renderer::mixPassSignature(enabled_ ? 1 : 0, texture_); // handle importé par declarePasses
}
```

## Ce que le graphe fournit

`builder.resources()` donne : `sceneColor`, `sceneDepth` (invalide si absente), `motionVectors` (invalide si non activés),
`output`, `aovs[]`, `renderSize` et `outputSize`. `builder.aov(Aov)` rend la texture d'un AOV, invalide si personne ne l'a
demandé. `builder.capabilities()` rend les capacités du périphérique.

| Appel | Effet |
|---|---|
| `graph().addPass(nom, type, exécution)` | Déclare une passe. `kRaster` : le graphe ouvre une passe de rendu sur les attachements déclarés. `kGeneric` : aucune passe ouverte, vous enregistrez ce que vous voulez (compute, copies, vos propres passes). |
| `.read(tex)`, `.readFinal(tex)` | Lit la version courante, ou le contenu final de l'image. |
| `.write(tex)` | Écriture hors attachement (passe générique, stockage, copie). |
| `.writeColor(tex, load, clear)`, `.writeDepth(tex, load, clear)` | Attachements d'une passe raster. `kLoad` lit l'ancien contenu et ordonne la passe après l'écrivain précédent. |
| `.sideEffect()` | La passe n'est jamais écartée. |
| `graph().createTexture(nom, desc)`, `createBuffer` | Ressource transitoire, allouée si une passe gardée l'utilise. |
| `graph().importTexture(nom, handle, desc)` | Ressource externe. L'écrire rend la passe visible. |
| `builder.addSceneInput(ressource)` | La passe de scène lit la ressource : la passe qui l'écrit s'exécute avant (carte d'ombre, tampon de clusters). |
| `builder.addFrameBinding(entry)` | Ajoute une liaison au groupe 0 de l'image, aux liaisons 3 à 7. Refusée hors de cette plage ou si déjà prise. |

Une passe qui n'a aucun effet visible (aucun écrit d'une ressource importée, aucun lecteur gardé) est écartée du graphe. Les
règles de dépendance complètes sont dans [Renderer et graphe de passes](/interne/renderer/).

## Où écrire : avant ou après la mise à l'échelle

- Écrire `resources().output` : la passe s'exécute après la mise à l'échelle (interface, superposition).
- Lire `sceneColor` avec `read` et la réécrire : post-traitement, avant la mise à l'échelle.
- Écrire dans une texture transitoire lue par la scène (`addSceneInput`) : la passe s'exécute avant la scène.

## Ordre d'enregistrement

L'ordre des passes est l'ordre des appels d'`addFeature`, il n'est pas déclaré. Ordre recommandé : plugins de la scène (ombres,
transparents, 2D du monde), puis post-traitements, puis superpositions, puis interface et présentation. Réenregistrez les plugins
d'interface après les autres (`removeFeature`, puis `addFeature`), comme le fait la démo.

## Lire FrameData dans un shader

`#include "frame_data.glsl"` donne `frame.view`, `frame.viewProj`, `frame.time` (secondes, delta, numéro d'image, graine de bruit),
`frame.viewport`, `frame.sunDirection`, `frame.sunColor`, `frame.exposure`, etc. : voir
[Conventions GLSL](/reference/conventions-glsl/). La disposition est la même que `orosmo::renderer::FrameData` (std140, 640
octets, vérifiée par `static_assert`).

## Capacités optionnelles

Un plugin qui a besoin de compute ou de tampons de stockage les lit dans `Capabilities` et les refuse dans `supports` : le moteur
ne les suppose jamais. C'est ce que font `GpuDrivenFeature::supportedBy` et `CrowdRenderer::supportedBy`.

## Limites

- Il n'existe pas encore de plugin voxel ni de tracé de rayons ; ils s'écriraient avec le mécanisme de cette page.
- Détail : [État du moteur](/projet/etat/).

## Voir aussi

- [Renderer et graphe de passes](/interne/renderer/)
- [Écrire et recharger des shaders](/guides/avance/shaders/)
- [Mélanger 2D et 3D](/guides/rendu-2d/melange-2d-3d/)
