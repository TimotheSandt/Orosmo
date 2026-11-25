---
title: "Animer un personnage : clips, machine à états, skinning"
description: "Afficher un modèle glTF animé : objets à créer dans l'ordre, clips, machine à états, mélange de poses et skinning GPU."
sidebar:
  order: 1
---

Animer un personnage glTF demande plusieurs objets qui se référencent entre eux. Aucun objet ne les regroupe : il faut créer la chaîne complète soi-même, dans l'ordre ci-dessous, puis appeler trois choses chaque image dans un ordre précis. Cette page donne la chaîne, puis détaille chaque pièce.

## La chaîne complète

Il y a deux phases : la mise en place (une fois) et l'image (à chaque tour de boucle).

Mise en place, dans cet ordre :

1. `assets::loadGltf` : le `Model` (maillages, peau, animations).
2. `assets::TextureCache`, `renderer::PbrShaderCache` et `renderer::PbrDefaults` : ce dont `RenderModel` a besoin.
3. `assets::RenderModel::create` avec `skinnable = true` : le modèle prêt à dessiner. Il doit rester à une adresse fixe (voir les pièges).
4. `anim::SkinnedModel::create` : le compute qui déforme le maillage.
5. Un `anim::Clip` par animation (`Clip::fromModel`).
6. Une `anim::StateMachine` initialisée avec `restPose(model)`, ses états, ses transitions, puis `start`.

Chaque image :

1. `machine.update(dt)` : avance le temps et calcule la pose.
2. `skinned.setPose(machine.pose())` : envoie les palettes d'os.
3. `skinned.dispatch(*encoder)` : enregistre le compute, **avant** `scene.beginFrame`, sur le même encodeur.
4. `scene.beginFrame`, `scene.setCamera`, `render.draw(scene, camera)`, `scene.endFrame`.

```cpp
#include <orosmo/anim/clip.hpp>
#include <orosmo/anim/skinning.hpp>
#include <orosmo/anim/state_machine.hpp>
#include <orosmo/assets/model.hpp>
#include <orosmo/assets/render_model.hpp>
#include <orosmo/assets/texture_cache.hpp>

namespace as = orosmo::assets;
namespace an = orosmo::anim;
namespace rd = orosmo::renderer;

struct Hero {
    as::Model model;
    std::unique_ptr<as::TextureCache> textures;
    std::unique_ptr<rd::PbrShaderCache> pbr;
    std::shared_ptr<rd::PbrDefaults> defaults;
    std::vector<an::Clip> clips;                       // rempli une fois, avant les états
    std::optional<an::StateMachine> machine;
    std::unique_ptr<as::RenderModel> render;           // adresse fixe : SkinnedModel la référence
    std::optional<an::SkinnedModel> skinned;           // déclaré après `render` : détruit avant lui
};

orosmo::core::Result<> loadHero(Hero &h, orosmo::rhi::Device &device, rd::ShaderLibrary &shaders,
                                rd::PipelineCache &pipelines, const std::filesystem::path &shaderDir) {
    auto loaded = as::loadGltf("res/heros.glb");
    if (!loaded) return std::unexpected(loaded.error());
    h.model = std::move(*loaded);

    h.textures = std::make_unique<as::TextureCache>(device);
    h.pbr = std::make_unique<rd::PbrShaderCache>(shaders, shaderDir);
    auto defaults = rd::PbrDefaults::create(device);
    if (!defaults) return std::unexpected(defaults.error());
    h.defaults = *defaults;

    // Le compute demande des tampons de stockage : on lit la capacité, on ne la suppose pas.
    const bool canSkin = an::SkinnedModel::supportedBy(device.capabilities());
    as::RenderModelOptions options;
    options.skinnable = canSkin;
    auto render = as::RenderModel::create(device, h.model, *h.textures, *h.pbr, h.defaults, options);
    if (!render) return std::unexpected(render.error());
    h.render = std::make_unique<as::RenderModel>(std::move(*render));

    if (canSkin) {
        auto skinned = an::SkinnedModel::create(device, shaders, pipelines, shaderDir, h.model, *h.render);
        if (!skinned) return std::unexpected(skinned.error());
        h.skinned.emplace(std::move(*skinned));
    }   // sinon le modèle reste en pose de repos

    for (std::size_t i = 0; i < h.model.animations.size(); ++i) {
        auto clip = an::Clip::fromModel(h.model, i);
        if (!clip) return std::unexpected(clip.error());
        h.clips.push_back(std::move(*clip));
    }
    h.machine.emplace(an::restPose(h.model));
    // h.clips ne change plus : les états gardent des pointeurs vers ses éléments.
    if (auto r = h.machine->addState({"marche", &h.clips[0], an::LoopMode::kLoop, 1.0f}); !r) return r;
    if (auto r = h.machine->addState({"salut", &h.clips[1], an::LoopMode::kOnce, 1.0f}); !r) return r;
    if (auto r = h.machine->addTransition({.from = "marche", .to = "salut", .duration = 0.3f, .condition = "saluer"}); !r) return r;
    if (auto r = h.machine->addTransition({.from = "salut", .to = "marche", .duration = 0.3f, .atEnd = true}); !r) return r;
    return h.machine->start("marche");
}

// Chaque image, dans l'ordre. `encoder` est celui de l'image ; la boucle complète est dans « Première application ».
void frame(Hero &h, rd::SceneRenderer &scene, rd::Camera &camera, orosmo::rhi::CommandEncoder &encoder, float dt) {
    (void)h.machine->update(dt);
    if (h.skinned) {
        (void)h.skinned->setPose(h.machine->pose());
        (void)h.skinned->dispatch(encoder);          // AVANT scene.beginFrame
    }
    (void)scene.beginFrame(encoder, {});
    (void)scene.setCamera(camera);
    (void)h.render->draw(scene, camera);
    // ... autres dessins ... puis scene.endFrame(...), voir « Première application »
}
```

