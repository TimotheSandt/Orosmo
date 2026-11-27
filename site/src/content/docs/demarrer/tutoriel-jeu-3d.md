---
title: "Tutoriel : un petit jeu 3D"
description: "De orosmo init à un personnage glTF animé, éclairé par un soleil qui projette des ombres, avec une caméra au clavier et un menu .cui."
sidebar:
  order: 4
  label: Tutoriel jeu 3D
---

Ce tutoriel part d'un projet créé par `orosmo init` et aboutit à un programme complet : un personnage glTF animé sur un
sol, un soleil dont l'ombre suit l'animation, un rendu HDR avec tonemapping, une caméra pilotée au clavier et à la souris,
et un menu `.cui` qu'Échap ouvre et ferme, avec un bouton Quitter.

Le programme tient en un fichier, `src/main.cpp`, plus l'état du menu et sa vue. Il est écrit avec la façade
`app::Application`, qui possède la fenêtre, le périphérique, les renderers, l'interface et la boucle : le programme ne décrit
que sa scène (des entités avec des composants de rendu) et ce qu'il fait à chaque image. L'API bas niveau reste disponible
(`Application::scene()`, `device()`…) pour ce que la façade ne couvre pas.

Les blocs de code de cette page, mis bout à bout, forment ces trois fichiers. `make e2e-init` (dépôt du moteur) les extrait
de la page (`tools/extract_doc_code.py`) et les compile dans un projet créé par `orosmo init`, avec les options du projet
(`-Wall -Wextra -Wpedantic -Werror`). Le programme a aussi été lancé sous Linux, sans fenêtre, avec OpenGL et Vulkan et le
personnage écrit par la démo (voir plus bas) : personnage animé, ombre portée sur le sol, aucune erreur de validation
(captures regardées à la main).

## Préparer le projet

```bash
orosmo init monjeu && cd monjeu          # voir « Créer un projet avec orosmo init »
eng=$(ORS_NO_PROGRESS=1 make -s info | sed -n '/^Engine:/{s/^Engine: *//;s/ (OROSMO_VERSION.*//;p;}')   # moteur utilisé par make (OROSMO_VERSION de config.mk)
[ -n "$eng" ] && test -d "$eng/res" && mkdir -p res && cp -R "$eng/res/." res/               # police et thèmes de ce moteur dans res/
mkdir -p res/models                      # le personnage ira dans res/models/hero.glb (voir plus bas)
rm src/HudView.cui src/hud_state.hpp     # le panneau du projet de démarrage est remplacé par le menu
```

