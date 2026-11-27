---
title: "La façade app::Application"
description: "Écrire un programme complet avec app::Application : réglages, ligne de commande, rappels d'image, entités avec composants de rendu, maillages et modèles animés, jeu et vues rechargeables, accès à l'API bas niveau."
sidebar:
  order: 0
---

`app::Application` crée et relie tout ce qu'un jeu demande : fenêtre, périphérique (Vulkan, avec repli sur OpenGL),
renderers, lumières, ombres et HDR facultatifs, police et interface, simulation à pas fixe, logique de jeu et vue `.cui`
rechargeables, puis la boucle. Le programme décrit la scène et réagit aux images ; l'API bas niveau reste accessible.

En-têtes : `orosmo/app/application.hpp`, `orosmo/app/render_components.hpp`, `orosmo/app/default_game.hpp`. Bibliothèque :
`libapp.a`. Programme de départ : [Lire la boucle d'une application](/demarrer/premiere-application/) ; programme complet :
[tutoriel du jeu 3D](/demarrer/tutoriel-jeu-3d/).

## Exemple minimal

```cpp
#include <orosmo/app/application.hpp>
namespace app = orosmo::app;
namespace rd = orosmo::renderer;

int main(int argc, char **argv)
{
    return app::Application::main(argc, argv, {.title = "Cube"}, [](app::Application &a) -> orosmo::core::Result<> {
        auto cube = a.addMesh(rd::MeshBuilder(rd::MeshLayout::kColored).addBox({1, 1, 1}));
        if (!cube)
            return std::unexpected(cube.error());
        const auto e = a.spawn(app::MeshRenderer{.mesh = *cube});
        a.spawn(app::LightComponent{.type = rd::LightType::kAmbient, .intensity = 0.3f});
        a.spawn(app::LightComponent{.direction = {-0.6f, -0.8f, -0.5f}});
        a.onUpdate([e](app::Application &self, const app::FrameTime &t) {
            self.world().editTransform(e)->rotation *= glm::dquat(glm::dvec3(0.0, t.dt, 0.0));
        });
        return {};
    });
}
```

`Application::main` lit la ligne de commande, fait du dossier de l'exécutable le dossier courant, ouvre le journal, crée
l'application, appelle la fonction de préparation, puis tourne jusqu'à la fermeture. Pour garder la main sur la boucle :
`Application::create(réglages)` (`Result<std::unique_ptr<Application>>`), puis `run()` ou `frame()` à chaque image.

## Réglages

| Champ de `ApplicationSettings` | Défaut | Rôle |
| --- | --- | --- |
| `title`, `width`, `height` | `"Orosmo"`, 1280, 720 | fenêtre |
| `backend`, `backendFallback` | Vulkan, oui | backend demandé, repli sur OpenGL s'il ne démarre pas |
| `headless` | non | sans fenêtre (rendu hors écran, pas fixe) |
| `validation`, `vsync` | oui, non | couches de validation (Vulkan, `GL_KHR_debug`), synchronisation verticale |
| `windowed` | non | vrai : toujours en fenêtre ; faux : état par défaut de `platform::Window` (fenêtre en debug, plein écran sinon) |
| `shadows`, `hdr` | non, non | plugin d'ombres (`castShadows` des composants), scène HDR et tonemapping |
| `ui`, `font`, `fontSize`, `theme` | oui, Roboto 48, `themes/dark.json` | interface |
| `simulation` | 60 Hz | `sim::SimulationSettings` ([Simulation](/guides/scene/simulation/)) |
| `game`, `views` | aucun | logique de jeu et vues rechargeables (`defaultSettings` les remplit) |
| `logFile` | vide | journal de `main` ; vide : fichier horodaté du dossier de données de l'application |
| `maxFrameDelta`, `clock` | 250 ms, horloge système | borne du delta d'image ; horloge (`ManualClock` dans un test) |
| `frames`, `captureDirectory`, `captureFrames` | 0, vide, 3 | arrêt après N images ; capture de `scene.png` et `ui.png` puis arrêt |

Options lues par `main` (`usage()`) : `--api vulkan|opengl`, `--strict-backend`, `--headless`, `--size WxH`, `--frames N`,
`--capture <dossier>`, `--capture-frames N`, `--help`.

`app::defaultSettings(titre)` (`default_game.hpp`, à inclure par **un seul** fichier du programme) donne les réglages d'un
projet `orosmo init` : bibliothèque de jeu `game` rechargée en debug, liée avec `OROSMO_GAME_STATIC` en version finale ;
vues `.cui` de `src/` reconstruites par `make game` ; validation et `logs/log.log` en debug.

## Une image

Ordre : événements, temps (delta borné, ou pas fixe sans fenêtre), jeu (`GameModule::update`), simulation (`advance`),
`onUpdate`, interface, `onUi`, système de rendu (transformations, lecteurs d'animation, lumières, caméra active), dessins
des composants, `onRender`, interface, présentation.

| Rappel | Signature | Usage |
| --- | --- | --- |
| `onUpdate` | `void(Application &, const FrameTime &)` | logique par image |
| `onUi` | `void(Application &, const FrameTime &)` | interface en C++ |
| `onRender` | `Result<>(Application &, RenderFrame &)` | dessins en plus, dans la passe de scène (`RenderFrame::scene`, `camera`, `encoder`) |

`FrameTime` : `dt` (delta borné, pour animations, caméra et interface), `fixedStep` (pas de la simulation), `steps` (pas
joués à cette image), `alpha` (fraction du pas suivant). `pauseSimulation(vrai)` arrête les pas ; `pauseAnimations(vrai)`
fige les modèles animés `kSingle`. `quit()` termine la boucle.

## Entités et composants de rendu

`spawn(composant, transform)` crée une entité dans le monde de la simulation (`world()`) avec un composant défini par le
moteur :

| Composant | Champs principaux |
| --- | --- |
| `MeshRenderer` | `mesh` (poignée), `mode` (`kSingle` : un dessin par entité ; `kInstanced` : un dessin instancié par maillage), `visible`, `castShadows` |
| `LightComponent` | `type`, `color`, `intensity`, `direction` (tournée par l'entité), `range`, angles du projecteur, `castShadows` |
| `CameraComponent` | `fovDegrees`, `nearPlane`, `farPlane`, `forward`, `up`, `active` ; la première caméra active remplace `camera()` |
| `AnimatedModelComponent` | `model` (poignée), `mode` (`kSingle` : lecteur du modèle ; `kCrowd` : foule, `clip` et `time` par entité), `visible`, `castShadows` |

Les ressources sont tenues par la façade et désignées par des poignées `core::Handle<T>` : `addMesh(MeshBuilder)`,
`addMesh(MeshData)`, `addMesh(constructeur, matériau PBR)`, `createMaterial`, `loadAnimatedModel(chemin)`,
`addAnimatedModel(modèle)`. Une poignée retirée ne retombe jamais sur un autre objet.

## Jeu, vue, API bas niveau

- `setView(cui::MaVue{...})` installe la vue racine (rechargée en développement : [Vues rechargées à chaud](/guides/interface/rechargement-vues/)) ;
  `game()` rend le module de jeu ([Logique de jeu rechargeable](/guides/scene/logique-rechargeable/)).
- Accès bas niveau : `device()`, `backend()`, `fellBack()`, `scene()` (`SceneRenderer`), `lights()`, `shadows()`, `ui()`,
  `window()` (nul sans fenêtre), `simulation()`, `modelLoader()`, `readOutput()` (image rendue), `resourcePath(chemin)`.

La démo `apps/demo` n'utilise pas la façade : elle garde l'API bas niveau, décrite dans
[Lire la boucle d'une application](/demarrer/premiere-application/#ce-que-la-façade-crée-dans-lordre).

## Limites

- Les tests tournent sans fenêtre ; la boucle fenêtrée n'est essayée qu'à la main (G350, G374).
- Non exposés par les réglages : anticrénelage, post-traitements autres que le tonemapping, `ShadowSettings`, images en vol
  (G357) ; ni outils de debug ni manette branchés (G380).
- Le rendu lit le monde de la simulation directement, sans instantané ni interpolation (G356).

## Voir aussi

- [Choisir le backend graphique](/guides/application/peripherique/)
- [Cadence d'images, mode sans fenêtre, captures](/guides/application/cadence-captures/)
- [Entités, composants et transformations](/guides/scene/entites-composants/)
