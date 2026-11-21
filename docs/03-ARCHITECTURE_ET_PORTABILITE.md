# 03 · Architecture et portabilité

## 1. Couches et règle de dépendance

Un module ne dépend que des modules **du dessous**. Jamais l'inverse. Une CI le vérifie (voir §8).

```
 apps/        terrain-viewer, terrain-gen, (futurs jeux)
 game/        logique de jeu (bibliothèque rechargeable en développement)
 ─────────────────────────────────────────────────────────────
 sim          boucle à pas fixe, accès aux données du monde
 world        bruit, terrain, planètes, biomes, export
 ui           UIManager, widgets, thèmes, code généré par le langage d'interface
 scene        entités, composants, transformations
 anim         squelettes, clips, machines à états, foules
 assets       import glTF, textures, cuisson, rechargement
 renderer     graphe de passes, plugins de rendu, éclairage, ombres, ciel, effets
   renderer2d   batch de sprites, tuiles, formes (plugin du renderer)
   (futurs plugins : voxel, ray tracing)
 ─────────────────────────────────────────────────────────────
 rhi          interface graphique abstraite (aucune API ici)
   rhi/vulkan   rhi/opengl   rhi/metal      ← seuls endroits où les API existent
 ─────────────────────────────────────────────────────────────
 platform     fenêtre, entrées, fichiers, horloge (GLFW ici)
 core         logger, erreurs, mémoire, tâches, maths, ressources, sérialisation
```

Notes :
- `world` ne dépend **pas** de `renderer` : la génération fonctionne sans GPU (mode ligne de commande, simulation). Les calculs sur GPU passent par `rhi` (compute) et sont isolés dans `world/gpu/`.
- `ui` produit des listes de dessin ; le plugin `renderer2d` les affiche.
- `game/` ne dépend que de l'API publique du moteur et se compile en bibliothèque séparée (§13). Aucun module du moteur ne dépend de `game/`.
- `platform` et `rhi` se rencontrent à un seul endroit : la création de la surface de fenêtre, par une fonction de `platform` appelée par le backend.

## 2. Arborescence

```
include/orosmo/<module>/*.hpp     API publique (stable, documentée)
src/<module>/                     implémentation privée
src/rhi/{vulkan,opengl,metal}/    backends
shaders/                          GLSL source, sous-dossiers par système
tests/<module>/                   tests unitaires
apps/<nom>/                       exécutables
docs/                             cette spécification et les rapports
tools/                            scripts (vérification des couches, génération)
```

Chaque module a un `README.md` : rôle en une phrase, API publique, dépendances, exemple d'usage, limites connues. Les en-têtes publics ne contiennent **aucun** type d'API graphique, ni `<GLFW/...>`, ni `<windows.h>`.

## 3. Interface RHI

