# 05 · Plan par jalons

L'ordre est choisi pour que le générateur de terrain existe tôt, sans reconstruire quand le moteur change de backend. Chaque jalon finit par une **démo** et une **définition de terminé** vérifiable. L'IA ne passe au suivant qu'après validation.

## M00 · Nettoyage du projet et du dépôt (tout premier travail)

Détail complet dans `docs/06-WORKFLOW_GIT_ET_NETTOYAGE.md` §6. Résumé : sauvegarde (tag, branche, bundle), référence de comportement dans `docs/BASELINE/`, inventaire en lecture seule, `docs/CLEANUP_PLAN.md` soumis à l'auteur, puis exécution sur une branche `chore/cleanup-*` : `.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`, `README.md` racine, `third_party/` documenté, déplacements propres avec `git mv`, build mis à jour à chaque étape. L'historique actuel est **conservé**.

**Démo** : le projet compile et se comporte comme avant le nettoyage, à partir d'un clone propre.
**Terminé** : fin de M00 de `docs/06` ; branche `dev` créée ; l'auteur fusionne vers `main` s'il est satisfait.

## M0 · Audit et base solide

- M00 est terminé et l'audit de `docs/04` est terminé et validé avant de commencer (l'audit se fait **après** M00, sur le dépôt rangé).
- **Portage Windows → Linux** : le projet a été commencé sous Windows et le développement continue sous Linux. Corriger la casse des `#include`, les fins de ligne (`.gitattributes`), les chemins, les dépendances et les options de lien ; documenter la commande de build Linux exacte dans `CLAUDE.md`.
- Build reproductible sur les trois systèmes ; CI qui compile et lance un test vide.
- Framework de tests (TEST-01 : **framework et exécution en CI** ; la couverture de `core` est suivie en M1 et celle de `world` en M3, sous le même identifiant, `Partiel` jusque-là), formatage, avertissements en erreur, ASan/UBSan.
- Scripts de vérification des couches et d'isolation des API (§8 de `docs/03`), en mode « avertissement » tant que le code n'est pas migré.
- Images et sorties de référence de l'application actuelle, **avec la capture d'écran (TOOL-03, P0)** et un outil de comparaison d'images.

**Démo** : l'application actuelle tourne via la CI, avec ses captures de référence.
**Terminé** : CI verte sur Windows et Linux (macOS si possible, sinon noté) ; `docs/STATUS.md` créé.

## M1 · Noyau

- CORE-02 à CORE-13 selon l'audit : erreurs, tâches, temps, maths avec `double`, ressources à handles, VFS, sérialisation **avec réflexion**, configuration, observateur de fichiers, profilage de base.
- Le logger existant est conservé et testé.
- Aucun appel graphique ajouté.

**Démo** : outil qui lance 10 000 tâches, recharge un fichier de configuration modifié sans redémarrer et affiche des mesures.
**Terminé** : tests unitaires verts, sanitizers propres, README par module ; couverture de tests de `core` (TEST-01, première moitié).

## M1 bis · Remise en ordre des couches (préparation de M2)

Décidé par l'auteur le 2026-11-17 : ce qui doit être nettoyé **avant** M2 mais qui n'appartient pas au noyau est fait ici, juste après M1, pour que M2 démarre sur des couches propres. Aucune fonctionnalité nouvelle.

