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

Les exemples du rendu 3D suivent les [conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples) :
`device` (`rhi::Device &`), `renderer` (`rd::SceneRenderer &`), `lights` (`rd::LightManager &`), `shaders`, `pipelines`,
`shaderDir`, `camera`, `encoder`. Pour les matériaux PBR s'y ajoutent, créés une fois :

```cpp
namespace as = orosmo::assets;
as::TextureCache textures(device);                       // textures partagées entre les modèles
rd::PbrShaderCache pbr(shaders, shaderDir);              // un programme par variante de matériau
auto defaults = rd::PbrDefaults::create(device);         // Result<shared_ptr<PbrDefaults>> : textures neutres
```

## Exemple minimal

Le projet créé par `orosmo init` affiche un cube coloré. Voici sa mise en place, abrégée :

```cpp
const std::filesystem::path shaderDir = GET_RESOURCE_PATH("shader/rhi");
rd::ShaderLibrary shaders(device);       // partagés par tous les renderers,
rd::PipelineCache pipelines(device);     // à garder vivants jusqu'à la fin

auto lightsResult = rd::LightManager::create(device);   // avant le renderer : détruit après lui (il garde un pointeur)
auto sceneResult  = rd::SceneRenderer::create(device, shaders, pipelines, {.shaderDirectory = shaderDir});
auto program      = shaders.loadGraphics(shaderDir / "mesh.vert", shaderDir / "mesh.frag");
auto cubeResult   = rd::Mesh::create(device, makeColoredCube());   // MeshData : position, normale, couleur
if (!sceneResult || !lightsResult || !program || !cubeResult) { /* afficher l'erreur, sortir */ }
rd::SceneRenderer &renderer = **sceneResult;
rd::LightManager  &lights   = *lightsResult;
rd::Mesh          &cube     = *cubeResult;
cube.setProgram(*program);                               // obligatoire : pas de programme par défaut

lights.setAmbientLight({1.0f, 1.0f, 1.0f}, 0.35f);
lights.addLight(rd::directionalLight({0.6f, 0.8f, 0.5f}, {1.0f, 0.96f, 0.90f}, 0.85f));
if (auto r = lights.upload(); !r) { /* ... */ }
renderer.setLights(&lights);                             // pointeur gardé : `lights` doit vivre plus longtemps

rd::Camera camera;
camera.setPosition({0.0f, 1.8f, 4.5f});
camera.setOrientation(glm::normalize(glm::vec3(0.0f, -0.35f, -1.0f))); // direction du regard
camera.setFovDegrees(60.0f);
```

Source : [`tools/engine-sdk/template/src/main.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tools/engine-sdk/template/src/main.cpp)
(la fonction `makeColoredCube` y est en entier). Pour créer ce projet : [Créer un projet](/demarrer/premier-projet/).

## Le cycle d'une image

Il y a deux `beginFrame` : celui du `rhi::Device`, qui donne l'image de la fenêtre, et celui du `SceneRenderer`, qui
reçoit l'encodeur de commandes. `drawMesh` n'exécute rien : il enregistre le dessin. C'est `endFrame` qui construit le
graphe de passes et l'écrit dans l'encodeur ; le GPU ne travaille qu'au `submit`. Dans l'ordre :

| Étape | Appel | Ce qui se passe |
|---|---|---|
| 1 | `window.newFrame()` | événements, entrées |
| 2 | `device.beginFrame()` | image de la fenêtre ; échoue (`kBusy`) si la fenêtre est réduite : sauter l'image |
| 3 | `device.createCommandEncoder()` | un encodeur par image |
| 4 | (compute : `skinned.dispatch(*encoder)`) | enregistré avant la scène |
| 5 | `renderer.setOutputResolution(w, h)`, `renderer.beginFrame(*encoder, options)` | résolution de sortie (taille du framebuffer, à chaque image : elle suit le redimensionnement), puis prépare la cible de la scène ; `beginFrame` refuse une résolution vide (`0 × 0`, état d'un renderer neuf) |
| 6 | `renderer.setCamera(camera)` | caméra de l'image |
| 7 | `renderer.drawMesh(mesh, camera)`, `model.draw(renderer, camera)` | dessins enregistrés |
| 8 | `renderer.endFrame(sortie)` | graphe construit : ombres, scène, plugins, copie vers la sortie |
| 9 | interface (`canvas.render(..., true)`) | dessinée par-dessus, sur la même cible |
| 10 | `renderer.blit().copy(*encoder, composed.color(0), renderer.blit().linearSampler(), *backbuffer, rhi::Format::kRGBA8Unorm, "present")` | copie de la cible (scène + interface) vers l'image de la fenêtre obtenue à l'étape 2 |
| 11 | `device.submit(std::move(encoder))` | le GPU exécute tout |
| 12 | `device.present()` | affichage ; toujours appelé si l'étape 2 a réussi, même après une erreur |

```cpp
renderer.setOutputResolution(static_cast<int>(size.width), static_cast<int>(size.height));   // avant beginFrame
auto ok = renderer.beginFrame(*encoder, {.clearColor = {0.10f, 0.13f, 0.18f, 1.0f}});
if (ok) ok = renderer.setCamera(camera);
if (ok) ok = renderer.drawMesh(cube, camera);                                      // enregistré
if (ok) ok = renderer.endFrame({composed.color(0), rhi::Format::kRGBA8Unorm, size}); // exécuté ici
```

Si `drawMesh` (ou `setCamera`) échoue après un `beginFrame` réussi, `endFrame` est sauté et l'image reste ouverte : le
`beginFrame` suivant est refusé (`kInvalidArgument`, « the previous frame was not ended »). Après une telle erreur, affichez
le message et arrêtez l'application, comme le fait le projet de démarrage (`Frame failed`, `EXIT_FAILURE`).

`composed` est une `rd::RenderTarget` sans profondeur à la taille du framebuffer, où la scène puis l'interface sont
dessinées avant la copie dans l'image de la fenêtre. Sa création et la boucle entière sont dans
[Lire la boucle d'une application](/demarrer/premiere-application/#la-boucle-dune-image).

Règles à retenir :

- l'encodeur et les tampons des maillages dessinés doivent rester valides jusqu'à `endFrame` ;
- `setCamera` vient après `beginFrame` et avant le premier `drawMesh` ;
- le travail compute de l'image (skinning) est enregistré sur le même encodeur, avant `SceneRenderer::beginFrame` ;
- `present()` est appelé à chaque tour où `Device::beginFrame()` a réussi, y compris quand on sort de la boucle.

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
auto meshResult = rd::Mesh::create(device, data);
if (!meshResult) { /* ... */ }
rd::Mesh &mesh = *meshResult;                  // Result par valeur : une seule étoile
mesh.setProgram(*program);
mesh.setPosition({0.0f, 1.0f, 0.0f});
mesh.setRotation({0.0f, 90.0f, 0.0f});         // degrés, X puis Y puis Z
mesh.setCullMode(rhi::CullMode::kBack);        // défaut kNone : les deux faces
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
auto created = rd::ShadowFeature::create(device, shaders, pipelines, shaderDir);
if (!created) { /* erreur */ }
auto added = renderer.addFeature(std::move(*created));
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