Principes : style « moderne » (état explicite, pas d'état global caché) pour que Vulkan et Metal soient naturels et qu'OpenGL soit une traduction. Le RHI ne montre ni `VkRenderPass`, ni descriptor sets, ni VAO. Les ressources sont des **handles opaques** (indice + génération) : un handle périmé est détecté, jamais déréférencé.

Esquisse (à affiner par l'IA, sans changer les principes) :

```cpp
namespace orosmo::rhi {

template <typename Tag> struct Handle { uint32_t index = 0, generation = 0; };
using BufferHandle   = Handle<struct BufferTag>;
using TextureHandle  = Handle<struct TextureTag>;
using SamplerHandle  = Handle<struct SamplerTag>;
using ShaderHandle   = Handle<struct ShaderTag>;
using PipelineHandle = Handle<struct PipelineTag>;
using BindGroupHandle = Handle<struct BindGroupTag>;

enum class Backend { Vulkan, OpenGL, Metal };

struct Capabilities {
    Backend backend;
    uint32_t maxTextureSize, maxColorAttachments, maxComputeWorkGroupSize[3];
    bool compute, indirectDraw, timestampQueries, anisotropy, bc7Textures;
    uint64_t minUniformAlignment, minStorageAlignment;
};

struct BufferDesc   { uint64_t size; BufferUsage usage; MemoryKind memory; const char* debugName; };
struct TextureDesc  { Extent3D size; Format format; uint32_t mips, layers; TextureUsage usage; const char* debugName; };

class Device {
public:
    virtual ~Device() = default;
    virtual const Capabilities& capabilities() const = 0;

    virtual std::expected<BufferHandle,  Error> createBuffer(const BufferDesc&, std::span<const std::byte> initial = {}) = 0;
    virtual std::expected<TextureHandle, Error> createTexture(const TextureDesc&) = 0;
    virtual std::expected<SamplerHandle, Error> createSampler(const SamplerDesc&) = 0;
    virtual std::expected<ShaderHandle,  Error> createShader(const ShaderDesc&) = 0;   // SPIR-V ou MSL, selon le backend
    virtual std::expected<PipelineHandle, Error> createGraphicsPipeline(const GraphicsPipelineDesc&) = 0;
    virtual std::expected<PipelineHandle, Error> createComputePipeline(const ComputePipelineDesc&) = 0;
    virtual std::expected<BindGroupHandle, Error> createBindGroup(const BindGroupDesc&) = 0;
    virtual void destroy(BufferHandle) = 0;   // + les autres types ; destruction différée jusqu'à la fin des images en vol

    virtual std::unique_ptr<CommandEncoder> beginFrame() = 0;     // acquiert l'image du swapchain
    virtual void submitAndPresent(std::unique_ptr<CommandEncoder>) = 0;
    virtual void resize(Extent2D) = 0;
    virtual void waitIdle() = 0;
};

class CommandEncoder {
public:
    virtual ~CommandEncoder() = default;
    virtual void beginRenderPass(const RenderPassDesc&) = 0;    // attachements, load/store, clear
    virtual void endRenderPass() = 0;
    virtual void setPipeline(PipelineHandle) = 0;
    virtual void setBindGroup(uint32_t slot, BindGroupHandle, std::span<const uint32_t> dynamicOffsets = {}) = 0;
    virtual void setVertexBuffer(uint32_t slot, BufferHandle, uint64_t offset = 0) = 0;
    virtual void setIndexBuffer(BufferHandle, IndexType, uint64_t offset = 0) = 0;
    virtual void setPushConstants(std::span<const std::byte>) = 0;   // émulé par un petit tampon sur OpenGL
    virtual void setViewport(const Viewport&) = 0;
    virtual void setScissor(const Rect&) = 0;
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) = 0;
    virtual void drawIndirect(BufferHandle args, uint64_t offset, uint32_t drawCount, uint32_t stride) = 0;
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;
    virtual void dispatchIndirect(BufferHandle args, uint64_t offset) = 0;
    virtual void copyBuffer(BufferHandle src, BufferHandle dst, const BufferCopy&) = 0;
    virtual void copyBufferToTexture(BufferHandle, TextureHandle, const TextureCopy&) = 0;
    virtual void generateMips(TextureHandle) = 0;
    virtual void beginDebugGroup(const char*) = 0;
    virtual void endDebugGroup() = 0;
    virtual void writeTimestamp(QueryPoolHandle, uint32_t index) = 0;
};

std::expected<std::unique_ptr<Device>, Error> createDevice(const DeviceDesc&, Window&);
}
```

### Règles du RHI

1. **Barrières et transitions de layout : le RHI les calcule.** Les couches hautes déclarent l'usage des ressources (lecture shader, cible de rendu, stockage, copie) ; le backend Vulkan en déduit les barrières ; OpenGL les traduit en `glMemoryBarrier` ; Metal les ignore au besoin. Aucune barrière manuelle dans le code du moteur.
2. **Destruction différée.** `destroy` n'est effectif qu'une fois les images en vol terminées.
3. **Pas d'état implicite.** Chaque passe déclare ses attachements ; chaque pipeline porte tout son état fixe.
4. **Les fonctionnalités optionnelles passent par `Capabilities`.** Le moteur demande, il ne suppose pas. Si une fonction est absente, le renderer choisit un chemin de repli ou refuse proprement avec un message clair.
5. **Un seul modèle de liaison** (voir §4).
6. **Mémoire** : Vulkan utilise VMA. Les buffers déclarent leur usage (`GpuOnly`, `Upload`, `Readback`).
7. **Erreurs** : `std::expected`. La perte de périphérique est remontée, pas ignorée.
8. **Nommage** : tout objet reçoit un `debugName` visible dans RenderDoc ou le débogueur GPU.

## 4. Modèle de liaison des ressources

Trois groupes, fixes pour tout le projet :

| Groupe | Contenu | Fréquence de changement |
| --- | --- | --- |
| 0 | Données par image : caméra, éclairage (UBO/SSBO), temps | une fois par image |
| 1 | Matériau ou passe : textures, échantillonneurs, constantes de matériau | par matériau ou par passe |
| 2 | Données par objet ou par tuile (offsets dynamiques) | par objet |

- Vulkan : un `VkDescriptorSet` par groupe.
- OpenGL : les (groupe, binding) sont convertis en emplacements plats par la table de réflexion des shaders (`glBindBufferRange`, `glBindTextureUnit`, `glBindImageTexture`).
- Metal : tables d'arguments ou liaisons directes, générées par SPIRV-Cross.
- Les structures `std140` et `std430` existantes sont gardées et partagées entre C++ et shader par **un seul fichier d'en-tête** généré ou vérifié (`static_assert` sur tailles et offsets).

## 5. Shaders

- **Source unique en GLSL 450** écrit dans un style compatible Vulkan (`layout(set = N, binding = M)`), avec `#include`.
- Compilation **hors ligne** (glslang ou shaderc) vers SPIR-V ; réflexion des liaisons par SPIRV-Cross.
- **OpenGL** : SPIRV-Cross régénère du GLSL pour la version minimale visée (4.3), ou `GL_ARB_gl_spirv` quand il est disponible (4.6). Le choix est documenté et testé.
- **Metal** : SPIRV-Cross génère du MSL (utilisé directement par un backend natif ; MoltenVK le fait lui-même sinon).
- Un cache de shaders compilés ; le rechargement à chaud recompile et recrée les pipelines sans redémarrage.
- Aucune différence de comportement selon le backend n'est acceptable pour un même shader : les tests d'images de référence (RHI-14) le vérifient.

## 6. Plateforme et fenêtre

- `platform::Window` cache GLFW. Elle expose : taille, DPI, événements, et `createSurface(rhi::Backend)` qui renvoie un descripteur opaque consommé par le backend.
- Le contexte OpenGL, le `VkSurfaceKHR` et le `CAMetalLayer` sont créés **dans les backends** à partir de ce descripteur.
- Les chemins de fichiers, l'horloge et les répertoires utilisateur passent par `platform` ; pas d'appel système direct ailleurs.

## 7. Conventions

- C++23, `-Wall -Wextra -Wpedantic -Werror` (sauf bibliothèques tierces). Pas de RTTI ni d'exceptions dans les chemins chauds.
- Noms : types en `PascalCase`, fonctions et variables en `camelCase`, constantes en `kPascalCase`, fichiers en `snake_case` ou comme l'existant (l'IA suit la convention du code déjà présent et la note dans `CLAUDE.md`).
- Un en-tête public par concept ; `#pragma once` ; pas d'inclusion circulaire ; déclarations anticipées de préférence.
- Pas de singleton global caché : les services (logger excepté) sont passés ou accessibles par un contexte explicite.
- Les unités sont dans les noms (`distanceMeters`, `angleRadians`). Repère : main droite, Y vers le haut, mètres. À confirmer par l'audit selon l'existant.

## 8. Vérifications automatiques (dans `tools/` et la CI)

1. **Couches** : script qui lit les `#include` et échoue si un module inclut un module de niveau supérieur.
2. **Isolation des API** : échec si `gl`, `GL_`, `Vk`, `vk`, `MTL`, `<GLFW/`, `<windows.h>` apparaissent hors de `src/rhi/<backend>/` et `src/platform/`.
3. **En-têtes publics autonomes** : chaque en-tête de `include/` se compile seul.
4. **Déterminisme** : somme de contrôle des sorties de génération comparée à une référence versionnée.
5. **Images de référence** : comparaison par backend, avec seuil et rapport de différence.

## 9. Ajouter un backend (voir aussi §14)

Le guide `docs/ADD_A_BACKEND.md` (à écrire par l'IA au jalon M4) doit permettre à un lecteur d'ajouter un backend en suivant : 1) implémenter `Device` et `CommandEncoder`, 2) fournir la table de traduction des formats, 3) traduire le modèle de liaison, 4) passer la suite de tests RHI, 5) passer les images de référence.

