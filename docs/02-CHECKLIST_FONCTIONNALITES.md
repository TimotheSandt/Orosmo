# 02 · Checklist des fonctionnalités

C'est la liste de référence. L'IA ne la modifie pas sans accord, sauf pour remplir l'état et la preuve dans `docs/STATUS.md` (jamais en cochant de mémoire).

**Priorités** : P0 = indispensable aux premiers jalons utiles · P1 = nécessaire au moteur complet · P2 = souhaitable · P3 = plus tard (le design ne doit pas l'empêcher).

**États** : `Fait` (preuve fournie) · `Partiel` · `Absent` · `Incompatible` (existe mais viole l'architecture, à adapter) · `Archivé` (retiré du dépôt cœur et conservé sur une branche d'archive) · `?` (pas encore audité).

**Existant déclaré par l'auteur** (à vérifier par l'audit, pas à croire) : logger, UIManager (avec un début d'interface écrit dans un **langage maison précompilé en C++**), InputManager, éclairage UBO/SSBO, TextRenderer FreeType, Makefile multiplateforme + vcpkg. Le rendu actuel est en OpenGL / GLFW / GLSL.

**Choix d'architecture déjà validés** (voir `03`) : éclairage forward+ clusterisé ; ombres en cascade avec cache ; rendu piloté par GPU (instancing, dessin indirect, culling GPU) ; assets glTF 2.0 + KTX2 + meshoptimizer ; animation par skinning GPU, machine à états et foules instanciées ; SMAA puis TAA ; tableaux de textures comme base, bindless en option ; voxel et ray tracing prévus par points d'accroche ; reverse-Z ; MRT et sorties auxiliaires à la demande ; 2D intégré au renderer ; logique de jeu en bibliothèque dynamique rechargeable ; thèmes d'interface en JSON ; rechargement à chaud de l'interface.

## A. Noyau (`core`)

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| CORE-01 | Logger (pile d'appels, dédoublonnage, couleurs, `<stacktrace>`) | P0 | Existe déjà. Niveaux, sortie fichier, thread-safe, testé. | Fait — preuve : `docs/STATUS.md`, « M1 » |
| CORE-02 | Gestion d'erreurs | P0 | `std::expected` pour les erreurs attendues ; assertions avec message ; aucune exception dans le flux normal. | Partiel |
| CORE-03 | Allocateurs | P1 | Arène par image et pool d'objets ; test de non-fuite ; utilisation documentée. | Fait — preuve : `docs/STATUS.md`, « M1 » |
| CORE-04 | Système de tâches | P0 | Pool de threads, `submit`, dépendances, attente d'un groupe, arrêt propre. Test de charge. | Fait — preuve : `docs/STATUS.md`, « M1 » |
| CORE-05 | Profilage | P1 | Marqueurs CPU, minuteries GPU (via RHI), export vers un outil (Tracy ou équivalent) ou fichier trace. | Partiel |
| CORE-06 | Configuration | P1 | Réglages et arguments de ligne de commande ; lecture/écriture JSON ; valeurs par défaut documentées. | Fait — preuve : `docs/STATUS.md`, « M1 » |
| CORE-07 | Temps | P0 | Horloge, pas fixe pour la simulation, delta variable pour le rendu, test déterministe. | Fait — preuve : `docs/STATUS.md`, « M1 » |
| CORE-08 | Événements | P1 | Bus d'événements typé, sans allocation par événement dans le cas courant. | Fait — preuve : `docs/STATUS.md`, « M1 » |
| CORE-09 | Mathématiques | P0 | Vecteurs, matrices, quaternions en `float` ; **`double` pour les positions mondiales** ; conversions explicites. | Fait — preuve : `docs/STATUS.md`, « M1 » |
| CORE-10 | Gestionnaire de ressources | P0 | Handles avec génération, chargement asynchrone, comptage, rechargement à chaud. | Fait — preuve : `docs/STATUS.md`, « M1 » |
| CORE-11 | Système de fichiers virtuel | P1 | Chemins logiques (`res://`, `user://`), indépendants de l'OS. | Fait — preuve : `docs/STATUS.md`, « M1 » |
| CORE-12 | Sérialisation et réflexion | P1 | JSON lisible et binaire compact ; versionnage ; **réflexion des champs** (nom, type, plage) utilisée par l'interface automatique et la migration d'état. | Fait — preuve : `docs/STATUS.md`, « M1 » |
| CORE-13 | Observateur de fichiers | P1 | Notifie les changements (shaders, thèmes, interface, paramètres, bibliothèque de jeu) sur les trois systèmes. | Partiel |

## B. Plateforme et entrées (`platform`)

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| PLAT-01 | Fenêtre | P0 | Abstraction au-dessus de GLFW : création, redimensionnement, plein écran, DPI élevé, surface pour le RHI. | Partiel (GLFW caché, DPI lisible ; ×2 non observé) |
| PLAT-02 | InputManager | P0 | Existe déjà. Dispositions QWERTY/AZERTY/DVORAK, remappage, état par image. | Partiel (molette faite) |
| PLAT-03 | Manettes | P2 | Axes, boutons, hot-plug. | Absent |
| PLAT-04 | Mode sans fenêtre | P0 | Démarrage sans fenêtre (génération et export en ligne de commande) sur les trois systèmes. | Partiel (`--headless` sous Linux) |
| PLAT-05 | Saisie de texte | P1 | Texte Unicode, composition IME, presse-papiers. | Partiel (texte Unicode, presse-papiers du système en mode fenêtré par `ui::useSystemClipboard` ; pas de composition IME : G107) |

## C. Couche graphique (`rhi`) : voir `03-ARCHITECTURE_ET_PORTABILITE.md`

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| RHI-01 | Interface RHI | P0 | Interface décrite en `03` ; aucun type d'API (`GLuint`, `VkBuffer`…) dans les en-têtes publics. | Partiel |
| RHI-02 | Backend OpenGL 4.3+ | P0 | Reprend le rendu existant derrière le RHI ; images de référence identiques à l'ancien rendu (tolérance définie). | Fait (OpenGL) — preuve : `docs/STATUS.md`, « M2 B6 » |
| RHI-03 | Backend Vulkan | P0 | Parité de fonctionnalités avec OpenGL ; couches de validation silencieuses ; mémoire via VMA. | Fait (Linux, un GPU ; Windows et macOS non vérifiés, G142) — preuve : `src/rhi/vulkan/` (volk, VMA, Vulkan 1.3), `tests/rhi/vulkan_device_tests.cpp`, `tests/rhi/vulkan_backend_tests.cpp`, `tests/rhi/rhi_device_tests.cpp` (`FOR_EACH_BACKEND`, `validationErrorCount() == 0`) : 125 cas GPU réussis en Vulkan ; `make demo-vulkan-check` OK. Limites : G129 à G134 |
| RHI-04 | Choix du backend à l'exécution | P0 | `--backend=vulkan|opengl|metal`, repli documenté, `Capabilities` interrogeable. | Partiel — fait : `--backend` / `--api`, `--strict-backend`, repli sur OpenGL (`resolveBackend`, `tests/demo/launch_options_tests.cpp`), `Capabilities`. **Manque** : Metal (reconnu mais refusé, M9) |
| RHI-05 | Buffers, textures (2D, tableau, cube, 3D, mips), échantillonneurs | P0 | Création, mise à jour, copie, génération de mips ; formats portables listés. | Partiel |
| RHI-06 | Pipelines graphiques et de calcul | P0 | État explicite ; cache de pipelines en mémoire **et sur disque**. | Fait — preuve : `PipelineCache` en mémoire (`tests/renderer/resources_tests.cpp`), cache Vulkan sur disque (`tests/rhi/vulkan_backend_tests.cpp`, « the pipeline cache is written at shutdown and read back… » : écrit puis relu, fichier étranger ignoré), calcul (`tests/rhi/rhi_device_tests.cpp`, « compute writes a storage buffer… ») |
| RHI-07 | Groupes de liaison | P0 | Modèle de liaison à trois groupes (voir `03`) traduit par chaque backend. | Partiel |
| RHI-08 | Passes de rendu et swapchain | P0 | Passes décrites par attachements ; redimensionnement sans fuite ; vsync réglable. | Partiel |
| RHI-09 | Compute | P0 | Dispatch direct, tampons de stockage, barrières gérées par le RHI ; taille de groupe lue dans `Capabilities`. | Fait (OpenGL) — preuve : `docs/STATUS.md`, « M2 lot A » |
| RHI-10 | Dessin instancié et indirect | P0 | `drawIndexed` instancié ; `drawIndirect` avec compteur (multi-draw indirect). | Partiel |
| RHI-11 | Minuteries GPU et noms de debug | P1 | Requêtes de temps par passe ; noms visibles dans RenderDoc. | Partiel |
| RHI-12 | Pipeline de shaders | P0 | GLSL → SPIR-V hors ligne → SPIRV-Cross (MSL, GLSL) ; réflexion des liaisons ; inclusions ; rechargement à chaud. | Partiel |
| RHI-13 | Backend Metal | P2 | Via MoltenVK validé d'abord ; backend natif seulement si les mesures le justifient. | Absent |
| RHI-14 | Tests de rendu | P0 | Images de référence par backend, comparaison automatisée, rapport de différence. | Partiel — fait : comparaison automatisée avec rapport et image de différence (`tools/demo_parity.py --diff-out`, `make demo-vulkan-check`, 15 captures OpenGL/Vulkan). **Manque** : une référence par backend (une seule référence OpenGL sert aux deux), une seule scène, hors `demo-test` et CI (G143) |
| RHI-15 | Rendu vers plusieurs cibles (MRT) | P0 | Jusqu'à 8 attachements couleur (lus dans `Capabilities`) + profondeur, format et opérations par cible ; sorties distinctes dans un même shader. | Fait (OpenGL) — preuve : `docs/STATUS.md`, « M2 B4 » |
| RHI-16 | Textures 3D et images de stockage | P0 | Lecture/écriture en compute ; formats flottants 16 et 32 bits, `RG16F`, `RGBA16F`, `R32F`, `R32UI` garantis. | Partiel |
| RHI-17 | Dispatch indirect et groupes dynamiques | P1 | `dispatchIndirect` ; offsets dynamiques des tampons. | Fait (OpenGL) — preuve : `docs/STATUS.md`, « M2 lot A » |
| RHI-18 | Capacités optionnelles réservées | P1 | `Capabilities` expose `rayQuery`, `accelerationStructure`, `bindless`, `meshShader`, `bufferDeviceAddress`, `sparseResources` (toutes à `false` tant que non implémentées) ; le moteur ne suppose jamais leur présence. | Fait — preuve : `docs/STATUS.md`, « M2 B6 » |
| RHI-19 | Transferts asynchrones | P2 | File de transfert séparée quand disponible ; chargement sans saccade. | Absent |

## D. Rendu 3D (`renderer`)

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| REN-01 | Graphe de passes et plugins de rendu | P1 | Passes déclaratives avec dépendances, ressources transitoires, ordre calculé ; **plugins de rendu** (maillages, terrain, 2D, voxels futurs, ray tracing futur) qui ajoutent leurs passes sans modifier le cœur. | Fait — preuve : `include/orosmo/renderer/render_graph.hpp` `RenderGraph`, `render_feature.hpp` `RenderFeature` ; `tests/renderer/render_graph_tests.cpp` (ordre déduit des accès, passe inutile écartée, cycle refusé, transitoires réutilisés, `read` / `readFinal`, durée GPU par passe), `SceneRenderer (REN-01, REN-03)` (un plugin ajoute des passes sans toucher au cœur). Limites : G151, G152. |
| REN-02 | Caméra | P0 | Perspective et orthographique ; **reverse-Z avec plan lointain infini** ; rendu relatif à la caméra ; décalage (jitter) prévu pour le TAA. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| REN-03 | Données par image (groupe 0) | P0 | Bloc de constantes décrit en `03` §10 : matrices et inverses, image précédente, caméra, temps, résolution, graine de bruit, lumière principale, paramètres d'atmosphère. | Fait — preuve : `include/orosmo/renderer/frame_data.hpp` `FrameData` (640 octets, 19 `offsetof` figés par `static_assert`), `shaders/rhi/frame_data.glsl` ; `tests/renderer/render_graph_tests.cpp` « FrameData (REN-03): the C++ block and frame_data.glsl share one std140 layout, read by a shader through the graph ». |
| REN-04 | Éclairage forward+ clusterisé | P0 | Pré-passe de profondeur ; clusters remplis en compute ; nombre de lumières limité par un budget réglable. | Fait — preuve : `include/orosmo/renderer/forward_plus.hpp` `ForwardPlusLighting` (pré-passe de profondeur `SceneRenderer::setDepthPrepass`, clusters remplis par `shaders/rhi/light_clusters.comp`, budgets `maxLights` et `maxLightsPerCluster`) ; `tests/renderer/forward_plus_tests.cpp` (« 1000 point lights: forward+ matches forward… », « clusters filled in compute… budgets »). Écart connu : un pixel entre forward et forward+ sur la démo (G161). Limites : G153. |
| REN-05 | Types de lumières | P0 | Existe (UBO/SSBO std140/std430) ; directionnelle, ponctuelle, spot migrées sur le RHI. Lumières de surface en P2. | Partiel — fait : ambiante, directionnelle, ponctuelles et projecteurs rendues (`shaders/include/orosmo/lights.glsl`, `mesh.frag`), `LightManager` std430 vérifié ; `tests/renderer/forward_plus_tests.cpp` (« point and spot lights are rendered in forward (REN-05) », « lights.glsl and the std430 light block read on the GPU match the CPU references »). **Manque** : lumières de surface (P2) ; un projecteur est testé comme une sphère dans les clusters (G166). |
| REN-06 | Mobilité et mode d'ombre des lumières | P1 | `Static`, `Stationary`, `Dynamic` ; ombres `None`, `Baked`, `Dynamic`, `Cached` ; comportement documenté et testé pour chaque combinaison. | Fait — preuve : `include/orosmo/renderer/shadows.hpp` `resolveShadowBehavior`, `shadow_feature.hpp` `ShadowFeature` ; `tests/renderer/shadow_tests.cpp` (« resolveShadowBehavior: documented behavior of every (mobility, shadow mode) combination (REN-06) », « ShadowFeature: every (mobility, mode) combination on a static scene; mobility violations »). Limites : G156. |
| REN-07 | Ombres en cascade | P0 | Lumière directionnelle, PCF, stabilité sans scintillement. | Fait — preuve : `ShadowFeature` (cascades de la directionnelle, PCF dans `shaders/rhi/shadow_receiver.glsl`, ajustement au texel) ; `tests/renderer/shadow_tests.cpp` (« cascadeSplits and cameraShapeFrom », « fitCascade… translation moves it by whole texels », « cascaded shadow under a caster, lit elsewhere, same image in camera-relative rendering ») ; démo `--shadows on`. Limites : G156. |
| REN-08 | Atlas d'ombres et cache | P1 | Budget par lumière, priorité, mise à jour amortie, **cache** qui ne redessine que ce qui bouge, ombres des spots, fondu à distance, ombres de contact. | Partiel — fait : atlas d'ombres à tuiles (`ShadowAtlasAllocator`), budget de mises à jour, priorité, cascades lointaines amorties, cache de la géométrie fixe (0 redessin quand rien ne bouge), ombres des projecteurs, fondu à distance ; `tests/renderer/shadow_tests.cpp` (« ShadowFeature cache: nothing redrawn when nothing moves… », « update budget, amortized cascades, atlas priority », « Spot shadow in mesh.frag, forward and forward+ »). **Manque** : ombres de contact ; fondu entre cascades ; le cache d'une cascade est redessiné dès que la caméra bouge d'un texel (G156). |
| REN-09 | Ombres avancées | P2 | Lumières ponctuelles (cubemap), pénombre variable (PCSS), ombres d'horizon pour le terrain. | Absent |
| REN-10 | Matériaux PBR | P0 | Métal/rugosité (glTF), albedo, normale, métal, AO, émission ; modes alpha importés de glTF (`OPAQUE`, `MASK`, `BLEND`) traduits vers `BlendMode` (REN-37) ; double face. | Absent |
| REN-11 | Variantes et instances de matériaux | P1 | Permutations par spécialisation de constantes, cache ; instances qui surchargent des paramètres. | Absent |
| REN-12 | Extensions de matériaux | P2 | Vernis, tissu, transmission, parallaxe. | Absent |
| REN-13 | IBL et sondes de lumière | P1 | Environnement HDR, irradiance en harmoniques sphériques, spéculaire préfiltré, sondes pour objets mobiles. | Absent |
| REN-14 | Occlusion ambiante | P1 | AO écran (GTAO) ; réglable. | Absent (module préinstallé, M13) |
| REN-15 | Réflexions et GI | P2 | Réflexions écran (SSR) ; éclairage global par sondes. P3 : GI par voxels ou par rayons. | Absent |
| REN-16 | Éclairage précalculé | P2 | Lightmaps ou sondes cuites par un outil hors ligne ; la lumière `Baked` les consomme. | Absent |
| REN-17 | HDR et tonemapping | P0 | Cible HDR, exposition réglable, tonemapping au choix. | Fait — preuve : `SceneRenderer::setSceneColorFormat` (RGBA16F), `include/orosmo/renderer/tonemap.hpp` (`kClamp`, `kAces`, `kAgx`, `kReinhardExtended`), `TonemapFeature`, exposition réglable (`FrameEnvironment::exposure`, `exposureEv`) ; `tests/renderer/post_effects_tests.cpp` (`post/math` sans GPU, `renderer/post` sur chaque backend) ; démo `--post hdr`. Limites : G158. |
| REN-18 | Post-traitement courant | P1 | Bloom, exposition automatique (histogramme), étalonnage par LUT, vignettage ; chaque effet déclare ses entrées. | Fait — preuve : `include/orosmo/renderer/post_effects.hpp` `BloomFeature`, `AutoExposureFeature` (histogramme en compute), `ColorGradingFeature` (LUT 3D), `VignetteFeature`, chacun déclare ses entrées dans le graphe ; `tests/renderer/post_effects_tests.cpp` ; démo `--post bloom` et `--post full`, captures de référence `scene-opengl-post-*.png`. Limites : G158. |
| REN-19 | Anticrénélage | P1 | SMAA d'abord, puis TAA (vecteurs de mouvement, rejet de l'historique) ; MSAA en option ; résolution dynamique. | Partiel — fait : FXAA (`FxaaFeature`), TAA (`TaaFeature` : jitter de Halton, reprojection par vecteurs de mouvement, écrêtage YCoCg, rejet de l'historique), SMAA 1x en variante analytique (`SmaaFeature`) ; `tests/renderer/antialiasing_tests.cpp` ; démo `--aa none, fxaa, smaa ou taa`. **Manque** : SMAA sans diagonales, sans arrondi des coins, sans seuil prédiqué par la profondeur ; MSAA absent (le RHI n'a pas de nombre d'échantillons) ; résolution dynamique : seulement l'échelle de rendu, pas de contrôleur (G159). |
| REN-20 | Effets coûteux optionnels | P2 | Profondeur de champ, flou de mouvement, upscaling type FSR. | Absent |
| REN-21 | Sorties auxiliaires (AOV) | P1 | Normales (octaédrique), vecteurs de mouvement, rugosité/métal, identifiant d'objet, profondeur linéaire, émission ; **activées seulement si une passe les demande** ; règle de résolution MSAA documentée (identifiants non moyennés). | Partiel — fait : normales octaédriques, vecteurs de mouvement par objet, identifiant d'objet, profondeur linéaire, rugosité/métal, émission, albédo, activés seulement si une passe les demande (`SceneRenderer::declareAov`, `RenderFeature::requiredAovs`, `include/orosmo/renderer/aov.hpp`) ; règle de résolution MSAA documentée (`aovResolveRule`) ; `tests/renderer/aov_tests.cpp` (« with no AOV requested, the scene target has the color only »). **Manque** : les objets instanciés et GPU n'écrivent aucun AOV ; rugosité, métal et émission sont des valeurs provisoires du matériau par défaut jusqu'aux matériaux PBR (REN-10) ; règle MSAA non exercée (pas de MSAA) (G169). |
| REN-22 | Boîte à outils de shaders | P1 | Fichiers GLSL d'inclusion partagés : maths, bruit, coordonnées, reconstruction de position depuis la profondeur, PBR, atmosphère, échantillonnage d'ombres. | Partiel — fait : `shaders/include/orosmo/` (`math`, `noise`, `coords`, `depth`, `pbr`, `lights`, `shadow` + `shaders/rhi/shadow_receiver.glsl`), chaque fichier compilé en calcul et en fragment sur chaque backend, fonctions pures comparées à leurs références CPU (`tests/renderer/shader_toolbox_tests.cpp`). **Manque** : l'atmosphère citée par le critère (déplacée vers le module REN-24 par A39 ; critère à amender, voir A56 dans `docs/QUESTIONS.md`) (G168). |
| REN-23 | Textures de bruit en compute | P1 | Bruit 3D (Worley, Perlin) généré au démarrage ; réutilisable par terrain, nuages et brouillard. | Absent (module préinstallé, M13) |
| REN-24 | Ciel et atmosphère | P1 | Tables de transmittance, diffusion multiple et vue aérienne, mises à jour par compute ; valable du sol à l'espace ; cycle jour/nuit. | Absent (module préinstallé, M13) |
| REN-25 | Brouillard | P1 | Exponentiel et de hauteur. Brouillard volumétrique par froxels en P2, avec ombres. | Absent (module préinstallé, M13) |
| REN-26 | Nuages volumétriques | P2 | Carte de couverture, rendu à résolution réduite avec accumulation temporelle. | Absent (module préinstallé, M13) |
| REN-27 | Eau | P2 | Plan d'eau avec profondeur, réflexion, écume. | Absent (module préinstallé, M13) |
| REN-28 | Instancing | P0 | Données par instance dans un tampon ; un seul draw call par groupe (maillage, matériau). | Fait — preuve : `include/orosmo/renderer/instance_batch.hpp` `InstanceBatchBuilder`, `instance_renderer.hpp` `InstanceRenderer`, `object_set.hpp` `ObjectSet` ; `tests/renderer/instancing_tests.cpp` (« one draw call per (mesh, material) batch »), `visibility_tests.cpp` ; démo `--instances 10000` : 10 000 instances en 2 draw calls. |
| REN-29 | Rendu piloté par GPU | P1 | Scène GPU persistante (liste d'objets modifiés), multi-draw indirect, culling frustum puis occlusion Hi-Z en compute. | Fait — preuve : `include/orosmo/renderer/gpu_driven.hpp` `GpuDrivenFeature`, `gpu_scene.hpp` `GpuScene` (miroir persistant, seuls les objets modifiés sont renvoyés), `shaders/rhi/gpu_cull.comp` (frustum, distance, taille, puis Hi-Z de l'image précédente, puis LOD), `hiz_build.comp`, `drawIndexedIndirect` multi-draw ; `tests/renderer/gpu_driven_tests.cpp` (même ensemble visible et même image que le culling CPU, occlusion Hi-Z) sur OpenGL et Vulkan ; démo `--instances 100000 --gpu-driven on`. Limites : G154. |
| REN-30 | Culling CPU | P0 | Frustum sur BVH ou grille ; petits objets. | Fait — preuve : `include/orosmo/renderer/culling.hpp` (`Aabb`, `Frustum` reverse-Z, `Bvh`, rejet des petits objets) ; `tests/renderer/visibility_tests.cpp` (« Bvh: same visible set as the linear test, fewer tests… », « Small-object culling… ») ; BVH : 279 tests d'objets contre 10 000 en linéaire. |
| REN-31 | LOD et imposteurs | P1 | LOD par distance, imposteurs pour la végétation lointaine, transitions sans trou. | Partiel — fait : `include/orosmo/renderer/lod.hpp` `selectLod` (distance, biais), fondu tramé complémentaire sans trou ni double couverture ; `tests/renderer/visibility_tests.cpp` (« selectLod… », « LOD cross-fade… covers every pixel exactly once »). **Manque** : imposteurs automatiques pour la végétation lointaine (G167). |
| REN-32 | Tri et ordre des transparents | P1 | Objets mélangés triés de l'arrière vers l'avant sans écriture de profondeur ; additifs après les mélangés ; priorité de tri réglable par matériau ; ordre global fixe défini en `03` §11 bis. | Fait — preuve : `include/orosmo/renderer/transparent_feature.hpp` (`sortTransparents` : groupes, priorité, arrière vers avant, sans écriture de profondeur, additifs après) ; `tests/renderer/transparency_tests.cpp` (« blended objects are sorted back to front… », « opaque geometry of the scene hides transparents… ») ; démo `--scene mixed`. |
| REN-37 | Modes de fusion des matériaux | P0 | `BlendMode` par matériau : `Opaque`, `Mask` (seuil), `MaskA2C` (alpha-to-coverage), `Dithered`, `Blend`, `Additive`, `Premultiplied`, `Transmission` ; chaque mode a un test d'image de référence. | Partiel — fait : les 8 modes de `include/orosmo/renderer/blend_mode.hpp` `BlendMode` avec leur état de pipeline ; `tests/renderer/blend_mode_tests.cpp` (« each BlendMode renders its reference image »), image de référence par mode. **Manque** : `kMaskA2C` se comporte comme `kMask` (le RHI n'a pas de champ alpha-to-coverage ni de nombre d'échantillons) ; `kTransmission` = `kBlend` sans réfraction (REN-38, module M13) (G171). |
| REN-38 | Réfraction et transmission | P2 | Copie de la scène opaque (avec mips) avant la passe transparente ; échantillonnage décalé par normale et indice de réfraction ; limites connues documentées (pas de réfraction entre couches de verre). | Absent (module préinstallé, M13) |
| REN-39 | Ombres des transparents | P1 | Option par matériau : aucune, pleine, découpée (masque) ou tramée ; choix documenté et testé. | Fait — preuve : `include/orosmo/renderer/transparent_shadow.hpp`, `ShadowFeature::setCasterTransparency` (aucune, pleine, masquée, tramée) ; `tests/renderer/transparent_shadow_tests.cpp` (« shadow under a transparent caster: none = lit, full = dark, masked… dithered ~ opacity », invalidation du cache) ; démo `--scene mixed --shadows on --transparent-shadow dithered`. Les objets instanciés projettent une ombre pleine seulement. Limites : G157. |
| REN-40 | Transparence indépendante de l'ordre (OIT) | P2 | OIT pondéré (weighted blended) en option, avec passe de composition ; OIT à listes chaînées ou à moments en P3. | Absent |
| REN-41 | Transparents et sorties auxiliaires | P1 | Par défaut un transparent n'écrit ni normales, ni identifiant, ni vecteurs de mouvement ; chaque matériau peut les demander explicitement (par exemple pour la sélection à la souris ou le TAA). | Partiel — fait : un transparent n'écrit aucun AOV par défaut et peut en demander (`Mesh::setAovMaterial`, `AovMaterial::request`) ; la passe des transparents déclare tous les AOV actifs (compatible TAA) ; `tests/renderer/aov_tests.cpp` (« REN-41: a transparent material writes no AOV unless it asks for it »), `antialiasing_tests.cpp` (TAA avec transparents). **Manque** : les objets instanciés n'ont pas d'AOV (G169). |
| REN-33 | Particules et décalques | P1 | Particules GPU (compute) ; décalques en P2 ; particules douces. | Absent (module préinstallé, M13) |
| REN-34 | Dessin de debug | P0 | Lignes, boîtes, sphères, repères ; aucun coût hors debug. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| REN-35 | Modes d'affichage de debug | P1 | Normales, albédo, surdessin, couleurs de LOD, cascades, lumières par cluster, filaire, gel du frustum de culling. | Partiel — fait : modes `normals`, `albedo`, `overdraw`, `lod`, `wireframe`, `motion` (`include/orosmo/renderer/debug_view.hpp` `DebugView`), gel du frustum de culling (`FrustumFreeze`) ; `tests/renderer/aov_tests.cpp` (« REN-35: albedo, overdraw, wireframe and normals views render ») ; démo `--debug-view`, `--freeze-frustum`. **Manque** : modes cascades et clusters (absents de la démo ; clusters : seulement `ForwardPlusLighting::readClusterLightCounts`) ; les objets instanciés ignorent `albedo`, `overdraw` et `normals` (G170). |
| REN-36 | Rendu du texte | P0 | Existe (FreeType, ancrage, retour à la ligne, alignement) ; migré sur le RHI. | Partiel |

## E. Rendu 2D et mélange 2D/3D (`renderer2d`, plugin du renderer)

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| R2D-01 | Batch de sprites | P0 | Quads instanciés, atlas ou tableau de textures, tri par couche (`z`) puis matériau ; un draw call par lot. | Fait — preuve : `include/orosmo/renderer/renderer2d.hpp` `Renderer2D` (sprites instanciés, tri par `z` puis matériau), `canvas_renderer.hpp` (interface par lots) ; `tests/renderer/renderer2d_tests.cpp` (« R2D-01: sprites of one material are ONE draw call… »), `canvas_tests.cpp` ; interface : 143 → 18 draw calls. Un matériau = une texture (atlas, pas de tableau de textures). Limites : G160. |
| R2D-02 | Caméra 2D | P0 | Orthographique ; mode pixel parfait (filtrage au plus proche, échelle entière). | Fait — preuve : `include/orosmo/renderer/camera2d.hpp` `Camera2D` (orthographique, rotation, mode pixel parfait : échelle entière, filtre au plus proche) ; `tests/renderer/renderer2d_tests.cpp` (« R2D-02: pixel perfect… », « world to screen… »). |
| R2D-03 | Formes, lignes, texte | P1 | Rectangles, cercles, lignes, polygones ; texte via REN-36. | Fait — preuve : `Renderer2D` (rectangles, contours, lignes, cercles, anneaux, polygones simples convexes ou non) ; `tests/renderer/renderer2d_tests.cpp` (« R2D-03: … ») ; texte par le chemin existant (`DrawList`). Limites : G160. |
| R2D-04 | Cartes de tuiles | P1 | Grille, atlas, couches, chargement d'un format courant (JSON). | Absent |
| R2D-05 | Mélange 2D/3D | P1 | Trois modes : 2D dans le monde avec test de profondeur ; 2D en superposition après le post-traitement ; 2D vers texture affichée sur un objet 3D. Ordre fixe : 3D opaque → 3D transparent → 2D monde → post-traitement → 2D en superposition. | Fait — preuve : `include/orosmo/renderer/layer2d_feature.hpp` `Layer2DFeature` (modes monde avec test de profondeur, superposition, vers texture) ; `tests/renderer/mixed_2d3d_tests.cpp` (« 2D in the world is depth tested… between transparents and post-processing », « 2D rendered to a texture is shown on a 3D object ») ; démo `--scene mixed`. Limites : G160. |
| R2D-06 | Plusieurs vues | P1 | Plusieurs caméras et zones d'affichage ; vue 3D incrustée dans une interface. | Fait — preuve : `mixed_2d3d_tests.cpp` « several views place their own cameras in zones of the output; a view shows in the interface (R2D-06) » (OpenGL et Vulkan). Vue incrustée exercée dans les tests seulement, pas dans la démo. |
| R2D-07 | Éclairage 2D | P3 | Réservé. | Absent |

## F. Contenu et animation (`assets`, `anim`)

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| ASSET-01 | Import glTF 2.0 | P0 | Maillages, matériaux, nœuds, caméras, lumières (KHR_lights_punctual), squelettes, animations. | Absent |
| ASSET-02 | Textures | P0 | PNG/JPG avec génération de mips ; espace colorimétrique correct (sRGB/linéaire). | Partiel |
| ASSET-03 | Textures compressées | P1 | KTX2 (BC7 sur PC, ASTC sur Mac/ARM). | Absent |
| ASSET-04 | Outil de cuisson | P1 | Convertit les assets en format binaire du moteur : optimisation des sommets (meshoptimizer), quantification, génération de LOD. | Absent |
| ASSET-05 | Rechargement à chaud des assets | P1 | Modification d'un fichier → mise à jour en direct sans redémarrer. | Absent |
| ASSET-06 | OBJ | P2 | Import simple. FBX seulement par conversion externe. | Absent |
| ANIM-01 | Squelettes et skinning GPU | P1 | Jusqu'à N os par sommet, matrices dans un tampon, compute ou vertex. | Absent |
| ANIM-02 | Clips, mélange, machine à états | P1 | Lecture, boucle, fondu, couches additives, transitions, événements d'animation. | Absent |
| ANIM-03 | Animation de propriétés | P1 | Caméra, lumière, paramètres de matériau, transformations, par courbes. | Absent |
| ANIM-04 | Foules instanciées | P1 | Textures d'animation de sommets ou skinning instancié ; milliers d'agents animés ; données par instance. | Absent |
| ANIM-05 | IK, morph targets, root motion | P2 | Au moins IK à deux os, cibles de forme, mouvement de racine. | Absent |
| ANIM-06 | Retargeting | P3 | Réutilisation d'animations entre squelettes. | Absent |

## G. Terrain et monde (`world`)

> **Archivé (décision A37, 2026-11-21)** : le code procédural (`orosmo::world`, `TerrainRenderer`, `shaders/rhi/terrain.*`) est archivé intact dans la branche `archive/procedural-world` et retiré du dépôt principal. Ces fonctionnalités poursuivront leur vie dans un projet séparé ou comme module/addon optionnel (jalon **M11**).

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| TER-01 | Bruit | P0 | Perlin, Simplex, Worley, fBm, distorsion de domaine ; **CPU et GPU donnent le même résultat** (tolérance fixée) ; testé. | Archivé (`archive/procedural-world`, A37) — était Fait (M3) |
| TER-02 | Génération par tuiles | P0 | Hauteur par tuile à partir de (graine, coordonnées, paramètres) ; déterministe ; asynchrone. | Archivé (`archive/procedural-world`, A37) — était Fait (M3) |
| TER-03 | Maillage à niveaux de détail | P0 | Quadtree ou clipmap ; pas de fissures entre niveaux ; distance de vue réglable. | Archivé (`archive/procedural-world`, A37) — était Fait (M3) |
| TER-04 | Streaming | P0 | Chargement/déchargement par distance ; pas de saccade mesurable sur la démo ; budget mémoire réglable. | Archivé (`archive/procedural-world`, A37) — était Partiel (budget et génération asynchrone ; absence de saccade non mesurée) |
| TER-05 | Érosion | P1 | Hydraulique et thermique, en compute ; résultat reproductible. | Hors moteur (A39) |
| TER-06 | Biomes | P1 | Température, humidité, altitude, pente → biome ; table modifiable. | Hors moteur (A39) |
| TER-07 | Texturage du terrain | P0 | Mélange par pente et altitude, triplanaire ; atlas ou tableau de textures. | Archivé (`archive/procedural-world`, A37) — était Fait (OpenGL, M3) |
| TER-08 | Végétation et dispersion | P1 | Placement déterministe, instancié (REN-28) ; densité par biome. | Hors moteur (A39) |
| TER-09 | Planètes sphériques | P1 | Cube-sphère à quadtree ; précision `double` côté CPU ; rendu relatif à la caméra ; transition sol/espace avec l'atmosphère. | Hors moteur (A39) |
| TER-10 | Eau, rivières, lacs | P2 | Niveau de la mer, bassins, tracé de rivières. | Hors moteur (A39) |
| TER-11 | Export | P0 | Carte de hauteur (PNG 16 bits, EXR), maillage (OBJ, glTF, STL imprimable), carte de biomes. | Archivé (`archive/procedural-world`, A37) — était Fait (M3) |
| TER-12 | Interface de réglage | P0 | Paramètres modifiables en direct, graine, préréglages sauvegardables ; affichage automatique à partir de la réflexion (UI-12). | Archivé (`archive/procedural-world`, A37) — était Fait (M3) |
| TER-13 | Lancer de rayon sur le terrain | P1 | Picking souris et hauteur en un point, CPU, cohérent avec le rendu. | Archivé (`archive/procedural-world`, A37) — était Fait (M3) |
| TER-14 | Grottes et voxels | P3 | Réservé ; l'architecture ne doit pas l'exclure. | Archivé (`archive/procedural-world`, A37) — hors cœur (projet séparé / M11) |

## H. Scène et simulation (`scene`, `sim`)

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| ECS-01 | Entités et composants | P1 | ECS maison ou EnTT (choix à justifier dans `docs/QUESTIONS.md`) ; tableaux contigus ; composants = données simples, **réfléchies** (CORE-12). | Absent |
| ECS-02 | Transformations hiérarchiques | P1 | Parents/enfants, grandes coordonnées en `double`. | Absent |
| ECS-03 | Chargement et sauvegarde de scène | P1 | Via CORE-12, versionné. | Absent |
| SIM-01 | Boucle de simulation à pas fixe | P1 | Découplée du rendu ; rejouable avec la même graine ; capture et restauration d'état ; points d'accroche pour systèmes. | Absent |
| SIM-02 | Accès aux données du monde | P2 | API de lecture (hauteur, biome, eau) utilisable sans rendu par la simulation. | Hors moteur (A39) |
| SIM-03 | Agents et écosystème | P3 | Hors moteur. Seuls les points d'accroche de SIM-01 et SIM-02 sont attendus. | Hors moteur (A39) |

## I. Interface (`ui`) et langage d'interface

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| UI-01 | UIManager | P0 | Existe (UIComponent, UIContainer, VBox, `DeferredValue<T>` à double tampon, mixin CRTP). Migré sur le RHI ; comportement identique. | Partiel |
| UI-02 | Rendu indépendant du backend | P0 | L'interface produit des listes de dessin (rectangles, texte, images, découpage, ordre `z`) ; un adaptateur les envoie au batch 2D (R2D-01). | Partiel |
| UI-03 | Widgets de base | P0 | Bouton, curseur, case à cocher, liste déroulante, champ de texte, sélecteur de couleur, graphique simple, onglets, défilement, infobulles. | Fait — preuve : `docs/STATUS.md`, « M3 » ; M5 : un clic sur le carré d'un `ColorPicker` ouvre la roue `ColorEditor` en fenêtre surgissante (`tests/ui/widgets_tests.cpp`, « ColorPicker: clicking the swatch opens a ColorEditor popup bound to the same color; sliders do not ») |
| UI-04 | Outils de debug | P1 | Dear ImGui (ou équivalent) sur le RHI, réservé aux outils : profileur, console. | Absent |
| UI-05 | Langage d'interface : compilateur vers C++ | P0 | Description déclarative de l'arbre, compilée en C++ sans coût d'exécution ; événements (`onClick`, `onChange`, `onHover`, `onFocus`) liés à des fonctions externes, **aucune logique métier dans le langage** ; liaison de données typée dans les deux sens ; C++ embarqué possible ; erreurs avec **fichier, ligne et colonne du source du langage** (diagnostics de `cui` ; une erreur du compilateur C++ dans le code généré est rapportée au fichier et à la ligne du `.cui` par `#line`, sans la colonne) ; intégration au build avec dépendances correctes. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| UI-06 | Mise en page complète | P0 | `VBox`, `HBox`, grille, superposition, marges, alignement, tailles fixes, flexibles et minimales. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| UI-07 | Thèmes en JSON | P0 | Fichier de données avec **jetons** (`color.primary`, `radius.medium`, polices, tailles, marges, ombres, bordures) et états (normal, survol, appuyé, désactivé, focus) ; les widgets lisent le thème, jamais de valeur en dur ; changement à chaud ; variantes clair/sombre ; surcharge locale possible dans le langage. | Partiel — preuve : `docs/STATUS.md`, « M3 » (ajouts) ; reste des tailles en dur dans les widgets (G111) |
| UI-08 | Rechargement à chaud de l'interface | P1 | Mode développement : le même arbre est interprété ou chargé en bibliothèque dynamique et se met à jour à l'enregistrement ; mode final : C++ généré lié directement. | Absent |
| UI-09 | Navigation et accessibilité de base | P1 | Clavier et manette, focus visible, ordre de tabulation. | Absent |
| UI-10 | Inspecteur d'interface | P1 | Arbre des composants et tailles calculées affichés dans les outils de debug. | Absent |
| UI-11 | Spécification et tests du langage | P0 | `docs/UI_LANGUAGE.md` : grammaire, exemples, limites ; tests de compilation avec entrées valides et invalides et messages d'erreur attendus. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| UI-12 | Paramètres réfléchis → curseurs | P1 | Une structure de paramètres réfléchie (CORE-12) est affichée automatiquement en widgets adaptés au type et à la plage. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| UI-13 | Rendu de l'interface économe | P1 | Élément caché : aucune commande ni aucun dessin ; élément hors de sa zone de découpage : éliminé à la production de la liste ; conteneur inchangé : ni remis en page ni redessiné, son image en cache est recomposée ; gain mesuré (frame CPU, nombre de dessins). | Partiel — fait : un élément caché, un onglet inactif ou une section repliée ne produisent aucune commande ; un élément hors de sa zone de découpage ou de la fenêtre est éliminé à la production de la liste (`tests/ui/lean_render_tests.cpp`, 4 cas) ; interface 143 → 18 draw calls grâce au batch 2D. **Manque** : cache par couche (conteneur inchangé ni remis en page ni redessiné), reporté à M6 sur proposition (A54 dans `docs/QUESTIONS.md`, G165). |

## J. Logique de jeu rechargeable (`game`)

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| GAME-01 | Module de jeu rechargeable | P1 | Bibliothèque dynamique avec interface C stable (`init`, `update`, `onReload`, `shutdown`) ; chargée, déchargée et rechargée à l'enregistrement. | Absent |
| GAME-02 | État côté moteur | P1 | Aucun état persistant dans la bibliothèque ; les systèmes s'enregistrent par nom à `init` et se désenregistrent au déchargement. | Absent |
| GAME-03 | Migration d'état | P1 | Ajout d'un champ à un composant : migration par réflexion (champs communs copiés, nouveaux à la valeur par défaut) ; en cas d'échec : sauvegarde, rechargement, restauration. | Absent |
| GAME-04 | Registre des gestionnaires | P1 | Les gestionnaires d'événements de l'interface sont retrouvés **par nom** au moment de l'appel en mode développement. | Absent |
| GAME-05 | Paramètres de gameplay en JSON | P1 | Rechargés instantanément, sans compilation. | Absent |
| GAME-06 | Construction incrémentale | P1 | Compilation d'un fichier en quelques secondes (ccache, en-têtes légers) ; sous Windows, la bibliothèque est copiée sous un nom unique avant chargement. | Absent |
| GAME-07 | Version finale | P1 | Logique liée statiquement, sans mécanisme de rechargement. | Absent |

## K. Voxels et ray tracing (extensions, points d'accroche)

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| EXT-01 | Plugin de rendu voxel | P2 | Voxels en maillage par chunks (maillage glouton) comme plugin de REN-01, sans modification du cœur. | Absent |
| EXT-02 | Tracé de rayons logiciel sur voxels | P2 | Voxels creux, DDA en compute ; fonctionne sans matériel de ray tracing. | Absent |
| EXT-03 | Interface d'accélération de rayons | P2 | `AccelerationStructure` (BLAS/TLAS) et requêtes de rayon dans le RHI, derrière `Capabilities::rayQuery` ; non implémenté dans OpenGL. | Incompatible |
| EXT-04 | Ray tracing matériel | P3 | Ombres, AO, réflexions quand disponible ; repli sur le chemin sans matériel. | Absent |
| EXT-05 | Traceur de chemin de référence | P3 | Pour calculer l'éclairage précalculé et comparer le rendu. | Absent |

## L. Outils, qualité et construction

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| TOOL-01 | Visualiseur de terrain | P0 | Application de démo : caméra libre, réglages, export. Démo principale. | Archivé (`archive/procedural-world`, A37) — était Partiel (remplacé par la démo unifiée `apps/demo/`) |
| TOOL-02 | Générateur en ligne de commande | P0 | `terrain-gen --seed N --size S --out fichier` sans fenêtre ; sorties de TER-11 ; code de retour fiable. | Archivé (`archive/procedural-world`, A37) — était Fait (M3) |
| TOOL-03 | Capture d'écran | P0 | Image depuis le rendu ; utilisée par RHI-14. (Priorité passée de P2 à P0 par l'auteur le 2026-11-17 : comparer les images dès le début.) | Partiel |
| BLD-01 | Construction multiplateforme | P0 | Un seul chemin de build documenté pour Windows, Linux, macOS ; vcpkg ; types debug/dev/release. L'IA peut **proposer** CMake (avec presets) en gardant le Makefile en enveloppe, sans migrer sans accord. | Partiel |
| REPO-01 | Dépôt propre et structuré | P0 | M00 de `docs/06` : `.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`, `README.md` racine, `third_party/` documenté, aucun fichier généré ou binaire inutile suivi, historique conservé, comportement identique à la référence. | Fait (confirmé par l'auteur le 2026-11-17) — preuve : `docs/STATUS.md`, « M0 » |
| GIT-01 | Flux de branches | P0 | `main` réservé à l'auteur, `dev` d'intégration, une branche par fonctionnalité, commits au format `type(ID): résumé`, fin de jalon par merge request de l'auteur (détail en `docs/06`). | Partiel |
| BLD-02 | Intégration continue | P0 | Matrice Windows/Linux/macOS : build, tests, vérification des couches. | Partiel |
| TEST-01 | Tests unitaires | P0 | Framework unique (Catch2 ou doctest) ; exécutés en CI ; couverture de `core` et `world`. | Partiel |
| TEST-02 | Assainisseurs et analyse | P1 | ASan, UBSan, clang-tidy, formatage automatique en CI. | Partiel |
| TEST-03 | Tests de déterminisme | P0 | Même graine → mêmes octets sur les trois systèmes (somme de contrôle de référence). | Partiel |
| DOC-01 | Documentation | P0 | `README.md` par module ; `docs/STATUS.md` à jour ; schéma des modules ; guide « ajouter un backend ». | Partiel |

## M. Modules optionnels, addons et registre (M11 et M12, décisions A34 et A38)

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| MOD-01 | Contrat et manifeste de module | P1 | Format déclaratif (`module.json`) : nom, compatibilité ABI moteur, en-têtes, archives `.a`, shaders, ressources, dépendances modules/vcpkg, points d'accroche. | Absent |
| MOD-02 | Distribution intégrée ou en addon | P1 | Un module peut être inclus directement dans une release (`bundled`) ou empaqueté séparément (`orosmo-addon-<nom>-<plateforme>.zip`) contre une release cœur. | Absent |
| MOD-03 | Intégration CLI et build (`orosmo` + `engine.mk`) | P1 | Commandes `orosmo addon list/add/remove/update` ; découverte et liaison automatiques par `engine.mk` (includes, archives, shaders, ressources, vcpkg). | Absent |
| MOD-04 | Module pilote vérifié de bout en bout | P1 | CI de bout en bout : projet `orosmo init` 100 % générique par défaut, puis ajout d'un module pilote via `orosmo addon add` et compilation sans toucher au cœur. | Absent |
| MOD-05 | Registre d'addons | P2 | Hébergement des paquets et d'un index (versions, compatibilité moteur, SHA-256, signature) ; le moteur refuse une archive dont l'empreinte ou la signature ne correspond pas à l'index. | Absent |
| MOD-06 | Publication d'un addon | P2 | `orosmo addon publish` ou workflow CI : valide le manifeste, construit par plateforme, dépose dans le registre ; SemVer ; retrait d'une version. | Absent |
| MOD-07 | Installation depuis le registre | P2 | `orosmo addon search/info/add <nom>[@version]` : versions compatibles, dépendances entre addons, empreintes vérifiées, `orosmo.lock`. | Absent |
| MOD-08 | Interface du gestionnaire d'addons | P2 | Application `orosmo::ui` : liste et détails du registre, état d'un projet, installer / mettre à jour / retirer, registres multiples ; mêmes fonctions que la CLI. | Absent |
| MOD-09 | Modèles de démarrage multiples et projet par défaut utilisateur | P1 | `orosmo init --template <minimal\|starter\|demo\|<nom>>` permet de choisir le modèle de départ (dont la démo complète pour découvrir le moteur), et l'utilisateur peut enregistrer un de ses propres projets comme modèle par défaut (`orosmo template set-default`). | Absent |

## N. Site vitrine, wiki et déploiement web (M14, décision A43)

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| WEB-01 | Wiki utilisateur et architecture (Astro) | P1 | Wiki construit avec Astro couvrant l'utilisation du moteur (SDK, build, `.cui`, exemples) et son fonctionnement interne (architecture, RHI, renderer, UI), mis à jour à chaque jalon. | Fait — preuve : `docs/STATUS.md`, « M14 » (`site/`, Astro 7 + Starlight : 6 pages « Utiliser le moteur », 6 pages « Architecture interne » ; `npm ci && npm run build && npm run check-links` : 16 pages, 470 liens internes, 0 cassé) ; CI GitHub pas encore observée (G203) |
| WEB-02 | Site vitrine du moteur | P1 | Site de présentation d'Orosmo (fonctionnalités, visuels/démos, téléchargement des releases, accès au wiki), publié après M6 ; **page « Histoire et méthode » sur l'origine du projet** : commencé par l'auteur avec OpenGL, IA utilisée mal au début, sans plan ni structure (projet mis en pause puis arrêté), repris récemment avec Claude, utilisé correctement (transparence, A43). | Partiel — contenu fait (`site/src/content/docs/index.mdx`, `histoire.md` liée depuis l'accueil et le pied de page, `etat.md`) ; **manque** : publication après M6 (G203, G204) et mise à jour du contenu de M6 (G206) |
| WEB-03 | Image Docker et registre en CI | P1 | `Dockerfile` reproductible ; la CI construit et publie l'image sur un registre pour un déploiement manuel ou automatique sur serveur (infra/coûts à trancher au début de M14). | Partiel — `site/Dockerfile` (node 24.20.0 alpine → nginx 1.30.4 alpine) construit et servi en local (`curl` : 200 sur l'accueil et le wiki, 404 sinon, santé `healthy`) ; registre GHCR (A74) ; **manque** : première publication sur le registre (G203) |
| WEB-04 | Publication CI sur tag `main` | P1 | Workflow CI déclenché sur tag `main` publiant la nouvelle version du site et du wiki ; procédure de mise à jour par jalon documentée. | Partiel — `.github/workflows/site.yml` (PR : build + image sans publication ; tag `v*` contenu dans `main` : publication GHCR ; actionlint : 0 erreur) ; procédure par jalon dans `docs/DEPLOIEMENT_SITE.md` ; **manque** : workflow jamais exécuté sur GitHub, déploiement serveur manuel (G203, G204) |
| WEB-05 | Staging automatique sur PR *(optionnel)* | P2 | Déploiement de prévisualisation sur un sous-domaine de test à chaque PR pour validation visuelle (à trancher au début de M14 selon coûts et spécifications). | Absent — optionnel, non retenu pour l'instant (A74 point 4, coût d'un hébergement de prévisualisation ; G205) |