Source : `apps/demo/src/Scene/anim_scene.cpp` (`AnimScene::init`, `buildModels`, `update`, `dispatch`, `render`), adapté. L'ordre des appels d'une image (deux `beginFrame`, encodeur, `endFrame`) est expliqué dans [Lire la boucle d'une application](/demarrer/premiere-application/). La démo : `apps/demo/bin/debug/main --scene anim` après `make all-debug`.

Pièges :

- Le `RenderModel` ne doit pas bouger après `SkinnedModel::create` (le skinning garde un pointeur vers lui) : le ranger dans un `unique_ptr`, comme ci-dessus. Détruire le `SkinnedModel` avant le `RenderModel` (déclarer le premier après le second dans la structure).
- `AnimState::clip` est un pointeur non possédé. Remplir le `std::vector<Clip>` complètement avant de prendre des adresses, et ne plus l'agrandir ensuite.
- `dispatch` enregistre du compute hors passe de rendu : il se place avant `scene.beginFrame`, sur l'encodeur de l'image, et il est soumis avant les dessins du modèle.
- Sans compute et sans tampons de stockage (`supportedBy` faux), `SkinnedModel::create` refuse (`kUnsupported`) et le modèle reste en pose de repos. Il n'y a pas de repli en vertex shader. `anim::skinVertices` fait le même calcul sur le CPU mais ne remplace pas le compute dans le dessin.
- Le skinning agit sur les nœuds qui portent une peau : la transformation du nœud du maillage est ignorée (règle glTF). La position dans le monde se donne par `render->setRootTransform(...)`.

## Poses et clips

Une `Pose` contient une `Transform` (translation, rotation, échelle) pour chaque nœud du modèle, dans le même ordre que `Model::nodes`. `restPose(model)` donne celle du fichier. `Clip::fromModel(model, i)` lit l'animation numéro `i` (`kOutOfRange` si elle n'existe pas, `kParse` si une piste vise un nœud absent). Un clip écrit dans une pose seulement les nœuds qu'il anime :

```cpp
an::Pose pose = an::restPose(model);
auto clip = an::Clip::fromModel(model, 0);
if (!clip) { /* ... */ }
(void)clip->sample(0.5f, pose);        // pose à t = 0,5 s (temps borné à [0, duration])
(void)skinned.setPose(pose);           // sans machine à états : jouer un clip à la main
```

| Appel | Effet |
|---|---|
| `Clip::name()`, `duration()`, `nodeCount()` | Nom, durée en secondes, nombre de nœuds attendus. |
| `Clip::sample(time, pose)` | Écrit les nœuds animés. `kInvalidArgument` si la pose n'a pas `nodeCount()` nœuds. |
| `Clip::addEvent(time, name)` | Ajoute un événement nommé (temps borné à `[0, duration]`). |
| `Clip::collectEvents(from, to, laps, out)` | Ajoute à `out` les événements franchis de `from` (exclu) à `to` (inclus), avec les tours de boucle. |
| `advanceClipTime(time, delta, duration, mode)` | Avance un temps local : renvoie un `ClipTime` (`time`, `wrapped`, `laps`, `finished`). |

`LoopMode::kOnce` tient la dernière pose à la fin ; `LoopMode::kLoop` revient à 0. Les interpolations de glTF `STEP`, `LINEAR` (avec slerp pour les rotations) et `CUBICSPLINE` sont prises en charge. Les pistes `weights` sont ignorées.

## Mélanger des poses

```cpp
auto mixed = an::blendPoses(walk, run, 0.3f);   // 30 % de `run` ; Result<Pose>
```

