# 02 · Checklist des fonctionnalités

C'est la liste de référence. L'IA ne la modifie pas sans accord, sauf pour remplir l'état et la preuve dans `docs/STATUS.md` (jamais en cochant de mémoire).

**Priorités** : P0 = indispensable aux premiers jalons utiles · P1 = nécessaire au moteur complet · P2 = souhaitable · P3 = plus tard (le design ne doit pas l'empêcher).

**États** : `Fait` (preuve fournie) · `Partiel` · `Absent` · `Incompatible` (existe mais viole l'architecture, à adapter) · `?` (pas encore audité).

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
| PLAT-05 | Saisie de texte | P1 | Texte Unicode, composition IME, presse-papiers. | Partiel (texte Unicode ; ni IME ni presse-papiers) |

## C. Couche graphique (`rhi`) : voir `03-ARCHITECTURE_ET_PORTABILITE.md`

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| RHI-01 | Interface RHI | P0 | Interface décrite en `03` ; aucun type d'API (`GLuint`, `VkBuffer`…) dans les en-têtes publics. | Partiel |
| RHI-02 | Backend OpenGL 4.3+ | P0 | Reprend le rendu existant derrière le RHI ; images de référence identiques à l'ancien rendu (tolérance définie). | Fait (OpenGL) — preuve : `docs/STATUS.md`, « M2 B6 » |
| RHI-03 | Backend Vulkan | P0 | Parité de fonctionnalités avec OpenGL ; couches de validation silencieuses ; mémoire via VMA. | Absent (ancien backend retiré du build et archivé, A1 ; réécriture en M4) |
| RHI-04 | Choix du backend à l'exécution | P0 | `--backend=vulkan|opengl|metal`, repli documenté, `Capabilities` interrogeable. | Partiel |
| RHI-05 | Buffers, textures (2D, tableau, cube, 3D, mips), échantillonneurs | P0 | Création, mise à jour, copie, génération de mips ; formats portables listés. | Partiel |
| RHI-06 | Pipelines graphiques et de calcul | P0 | État explicite ; cache de pipelines en mémoire **et sur disque**. | Partiel |
| RHI-07 | Groupes de liaison | P0 | Modèle de liaison à trois groupes (voir `03`) traduit par chaque backend. | Partiel |
| RHI-08 | Passes de rendu et swapchain | P0 | Passes décrites par attachements ; redimensionnement sans fuite ; vsync réglable. | Partiel |
| RHI-09 | Compute | P0 | Dispatch direct, tampons de stockage, barrières gérées par le RHI ; taille de groupe lue dans `Capabilities`. | Fait (OpenGL) — preuve : `docs/STATUS.md`, « M2 lot A » |
| RHI-10 | Dessin instancié et indirect | P0 | `drawIndexed` instancié ; `drawIndirect` avec compteur (multi-draw indirect). | Partiel |
| RHI-11 | Minuteries GPU et noms de debug | P1 | Requêtes de temps par passe ; noms visibles dans RenderDoc. | Partiel |
| RHI-12 | Pipeline de shaders | P0 | GLSL → SPIR-V hors ligne → SPIRV-Cross (MSL, GLSL) ; réflexion des liaisons ; inclusions ; rechargement à chaud. | Partiel |
| RHI-13 | Backend Metal | P2 | Via MoltenVK validé d'abord ; backend natif seulement si les mesures le justifient. | Absent |
| RHI-14 | Tests de rendu | P0 | Images de référence par backend, comparaison automatisée, rapport de différence. | Partiel |
| RHI-15 | Rendu vers plusieurs cibles (MRT) | P0 | Jusqu'à 8 attachements couleur (lus dans `Capabilities`) + profondeur, format et opérations par cible ; sorties distinctes dans un même shader. | Fait (OpenGL) — preuve : `docs/STATUS.md`, « M2 B4 » |
| RHI-16 | Textures 3D et images de stockage | P0 | Lecture/écriture en compute ; formats flottants 16 et 32 bits, `RG16F`, `RGBA16F`, `R32F`, `R32UI` garantis. | Partiel |
| RHI-17 | Dispatch indirect et groupes dynamiques | P1 | `dispatchIndirect` ; offsets dynamiques des tampons. | Fait (OpenGL) — preuve : `docs/STATUS.md`, « M2 lot A » |
| RHI-18 | Capacités optionnelles réservées | P1 | `Capabilities` expose `rayQuery`, `accelerationStructure`, `bindless`, `meshShader`, `bufferDeviceAddress`, `sparseResources` (toutes à `false` tant que non implémentées) ; le moteur ne suppose jamais leur présence. | Fait — preuve : `docs/STATUS.md`, « M2 B6 » |
| RHI-19 | Transferts asynchrones | P2 | File de transfert séparée quand disponible ; chargement sans saccade. | Absent |

## D. Rendu 3D (`renderer`)

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| REN-01 | Graphe de passes et plugins de rendu | P1 | Passes déclaratives avec dépendances, ressources transitoires, ordre calculé ; **plugins de rendu** (maillages, terrain, 2D, voxels futurs, ray tracing futur) qui ajoutent leurs passes sans modifier le cœur. | Absent |
| REN-02 | Caméra | P0 | Perspective et orthographique ; **reverse-Z avec plan lointain infini** ; rendu relatif à la caméra ; décalage (jitter) prévu pour le TAA. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| REN-03 | Données par image (groupe 0) | P0 | Bloc de constantes décrit en `03` §10 : matrices et inverses, image précédente, caméra, temps, résolution, graine de bruit, lumière principale, paramètres d'atmosphère. | Partiel |
| REN-04 | Éclairage forward+ clusterisé | P0 | Pré-passe de profondeur ; clusters remplis en compute ; nombre de lumières limité par un budget réglable. | Absent |
| REN-05 | Types de lumières | P0 | Existe (UBO/SSBO std140/std430) ; directionnelle, ponctuelle, spot migrées sur le RHI. Lumières de surface en P2. | Partiel |
| REN-06 | Mobilité et mode d'ombre des lumières | P1 | `Static`, `Stationary`, `Dynamic` ; ombres `None`, `Baked`, `Dynamic`, `Cached` ; comportement documenté et testé pour chaque combinaison. | Absent |
| REN-07 | Ombres en cascade | P0 | Lumière directionnelle, PCF, stabilité sans scintillement. | Absent |
| REN-08 | Atlas d'ombres et cache | P1 | Budget par lumière, priorité, mise à jour amortie, **cache** qui ne redessine que ce qui bouge, ombres des spots, fondu à distance, ombres de contact. | Absent |
| REN-09 | Ombres avancées | P2 | Lumières ponctuelles (cubemap), pénombre variable (PCSS), ombres d'horizon pour le terrain. | Absent |
| REN-10 | Matériaux PBR | P0 | Métal/rugosité (glTF), albedo, normale, métal, AO, émission ; modes alpha importés de glTF (`OPAQUE`, `MASK`, `BLEND`) traduits vers `BlendMode` (REN-37) ; double face. | Absent |
| REN-11 | Variantes et instances de matériaux | P1 | Permutations par spécialisation de constantes, cache ; instances qui surchargent des paramètres. | Absent |
| REN-12 | Extensions de matériaux | P2 | Vernis, tissu, transmission, parallaxe. | Absent |
| REN-13 | IBL et sondes de lumière | P1 | Environnement HDR, irradiance en harmoniques sphériques, spéculaire préfiltré, sondes pour objets mobiles. | Absent |
| REN-14 | Occlusion ambiante | P1 | AO écran (GTAO) ; réglable. | Absent |
| REN-15 | Réflexions et GI | P2 | Réflexions écran (SSR) ; éclairage global par sondes. P3 : GI par voxels ou par rayons. | Absent |
| REN-16 | Éclairage précalculé | P2 | Lightmaps ou sondes cuites par un outil hors ligne ; la lumière `Baked` les consomme. | Absent |
| REN-17 | HDR et tonemapping | P0 | Cible HDR, exposition réglable, tonemapping au choix. | Absent |
| REN-18 | Post-traitement courant | P1 | Bloom, exposition automatique (histogramme), étalonnage par LUT, vignettage ; chaque effet déclare ses entrées. | Absent |
| REN-19 | Anticrénélage | P1 | SMAA d'abord, puis TAA (vecteurs de mouvement, rejet de l'historique) ; MSAA en option ; résolution dynamique. | Partiel |
| REN-20 | Effets coûteux optionnels | P2 | Profondeur de champ, flou de mouvement, upscaling type FSR. | Absent |
| REN-21 | Sorties auxiliaires (AOV) | P1 | Normales (octaédrique), vecteurs de mouvement, rugosité/métal, identifiant d'objet, profondeur linéaire, émission ; **activées seulement si une passe les demande** ; règle de résolution MSAA documentée (identifiants non moyennés). | Partiel |
| REN-22 | Boîte à outils de shaders | P1 | Fichiers GLSL d'inclusion partagés : maths, bruit, coordonnées, reconstruction de position depuis la profondeur, PBR, atmosphère, échantillonnage d'ombres. | Absent |
| REN-23 | Textures de bruit en compute | P1 | Bruit 3D (Worley, Perlin) généré au démarrage ; réutilisable par terrain, nuages et brouillard. | Absent |
| REN-24 | Ciel et atmosphère | P1 | Tables de transmittance, diffusion multiple et vue aérienne, mises à jour par compute ; valable du sol à l'espace ; cycle jour/nuit. | Absent |
| REN-25 | Brouillard | P1 | Exponentiel et de hauteur. Brouillard volumétrique par froxels en P2, avec ombres. | Absent |
| REN-26 | Nuages volumétriques | P2 | Carte de couverture, rendu à résolution réduite avec accumulation temporelle. | Absent |
| REN-27 | Eau | P2 | Plan d'eau avec profondeur, réflexion, écume. | Absent |
| REN-28 | Instancing | P0 | Données par instance dans un tampon ; un seul draw call par groupe (maillage, matériau). | Partiel |
| REN-29 | Rendu piloté par GPU | P1 | Scène GPU persistante (liste d'objets modifiés), multi-draw indirect, culling frustum puis occlusion Hi-Z en compute. | Absent |
| REN-30 | Culling CPU | P0 | Frustum sur BVH ou grille ; petits objets. | Absent |
| REN-31 | LOD et imposteurs | P1 | LOD par distance, imposteurs pour la végétation lointaine, transitions sans trou. | Absent |
| REN-32 | Tri et ordre des transparents | P1 | Objets mélangés triés de l'arrière vers l'avant sans écriture de profondeur ; additifs après les mélangés ; priorité de tri réglable par matériau ; ordre global fixe défini en `03` §11 bis. | Absent |
| REN-37 | Modes de fusion des matériaux | P0 | `BlendMode` par matériau : `Opaque`, `Mask` (seuil), `MaskA2C` (alpha-to-coverage), `Dithered`, `Blend`, `Additive`, `Premultiplied`, `Transmission` ; chaque mode a un test d'image de référence. | Absent |
| REN-38 | Réfraction et transmission | P2 | Copie de la scène opaque (avec mips) avant la passe transparente ; échantillonnage décalé par normale et indice de réfraction ; limites connues documentées (pas de réfraction entre couches de verre). | Absent |
| REN-39 | Ombres des transparents | P1 | Option par matériau : aucune, pleine, découpée (masque) ou tramée ; choix documenté et testé. | Absent |
| REN-40 | Transparence indépendante de l'ordre (OIT) | P2 | OIT pondéré (weighted blended) en option, avec passe de composition ; OIT à listes chaînées ou à moments en P3. | Absent |
| REN-41 | Transparents et sorties auxiliaires | P1 | Par défaut un transparent n'écrit ni normales, ni identifiant, ni vecteurs de mouvement ; chaque matériau peut les demander explicitement (par exemple pour la sélection à la souris ou le TAA). | Absent |
| REN-33 | Particules et décalques | P1 | Particules GPU (compute) ; décalques en P2 ; particules douces. | Absent |
| REN-34 | Dessin de debug | P0 | Lignes, boîtes, sphères, repères ; aucun coût hors debug. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| REN-35 | Modes d'affichage de debug | P1 | Normales, albédo, surdessin, couleurs de LOD, cascades, lumières par cluster, filaire, gel du frustum de culling. | Partiel |
| REN-36 | Rendu du texte | P0 | Existe (FreeType, ancrage, retour à la ligne, alignement) ; migré sur le RHI. | Partiel |

## E. Rendu 2D et mélange 2D/3D (`renderer2d`, plugin du renderer)

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| R2D-01 | Batch de sprites | P0 | Quads instanciés, atlas ou tableau de textures, tri par couche (`z`) puis matériau ; un draw call par lot. | Partiel |
| R2D-02 | Caméra 2D | P0 | Orthographique ; mode pixel parfait (filtrage au plus proche, échelle entière). | Partiel |
| R2D-03 | Formes, lignes, texte | P1 | Rectangles, cercles, lignes, polygones ; texte via REN-36. | Partiel |
| R2D-04 | Cartes de tuiles | P1 | Grille, atlas, couches, chargement d'un format courant (JSON). | Absent |
| R2D-05 | Mélange 2D/3D | P1 | Trois modes : 2D dans le monde avec test de profondeur ; 2D en superposition après le post-traitement ; 2D vers texture affichée sur un objet 3D. Ordre fixe : 3D opaque → 3D transparent → 2D monde → post-traitement → 2D en superposition. | Partiel |
| R2D-06 | Plusieurs vues | P1 | Plusieurs caméras et zones d'affichage ; vue 3D incrustée dans une interface. | Absent |
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

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| TER-01 | Bruit | P0 | Perlin, Simplex, Worley, fBm, distorsion de domaine ; **CPU et GPU donnent le même résultat** (tolérance fixée) ; testé. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| TER-02 | Génération par tuiles | P0 | Hauteur par tuile à partir de (graine, coordonnées, paramètres) ; déterministe ; asynchrone. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| TER-03 | Maillage à niveaux de détail | P0 | Quadtree ou clipmap ; pas de fissures entre niveaux ; distance de vue réglable. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| TER-04 | Streaming | P0 | Chargement/déchargement par distance ; pas de saccade mesurable sur la démo ; budget mémoire réglable. | Partiel (budget et génération asynchrone ; absence de saccade non mesurée) |
| TER-05 | Érosion | P1 | Hydraulique et thermique, en compute ; résultat reproductible. | Absent |
| TER-06 | Biomes | P1 | Température, humidité, altitude, pente → biome ; table modifiable. | Absent |
| TER-07 | Texturage du terrain | P0 | Mélange par pente et altitude, triplanaire ; atlas ou tableau de textures. | Fait (OpenGL) — preuve : `docs/STATUS.md`, « M3 » |
| TER-08 | Végétation et dispersion | P1 | Placement déterministe, instancié (REN-28) ; densité par biome. | Absent |
| TER-09 | Planètes sphériques | P1 | Cube-sphère à quadtree ; précision `double` côté CPU ; rendu relatif à la caméra ; transition sol/espace avec l'atmosphère. | Absent |
| TER-10 | Eau, rivières, lacs | P2 | Niveau de la mer, bassins, tracé de rivières. | Absent |
| TER-11 | Export | P0 | Carte de hauteur (PNG 16 bits, EXR), maillage (OBJ, glTF, STL imprimable), carte de biomes. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| TER-12 | Interface de réglage | P0 | Paramètres modifiables en direct, graine, préréglages sauvegardables ; affichage automatique à partir de la réflexion (UI-12). | Fait — preuve : `docs/STATUS.md`, « M3 » |
| TER-13 | Lancer de rayon sur le terrain | P1 | Picking souris et hauteur en un point, CPU, cohérent avec le rendu. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| TER-14 | Grottes et voxels | P3 | Réservé ; l'architecture ne doit pas l'exclure. | Absent |

## H. Scène et simulation (`scene`, `sim`)

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| ECS-01 | Entités et composants | P1 | ECS maison ou EnTT (choix à justifier dans `docs/QUESTIONS.md`) ; tableaux contigus ; composants = données simples, **réfléchies** (CORE-12). | Absent |
| ECS-02 | Transformations hiérarchiques | P1 | Parents/enfants, grandes coordonnées en `double`. | Absent |
| ECS-03 | Chargement et sauvegarde de scène | P1 | Via CORE-12, versionné. | Absent |
| SIM-01 | Boucle de simulation à pas fixe | P1 | Découplée du rendu ; rejouable avec la même graine ; capture et restauration d'état ; points d'accroche pour systèmes. | Absent |
| SIM-02 | Accès aux données du monde | P2 | API de lecture (hauteur, biome, eau) utilisable sans rendu par la simulation. | Absent |
| SIM-03 | Agents et écosystème | P3 | Hors moteur. Seuls les points d'accroche de SIM-01 et SIM-02 sont attendus. | Absent |

## I. Interface (`ui`) et langage d'interface

| ID | Fonctionnalité | Prio | Critère d'acceptation | État |
| --- | --- | --- | --- | --- |
| UI-01 | UIManager | P0 | Existe (UIComponent, UIContainer, VBox, `DeferredValue<T>` à double tampon, mixin CRTP). Migré sur le RHI ; comportement identique. | Partiel |
| UI-02 | Rendu indépendant du backend | P0 | L'interface produit des listes de dessin (rectangles, texte, images, découpage, ordre `z`) ; un adaptateur les envoie au batch 2D (R2D-01). | Partiel |
| UI-03 | Widgets de base | P0 | Bouton, curseur, case à cocher, liste déroulante, champ de texte, sélecteur de couleur, graphique simple, onglets, défilement, infobulles. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| UI-04 | Outils de debug | P1 | Dear ImGui (ou équivalent) sur le RHI, réservé aux outils : profileur, console. | Absent |
| UI-05 | Langage d'interface : compilateur vers C++ | P0 | Description déclarative de l'arbre, compilée en C++ sans coût d'exécution ; événements (`onClick`, `onChange`, `onHover`, `onFocus`) liés à des fonctions externes, **aucune logique métier dans le langage** ; liaison de données typée dans les deux sens ; C++ embarqué possible ; erreurs avec **fichier, ligne et colonne du source du langage** ; intégration au build avec dépendances correctes. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| UI-06 | Mise en page complète | P0 | `VBox`, `HBox`, grille, superposition, marges, alignement, tailles fixes, flexibles et minimales. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| UI-07 | Thèmes en JSON | P0 | Fichier de données avec **jetons** (`color.primary`, `radius.medium`, polices, tailles, marges, ombres, bordures) et états (normal, survol, appuyé, désactivé, focus) ; les widgets lisent le thème, jamais de valeur en dur ; changement à chaud ; variantes clair/sombre ; surcharge locale possible dans le langage. | Fait — preuve : `docs/STATUS.md`, « M3 » (ajouts) |
| UI-08 | Rechargement à chaud de l'interface | P1 | Mode développement : le même arbre est interprété ou chargé en bibliothèque dynamique et se met à jour à l'enregistrement ; mode final : C++ généré lié directement. | Absent |
| UI-09 | Navigation et accessibilité de base | P1 | Clavier et manette, focus visible, ordre de tabulation. | Absent |
| UI-10 | Inspecteur d'interface | P1 | Arbre des composants et tailles calculées affichés dans les outils de debug. | Absent |
| UI-11 | Spécification et tests du langage | P0 | `docs/UI_LANGUAGE.md` : grammaire, exemples, limites ; tests de compilation avec entrées valides et invalides et messages d'erreur attendus. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| UI-12 | Paramètres réfléchis → curseurs | P1 | Une structure de paramètres réfléchie (CORE-12) est affichée automatiquement en widgets adaptés au type et à la plage. | Fait — preuve : `docs/STATUS.md`, « M3 » |

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
| TOOL-01 | Visualiseur de terrain | P0 | Application de démo : caméra libre, réglages, export. Démo principale. | Partiel (visualiseur livré ; caméra et export vérifiés à la main seulement) |
| TOOL-02 | Générateur en ligne de commande | P0 | `terrain-gen --seed N --size S --out fichier` sans fenêtre ; sorties de TER-11 ; code de retour fiable. | Fait — preuve : `docs/STATUS.md`, « M3 » |
| TOOL-03 | Capture d'écran | P0 | Image depuis le rendu ; utilisée par RHI-14. (Priorité passée de P2 à P0 par l'auteur le 2026-11-17 : comparer les images dès le début.) | Partiel |
| BLD-01 | Construction multiplateforme | P0 | Un seul chemin de build documenté pour Windows, Linux, macOS ; vcpkg ; types debug/dev/release. L'IA peut **proposer** CMake (avec presets) en gardant le Makefile en enveloppe, sans migrer sans accord. | Partiel |
| REPO-01 | Dépôt propre et structuré | P0 | M00 de `docs/06` : `.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`, `README.md` racine, `third_party/` documenté, aucun fichier généré ou binaire inutile suivi, historique conservé, comportement identique à la référence. | Fait (confirmé par l'auteur le 2026-11-17) — preuve : `docs/STATUS.md`, « M0 » |
| GIT-01 | Flux de branches | P0 | `main` réservé à l'auteur, `dev` d'intégration, une branche par fonctionnalité, commits au format `type(ID): résumé`, fin de jalon par merge request de l'auteur (détail en `docs/06`). | Partiel |
| BLD-02 | Intégration continue | P0 | Matrice Windows/Linux/macOS : build, tests, vérification des couches. | Partiel |
| TEST-01 | Tests unitaires | P0 | Framework unique (Catch2 ou doctest) ; exécutés en CI ; couverture de `core` et `world`. | Partiel |
| TEST-02 | Assainisseurs et analyse | P1 | ASan, UBSan, clang-tidy, formatage automatique en CI. | Partiel |
| TEST-03 | Tests de déterminisme | P0 | Même graine → mêmes octets sur les trois systèmes (somme de contrôle de référence). | Partiel |
| DOC-01 | Documentation | P0 | `README.md` par module ; `docs/STATUS.md` à jour ; schéma des modules ; guide « ajouter un backend ». | Partiel |