- **Couches** : amener `tools/baseline/layers.txt` à **zéro** violation (`python3 tools/check_layers.py --strict`). Aujourd'hui : `Profiler` (core) inclut `Graphics/Core/GraphicsRuntime.h` ; `Window.cpp` (platform) inclut des en-têtes rhi (`GraphicsDiagnostics`, `RenderState`, `GraphicsRuntime`, `WindowContext`). Inverser ces dépendances par des interfaces (le `Profiler` reçoit un fournisseur de minuteries GPU ; la fenêtre ne connaît plus le contexte graphique).
- **Appels système dans `core`** : sortir `<windows.h>` de `Logger`, `Utilities` et `FPSCounter` (derrière une interface `platform`), sans changer le comportement. Ajouté après la revue CodeRabbit de M1 (G59) : `std::getenv` de `Vfs::defaultUserDirectory` et l'accès direct à `std::filesystem` de `FileWatcher`/`TraceRecorder` passent aussi par cette interface.
- **En-têtes autonomes** : `check_headers.py --strict` à zéro (aujourd'hui `Graphics/Upscaling/IFrameGenerationMode.h`).
- **Nommage hérité** : `utilities.h` / `Utilities.cpp` (G12), dossier d'en-têtes `Graphics/Backend/` contre `Backends/` (G13).
- **Pas de changement de comportement** : mêmes tests, même capture de référence (`docs/BASELINE/reference/`).

**Démo** : `make check` sans aucun avertissement de couches ni d'en-tête ; capture identique à la référence.
**Terminé** : baselines `layers.txt` vide, `check_headers.py --strict` vert, 0 régression de test, README des modules mis à jour.

## M2 · RHI et backend OpenGL (migration de l'existant)

- Définir l'interface (`docs/03` §3), la couche plateforme (PLAT-01, PLAT-04).
- Écrire le backend OpenGL 4.3+ avec MRT (RHI-15), textures 3D et images de stockage (RHI-16), compute (RHI-09), dessin instancié et indirect (RHI-10), `Capabilities` incluant les capacités optionnelles réservées (RHI-18).
- **Migrer le rendu actuel** (éclairage, texte, UI) derrière le RHI.
- Pipeline de shaders (RHI-12) : GLSL → SPIR-V → GLSL cible ; réflexion ; inclusions ; rechargement à chaud.
- Tests d'images de référence (RHI-14).
- L'interface produit des listes de dessin (UI-02) ; le texte passe par le RHI.

### Découpage de M2 en lots et sous-lots (décidé par l'auteur le 2026-11-18)

Trois lots, chacun fusionné dans `dev` puis arrêt pour validation. Le **lot B** est une **réécriture explicite** des façades de rendu sur `Device` / `CommandEncoder` (passes, groupes de liaison, pipelines à état fixe), sans adaptateur d'état implicite : l'ancienne abstraction `IGraphicsDevice` et les backends associés disparaissent. L'API publique des façades **change** ; `Game`, `World` et l'interface suivent. Chaque sous-lot est une branche, fusionnée quand compilation, tests et contrôles sont verts ; **la démo peut être temporairement incomplète entre deux sous-lots** (notée dans leur compte rendu) et la capture de référence ne redevient identique qu'à B6.

| Lot | Contenu | État |
| --- | --- | --- |
| A | Interface RHI, backend OpenGL hors écran, tests GPU, `make rhi-demo` | **Fait** (`docs/releases/M2-lotA.md`) |
| B1 | **Pipeline de shaders** (RHI-12) : GLSL 450 → SPIR-V → GLSL 430, réflexion, `#include`, cache mémoire et disque, rechargement à chaud | **Fait**, fusionné dans `dev` (branche `feat/RHI-12-shader-pipeline`) |
| B2 | **Surface de fenêtre du RHI** : le périphérique OpenGL adopte une fenêtre GLFW créée par la couche appelante (backbuffer = framebuffer par défaut, `present` = échange des tampons, vsync, `resize`) ; le contexte et les attributs de contexte restent dans `src/rhi/opengl/`. Test de présentation sous xvfb | **Fait**, fusionné dans `dev` (branche `feat/RHI-08-window-surface`) |
| B3 | **Couche de ressources** `engine::renderer` (`include/engine/renderer/`, `src/renderer/`, archive `librenderer.a`) : `GpuBuffer`, `Texture`, `RenderTarget` (MRT, profondeur lisible, `passDesc`), `ShaderLibrary` / `ShaderProgram` (rechargement à chaud branché), `PipelineCache`, `Image` (relecture, PNG, comparaison), `GpuTimer` ; ajouts au RHI : `textureDesc`, `timestampsReady`. **Écart au plan initial (noté le 2026-11-18)** : ces classes sont **nouvelles** et coexistent avec les anciennes façades jusqu'à ce que leurs consommateurs migrent (B4, B5) ; les supprimer maintenant aurait cassé la compilation à chaque commit. La suppression de `Buffer`, `Texture`, `RenderTarget`, `ShaderProgram`, `ShaderLibrary`, `Screenshot`, `GPUTimestampQuery` et de `GraphicsRenderState` (G38) passe donc en B6 | **Fait**, fusionné dans `dev` (branche `feat/RHI-02-render-resources`) |
| B4 | **Couche de scène** `engine::renderer` : `Camera` (sans entrées), `LightManager` (SSBO vérifié), `Mesh` (sommets, instances, transformation, textures, constantes), `FullscreenBlit`, extensions d'upscale / post-traitement / génération d'images, `SceneRenderer` (échelle de rendu, jitter, historique, MRT) ; shaders `shaders/rhi/` en GLSL 450 ; **parité avec l'image de référence** du rendu hérité (terrain graine 1, 1280x720). **Écart au plan (noté le 2026-11-18)** : `Sprite` et `Renderer2D` appartiennent au côté interface et passent en B5 ; `Game` et `World` ne basculent sur la nouvelle couche (et la démo ne présente via le RHI) qu'en B6, avec l'interface, car l'ancien et le nouveau runtime graphique ne peuvent pas dessiner dans la même fenêtre | **Fait**, fusionné dans `dev` (branche `feat/RHI-02-scene-renderer`) |
| B5 | **Texte et interface** : `Sprite` et `Renderer2D` (listes de dessin, découpage) ; `TextRenderer` (atlas MSDF) sur le RHI (REN-36) ; l'interface produit des **listes de dessin** (rectangles, texte, images, découpage, ordre `z`) sans connaître le RHI (UI-02) ; un adaptateur provisoire les dessine par le renderer (le batch R2D-01 arrive en M5, `docs/QUESTIONS.md` A21) ; singleton `UI::Manager` remplacé par un contexte explicite (A10 pour ce module) | À faire |
| B6 | **Bascule, retrait et vérification** : `Game`, `World`, `main` passent sur `SceneRenderer`, le périphérique RHI de fenêtre (B2) et l'interface de B5 (la démo présente via le RHI ; pilotage de la caméra par les entrées, G80) ; suppression de `IGraphicsDevice`, des anciens backends OpenGL et Vulkan (archivés, A1 ; `--api vulkan` répond « non disponible avant M4 ») et de `AccelerationStructure` si sans usage ; capacités honnêtes (RHI-18) ; `check_api_isolation.py` bloquant pour `gl*`, `GL_*`, `Vk*`, `MTL*` (la baseline ne garde que `GLFW`/`<windows.h>` jusqu'au lot C) ; **tests d'images de référence** par backend avec tolérance et rapport de différence (RHI-14), en CI ; capture identique à `docs/BASELINE/reference/` ; documentation du RHI et STATUS | À faire |
| C | `platform::Window` (GLFW caché, DPI, plein écran), mode sans fenêtre (PLAT-04), entrées (PLAT-02, `Camera::Inputs`), isolation des API **intégralement bloquante** (GLFW compris), vsync | À faire |