`src/game/`, la logique de jeu du projet de démarrage, reste : c'est la bibliothèque rechargée à chaud en développement, où
iront les règles du jeu (voir [Logique de jeu rechargeable](/reference/construction/#logique-de-jeu-rechargeable)). Elle ne fait rien ici : son
système fait tourner les entités qui ont un composant `Spin`, et ce programme n'en crée pas.

Pourquoi copier le `res/` du moteur : dès que le projet a son propre dossier `res/`, c'est le seul copié à côté de l'exécutable
(voir [Chemins des ressources](/demarrer/premiere-application/#chemins-des-ressources)).

Il faut un modèle glTF avec une peau (skin) et au moins une animation. Le chemin est la constante `kHeroModel`, en haut
de `src/main.cpp` (section 1), relative au dossier des ressources : `models/hero.glb` par défaut (`res/models/hero.glb`).

Le modèle essayé est CesiumMan, du dépôt d'exemples de Khronos
([glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CesiumMan)) : un personnage
d'environ 1,5 m, debout selon +Y, avec une animation de marche. Il s'affiche sans réglage d'échelle ni de rotation.

```bash
curl -L -o res/models/hero.glb \
  https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/CesiumMan/glTF-Binary/CesiumMan.glb
```

Licence : © 2017 Cesium, [Creative Commons Attribution 4.0](https://creativecommons.org/licenses/by/4.0/) (CC-BY-4.0).
Si vous distribuez le modèle, citez « Cesium Man, © 2017 Cesium, CC-BY 4.0 » ; le logo Cesium de la texture est une
marque, exclue de cette licence.

Autre source, si vous avez compilé le dépôt du moteur (voir [Compiler le moteur](/demarrer/compiler-le-moteur/)) :
`apps/demo/bin/debug/main --scene anim` écrit un personnage généré par le code, `figure.gltf`, `figure.bin` et
`figure_albedo.png`, dans le dossier `orosmo-demo-anim` du dossier temporaire du système (`/tmp/orosmo-demo-anim/` sous
Linux, vidé au redémarrage). Copiez les trois fichiers dans `res/models/` et donnez à `kHeroModel` la valeur
`"models/figure.gltf"`. C'est le modèle avec lequel le programme a été lancé.

## L'état du menu et sa vue

Ces deux fichiers sont ceux de l'exemple [Un menu qui s'ouvre avec Échap](/guides/interface/langage-cui/#exemple--un-menu-qui-souvre-avec-échap).
La vue ne décide de rien : elle affiche `state.open` et ses boutons changent des champs que la boucle C++ lit.

```cpp title="src/menu_state.hpp"
#pragma once

#include <array>
#include <string>

struct MenuState
{
    bool open = false;        // menu visible ; le jeu se met en pause tant qu'il l'est
    bool quit = false;        // mis à vrai par le bouton Quitter ; la boucle principale le lit
    int theme = 0;            // index dans kThemes
    bool themeChanged = true; // la boucle recharge le thème quand c'est vrai

    static constexpr std::array<const char *, 3> kThemes{"dark", "light", "framed"};

    void toggle() { open = !open; }
    void resume() { open = false; }
    void requestQuit() { quit = true; }
    void nextTheme()
    {
        theme = (theme + 1) % static_cast<int>(kThemes.size());
        themeChanged = true;
    }
    std::string themeName() const { return kThemes[static_cast<std::size_t>(theme)]; }
};
```

```text title="src/MenuView.cui"
import "orosmo/ui"
import "menu_state.hpp"

view MenuView {
    @Mutable var state: MenuState

    Overlay {
        Panel {
            Label("Menu").variant("title")
            Button("Reprendre").onClick(state.resume).id("resume")
            Button("Thème : \(state.themeName())").onClick(state.nextTheme).id("theme")
            Button("Quitter").onClick(state.requestQuit).id("quit")
        }
        .width(260)
        .height(.fit)
        .align(.center)
        .visible(state.open)
    }
}
```

Le `Makefile` du projet compile chaque `.cui` de `src/` en `MenuView.gen.h` et `MenuView.gen.cpp`, qui définissent la
structure `cui::MenuView` installée par `setView` (section 6). En développement (`make debug`), la vue est aussi compilée
dans la bibliothèque de jeu : enregistrer `MenuView.cui` pendant que le programme tourne reconstruit le menu, sans
redémarrer ; une erreur garde l'ancien menu (voir [Écrire une vue en .cui](/guides/interface/langage-cui/)).

## 1. En-têtes et noms

`default_game.hpp` donne les réglages d'un projet `orosmo init` (`defaultSettings`) ; il n'est inclus que par ce fichier, car
il dépend des options de compilation du projet. `MenuView.gen.h` est produit à la compilation à partir de `MenuView.cui`.
Dans tout le programme, `a` est l'application (`app::Application`).

```cpp title="src/main.cpp"
// main.cpp du tutoriel « Un petit jeu 3D » : un personnage glTF animé sur un sol, éclairé par un soleil qui projette des ombres,
// une caméra au clavier et à la souris, un menu .cui ouvert par Échap.
#include "MenuView.gen.h"
#include "menu_state.hpp"

#include <orosmo/anim/animated_model.hpp>
#include <orosmo/app/default_game.hpp>
#include <orosmo/platform/input_manager.hpp>
#include <orosmo/platform/window.hpp>
#include <orosmo/ui/ui.hpp>

#include <iostream>

namespace app = orosmo::app;
namespace platform = orosmo::platform;
namespace rd = orosmo::renderer;

namespace
{
// Modèle du personnage (glTF avec peau et animations), relatif au dossier des ressources.
constexpr const char *kHeroModel = "models/hero.glb";
```

## 2. Le personnage animé

`loadAnimatedModel` lit le fichier, crée un maillage et un matériau PBR par primitive, un clip par animation et le lecteur
(voir [Animer un personnage](/guides/animation/squelettes/)). Le skinning passe par le compute si le périphérique le permet,
sinon par le CPU : c'est lu dans les capacités, pas supposé. Avec les ombres de l'application, les matériaux reçoivent les
ombres sans réglage.

Le personnage devient une entité avec un composant `AnimatedModelComponent` : le système de rendu de la scène avance son
lecteur à chaque image et le dessine. `castShadows` en fait un caster mobile : l'ombre suit la pose.

```cpp title="src/main.cpp (suite)"
// ── 2. Le personnage animé : le premier clip du fichier joué en boucle
orosmo::core::Result<> addHero(app::Application &a)
{
    auto hero = a.loadAnimatedModel(a.resourcePath(kHeroModel));
    if (!hero)
        return std::unexpected(hero.error());
    orosmo::anim::AnimatedModel &model = *a.animatedModel(*hero);
    if (!model.clips().empty())
    {
        if (auto played = model.play(model.clips().front().name()); !played)
            return played;
    }
    a.spawn(app::AnimatedModelComponent{.model = *hero, .castShadows = true});
    return {};
}
```

## 3. Le sol

Un plan de 20 m construit par le code (`MeshBuilder`, disposition PBR), avec un matériau non métallique
(`metallic = 0`) : un métal pur ne renvoie rien sous la lumière ambiante (voir
[Métal sans environnement](/guides/rendu-3d/materiaux-pbr/#métal-sans-environnement)). `addMesh(constructeur, matériau)`
crée le matériau, récepteur d'ombres puisque l'application en a. Le sol reçoit sans projeter : pas de `castShadows`.

```cpp title="src/main.cpp (suite)"
// ── 3. Le sol : un plan et un matériau PBR
orosmo::core::Result<> addGround(app::Application &a)
{
    const rd::PbrMaterialDesc stone{.name = "sol", .params = {.baseColorFactor = {0.55f, 0.55f, 0.5f, 1.0f}, .metallic = 0.0f, .roughness = 0.9f}};
    auto ground = a.addMesh(rd::MeshBuilder().addPlane({20.0f, 20.0f}), stone);
    if (!ground)
        return std::unexpected(ground.error());
    a.spawn(app::MeshRenderer{.mesh = *ground});
    return {};
}
```

## 4. Le contrôleur de caméra

C'est du code du jeu, repris de [Fenêtre, clavier et souris](/guides/application/fenetre-entrees/). Les actions nommées
sont liées une fois ; la vitesse est multipliée par la durée de l'image `dt`. Le clic droit maintenu capture le curseur pour
tourner la vue. `gui.wantsKeyboard()` et `gui.wantsMouse()` évitent de bouger la caméra quand l'interface utilise le clavier
ou la souris.

Un `KeyButton` désigne la touche qui porte la lettre dans la disposition active : `kW` est la touche marquée W, sur un
clavier QWERTY comme AZERTY. Pour des touches à la même place que W A S D sur un clavier AZERTY, liez `kZ`, `kQ`, `kS`,
`kD` ; `InputManager::getKeyLayout()` donne la disposition détectée.

```cpp title="src/main.cpp (suite)"
// ── 4. Le contrôleur de caméra
struct CameraController
{
    bool looking = false;
    glm::dvec2 last{0.0};

    static void bindKeys()
    {
        using platform::InputManager;
        using platform::KeyButton;
        InputManager::bindActionToInput("forward", KeyButton::kW); // la touche qui porte la lettre W
        InputManager::bindActionToInput("back", KeyButton::kS);
        InputManager::bindActionToInput("left", KeyButton::kA);
        InputManager::bindActionToInput("right", KeyButton::kD);
        InputManager::bindActionToInput("up", KeyButton::kSpace);
        InputManager::bindActionToInput("down", KeyButton::kLeftShift);
        InputManager::bindActionToInput("look", platform::MouseButton::kRight);
    }

    void update(platform::Window &window, const orosmo::ui::Ui &gui, rd::Camera &camera, float dt)
    {
        platform::InputManager &input = window.input();
        const float distance = 4.0f * dt; // 4 m/s
        if (!gui.wantsKeyboard())
        {
            if (input.isActionActive("forward")) camera.moveForward(distance);
            if (input.isActionActive("back")) camera.moveForward(-distance);
            if (input.isActionActive("right")) camera.moveRight(distance);
            if (input.isActionActive("left")) camera.moveRight(-distance);
            if (input.isActionActive("up")) camera.moveUp(distance);
            if (input.isActionActive("down")) camera.moveUp(-distance);
        }
        if (input.isActionActive("look") && !gui.wantsMouse())
        {
            window.setCursorMode(platform::CursorMode::kDisabled); // curseur capturé
            const glm::dvec2 mouse = window.getCursorPosition();
            if (looking)
            {
                camera.rotateYaw(-0.1f * static_cast<float>(mouse.x - last.x)); // 0,1 degré par pixel
                camera.rotatePitch(-0.1f * static_cast<float>(mouse.y - last.y));
            }
            last = mouse;
            looking = true;
        }
        else
        {
            release(window);
        }
    }

    // Rend le curseur : appelé aussi quand le menu s'ouvre en pause, où update n'est plus appelé.
    void release(platform::Window &window)
    {
        window.setCursorMode(platform::CursorMode::kNormal);
        looking = false;
    }
};
} // namespace
```

## 5. Les réglages de l'application

`defaultSettings` règle ce que le projet de démarrage règle déjà : la logique de jeu (`src/game/`, rechargée à chaud en
développement), le rechargement des vues `.cui`, les couches de validation et le journal `logs/log.log` en debug
seulement. Le programme ajoute :

- les ombres (`shadows`) : le plugin d'ombres est créé avec le renderer, avant les modèles, dont les matériaux reçoivent
  ainsi ses liaisons ;
- le HDR (`hdr`) : la scène est en `RGBA16F`, et le tonemapping expose, compresse et encode la sortie en sRGB. Sans lui,
  les matériaux PBR s'affichent trop sombres et trop contrastés (voir [HDR et post-traitement](/guides/rendu-3d/post-traitement/)) ;
- la couleur du ciel (`clearColor`) ;
- une fenêtre, jamais le plein écran (`windowed`).

Le périphérique est Vulkan, avec repli sur OpenGL s'il ne démarre pas ; `--api opengl` le force (voir
[Choisir le backend graphique](/guides/application/peripherique/)). `Application::main` lit la ligne de commande
(`--help` liste les options), crée tout, appelle la fonction de préparation (section 6), puis tourne jusqu'à la fermeture
de la fenêtre. `MenuState` et le contrôleur sont déclarés avant l'application : l'arbre du menu garde des liaisons vers
`menu`, et les rappels de l'application vers les deux.

```cpp title="src/main.cpp (suite)"
// ── 5. Les réglages de l'application
int main(int argc, char **argv)
{
    MenuState menu; // lié par la vue : déclaré avant l'application, détruit après elle
    CameraController controller;
    app::ApplicationSettings settings = app::defaultSettings("Tutoriel");
    settings.shadows = true; // plugin d'ombres : les composants qui ont `castShadows` projettent
    settings.hdr = true;     // scène en RGBA16F, puis tonemapping
    settings.clearColor = {0.45f, 0.6f, 0.8f, 1.0f};
    settings.windowed = true; // sans lui, un moteur construit sans DEBUG ouvre la fenêtre en plein écran
```

## 6. La scène et le menu

Deux lumières, qui sont aussi des entités : l'ambiante et le soleil. La `direction` d'une lumière est son sens de
propagation, du soleil vers le sol. Le soleil projette (`castShadows`), en `ShadowMode::kDynamic` : ses vues d'ombre sont
redessinées à chaque image et suivent la pose animée (en `kCached`, l'ombre d'un personnage qui s'anime sur place ne
bougerait pas, voir [Ajouter des ombres](/guides/rendu-3d/ombres/#un-modèle-animé-skinning)).

La caméra de l'application part un peu au-dessus du sol, regard vers -Z (la direction est un vecteur, pas des angles).
`setView` installe le menu comme racine de l'interface.

```cpp title="src/main.cpp (suite)"
    return app::Application::main(argc, argv, settings, [&](app::Application &a) -> orosmo::core::Result<> {
        // ── 6. La scène et le menu
        if (auto added = addHero(app); !added)
            return added;
        if (auto added = addGround(app); !added)
            return added;
        a.spawn(app::LightComponent{.type = rd::LightType::kAmbient, .color = {0.75f, 0.85f, 1.0f}, .intensity = 0.3f});
        a.spawn(app::LightComponent{.color = {1.0f, 0.95f, 0.85f}, .intensity = 2.5f, .direction = {-0.4f, -0.8f, -0.3f}, .castShadows = true});
        a.camera().setPosition({0.0f, 1.6f, 5.0f});
        a.camera().setOrientation(glm::normalize(glm::vec3(0.0f, -0.2f, -1.0f)));
        CameraController::bindKeys();
```

## 7. Chaque image

`onUi` est appelé à chaque image, après la mise à jour de l'interface ; l'application fait le reste (simulation, rendu de
la scène, ombres, tonemapping, interface par-dessus, présentation). Dans l'ordre :

1. Échap ouvre ou ferme le menu (sauf si un champ de texte a le clavier) ;
2. le thème est rechargé s'il a changé (au départ aussi : `menu.themeChanged` vaut vrai) ;
3. le bouton Quitter arrête la boucle ;
4. menu ouvert, le jeu est en pause : simulation et animation arrêtées, caméra immobile et curseur rendu ; sinon la caméra
   suit le clavier et la souris.

Sans fenêtre (`--headless`), `a.window()` est nul : seules les entrées sont sautées.

```cpp title="src/main.cpp (suite)"
        // ── 7. Chaque image : menu, thème, pause, caméra
        a.onUi([&](app::Application &a, const app::FrameTime &time) {
            platform::Window *window = a.window(); // nul sans fenêtre (--headless)
            if (window != nullptr && !a.ui().wantsKeyboard() && window->input().isKeyJustPressed(platform::KeyButton::kEscape))
                menu.toggle();
            if (menu.themeChanged)
            {
                menu.themeChanged = false;
                if (auto loaded = a.ui().themes().load(a.resourcePath("themes/" + menu.themeName() + ".json")); !loaded)
                    std::cerr << "Theme: " << loaded.error().message << '\n';
            }
            if (menu.quit)
                a.quit();
            a.pauseSimulation(menu.open);
            a.pauseAnimations(menu.open);
            if (window == nullptr)
                return;
            if (menu.open)
                controller.release(*window); // le menu s'ouvre pendant un clic droit : le curseur redevient libre
            else
                controller.update(*window, a.ui(), a.camera(), static_cast<float>(time.dt));
        });
        return a.setView(cui::MenuView{.state = &menu});
    });
}
```

## Compiler et lancer

```bash
make debug       # compile src/main.cpp et MenuView.cui -> bin/debug/main (et src/game/ -> bin/debug/game.so)
make run
./bin/debug/main --api opengl --headless --capture captures   # sans fenêtre : captures/scene.png et captures/ui.png
```

| Touche | Effet |
|---|---|
| W, S | avancer, reculer |
| A, D | gauche, droite |
| Espace, Maj gauche | monter, descendre |
| clic droit maintenu + souris | tourner la vue |
| Échap | ouvrir ou fermer le menu (le jeu se met en pause) |

## Limites

- Un personnage animé ne projette une ombre juste qu'avec une lumière en `ShadowMode::kDynamic`, qui redessine ses vues
  d'ombre à chaque image (plus coûteux que `kCached`). Ce comportement a été vérifié à la main (l'ombre suit la pose en
  `kDynamic`, se fige en `kCached`) ; il n'a pas encore de test automatique.
- Le programme est compilé par `make e2e-init`, mais pas lancé par lui : il faut un modèle glTF animé, que le dépôt ne
  contient pas. Son lancement (OpenGL et Vulkan, sans fenêtre) a été vérifié à la main.
- Le thème est rechargé tel quel depuis `res/themes/` ; les couleurs de l'interface ne passent pas par le tonemapping.

Détail et suivi : [État du projet](/projet/etat/).

## Voir aussi

- [Lire la boucle d'une application](/demarrer/premiere-application/)
- [Animer un personnage](/guides/animation/squelettes/)
- [Ajouter des ombres](/guides/rendu-3d/ombres/)
- [Écrire une vue en .cui](/guides/interface/langage-cui/)