## 10. Données fournies aux shaders (groupe 0, une fois par image)

Tous les shaders d'effet (atmosphère, nuages, brouillard, post-traitement) reçoivent le même bloc, défini **une seule fois** dans un en-tête partagé C++/GLSL avec `static_assert` sur tailles et offsets :

```cpp
struct alignas(16) FrameData {            // std140
    mat4  view, proj, viewProj;
    mat4  invView, invProj, invViewProj;
    mat4  prevViewProj;                   // vecteurs de mouvement, TAA
    vec4  cameraRelativeOrigin;           // position caméra ramenée en float (rendu relatif)
    vec4  cameraWorldHigh, cameraWorldLow;// position monde en double scindée en deux float
    vec4  jitter;                         // xy courant, zw précédent
    vec4  viewport;                       // largeur, hauteur, 1/largeur, 1/hauteur
    vec4  time;                           // secondes, delta, numéro d'image, graine de bruit de l'image
    vec4  sunDirection, sunColor;         // lumière principale (couleur × intensité)
    vec4  planet;                         // rayon, hauteur de l'atmosphère, albédo du sol, réservé
    vec4  scatteringRayleigh, scatteringMie; // coefficients de diffusion
    vec4  exposure;                       // exposition, réservé
};
```

Règles :
- Les positions monde utilisent `double` côté CPU ; le shader ne voit que des valeurs **relatives à la caméra**.
- Les tables d'atmosphère, la profondeur, les AOV et les textures de bruit sont des ressources liées au groupe 1 de la passe qui les utilise (jamais globales implicites).
- La reconstruction de position depuis la profondeur (reverse-Z, matrices inverses) est fournie par la boîte à outils GLSL (REN-22). Aucun effet ne la réécrit.
- Les paramètres d'un effet sont une structure réfléchie (CORE-12) : l'interface les affiche automatiquement (UI-12).