Les identifiants de `docs/02` restent ceux de la table de rattachement (M2). Un sous-lot n'est « Fait » que sur preuve (CLAUDE.md règle 1) ; STATUS est mis à jour à chaque fusion.

**Démo** : l'application actuelle, même image qu'avant, sans aucun appel `gl*` hors de `src/rhi/opengl/`.
**Terminé** : règle d'isolation des API en mode bloquant ; images identiques à la référence dans la tolérance ; documentation du RHI.

## M3 · Premier générateur de terrain et interface de base (la démo qui compte)

- TER-01 (bruit CPU et GPU), TER-02, TER-03, TER-04, TER-07, TER-11, TER-12, TER-13.
- TOOL-01 (visualiseur) et TOOL-02 (ligne de commande, sans fenêtre).
- REN-02 (caméra, reverse-Z), REN-34 (debug), éclairage existant migré.
- TEST-03 : déterminisme.
- **Interface** : fin du langage d'interface pour ce que le visualiseur exige : UI-03 (widgets), UI-05 (compilateur, erreurs lisibles), UI-06 (mise en page), UI-07 (thèmes JSON), UI-11 (spécification et tests), UI-12 (paramètres réfléchis → curseurs).
- **Première release utilisable (décidé par l'auteur le 2026-11-18)** : M3 est le premier jalon dont la release (`vX.Y.Z`) est présentée comme utilisable et à peu près stable ; les releases précédentes (`v0.1.0`…) restent des instantanés pour développeurs. Tâches (rattachées à DOC-01, BLD-01, BLD-02) :
  - **Guide d'utilisation** `docs/USING_THE_ENGINE.md`, relié depuis le `README.md` racine : installer depuis la release par l'exécutable `engine` (`engine init`, `engine update`) **et** par le zip `engine-pkg-<plateforme>.zip` (`ENGINE_DIR`), prérequis par système, projet minimal complet, limites connues, version minimale du compilateur.
  - **Mettre à jour** `tools/engine-sdk/dist-README.md` et le corps de la release (`build-engine.yml`) : archives `librhi.a` et `libplatform.a`, en-têtes `include/engine/…`, API du RHI.
  - **Test de bout en bout en CI** : à partir du paquet construit (sans les sources du moteur), créer un projet vide avec `engine init`, le compiler et le lancer (capture ou mode sans fenêtre) sur Linux, Windows et macOS ; échec de la CI si le chemin documenté casse.
  - **Stabilité de l'API** : figer les en-têtes publics `include/engine/…` (migration `PascalCase` → `camelCase` terminée pour les modules livrés), table des changements incompatibles dans `docs/releases/`, numéro de version sémantique ; les identifiants de `docs/02` livrés doivent être `Fait` ou limités dans le guide.
  - **Exemple fourni** : le visualiseur et `terrain-gen` compilables depuis le paquet comme exemples de projet.

**Démo** : visualiseur avec graine et paramètres modifiables en direct dans une interface décrite dans le langage maison, thème changeable ; `terrain-gen --seed 42 --size 4096 --out relief.png` produit une carte de hauteur identique à chaque lancement.
**Terminé** : somme de contrôle de référence ; pas de fissure entre niveaux de détail ; budget mémoire respecté ; exports vérifiés en les rouvrant ; tests de compilation du langage verts ; `docs/UI_LANGUAGE.md` écrit. La release de fin de M3 est utilisable : `docs/USING_THE_ENGINE.md` écrit, `engine init` + compilation vérifiés en CI sur les trois systèmes, API publique figée.

## M4 · Backend Vulkan

- Backend complet avec VMA, barrières déduites, pipelines en cache (mémoire et disque, RHI-06).
- Sélection du backend à l'exécution (RHI-04).
- Guide `docs/ADD_A_BACKEND.md`.

**Démo** : le visualiseur de M3, lancé avec `--backend=vulkan` et `--backend=opengl`, donne la même image.
**Terminé** : couches de validation Vulkan silencieuses ; images de référence dans la tolérance sur les deux backends ; mesure de performance comparée dans `docs/`.

## M5 · Cœur du rendu

- REN-01 (graphe de passes, plugins), REN-03 (données par image), REN-04 (forward+ clusterisé), REN-05, REN-06 à REN-08 (ombres, mobilité, cache), REN-17 à REN-19 (HDR, post-traitement, anticrénélage), REN-21 (AOV et MRT à la demande), REN-22 (boîte à outils), REN-28 à REN-31 (instancing, rendu piloté par GPU, culling, LOD), REN-32, REN-35, REN-37 (modes de fusion), REN-39 (ombres des transparents), REN-41 (transparents et sorties auxiliaires).
- **2D** : R2D-01, R2D-02, R2D-03, R2D-05, R2D-06 ; l'interface passe par le batch 2D.

**Démo** : terrain avec ombres en cascade et cache, des milliers d'objets instanciés en quelques draw calls, normales et vecteurs de mouvement activés à la demande par un effet, scène mélangeant 3D, sprites dans le monde et interface.
**Terminé** : tests d'images par backend, profilage GPU par passe visible, nombre de draw calls mesuré et documenté, aucune régression sur les démos précédentes.

## M6 · Contenu importé et animation

- ASSET-01 à ASSET-05 (glTF, textures, KTX2, cuisson, rechargement), REN-10, REN-11 (matériaux PBR et variantes), REN-13 (IBL, sondes).
- ANIM-01 à ANIM-04 (squelettes, clips, machine à états, animation de propriétés, foules instanciées).

**Démo** : un modèle glTF animé, puis mille instances animées avec un seul draw call par lot ; un asset modifié sur disque se met à jour en direct.
**Terminé** : formats vérifiés par import/export de fichiers d'exemple ; mesure du coût GPU du skinning ; README des formats pris en charge.

## M7 · Atmosphère, effets et monde riche

- REN-23 (bruit en compute), REN-24 (atmosphère), REN-25 (brouillard), REN-26 (nuages si le temps le permet), REN-33 (particules), REN-14 (AO écran), REN-27 (eau), REN-38 (réfraction et transmission).
- TER-05 (érosion en compute), TER-06 (biomes), TER-08 (végétation), TER-09 (planète sphérique, double précision, transition sol/espace), TER-10 si le temps le permet.

**Démo** : une planète vue de l'espace, puis atterrissage continu jusqu'au sol, avec atmosphère, brouillard et nuages, sans saut ni tremblement.
**Terminé** : déterminisme conservé ; pas de dégradation visible de précision ; érosion reproductible ; coût GPU de chaque effet noté.

## M8 · Interface avancée, scène, simulation et logique rechargeable

- UI-04, UI-08 (rechargement à chaud de l'interface), UI-09, UI-10.
- ECS-01 à ECS-03, SIM-01, SIM-02.
- GAME-01 à GAME-07 : module de jeu en bibliothèque dynamique, registre de gestionnaires, migration d'état, paramètres JSON, version finale statique.

**Démo** : on modifie un fichier de logique pendant que le programme tourne, on compile, et le comportement change sans redémarrage ni perte d'état ; on modifie un fichier d'interface et l'écran se met à jour ; un petit programme de simulation (quelques centaines d'agents) lit le relief et tourne sans fenêtre, rejouable avec la même graine.
**Terminé** : cas de rechargement testés (changement de corps de fonction, ajout de champ, changement de hiérarchie avec repli sur sauvegarde/restauration) ; boucle à pas fixe testée ; rejouabilité vérifiée.

## M9 · macOS et Metal

- Valider Vulkan via MoltenVK, régler les différences.
- Mesurer. **Décision documentée** : backend Metal natif oui ou non (RHI-13).
- Si oui : implémenter `src/rhi/metal/` en suivant `ADD_A_BACKEND.md`.

**Démo** : visualiseur sur un Mac.
**Terminé** : CI macOS verte ; limites connues dans `docs/GAPS.md`.

## M10 · Extensions et finition

- Vérifier que les points d'accroche tiennent : un plugin voxel minimal (EXT-01) s'ajoute **sans modifier** le renderer ni les backends ; l'interface d'accélération de rayons (EXT-03) est présente dans le RHI, désactivée dans OpenGL.
- Revue de documentation, schémas, guide de démarrage.
- Nettoyage : TODO restants, code mort, avertissements.
- Passe de performance : budgets de mémoire et de temps notés par système.
- Emballage : binaires des démos, README principal.

**Terminé** : passe de confirmation de `docs/04` (§« Confirmation de la couverture ») sans écart non justifié.

## Fonctions non rattachées à un jalon

Les identifiants de priorité P2 ou P3 non cités ci-dessus (par exemple REN-09, REN-12, REN-15, REN-16, REN-20, REN-40, RHI-19, PLAT-03 ; TOOL-03 est passé en M0) sont traités après M10, par ordre d'intérêt décidé avec l'auteur. RHI-11 (minuteries GPU), RHI-17 (dispatch indirect), CORE-03, CORE-05, CORE-08, PLAT-05 et REN-36 (texte) sont à placer par l'IA dans le jalon où elles servent en premier, et à signaler dans `docs/STATUS.md`. Aucun identifiant ne doit rester sans jalon : l'audit produit la table de rattachement dans `docs/GAPS.md`.

## Règles de conduite pour tous les jalons

1. Un jalon à la fois. Pas de fonctionnalité d'un jalon futur « au passage ».
2. Chaque fonction du jalon est liée à un ID de la checklist ; `docs/STATUS.md` est mis à jour avec la preuve.
3. Si l'ordre ou la portée doivent changer, l'IA le propose dans `docs/QUESTIONS.md` et attend.
4. En fin de jalon : message de synthèse avec (a) la commande de démo, (b) le résultat des tests, (c) ce qui n'est pas fait et pourquoi, (d) le jalon suivant.
5. Git : une branche par fonctionnalité depuis `dev`, fusion dans `dev` quand c'est vert, jamais de commit sur `main`. En fin de jalon, l'IA prépare `docs/releases/<jalon>.md` et la description de la merge request, puis s'arrête : l'auteur teste sur `dev`, signale les bugs, et fusionne vers `main` quand le jalon lui convient (détail dans `docs/06`).
