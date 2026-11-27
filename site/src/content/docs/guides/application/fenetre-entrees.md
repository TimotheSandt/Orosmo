---
title: Fenêtre, clavier et souris
description: Créer une fenêtre, lire le clavier, la souris et la molette, régler le plein écran, le curseur et le DPI.
sidebar:
  order: 1
---

`orosmo::platform::Window` ouvre une fenêtre (GLFW, caché dans `src/platform/`) et `InputManager` lit les entrées de cette
fenêtre. Les deux sont dans `include/orosmo/platform/`. La fenêtre ne connaît aucune API graphique : elle reçoit du RHI un
rappel appelé juste avant sa création.

## Exemple minimal

```cpp
#include <orosmo/platform/input_manager.hpp>
#include <orosmo/platform/window.hpp>

namespace platform = orosmo::platform;

int main()
{
    platform::Window window("monjeu", 1280, 720, platform::WindowState::kWindowed);
    if (auto opened = window.init(); !opened)   // Result<> : opened.error() dit pourquoi (pas d’affichage, taille invalide…)
        return 1;

    platform::InputManager &input = window.input();   // = InputManager::getInstance(window.getWindow())
    while (!window.shouldClose())
    {
        if (!window.newFrame())         // événements, entrées, compteur de FPS ; faux : fenêtre inutilisable
            break;
        if (input.isKeyJustPressed(platform::KeyButton::kEscape))
            window.requestClose();
    }
    window.close();                     // détruit la fenêtre AVANT l'arrêt de GLFW (sinon son destructeur arrive après terminate)
    platform::Window::terminate();      // après la destruction de toutes les fenêtres et du périphérique
}
```

`init` rend un `core::Result<>` : `kInvalidArgument` pour une taille nulle ou négative, `kAlreadyExists` si la fenêtre est
déjà créée, `kUnsupported` si GLFW ne démarre pas (pas d'affichage) ou refuse la fenêtre. Ces cas reviennent tous à votre
code (avant M8, un échec de `glfwInit` terminait le processus).

`newFrame()` traite les événements du système et met à jour l'`InputManager` de la fenêtre : lisez les entrées après
cet appel. Il fait aussi `core::Profiler::Process()`, compte l'image (FPS) et applique la limite de `setMaxFPS` (il peut
attendre). Une boucle qui gère elle-même cadence et mesures appelle plutôt `pollEvents()`, qui ne fait que lire les
événements et mettre à jour les entrées. Les deux rendent faux sans fenêtre utilisable : sortez alors de la boucle. Avec un périphérique graphique, `init` reçoit en plus le rappel `[&] { (void)rhi::prepareWindowHints(desc); }`
(voir [Choisir le backend graphique](/guides/application/peripherique/)).

## Créer la fenêtre

| Constructeur | Effet |
|---|---|
| `Window()` | fenêtre « Window », 800 x 600, état par défaut : voir l'encadré |
| `Window(title, width, height)` | titre et taille, état par défaut : voir l'encadré |
| `Window(title, width, height, state)` | idem avec un `WindowState` |
| `Window(WindowParameters)` | tous les paramètres |

:::caution
Les constructeurs sans `WindowState` ouvrent la fenêtre en `kWindowed` si le programme est compilé avec `DEBUG`, et en
`kFullscreen` sinon. Pour un résultat identique en debug et en release, passez l'état explicitement, comme ci-dessus.
:::

| Champ de `WindowParameters` | Type | Défaut | Effet |
|---|---|---|---|
| `title` | `std::string` | `"Window"` | titre |
| `windowState` | `WindowState` | `kWindowed` (dans la structure) | `kWindowed`, `kBorderless`, `kFullscreen`, `kFullscreenUnfocused` |
| `width`, `height` | `int` | 800, 600 | taille en pixels de fenêtre |
| `posX`, `posY` | `int` | 100, 100 | position |
| `maxFPS` | `int` | 0 | limite d'images par seconde (0 : aucune), voir `setMaxFPS` |
| `clearColor` | `glm::vec4` | 0,07 0,13 0,17 1 | couleur de fond, lue par `getClearColor` |
| `trueEveryms` | `double` | 500 | période, en millisecondes, de recalcul des FPS moyen, minimal et maximal de `getAverageFPS`, `getMinFPS`, `getMaxFPS` (0 : à chaque image) |