## 11. Rendu : plugins, MRT, AOV, 2D

**Plugins de rendu (render features).** Le renderer ne connaît que l'interface `RenderFeature` : `declarePasses(RenderGraphBuilder&)`, `prepare(const FrameData&)`, `declareAov()`. Maillages, terrain, 2D, et plus tard voxels et ray tracing sont des plugins. Un plugin accède aux capacités optionnelles via `Capabilities` et se désactive proprement si elles manquent.

**Sorties multiples (MRT) et AOV.**
- `RenderPassDesc` accepte jusqu'à `Capabilities::maxColorAttachments` cibles couleur (au moins 4 attendues) plus la profondeur, chacune avec format, `loadOp`, `storeOp`.
- Formats recommandés : couleur `RGBA16F` (HDR), normales octaédriques `RG16_SNORM` ou `RG8_SNORM`, vecteurs de mouvement `RG16F`, identifiant `R32UI`, rugosité/métal `RG8_UNORM`.
- Une passe de post-traitement déclare ses entrées (`normals`, `motion`, `id`, `linearDepth`). Le renderer active **seulement** les AOV demandés et sélectionne la variante de shader correspondante ; sans demande, la passe n'écrit qu'une cible.
- Avec MSAA : les AOV continus (normales, couleur) se résolvent par moyenne, les identifiants prennent un échantillon sans moyenne. La règle est appliquée par le renderer, pas par chaque effet.
- Alternative : reconstruire les normales depuis la profondeur en compute quand le MRT est trop coûteux. Les deux chemins existent, choisis par option.

**Ordre de rendu d'une image** (fixe) : mise à jour des données → culling GPU → ombres → pré-passe de profondeur → remplissage des clusters → passe principale (3D opaque, puis 3D transparent, puis 2D du monde) → effets (atmosphère, brouillard, nuages) → post-traitement HDR → tonemapping → 2D en superposition (interface) → présentation.

**2D.** Le plugin `renderer2d` utilise un batch unique (quads instanciés dans un tampon, atlas ou tableau de textures, tri par couche puis matériau). L'interface lui envoie ses listes de dessin. Les trois modes de mélange sont décrits en R2D-05.

**Éclairage et ombres.** Chaque lumière porte `mobility` (`Static`, `Stationary`, `Dynamic`) et `shadowMode` (`None`, `Baked`, `Dynamic`, `Cached`).
- `Cached` : l'ombre de la géométrie fixe est conservée dans l'atlas ; seuls les objets mobiles sont redessinés par-dessus. Le cache est invalidé quand la lumière ou la géométrie fixe change.
- `Baked` : consomme des lightmaps ou sondes cuites (REN-16). Sur un monde généré à la volée, l'IA le laisse inactif tant que l'outil de cuisson n'existe pas.
- Le budget d'ombres (taille d'atlas, nombre de mises à jour par image) est réglable et lisible dans les outils de debug.

