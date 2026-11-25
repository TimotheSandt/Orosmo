---
title: "Dessiner une scène : caméra et maillages"
description: "Afficher des maillages avec SceneRenderer, une caméra et une lumière, et comprendre l'ordre des appels d'une image."
sidebar:
  order: 1
  label: Scène
---

`SceneRenderer` dessine la 3D : il possède la cible de la scène (couleur et profondeur), envoie au GPU la caméra, les
lumières et les données de l'image (groupe 0 des shaders), et joue les dessins de maillages. Les fonctions en plus
(ombres, forward+, post-traitement, transparents) s'y branchent comme plugins.

## Noms utilisés dans les exemples

Les pages du rendu 3D reprennent les noms du projet `orosmo init`
([Lire la boucle d'une application](/demarrer/premiere-application/)) :

| Nom | Type | D'où il vient |
|---|---|---|
| `rd`, `rhi` | alias | `namespace rd = orosmo::renderer;`, `namespace rhi = orosmo::rhi;` |
| `device` | `core::Result<std::unique_ptr<rhi::Device>>` | `rhi::createDevice(desc)` ; `**device` est le `rhi::Device &` |
| `shaders`, `pipelines` | `rd::ShaderLibrary`, `rd::PipelineCache` | créés une fois, gardés jusqu'à la fin |
| `shaderDir` | `std::filesystem::path` | `GET_RESOURCE_PATH("shader/rhi")` |
| `scene` | `core::Result<std::unique_ptr<rd::SceneRenderer>>` | `rd::SceneRenderer::create(...)` ; `(*scene)->` ou `**scene` |
| `lights` | `core::Result<rd::LightManager>` | `rd::LightManager::create(**device)` ; `lights->`, `&*lights` |
| `camera` | `rd::Camera` | objet simple |
| `textures`, `pbr`, `defaults` | `as::TextureCache`, `rd::PbrShaderCache`, `Result<shared_ptr<rd::PbrDefaults>>` | pour les matériaux PBR, voir [Matériaux PBR](/guides/rendu-3d/materiaux-pbr/) |

Une fabrique renvoie un `Result` : on teste `if (!x)` et on lit `x.error().message` avant de s'en servir.

## Exemple minimal

Le projet créé par `orosmo init` affiche un cube coloré. Voici sa mise en place, abrégée (gestion d'erreur réduite) :

```cpp
const std::filesystem::path shaderDir = GET_RESOURCE_PATH("shader/rhi");
rd::ShaderLibrary shaders(**device);       // partagés par tous les renderers,
rd::PipelineCache pipelines(**device);     // à garder vivants jusqu'à la fin

auto scene = rd::SceneRenderer::create(**device, shaders, pipelines, {.shaderDirectory = shaderDir});
auto program = shaders.loadGraphics(shaderDir / "mesh.vert", shaderDir / "mesh.frag");
auto cube = rd::Mesh::create(**device, makeColoredCube());  // MeshData : position, normale, couleur
if (!scene || !program || !cube) { /* erreur */ }
cube->setProgram(*program);                                 // obligatoire : pas de programme par défaut

auto lights = rd::LightManager::create(**device);
lights->setAmbientLight({1.0f, 1.0f, 1.0f}, 0.35f);
lights->addLight(rd::directionalLight({0.6f, 0.8f, 0.5f}, {1.0f, 0.96f, 0.90f}, 0.85f));
(void)lights->upload();
(*scene)->setLights(&*lights);                              // pointeur gardé : `lights` doit vivre plus longtemps

rd::Camera camera;
camera.setPosition({0.0f, 1.8f, 4.5f});
camera.setOrientation(glm::normalize(glm::vec3(0.0f, -0.35f, -1.0f))); // direction du regard
camera.setFovDegrees(60.0f);
```

`device` est le `Result<std::unique_ptr<rhi::Device>>` rendu par `rhi::createDevice`, d'où `**device`.
Source : [`tools/engine-sdk/template/src/main.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tools/engine-sdk/template/src/main.cpp)
(la fonction `makeColoredCube` y est en entier). Pour créer ce projet : [Créer un projet](/demarrer/premier-projet/).

## Le cycle d'une image

Il y a deux `beginFrame` : celui du `rhi::Device`, qui donne l'image de la fenêtre, et celui du `SceneRenderer`, qui
reçoit l'encodeur de commandes. `drawMesh` n'exécute rien : il enregistre le dessin. C'est `endFrame` qui construit le
graphe de passes et l'écrit dans l'encodeur ; le GPU ne travaille qu'au `submit`.

```cpp
while (!window.shouldClose()) {
    if (!window.newFrame()) continue;                      // entrées, FPS
    auto backbuffer = (*device)->beginFrame();             // image de la fenêtre
    if (!backbuffer) continue;                             // fenêtre réduite (kBusy) : sauter l'image, pas quitter
    auto encoder = (*device)->createCommandEncoder();      // un encodeur par image
    // (compute éventuel ici, avant scene->beginFrame : skinning)
    camera.setAspect(size.width, size.height);
    (*scene)->setOutputResolution(pixelW, pixelH);
    auto ok = (*scene)->beginFrame(*encoder, {.clearColor = {0.10f, 0.13f, 0.18f, 1.0f}});
    if (ok) ok = (*scene)->setCamera(camera);              // caméra de l'image
    if (ok) ok = (*scene)->drawMesh(*cube, camera);        // enregistré, pas encore dessiné
    if (ok) ok = (*scene)->endFrame({composed.color(0), rhi::Format::kRGBA8Unorm, size}); // graphe construit ici
    // interface : ui.draw(list) puis canvas->render(*encoder, list, {composed.color(0), rhi::Format::kRGBA8Unorm, size}, true)
    if (ok) ok = (*scene)->blit().copy(*encoder, composed.color(0), (*scene)->blit().linearSampler(), *backbuffer,
                                       rhi::Format::kRGBA8Unorm, "present");
    auto submitted = (*device)->submit(std::move(encoder)); // le GPU exécute tout
    (void)(*device)->present();                            // toujours, même si l'image a échoué
}
(*device)->waitIdle();
```

`composed` est une cible intermédiaire sans profondeur, à la taille du framebuffer, créée à la première image puis
redimensionnée :

```cpp
rd::RenderTarget composed;                                    // déclarée avant la boucle
const rhi::Extent2D size{static_cast<std::uint32_t>(pixelW), static_cast<std::uint32_t>(pixelH)};
if (!composed.valid()) {
    auto target = rd::RenderTarget::create(**device, {.size = size, .depth = false, .debugName = "composed"});
    if (!target) { /* erreur */ }
    composed = std::move(*target);
} else if (auto r = composed.resize(size.width, size.height); !r) { /* erreur */ }
```

`pixelW` et `pixelH` viennent de `window.getFramebufferSize(pixelW, pixelH)`. `endFrame` copie la scène dans `composed`,
l'interface est dessinée par-dessus (`loadExisting = true`), puis le tout est copié dans l'image de la fenêtre.

Règles à retenir :

- l'encodeur et les tampons des maillages dessinés doivent rester valides jusqu'à `endFrame` ;
- `setCamera` vient après `beginFrame` et avant le premier `drawMesh` ;
- le travail compute de l'image (skinning) est enregistré sur le même encodeur, avant `SceneRenderer::beginFrame` ;
- `present()` est appelé à chaque tour où `Device::beginFrame()` a réussi, y compris quand on sort de la boucle.

La boucle complète, avec les entrées et l'interface : [Lire la boucle d'une application](/demarrer/premiere-application/).

## Caméra

`rd::Camera` ne lit ni le clavier ni la souris : l'application la déplace. La profondeur est en reverse-Z (1 au plan
proche, 0 au loin) et le plan lointain est infini par défaut. Le monde a Y en haut ; la caméra regarde vers -Z par défaut.

| Option | Type | Défaut | Effet |
|---|---|---|---|
| `setPosition` | `glm::vec3` | `(0, 0, 0)` | Position dans le monde. |
| `setOrientation` | `glm::vec3` | `(0, 0, -1)` | Direction du regard (un vecteur, pas des angles). |
| `setUp` | `glm::vec3` | `(0, 1, 0)` | Haut de la caméra. |
| `setFovDegrees` | `float` | `70` | Champ vertical, en degrés. |
| `setNearPlane` | `float` | `0.1` | Plan proche (m). |
| `setFarPlane` | `float` | infini | Plan lointain ; une valeur finie borne le reverse-Z. |
| `setOrthographic` | `float` | `100` | Passe en orthographique ; hauteur visible en mètres. `setPerspective()` revient en perspective. |
| `setAspect` | `float` ou `(largeur, hauteur)` | `16/9` | Rapport largeur / hauteur ; à mettre à jour au redimensionnement. |
| `setWireframe` | `bool` | `false` | Dessine aussi les arêtes des maillages. |

Déplacements : `move`, `moveForward`, `moveRight`, `moveUp`, `rotateYaw`, `rotatePitch` (refuse de passer à moins de
10° de la verticale), `rotateRoll`, et `rotate({tangage, lacet, roulis})`, en degrés.

## Maillages

Un `rd::Mesh` se crée depuis un `rd::MeshData` : sommets entrelacés, index, et la taille en flottants de chaque attribut.
Ces tailles doivent correspondre aux entrées du shader : `{3, 3, 3}` (position, normale, couleur) pour `mesh.vert`,
`rd::kPbrVertexAttributeSizes` (16 flottants) pour les [matériaux PBR](/guides/rendu-3d/materiaux-pbr/).

```cpp
rd::MeshData data;
data.attributeSizes = {3, 3, 3};               // position, normale, couleur (entrées de mesh.vert)
data.vertices = {/* x, y, z, nx, ny, nz, r, g, b, ... */};
data.indices = {0, 1, 2, 0, 2, 3};
auto mesh = rd::Mesh::create(**device, data);
mesh->setProgram(*program);
mesh->setPosition({0.0f, 1.0f, 0.0f});
mesh->setRotation({0.0f, 90.0f, 0.0f});        // degrés, X puis Y puis Z
mesh->setCullMode(rhi::CullMode::kBack);       // défaut kNone : les deux faces
```

| Option | Type | Défaut | Effet |
|---|---|---|---|
| `setPosition`, `setRotation`, `setScale` | `glm::vec3` | `0`, `0`, `1` | Transformation ; rotation en degrés, X puis Y puis Z. |
| `setModelMatrix` | `glm::mat4` | aucune | Matrice donnée telle quelle, à la place des trois précédentes. |
| `setCullMode` | `rhi::CullMode` | `kNone` | Faces éliminées ; le sens direct est anti-horaire (comme glTF). |
| `setProgram` | `shared_ptr<ShaderProgram>` | aucun | Programme du dessin ; sans lui `drawMesh` renvoie une erreur. |
| `setTextures` | `span<const TextureBinding>` | aucune | Textures aux liaisons 0, 1, 2… du groupe 1 (handles non possédés). |
| `setMaterialGroup` | `rhi::BindGroupHandle` | aucun | Groupe 1 fourni (matériau PBR, ombres) ; non possédé. |
| `setObjectId` | `uint32_t` | `0` | Identifiant écrit dans l'AOV `kObjectId`. |
| `setAovMaterial` | `AovMaterial` | opaque | Mode de fusion du dessin, voir [Transparence](/guides/rendu-3d/transparence/). |

Un modèle glTF se dessine avec `assets::RenderModel`, qui crée ces maillages pour vous :
[Charger un glTF](/guides/contenus/gltf/).

`drawMesh` enregistre un dessin par appel. Au-delà de quelques dizaines de copies d'un même maillage (arbres, rochers,
foule), utilisez l'instancing : [Beaucoup d'objets](/guides/rendu-3d/instancing-culling/) donne le tableau de choix.

## Réglages du renderer

| Option | Type | Défaut | Effet |
|---|---|---|---|
| `SceneRendererConfig::shaderDirectory` | chemin | vide | Dossier des shaders du moteur, `GET_RESOURCE_PATH("shader/rhi")` (obligatoire). |
| `SceneRendererConfig::framesInFlight` | `uint32_t` | `2` | Images en vol (anneau des données par image). |
| `FrameOptions::clearColor` | `array<float, 4>` | `{0, 0, 0, 1}` | Couleur d'effacement de la scène. |
| `FrameOptions::clearDepth` | `bool` | `true` | Efface la profondeur au `beginFrame`. |
| `setOutputResolution` | `int, int` | `0, 0` | Taille de la sortie, à donner à chaque image. |
| `setRenderScale` | `float` dans `(0, 1]` | `1` | Rend la scène plus petite puis l'agrandit (copie bilinéaire). |
| `setSceneColorFormat` | `rhi::Format` | `kRGBA8Unorm` | `kRGBA16Float` ou `kRGBA32Float` pour une scène HDR. |
| `setLights` | `LightManager *` | ambiante nulle | Lumières lues par les shaders (pointeur gardé). |
| `setDepthPrepass` | `bool` | `false` | Pré-passe de profondeur (activée par le forward+). |
| `setCameraRelative` | `bool` | `false` | Rendu relatif à la caméra, pour les grandes coordonnées. |
| `setDeltaTime` | `float` | `0` | Durée de l'image, lue par l'exposition automatique. |

## Ajouter un plugin

Ombres, forward+, IBL, transparents, post-traitement et anticrénelage sont des `RenderFeature` enregistrés par
`addFeature` (ou par une fonction d'aide qui l'appelle). Deux règles :

1. Les passes s'exécutent dans l'ordre d'enregistrement. Ordre conseillé : ombres, forward+, IBL, transparents, 2D dans le
   monde, TAA, post-traitements, FXAA ou SMAA, puis les superpositions (interface).
2. Configurer d'abord, compiler ensuite. Plusieurs plugins changent les macros du programme de maillage
   (`forwardPlusDefines()`, `aovShaderDefines()`, variante réceptrice des ombres). Installez les plugins, puis chargez les
   programmes avec les macros obtenues ; si l'ensemble change, rechargez-les.

`addFeature` renvoie un pointeur non possédé vers le plugin, à convertir vers son type :

```cpp
auto created = rd::ShadowFeature::create(**device, shaders, pipelines, shaderDir);
if (!created) { /* erreur */ }
auto added = (*scene)->addFeature(std::move(*created));
if (!added) { /* kUnsupported : capacité absente ; kAlreadyExists : nom déjà pris */ }
auto *shadows = static_cast<rd::ShadowFeature *>(*added);
```

## Limites

- Côté renderer, les textures sont 2D seulement (pas de tableau, cube ni 3D, bien que le RHI les offre).
- L'argument `camera` de `drawMesh` ne sert qu'au fil de fer : les matrices viennent de `setCamera`.
- Il n'y a pas de programme de maillage par défaut, ni d'assemblage automatique des macros des plugins.

Détail et suivi : [État du projet](/projet/etat/).

## Voir aussi

- [Lumières](/guides/rendu-3d/lumieres/)
- [Matériaux PBR](/guides/rendu-3d/materiaux-pbr/)
- [Beaucoup d'objets : instancing et culling](/guides/rendu-3d/instancing-culling/)
- [Dessin et vues de debug](/guides/rendu-3d/debug-visuel/)
- [Le renderer (interne)](/interne/renderer/)
