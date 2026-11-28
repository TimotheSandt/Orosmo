# rhi/vulkan : backend Vulkan 1.3 du RHI (M4)

Seul endroit (avec `src/rhi/opengl/`) où existent `Vk*`, `vk*`, volk et VMA. Même contrat que le backend OpenGL (`include/orosmo/rhi/device.hpp`) :
**le comportement observable (résultats, codes d'erreur, validations) doit être celui de `src/rhi/opengl/`**, qui sert de référence
(`gl_device.cpp`, `gl_encoder.cpp`) ; les tests communs (`tests/rhi/rhi_device_tests.cpp`) tournent sur les deux backends.

| Fichier | Rôle |
| --- | --- |
| `vk_device.hpp` | **Contrat interne** : structures des ressources, classe `VulkanDevice`, qui possède quoi |
| `vk_device.cpp` | instance, GPU, périphérique logique, VMA, capacités, séries de soumission, libération différée, réserves de tampons de commandes, lot et anneau de transferts |
| `vk_resources.cpp` | tampons, textures, échantillonneurs, requêtes de temps ; `update*` / `read*` synchrones |
| `vk_pipelines.cpp` | shaders (réflexion SPIR-V), dispositions de descripteurs, pipelines, groupes de liaison, cache de pipelines |
| `vk_encoder.{hpp,cpp}` | `CommandEncoder` : enregistrement, passes, barrières, `Device::submit` |
| `vk_swapchain.{hpp,cpp}` | surface, chaîne d'échange, image de fond, `beginFrame` / `present` / `resize` / `setVSync` |
| `vk_translate.hpp` | tables RHI -> Vulkan ; `vk_region.hpp` région de texture ; `vk_sync.hpp` barrières ; `vk_util.hpp` `makeStruct` |
| `vk_vma.cpp` | implémentation de VMA (une seule unité) |

## Chargement et exigences

- **volk** (vcpkg) charge Vulkan à l'exécution (`volkInitialize`, `volkLoadInstance`, `volkLoadDevice`) : le moteur ne se lie PAS au
  chargeur Vulkan. Sans chargeur ou sans GPU compatible, `createDevice` renvoie `kUnsupported` avec la raison ; les tests et
  les démos retombent sur OpenGL (RHI-04), sauf sous macOS, où `fallbackBackend` ne renvoie rien. Les fonctions sont globales : un seul pilote par processus.
- **Vulkan 1.3** exigé : rendu dynamique (`VK_KHR_dynamic_rendering`, noyau 1.3), `synchronization2`, sémaphore à chronologie,
  `hostQueryReset`. Un GPU qui ne les a pas est écarté avec la raison (liste dans le message d'erreur). Choix du GPU : discret,
  puis intégré, puis processeur (lavapipe) ; `ENGINE_VULKAN_DEVICE=<indice ou fragment du nom>` force un GPU.
- **Couches de validation** (`DeviceDesc::validation`) : `VK_LAYER_KHRONOS_validation` + validation de synchronisation
  (`ENGINE_VULKAN_SYNC_VALIDATION=0` la coupe). Chaque message d'erreur ou d'avertissement est compté
  (`Device::validationErrorCount`) ; les tests exigent 0.

## Séries et libération différée

Un seul sémaphore à chronologie ; chaque soumission le fait croître de 1 (`Serial`). `destroy(handle)` retire le handle tout de
suite (il devient périmé) et met la libération en file avec la série courante : elle s'exécute quand le GPU a atteint cette
série (`collectReleases`) **et** que `framesInFlight()` images (A61 G, `DeviceDesc::framesInFlight`) ont été présentées depuis (même compteur que le backend OpenGL :
`DeviceStats::pendingDestruction` évolue pareil) ; `waitIdle` libère sans attendre d'image, les tampons de transfert internes
n'attendent que le GPU. `beginFrame` attend la série de l'image N - 2 (2 images d'avance au plus).

## Tampons de commandes

Réserves (`VkCommandPool`, sans `RESET_COMMAND_BUFFER_BIT`) en anneau : la réserve courante donne ses tampons dans l'ordre
(`acquireCommandBuffer`). À chaque `present` (et après `kCommandBuffersPerPool` tampons), on passe à une réserve dont tous les
tampons sont rendus (`retireCommandBuffer` : soumis ; `releaseUnsubmitted` : abandonnés, quel que soit leur état) et finis par le
GPU : elle est remise à zéro **en bloc** (`vkResetCommandPool`) ; sinon une réserve neuve. En régime établi : une réserve par image
en vol, aucune allocation ni remise à zéro par tampon.

## Dispositions « de repos » et barrières déduites

Chaque texture a une **disposition de repos** (`TextureRes::restLayout`), fixée à la création selon ses usages :
`kStorage` -> `GENERAL` ; sinon `kSampled` -> `SHADER_READ_ONLY` (`DEPTH_STENCIL_READ_ONLY` pour la profondeur) ; sinon
`kRenderTarget` -> `COLOR_ATTACHMENT` / `DEPTH_STENCIL_ATTACHMENT` ; sinon `GENERAL`. **Entre deux commandes, toute texture est
à son repos.** Une commande qui exige une autre disposition (cible de rendu, copie, mips) y fait passer l'image (transition
précise), puis la **ramène au repos avant de finir**. Conséquences : un encodeur n'a jamais à connaître l'état laissé par un
autre (pas d'état partagé entre encodeurs, un encodeur jamais soumis ne casse rien) ; l'échantillonnage dans une passe ne
demande aucune barrière ; une texture neuve (UNDEFINED) est mise au repos par le lot de transferts, en tête de la prochaine soumission.

Les barrières mémoire sont **conservatrices** : une dépendance « tout contre tout » (`memoryBarrierAll`) avant et après chaque
commande hors passe (copie, dispatch, mips) et autour de chaque passe. C'est correct (la validation de synchronisation ne dit
rien) mais pas optimal ; l'affinement par ressource est suivi dans `docs/GAPS.md`. Pas de barrière DANS une passe : une
écriture de stockage lue par un dessin suivant de la même passe n'est pas prise en charge (le RHI l'interdit hors compute).

## Transferts

`updateBuffer` (tampon `kGpuOnly`), `updateTexture`, tampon créé avec des données : copie dans un **lot** (`batch()`), **joint en
tête de la prochaine soumission** (même `vkQueueSubmit2`, `submitWithBatch`), donc **ordonné avec les soumissions** comme
`glBufferSubData` sans attendre le GPU ni soumettre à part (une soumission par image en régime établi). Un `update*` s'applique
avant la prochaine soumission, pas à la position où un encodeur déjà enregistré l'a vu. `flushPending` ne soumet le lot seul que
quand rien d'autre ne part (`present` sans encodeur après l'envoi, `waitIdle`, lecture d'un tampon mappé, lot > 64 Mio).

- **Anneau de transfert** : blocs de `kStagingBlockSize` (4 Mio) mappés en permanence, remplis linéairement ; un bloc repart de son
  début quand le GPU a fini la dernière série qui l'a lu ; au-delà de `kStagingRetainedBlocks` blocs libres, les blocs en trop sont
  détruits. Un envoi de plus de `kStagingDedicatedThreshold` (1 Mio) prend un tampon dédié, libéré avec son lot. Décalage aligné sur
  16 octets et sur la taille d'un texel (copie vers une texture).
- **Barrières groupées** : une barrière complète en début de lot (après le travail déjà soumis) et une en fin (avant ce qui suit) ;
  entre deux copies de tampon du lot, une barrière seulement si la plage recouvre une copie précédente (la dernière écriture gagne).
  Les transitions des envois de texture n'en tiennent pas lieu (une barrière d'image ne couvre pas la mémoire des tampons).