| Fonction | Effet |
|---|---|
| `blend(a, b, weight)` | Mélange deux `Transform` : translation et échelle linéaires, rotation par slerp. |
| `blendPoses(a, b, weight, mask = {})` | Mélange nœud par nœud. `mask` : un poids par nœud dans `[0, 1]`, multiplié par `weight` (un masque à 0 garde `a`). Sert à ne mélanger que le haut du corps. |
| `additiveDelta(pose, reference)` | Différence entre une pose et sa référence : base d'une couche additive. |
| `applyAdditive(base, delta, weight, mask = {})` | Ajoute la différence à `base`. Poids 0 : `base` inchangée ; 1 : différence entière. |
| `worldMatrices(parents, pose)` | Matrices monde d'une pose ; `nodeParents(model)` donne les parents. |

Toutes ces fonctions rendent `kInvalidArgument` si les tailles diffèrent.

## Machine à états

Une `StateMachine` joue un état à la fois et passe de l'un à l'autre par fondu enchaîné.

| Champ (`AnimState`) | Type | Défaut | Effet |
|---|---|---|---|
| `name` | `std::string` | : | Nom de l'état (unique : `kAlreadyExists` sinon). |
| `clip` | `const Clip *` | `nullptr` | Clip joué. Non possédé, doit survivre à la machine. Sa taille doit être celle de la pose. |
| `loop` | `LoopMode` | `kLoop` | Boucle ou une seule fois. |
| `speed` | `float` | `1.0f` | Vitesse de lecture. |

| Champ (`AnimTransition`) | Type | Défaut | Effet |
|---|---|---|---|
| `from` | `std::string` | : | État d'origine. Vide : depuis n'importe quel état (sauf `to`). |
| `to` | `std::string` | : | État d'arrivée (`kNotFound` s'il n'existe pas). |
| `duration` | `float` | `0.2f` | Durée du fondu en secondes. 0 : coupe nette. |
| `condition` | `std::string` | vide | Nom d'un booléen vrai (`setBool`) ou d'un déclencheur posé (`setTrigger`). Vide : aucune condition. |
| `atEnd` | `bool` | `false` | Seulement quand le clip de l'état d'origine a fini (`kOnce`) ou fait un tour (`kLoop`). |

Une transition doit avoir une condition, `atEnd`, ou les deux : sinon `addTransition` rend `kInvalidArgument` (elle ne partirait jamais).

| Appel | Effet |
|---|---|
| `addState(state)`, `addTransition(t)` | Déclarent le graphe. |
| `start(name)` | Entre dans l'état, sans fondu, au temps 0. |
| `setBool(name, value)`, `getBool(name)` | Paramètres booléens. |
| `setTrigger(name)` | Déclencheur : vrai jusqu'à ce qu'une transition le consomme. |
| `update(dt)` | Avance de `dt` secondes : temps, transitions, pose, événements. `kInvalidArgument` avant `start`. |
| `pose()` | Pose calculée par le dernier `update`. |
| `current()`, `currentTime()`, `inTransition()`, `blendWeight()` | État courant, temps, fondu en cours, poids de l'état courant dans le fondu. |
| `takeEvents()` | Rend et vide les `FiredEvent` (`state`, `name`) franchis pendant les `update`. |

```cpp
h.clips[0].addEvent(0.25f, "pas");        // événement porté par le clip, à 0,25 s
h.machine->setTrigger("saluer");          // consommé par la transition marche -> salut
(void)h.machine->update(dt);
for (const an::FiredEvent &e : h.machine->takeEvents()) {
    // e.state == "marche", e.name == "pas" : jouer un son
}
```

Une transition commencée va à son terme : pendant un fondu, aucune autre transition n'est évaluée. Tout est déterministe : les mêmes appels de `update` et de paramètres donnent la même pose, sans horloge.

## Limites

- ANIM-01, ANIM-02 sont Fait. ANIM-05 (IK, cibles de forme, mouvement de racine) et ANIM-06 (retargeting) sont Absents.
- Skinning en compute seulement, 4 os par sommet au plus ; pas de repli en vertex shader (G198).
- Un maillage GPU dessiné par plusieurs nœuds skinnés est déformé pour le premier seulement. Les normales et tangentes suivent `mat3(palette)` : exact sans échelle non uniforme dans les os.
- Pas d'objet qui regroupe modèle, clips, machine et skinning, ni de repli CPU automatique : c'est à l'application de les assembler comme ci-dessus.
- Pour beaucoup de personnages, voir [Foules instanciées](/guides/animation/foules/).

## Voir aussi

- [Importer un modèle glTF](/guides/contenus/gltf/)
- [Foules instanciées](/guides/animation/foules/)
- [Animer des propriétés](/guides/animation/proprietes/)
- [Lire la boucle d'une application](/demarrer/premiere-application/)