`WindowParameters` a d'autres champs (`vsync`, `taskbarVisible`, positions et tailles en mode fenêtre) qui ne servent pas
à l'application. La vsync se règle sur le périphérique (`Device::setVSync`), pas ici : voir
[Cadence d'images](/guides/application/cadence-captures/).

## Taille, DPI, plein écran

```cpp
int fbW = 0, fbH = 0;
window.getFramebufferSize(fbW, fbH);        // pixels réels : taille de la cible de rendu
glm::vec2 scale = window.getContentScale(); // 1 = 96 dpi ; 2 sur un écran « Retina »
window.toggleBorderless();                  // plein écran sans bordure
window.toggleFullscreen();
window.changeWindowState(platform::WindowState::kWindowed);
```

`getWidth()` et `getHeight()` donnent la taille de la fenêtre en pixels logiques (ceux de l'interface) ;
`getFramebufferSize` donne les pixels réels, qui diffèrent sur un écran à échelle. Rendez à la taille du framebuffer.
Fenêtre réduite, le framebuffer a une taille nulle : `Device::beginFrame` renvoie alors une erreur et l'image est sautée.

## Curseur et presse-papiers

```cpp
window.setCursorMode(platform::CursorMode::kDisabled);  // kNormal, kHidden, kDisabled (capturé)
glm::dvec2 p = window.getCursorPosition();               // pixels de fenêtre, origine en haut à gauche
window.setCursorPosition({100.0, 100.0});
window.setClipboardText("texte");  std::string t = window.getClipboardText();
```

`kDisabled` capture le curseur : c'est ce qu'il faut pour une caméra à la souris.

## Clavier et souris

```cpp
using platform::KeyButton; using platform::MouseButton;

input.isKeyPressed(KeyButton::kW);            // tenue (à partir de la 2e image)
input.isKeyJustPressed(KeyButton::kSpace);    // pressée cette image
input.isKeyJustReleased(KeyButton::kSpace);
input.isMouseButtonJustPressed(MouseButton::kLeft);
glm::vec2 pos    = input.getMousePosition();
glm::vec2 delta  = input.getMouseDelta();
glm::vec2 wheel  = input.getMouseScroll();    // cumulée depuis le dernier update
```

Chaque touche a un `InputState` : `kRelease`, `kPressBegin` (image de l'appui), `kPressed`, `kPressEnd` (image du
relâchement). `isKeyPressed` n'est vrai qu'à partir de l'image qui suit l'appui, car la première image est `kPressBegin`.

### Actions nommées

```cpp
platform::InputManager::bindActionToInput("Camera::MoveForward", KeyButton::kZ);
platform::InputManager::bindActionToInput("Camera::Rotate", MouseButton::kRight);
...
if (input.isActionActive("Camera::MoveForward")) camera.moveForward(distance);
```

`bindActionToInput` accepte plusieurs touches et boutons ; l'action est active quand **tous** sont tenus. Une action
non liée journalise un avertissement et renvoie `false`. `unbindAction` et `isActionBound` complètent l'API. C'est ce que fait
`apps/demo/src/Game/CameraController.cpp`.

### Un contrôleur de caméra complet

Vitesse en unités par seconde, multipliée par le temps de l'image `dt` (en secondes). `ui` est le `ui::Ui` de l'application ;
s'il n'y en a pas, retirez les deux tests `wantsMouse` et `wantsKeyboard`.

```cpp
void bindCameraKeys()   // une fois, au démarrage
{
    using platform::InputManager; using platform::KeyButton; using platform::MouseButton;
    InputManager::bindActionToInput("forward",  KeyButton::kW);   // la touche qui porte la lettre W
    InputManager::bindActionToInput("back",     KeyButton::kS);
    InputManager::bindActionToInput("left",     KeyButton::kA);
    InputManager::bindActionToInput("right",    KeyButton::kD);
    InputManager::bindActionToInput("up",       KeyButton::kSpace);
    InputManager::bindActionToInput("down",     KeyButton::kLeftShift);
    InputManager::bindActionToInput("look",     MouseButton::kRight);
}

void updateCamera(platform::Window &window, ui::Ui &ui, rd::Camera &camera, float dt)
{
    platform::InputManager &input = platform::InputManager::getInstance(window.getWindow());
    const float distance = 6.0f * dt;                       // 6 unités par seconde
    if (!ui.wantsKeyboard())
    {
        if (input.isActionActive("forward")) camera.moveForward(distance);
        if (input.isActionActive("back"))    camera.moveForward(-distance);
        if (input.isActionActive("right"))   camera.moveRight(distance);
        if (input.isActionActive("left"))    camera.moveRight(-distance);
        if (input.isActionActive("up"))      camera.moveUp(distance);
        if (input.isActionActive("down"))    camera.moveUp(-distance);
    }
    static bool looking = false;                            // pas de saut à la première image capturée
    static glm::dvec2 last{};
    if (input.isActionActive("look") && !ui.wantsMouse())
    {
        window.setCursorMode(platform::CursorMode::kDisabled);
        const glm::dvec2 mouse = window.getCursorPosition();
        if (looking)
        {
            const glm::dvec2 delta = mouse - last;
            camera.rotateYaw(-0.1f * static_cast<float>(delta.x));
            camera.rotatePitch(-0.1f * static_cast<float>(delta.y));
        }
        last = mouse;
        looking = true;
    }
    else
    {
        window.setCursorMode(platform::CursorMode::kNormal);
        looking = false;
    }
}
```

Adapté de `apps/demo/src/Game/CameraController.cpp`. La démo lie `kZ`, `kQ`, `kS`, `kD` parce que son auteur a un clavier
AZERTY : un `KeyButton` est la touche qui porte la lettre, donc sur un clavier QWERTY `kZ` est la touche du bas. L'exemple
ci-dessus lie W, A, S, D, qui sont les lettres imprimées sur la touche quelle que soit la disposition. `rotateYaw` et
`rotatePitch` prennent des degrés ; ajustez le facteur 0,1 (degrés par pixel) à votre goût. Les variables `static` sont là pour la brièveté :
dans une application, rangez ces deux valeurs dans une classe.

### Disposition du clavier

Les `KeyButton` sont des touches logiques : `kZ` est la touche qui porte la lettre Z dans la disposition active. La
disposition (`KeyLayout` : `kQwerty`, `kQwertz`, `kAzerty`, `kDvorak`) est détectée à la première obtention d'un
`InputManager` ; `setKeyLayout` la force et `getKeyLayout` la lit.

### Texte, raccourcis

`getTextInput()` donne le texte saisi depuis le dernier `update` (UTF-8, répétitions comprises), `getKeyPresses()` les
appuis dans l'ordre avec Maj, Ctrl, Alt, Super, et `getShortcuts()` des chaînes comme `"Ctrl+S"` ou `"F5"`. Pour une interface
`ui::Ui`, `ui::readWindowInput(window, dt)` fait la traduction et `ui::useSystemClipboard(ui, window)` branche le
presse-papiers du système.

## Limites

- Le facteur d'échelle ×2 (écrans « Retina ») est lu mais n'a pas été observé sur un vrai écran.
- Pas de manettes.
- Saisie de texte : Unicode et presse-papiers fonctionnent en mode fenêtré ; la composition (IME, saisie du chinois ou du
  japonais) n'est pas gérée.

Voir [État du moteur](/projet/etat/).