- **Tampons `kUpload`** : `updateBuffer` écrit directement dans la mémoire mappée (`vmaFlushAllocation`), sans lot ni barrière,
  **seulement si le GPU n'utilise plus le tampon** : `BufferRes::gpuUse` garde la dernière série qui le lit ou l'écrit (soumission
  d'un encodeur qui le lie ou le copie, lot qui le copie). Sinon, copie ordonnée comme pour `kGpuOnly` : le résultat est toujours
  celui de l'ancien chemin, seul le coût change. Suivi par tampon (pas par plage) : un tampon par image en vol côté appelant,
  réécrit après le `beginFrame` qui a attendu son image, obtient l'écriture directe.
- `readTimestamps` / `timestampsReady` ne soumettent rien : seuls des encodeurs déjà soumis écrivent des horodatages ; s'ils ne sont
  pas prêts, `readTimestamps` attend la dernière série soumise.
- Compteurs internes `VulkanDevice::transferCounters()` (soumissions, blocs, tampons dédiés, barrières, réserves) : diagnostic et
  tests (`tests/rhi/vulkan_transfer_tests.cpp`).

`read*` et les copies bloquantes passent par `immediate()` (soumet après le lot, même soumission, et attend).

## Descripteurs : groupes fixes, tout tampon dynamique

Trois groupes (`kGroupCount`), liaisons < `kBindingsPerGroup` (docs/03 §4) ; groupe = `set`, liaison = `binding` du GLSL 450.

- La disposition d'un groupe vient **du shader** (réflexion SPIR-V : `ShaderInterface`), jamais du groupe de liaison, et **tout
  tampon y est `*_BUFFER_DYNAMIC`** (voir `vk_translate.hpp`). Un groupe de liaison n'a donc pas de disposition propre : une
  entrée non dynamique reçoit le décalage 0, une entrée `dynamicOffset` reçoit sa valeur de `setBindGroup`.
- `descriptorSetFor(groupe, disposition)` crée à la demande le jeu de descripteurs de ce groupe pour cette disposition et le garde
  (cache par `SetLayout::id`). Les entrées en trop du groupe sont ignorées ; une liaison absente ou d'un autre type est
  `kInvalidArgument`. Les jeux sont libérés avec le groupe.
- L'encodeur lie les jeux **au moment du dessin ou du dispatch** (pas à `setBindGroup`) : l'ordre `setPipeline` / `setBindGroup`
  est libre. Décalages dynamiques passés à `vkCmdBindDescriptorSets` : un par tampon de la disposition, dans l'ordre croissant
  des liaisons ; `setBindGroup` fournit les siens dans l'ordre des ENTRÉES du groupe (celles marquées `dynamicOffset`).
- Texture échantillonnée : `COMBINED_IMAGE_SAMPLER`, disposition = `restLayout` de la texture. Image de stockage : `GENERAL`, vue
  du niveau `mipLevel` (tableaux, cubes et 3D liés en entier, vues `2D_ARRAY` / `3D`).
- Une liaison tableau (`uniform sampler2D t[3]` en liaison `b`) prend ses éléments dans les entrées `b`, `b + 1`, `b + 2` du
  groupe, comme les emplacements plats d'OpenGL. Tampon de taille 0 : `VK_WHOLE_SIZE` (la fin du tampon, décalage dynamique déduit).
- Le pipeline exige un groupe à chaque emplacement que ses shaders utilisent : dessiner sans groupe lié à cet emplacement est
  `kInvalidArgument` (OpenGL lirait la liaison laissée par un dessin précédent ; Vulkan n'a pas de liaison par défaut).
- Réserves de descripteurs (`FREE_DESCRIPTOR_SET_BIT`, 256 jeux) créées à la demande quand la dernière est pleine.

## Shaders, pipelines et groupes de liaison (vk_pipelines.cpp)

- `createShader` n'accepte que `ShaderLanguage::kSpirv` (`createShaderFromGlsl` livre déjà du SPIR-V aux backends qui l'annoncent
  par `Capabilities::spirvShaders`). Réflexion par SPIRV-Cross (déjà dans le moteur).
- Pipelines graphiques en **rendu dynamique** (`VkPipelineRenderingCreateInfo` : formats des cibles du pipeline) ; viewport et
  ciseau dynamiques. La passe doit avoir les mêmes formats que le pipeline (sinon `kInvalidArgument` à `setPipeline`).
- Dispositions de groupe (étages réunis) et de pipeline dédupliquées (`setLayouts_`, `pipelineLayouts_`) ; un groupe inutilisé
  avant un groupe utilisé reçoit la disposition vide. Une plage de push constants (taille max des étages, étages réunis) ; à chaque
  dessin ou dispatch, toute la plage est poussée (zéros au-delà des valeurs reçues : `maintenance4` n'est pas activé).
- « Édition de liens » faite par le backend : une entrée du shader de fragments sans sortie du shader de sommets de même
  emplacement et de même type est `kParse` (comme `glLinkProgram`) ; une entrée du shader de sommets sans attribut est
  `kInvalidArgument` (Vulkan n'a pas de valeur constante par défaut).
- **Cache de pipelines (RHI-06)** : un `VkPipelineCache` partagé par toutes les créations, chargé à la création du périphérique depuis
  `<dossier>/vulkan_<vendor>_<device>.bin`, `<dossier>` = `ENGINE_VULKAN_PIPELINE_CACHE_DIR`, sinon
  `userDataDirectory()/Orosmo/pipeline_cache`. L'en-tête (`VkPipelineCacheHeaderVersionOne` : vendorID, deviceID, UUID du cache) est
  vérifié : un fichier d'un autre GPU ou pilote est ignoré. Réécrit à l'arrêt (fichier temporaire puis renommage). Pas de cache
  « même description -> même `VkPipeline` » : le backend OpenGL n'en a pas non plus (le renderer a son `PipelineCache`).

## Encodeur (vk_encoder.cpp)

- Un tampon de commandes primaire par encodeur, pris à la création ; un encodeur en erreur ou détruit sans `submit` le rend au
  recyclage (remise à zéro, jamais soumis). Mêmes validations et codes d'erreur que `GlEncoder` ; `submit` renvoie la première
  erreur, ou `kInvalidArgument` si une passe ou un groupe de debug reste ouvert.
- Passes : `vkCmdBeginRendering`, attachements en `COLOR_ATTACHMENT` / `DEPTH_STENCIL_ATTACHMENT` (transition depuis le repos,
  retour au repos à `endRenderPass`) ; viewport et ciseau couvrent la cible au début de la passe. Une tranche de texture 3D est
  une vue 2D de l'image (`2D_ARRAY_COMPATIBLE`).
- Push constants, sommets, indices et groupes sont mémorisés et liés au dessin ; le pipeline graphique est lié à `setPipeline`, le
  pipeline de calcul au dispatch.
- Dessins indirects : `vkCmdDraw*Indirect` en un appel si `multiDrawIndirect`, sinon un appel par commande.
- GPU Metal paravirtualisé (MoltenVK sur « Apple Paravirtual device », machines virtuelles macOS dont le runner de la CI, M9-L2,
  G510) : `drawIndexed` y est enregistré comme un `vkCmdDrawIndexedIndirect` d'une commande, écrite dans un tampon d'arguments de
  l'encodeur (mémoire d'envoi, 256 commandes puis le double ; détruit avec l'encodeur, après sa soumission). Le dessin indexé
  direct de ce pilote ne lit pas les indices de la plupart des tampons d'index (rien n'est tracé, sans message) ; l'indirect les
  lit. Choisi à la création (`VulkanDevice::detectIndexedDrawWorkaround`, journalisé) ; `ENGINE_VULKAN_INDEXED_VIA_INDIRECT=0|1`
  le force (diagnostic, tests). Sans `drawIndirectFirstInstance`, le contournement n'est pas pris.
- `generateMips` : chaîne de `vkCmdBlitImage` linéaires (refusé, `kUnsupported`, si le format n'a pas le filtrage linéaire par
  blit) ; même refus qu'OpenGL pour la profondeur et les entiers.
- Requêtes de temps : une requête Vulkan doit être remise à zéro avant d'être réécrite. Hors passe, `vkCmdResetQueryPool` juste
  avant l'écriture ; dans une passe (interdit), la remise à zéro va dans un **prologue** soumis juste avant l'encodeur, et la même
  requête ne peut pas y être écrite deux fois par le même encodeur (`kUnsupported`).
- Une ressource utilisée par un encodeur doit vivre jusqu'à son `submit` (la libération différée compte depuis la destruction).

## Repère (A26)

Origine en haut à gauche, y = +1 en haut en NDC, profondeur [0, 1], reverse-Z côté caméra. Vulkan : viewport à **hauteur
négative** (`{x, y + h, w, -h}`, noyau 1.1), ce qui garde `kCounterClockwise` du RHI = `COUNTER_CLOCKWISE`. Le ciseau part du haut
nativement. `readTexture` : première ligne = haut.

## Présentation

Deux chemins, même contrat (`beginFrame` renvoie l'image de fond RGBA8 UNORM à la taille du framebuffer, `kBusy` pour un
framebuffer de taille nulle ; fenêtre créée APRÈS `prepareWindowHints`, `GLFW_NO_API`) :

- **Image de la chaîne directe (G185)** : quand la surface propose `R8G8B8A8_UNORM` avec les usages de l'image de fond (cible,
  échantillonnée, source et destination de copie) — Wayland chez Mesa —, `beginFrame` acquiert l'image de la chaîne et la met dans
  l'emplacement de l'image de fond (même poignée). La première soumission qui suit attend l'acquisition ; `present` la passe en
  disposition de présentation et la présente, sans copie. Son contenu ne survit pas d'une image à l'autre (il faut redessiner
  toute l'image, ce que fait le moteur). Un second `beginFrame` sans `present` rend la même image.
- **Copie** (surface sans RGBA8, comme X11 chez Mesa, `DeviceDesc::directPresentation = false`, `setDirectPresentation(false)`
  ou `ENGINE_VK_DIRECT_SWAPCHAIN=0`, qui l'emporte) : l'image de fond est une texture RGBA8
  du périphérique ; `present()` la copie (blit, mise à l'échelle si la taille a changé) dans l'image acquise puis présente.

Le dessin direct se coupe et se rallume en cours de route (`setDirectPresentation`) : la chaîne est reconstruite à
l'acquisition suivante. À très haute cadence sans vsync, il peut scintiller sur certains compositeurs (G193) : le coupant,
on revient à la copie.

`transferCounters()` compte les deux (`directPresents`, `copiedPresents`) ; la démo les journalise à l'arrêt.

## Limites connues (voir `docs/GAPS.md`)

Barrières conservatrices ; une seule file ; états de dispositions par texture entière (pas par niveau) ; couleurs de bord des
échantillonneurs : noir transparent, noir opaque ou blanc opaque ; D24S8 remplacé par D32S8 quand le pilote n'a pas D24S8 ;
écritures de stockage dans une passe de rendu non prises en charge ; une ressource détruite entre l'enregistrement et le `submit`
d'un encodeur qui l'utilise n'est pas protégée ; pas de cache mémoire « même description -> même pipeline » ; `drawIndexedIndirect`
refuse un décalage d'indices non nul (contrat d'OpenGL).