**Performance.** Instancing partout où c'est possible ; tampons de maillages partagés ; scène GPU persistante ; tri par pipeline ; tableaux de textures. Le bindless n'est utilisé que s'il est exposé par `Capabilities`, avec repli par tableaux de textures.

## 11 bis. Transparence

Chaque matériau porte un `BlendMode`. L'ordre de rendu dans la passe principale est fixe.

| Mode | Usage | Écrit la profondeur | Tri nécessaire |
| --- | --- | --- | --- |
| `Opaque` | Cas général | Oui | Non |
| `Mask` | Feuillage, grilles : pixel gardé ou rejeté selon un seuil | Oui | Non |
| `MaskA2C` | Comme `Mask`, bords adoucis avec MSAA (alpha-to-coverage) | Oui | Non |
| `Dithered` | Fondu de LOD, objets qui s'effacent (lissé par le TAA) | Oui | Non |
| `Blend` | Verre, eau simple, effets | Non | Oui |
| `Additive` | Feu, lumières, étincelles | Non | Non |
| `Premultiplied` | Interface, sprites 2D | Non | Par couche |
| `Transmission` | Verre qui réfracte la scène derrière lui | Non | Oui |

**Ordre** : opaques et masqués (avec profondeur) → transparents `Blend` et `Transmission`, triés de l'arrière vers l'avant, test de profondeur sans écriture → `Additive` → sprites 2D du monde → post-traitement.

**Tri** : par distance du centre de l'objet à la caméra, avec une **priorité** par matériau qui l'emporte. Les grands objets (plans d'eau) sont découpés ou reçoivent une priorité explicite. Les cas insolubles par tri passent par l'OIT (REN-40).

**Éclairage et brouillard** : les transparents sont éclairés par le même forward+ clusterisé et reçoivent le brouillard et l'atmosphère dans leur propre shader.

**Ombres** (`REN-39`) : chaque matériau choisit `none`, `solid`, `cutout` (masque) ou `dithered`. Le défaut est `cutout` pour `Mask` et `MaskA2C`, `none` pour `Blend`.

**Sorties auxiliaires** (`REN-41`) : un transparent n'écrit par défaut ni normales, ni identifiant, ni vecteurs de mouvement. Le matériau peut les demander ; le renderer prévient dans le journal quand l'écriture est incompatible avec le mode (par exemple un identifiant sur un `Blend` superposé).

**Réfraction** (`REN-38`) : le renderer copie la couleur de la scène opaque dans une texture avec mips avant la passe transparente. Un matériau `Transmission` l'échantillonne en décalant les coordonnées selon la normale et l'indice de réfraction. Limite connue : un objet ne se réfracte pas lui-même, et deux couches de verre successives ne se déforment pas mutuellement.

**Profondeur et effets** : les transparents ne sont pas dans la profondeur de la pré-passe. Les effets qui en dépendent (profondeur de champ, brouillard volumétrique) peuvent demander une passe de profondeur supplémentaire pour les transparents importants, déclarée par le matériau.

**OIT** (`REN-40`, option) : OIT pondéré par défaut (deux cibles supplémentaires et une passe de composition) ; les variantes exactes viennent plus tard. Le plugin d'OIT ne modifie pas le cœur du renderer.

**Défauts recommandés** : opaque pour presque tout, `MaskA2C` pour le feuillage, `Dithered` pour les fondus de LOD, `Blend` trié pour un petit nombre d'objets, `Additive` pour les effets lumineux, `Transmission` pour les rares objets réfractifs.

## 12. Interface, langage d'interface et thèmes

- **Pipeline** : fichier du langage → compilateur → C++ généré → compilé avec le reste. Le compilateur est un programme (ou une bibliothèque) distinct, appelé par le build, avec des dépendances déclarées pour ne recompiler que le nécessaire.
- **Séparation stricte** : le langage décrit la **structure** et les **liaisons** ; la **logique** est en C++ dans d'autres fichiers ; l'**apparence** est dans un thème JSON.
- **Événements** : un événement nomme une fonction (`onClick: startGeneration`). En mode développement, la fonction est retrouvée **par nom dans un registre** à l'appel (GAME-04). En version finale, le code généré appelle la fonction directement.
- **Liaison de données** : valeurs typées, dans les deux sens, à travers `DeferredValue<T>` (écriture visible à l'image suivante).
- **Thèmes** : jetons nommés, états des widgets, variantes ; chargement à chaud. Un widget ne contient jamais de couleur ou de taille en dur sauf surcharge explicite et locale.
- **Rendu** : l'interface produit des listes de dessin (rectangles, texte, images, découpage, ordre `z`) ; elle ne connaît ni le RHI ni le backend.
- **Rechargement à chaud** : en développement, le même arbre est interprété (ou chargé en bibliothèque dynamique) et mis à jour à l'enregistrement du fichier ; en version finale, le C++ généré est lié directement.
- **Documentation** : `docs/UI_LANGUAGE.md` (grammaire, exemples, limites, messages d'erreur), écrite à partir du code existant et validée par des tests de compilation.

## 13. Logique de jeu rechargeable

- La logique de jeu est une **bibliothèque dynamique** distincte de l'exécutable du moteur, avec une interface C stable : `GameApi { init, update, onReload, shutdown }`.
- Tout état qui doit survivre à un rechargement vit **dans le moteur** (composants ECS, structures de données simples), jamais dans la bibliothèque. Les composants sont des données réfléchies (CORE-12), ce qui permet la migration d'état.
- Les systèmes s'enregistrent **par nom** à `init` et se désenregistrent au déchargement. Le moteur ne conserve ni pointeur vers du code de la bibliothèque, ni `std::function`, ni variable `static` définie dedans.
- Changement du corps d'une fonction : rechargé sans perte. Ajout d'un champ : migration par réflexion. Changement de hiérarchie ou de fonctions virtuelles : sauvegarde de l'état, rechargement, restauration.
- Windows : copie sous un nom unique avant chargement (le fichier d'origine reste libre). Linux et macOS : `dlopen` d'un chemin unique.
- Les paramètres de gameplay vivent en JSON rechargés instantanément.
- Version finale : la logique est liée statiquement ; aucun code de rechargement n'est compilé.
- Alternative écartée pour l'instant : un langage de script embarqué (Lua). À réévaluer si le C++ rechargé se révèle trop lent à compiler.

## 14. Extensions prévues : voxels et ray tracing

Ces fonctions ne sont pas écrites maintenant ; l'architecture doit seulement les accepter.

- **Voxels** : un plugin de rendu (maillage par chunks, ou tracé de rayons en compute sur voxels creux). Besoins du RHI déjà couverts : textures 3D, images de stockage, compute, dessin indirect, grands buffers.
- **Ray tracing** : `AccelerationStructure` (BLAS/TLAS) et requêtes de rayon dans le RHI, activés par `Capabilities::rayQuery`. Vulkan et Metal les exposent ; OpenGL non. Sans matériel, le plugin choisit un tracé logiciel en compute ou se désactive.
- **Critère** : ajouter l'un de ces plugins ne doit modifier ni le renderer, ni les backends existants, hors l'ajout des fonctions d'accélération au RHI.

## 15. Cœur générique, modules optionnels et addons (décision A34)

La release distribuable du moteur (`orosmo-pkg-<plateforme>.zip` et le projet généré par `orosmo init`) vise un **moteur générique** : seuls les modules cœur (`core`, `platform`, `rhi`, `renderer` générique, `ui`, `thirdparty`, `cui`) y sont inclus par défaut.

Les modules spécialisés (comme `world` et `TerrainRenderer`, conservés dans le dépôt pour les démos internes) n'appartiennent pas au cœur distribué :
- **Aujourd'hui (M3+)** : ils sont compilés en archives distinctes (`libworld.a`, `libterrain_renderer.a`), exclus de `dist/`, et reliés explicitement par les applications internes du dépôt via `ENGINE_EXTRA_MODULES`.
- **À terme (M11, `MOD-01` à `MOD-04`)** : un système de manifestes et de packaging permettra d'ajouter facilement tout module utilitaire, soit **intégré dans la release** (`bundled`), soit **distribué en addon séparé** (`orosmo addon add`), branché automatiquement par `engine.mk` sans modifier le cœur du moteur.
- **Ensuite (M12, `MOD-05` à `MOD-08`)** : un **registre** d'addons (index et archives vérifiés par empreinte et signature) et un **gestionnaire** graphique pour les parcourir et les installer. C'est un produit à part, construit avec le moteur : il n'entre pas dans le cœur et ne dépend d'aucun backend.
