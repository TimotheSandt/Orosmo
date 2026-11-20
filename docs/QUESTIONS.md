# Questions pour l'auteur

Réponses de l'auteur (2026-11-16) : Q1 apt d'abord, vcpkg en repli ; Q2 commiter l'état actuel dans la branche de nettoyage ; Q4 option A ; Q5 licence plus tard (Apache-2.0 probable) ; Q6 garder les deux en-têtes (usage décidé à l'implémentation de l'UI). Q3 et Q7 : sans réponse, rien n'a été poussé ni nettoyé.

## M00

**Q1. Installer vcpkg et les dépendances pour obtenir la référence de comportement ?**
Sans cela, aucune image de référence n'existe et « même résultat qu'avant » ne se vérifie que par compilation + tests. Recommandation : oui, vcpkg dans `~/vcpkg` (hors dépôt), triplet `x64-linux`.

**Q2. Que faire des modifications non commitées de `dev` ?**
Voir `docs/CLEANUP_PLAN.md` §1.1 : module Benchmark (non suivi, 670 lignes), modifications de `Libraries/Makefile` et `demo/Makefile`, `implementation_plan_benchmark.md` indexé. Recommandation : tu les commites toi-même sur une branche `feat/benchmark`, ou je les commite sur une branche `chore/cleanup-wip` avant de commencer. Je ne commite pas sur `dev`.

**Q3. `main` local a 44 commits de plus que `origin/main`, et `dev` local 19 de plus que `origin/dev`.**
Rien n'est poussé. Est-ce voulu ? Je ne pousse rien dans M00.

**Q4. Structure cible : choix A (recommandé) ou B ?** Voir `docs/CLEANUP_PLAN.md` §3.

**Q5. Licence** du dépôt (aucun fichier `LICENSE` aujourd'hui) : laquelle ?

**Q6. Fichiers sans référence** `Libraries/includes/UI/UI.h` et `Libraries/includes/Graphics/Presentation/PresentationController.h` : les garder (règle 3 : pas de suppression sans accord) ? Recommandation : garder, signaler dans `docs/GAPS.md` à l'audit.

**Q7. Stashes (17) et anciennes branches** (`UI`, `feat/engine-init-tool`, `fix/makefile-lint-check-format`, etc.) : je n'y touche pas ; veux-tu un tri ?

## Audit (2026-11-17) : désaccords, ambiguïtés et choix à valider

Chaque point est une **proposition**, rien n'est appliqué. Références : `docs/AUDIT_REPORT.md`, `docs/PORTABILITY_REPORT.md`, `docs/UI_LANGUAGE_AUDIT.md`.

**A1. Le plan suppose un seul backend existant, il y en a deux.** `docs/05` (M2) migre le rendu OpenGL « derrière le RHI », M4 écrit Vulkan. Dans le code, un backend Vulkan fonctionnel (2 500 lignes) et un OpenGL existent déjà derrière une autre abstraction (`IGraphicsDevice`). Proposition : M2 réécrit l'interface (handles, `CommandEncoder`, groupes) et porte **les deux** backends existants à parité de tests ; M4 devient parité/VMA/cache disque/validation. Alternative : conserver l'abstraction actuelle jusqu'à M3. *Décision demandée.*

**A2. Arborescence et nommage de `docs/03`.** `docs/03` §2 : `include/orosmo/<module>/*.hpp`, fichiers en `snake_case`, fonctions en `camelCase`. Le code : `include/<Module>/*.h`, fichiers en `PascalCase`, méthodes en `PascalCase` (`GetInstance`), macros `LOG_*`. `CLAUDE.md` demande de suivre l'existant. Proposition : **garder l'existant**, l'inscrire dans `CLAUDE.md`, et n'ajouter `engine/` que si l'API est publiée à des tiers. *Choix demandé.*

**A3. `-Wall -Wextra -Wpedantic -Werror`** (`docs/03` §7) : le build actuel émet 6 avertissements (`-Wmissing-field-initializers`). Proposition : corriger puis activer `-Werror` en M0 pour le code du dépôt (pas pour les bibliothèques tierces).

**A4. TEST-01 : Catch2 ou doctest.** Le dépôt a un framework maison (`tests/framework`, 102 tests). Proposition : garder le framework maison tant qu'il suffit, ou migrer vers doctest (un seul en-tête) en M0 en conservant les tests. *Choix demandé.*

**A5. Dépendances à ajouter.** JSON (CORE-12, UI-07, GAME-05) : nlohmann-json, simdjson ou autre ; SPIRV-Cross et VMA (`docs/03` §3-5) ; éventuellement `doctest`, `entt`, `meshoptimizer`. Aucune n'est dans `vcpkg.json`. *Liste à valider avant M1.*

**A6. TOOL-03 (P2) requis par RHI-14 (P0).** Les tests d'images de référence exigent la capture. Proposition : placer TOOL-03 en M2.

**A7. Identifiants sans jalon dans `docs/05`** : `R2D-04` (M5 proposé), `RHI-17` (M5), `CORE-03` (M5), `CORE-05` (M1 pour le CPU, M2/M4 pour le GPU), `CORE-08` (M3), `PLAT-05` (M3, champ de texte), `TOOL-03` (M2). `docs/05` cite en M1 « CORE-02 à CORE-13 » **et** dit de placer `CORE-03/05/08` au premier usage : contradiction légère, tranchée comme ci-dessus.

**A8. `world` sans `renderer`.** `docs/03` §1 l'impose ; le terrain de la démo (`apps/demo/src/ProceduralGeneration/TerrainGenerator`, `Grid`) possède un `Mesh`. Proposition : séparer données (grille de hauteurs, déterministe, sans GPU) et maillage dès M3.

**A9. Cycles `Graphics` ↔ `UI` ↔ `Profiler`** (règle 5). Proposition : sortir `Profiler` de `Graphics/Core` (ou l'inverser par une interface), déplacer `TextRenderer` sous `renderer`, avant M2. *Accord demandé pour un jalon court « M1 bis ».*

**A10. Singletons.** `docs/03` §7 : « pas de singleton global caché ». Le code en a six (`GraphicsRuntime`, `ShaderLibrary`, `UI::Manager`, `InputManager::GetInstance`, `Logger`, `GraphicsRenderState`). Proposition : les remplacer pendant M2 par un contexte explicite, `Logger` excepté.

**A11. Capacités Vulkan annoncées à tort** (`MakeAdvertisedCapabilities`) et minuterie GPU Vulkan factice : violent les règles 3 et 11. Proposition : les rendre honnêtes immédiatement (correctif `fix/`), avant M2, sans attendre. *Autorisation demandée : c'est du code existant.*

**A12. Graine par horloge.** `Noise()` et `PCGRandom::init()` s'initialisent avec `high_resolution_clock` (règle 9). Proposition : constructeurs sans graine supprimés, graine explicite partout (`fix/` + test).

**A13. Langage d'interface** (`docs/UI_LANGUAGE_AUDIT.md`) : (a) garde-t-on Rust ? (le build exige `cargo`) ; (b) syntaxe des événements : `onClick: startGeneration` (nom de fonction, comme `docs/03` §12) ou `.onClick(startGeneration)` (modificateur, cohérent avec la grammaire actuelle) ; (c) correction du nom d'en-tête généré (vue ≠ fichier) ; (d) `docs/UI_DSL_Spec.md` (anglais, v0.2) vs `docs/UI_LANGUAGE.md` (UI-11) : traduire/fusionner ?

**A14. CMake.** BLD-01 invite à *proposer* CMake avec presets en gardant le Makefile en enveloppe. Le Makefile actuel (550 lignes, branches PowerShell) est le principal risque de portage. Proposition : en M0, prototype CMake limité au moteur + tests, comparé au Makefile avant décision. *Décision demandée.*

**A15. Ajustement de jalons** : voir AUDIT_REPORT § « Proposition d'ajustement du plan » (M2 plus gros ; CI Linux de tests dès M0 ; casser les cycles avant M2).

**A16. `main` locale** a 44 commits de plus que `origin/main` ; `dev` locale en a plus de 50 de plus que `origin/dev`. Rien n'est poussé (Q3 non tranché).

## Décisions de l'auteur sur l'audit (2026-11-17) et suites en M0

| Point | Décision | Application |
| --- | --- | --- |
| A1 | Suivre le plan : OpenGL jusqu'en M3, Vulkan seulement à partir de M4 | M0 ne touche pas à Vulkan (sauf deux contournements de compilation). `--capture` refuse Vulkan jusqu'en M4. **À confirmer en M2** : que devient l'ancien backend Vulkan pendant M2-M3 (retiré du build, ou laissé tel quel sans garantie) ? |
| A2 | Suivre `docs/03` même si cela change beaucoup | Inscrit dans `CLAUDE.md` (« Conventions de code »). Le code neuf suit `docs/03` ; le code existant migre **module par module au jalon qui le réécrit** (M1 core, M2 rhi/platform/renderer…). **Question A17 ci-dessous.** |
| A3 | (non tranché) `docs/03` §7 demande `-Werror` | Appliqué (`-Wall -Wextra -Wpedantic -Werror`, `WERROR=0` pour désactiver) |
| A4 | (non tranché) Catch2 ou doctest | **doctest** (un seul en-tête, `docs/02` TEST-01) ; framework maison supprimé |
| A5 | Ajouter les dépendances, dire à l'auteur quoi faire à temps | M0 ajoute **doctest** : `sudo apt install doctest-dev` (Linux) ; `vcpkg.json` le liste pour Windows/macOS |
| A6 | TOOL-03 passe en P0 | `docs/02` modifié ; fait en M0 (capture PNG + outil de comparaison) |
| A11 | Corriger en temps voulu, selon le plan | Rien changé : capacités Vulkan fausses et minuterie GPU factice restent notées (`GAPS.md`) pour M2/M4 |

**A17. Quand migrer l'arborescence et les noms vers `docs/03` ?** Option 1 (recommandée) : au fil des jalons, comme écrit plus haut. Option 2 : une migration de masse dédiée avant M1 (`include/orosmo/<module>/*.hpp`, fichiers en `snake_case`, fonctions en `camelCase`), qui touche ~19 000 lignes mécaniquement mais fait passer tous les `#include` et tous les appels ; je la ferais avec les tests comme filet. *Dis-moi si tu préfères l'option 2.*

**A18. Windows et macOS dans la CI.** Les jobs correspondants de `.github/workflows/ci.yml` n'ont jamais tourné (aucune machine disponible pendant M0) : ils sont en `continue-on-error`. Quand tu auras vu un premier run, dis-moi ce qu'il affiche pour que je corrige et retire ce drapeau.

**A19. Disque presque plein.** Pendant M0, `/` était à 100 % (97 Go sur 102) ; `~/vcpkg` pèse 7,5 Go (`buildtrees`, `downloads` supprimables avec `vcpkg` : `rm -rf ~/vcpkg/buildtrees ~/vcpkg/downloads`) et `~/.cache` 9,4 Go. Je n'ai rien supprimé chez toi, seulement mes fichiers temporaires.

## Revue CodeRabbit de la PR #4 : questions sur le plan

**A20. TEST-01 et la clôture de M0.** Le critère de TEST-01 (`docs/02`) inclut « couverture de `core` et `world` », que le plan traite en M1 (core) et M3 (world). M0 livre le framework (doctest), le build avec avertissements en erreur et la CI, pas la couverture. Proposition : dans `docs/05`, M0 clôt **« TEST-01 (framework et exécution en CI) »** ; la couverture de `core` et de `world` est suivie dans M1 et M3 sous les mêmes identifiants (`Partiel` jusque-là). Sinon, scinder TEST-01 en deux identifiants. *Décision demandée.*

**A21. UI-02 et le batch 2D (R2D-01).** UI-02 (M2) demande « un adaptateur les envoie au batch 2D (R2D-01) », mais le batch est planifié en M5. Proposition : en M2, l'interface produit ses **listes de dessin** et un adaptateur provisoire les dessine par le renderer actuel ; l'adaptateur vers le batch R2D-01 est livré avec R2D-01 en M5. UI-02 reste `Partiel` entre les deux. *Décision demandée ; aucun jalon n'est ajouté.*

**A22. REPO-01 rétrogradé en `Partiel`** (remarque CodeRabbit, juste : « comportement identique » exigeait des captures d'avant M00, qui n'existent pas, et l'exception `third_party/` n'est pas validée). Pour le repasser à `Fait` : valider l'absence de `third_party/` (rien de copié) et accepter que la preuve d'identité soit tests + journal de démarrage + capture reproductible. *Ton accord demandé.*

**A23. Dédoublonnage de la CI et validation du commit de fusion.** CodeRabbit (2e passage) objecte que `pull_request` teste le **commit de fusion** `dev` + `main`, pas la tête de `dev` : le dédoublonnage que tu m'as demandé retire cette validation. Dans ton flux, `main` ne reçoit que des fusions de `dev`, donc le commit de fusion est identique à la tête de `dev` tant que `main` n'a pas divergé ; le risque est faible et le gain est de 12 minutes de macOS par push. **Je garde le dédoublonnage** ; si `main` reçoit un jour autre chose que `dev`, retire la condition `if:` des trois jobs de `.github/workflows/ci.yml`. *Ton avis demandé.*

## Réponses de l'auteur (2026-11-17, après M0) et suites

| Point | Décision | Application |
| --- | --- | --- |
| A17 | Option 1 : migration au fil des jalons | Inscrit (CLAUDE.md « Conventions de code ») |
| A20 | TEST-01 : framework et CI en M0, couverture de `core` en M1 et de `world` en M3, **sous le même identifiant** | `docs/05` modifié (M0 et M1) |
| A9 | Un **M1 bis** après M1 pour les nettoyages de couches avant M2 | `docs/05` : section « M1 bis » ajoutée |
| A22 | REPO-01 reste `Fait` (pas `Partiel`) | `STATUS.md`, `AUDIT_REPORT.md`, `GAPS.md` remis à `Fait` |
| A23 | Ne pas dupliquer un run déjà existant | Job `plan` dans `ci.yml` : saute le run de la PR si la base est contenue dans la tête **et** qu'un run de push existe pour ce commit |
| A19 | Compris : seuls des caches de compilation seront supprimés | Aucune action de ma part |
| A11 | Corriger en temps voulu, selon le plan | Rien à faire avant M2/M4 |
| Q5 | Licence à voir plus tard | Rien (note : un stash contient une GPL-3.0, voir `docs/notes/archive/README.md`) |
| Q3 / A16 | Aligner `main` sur le remote | `main` locale ramenée sur `origin/main` ; **rien n'est perdu** : `main` locale (44 commits d'avance) était entièrement contenue dans `dev` |
| Q7 | Analyser puis supprimer les stashes | Analysés ; 2 archivés ; patchs complets hors dépôt ; suppression en attente de ton « go » |
| A18 | macOS : on ne se bloque pas dessus | macOS reste `continue-on-error` ; on le retouche à chaque PR |

**A5 (JSON) : décidé le 2026-11-17 par l'auteur : nlohmann-json** (simdjson plus tard, seulement pour les gros fichiers en lecture seule, ex. glTF, si les mesures le justifient). Ajouté à `vcpkg.json` (toutes plateformes, en-têtes seuls).

**A12 (graine) : décidé le 2026-11-17 par l'auteur** : constructeurs sans graine supprimés ; l'application peut choisir l'heure ou le hasard, mais **explicitement**, et journalise la graine. Fait en M1 (`fix/A12-graine-explicite`).

## Décisions prises en M1 bis, à valider par l'auteur

**A24. Comment `core` utilise le système sans dépendre de `platform`.** Le plan dit « sortir `<windows.h>` de `Logger`, `Utilities` et `FPSCounter` derrière une interface `platform` ». Or `core` est la couche 0 (`docs/03` §1) : il ne peut rien inclure au-dessus. J'ai donc fait un **port** : `include/orosmo/core/system.hpp` (dans `core`) *déclare* six fonctions, `src/platform/system.cpp` (nouvelle archive `libplatform.a`) les *définit*. Aucun `#include` ne remonte, aucun enregistrement à l'exécution, comportement identique, et une plateforme sans implémentation échoue à l'édition de liens. Alternative écartée : une interface virtuelle que `platform` installerait au démarrage (plus souple pour les tests, mais l'application doit penser à l'installer, et `Logger` fonctionnerait sans elle avec des valeurs par défaut différentes). **`std::filesystem` reste** dans `Vfs`, `FileWatcher`, `Config`, `TraceRecorder` : c'est la bibliothèque standard, pas un appel système, et l'envelopper dans `platform` ajouterait une couche sans autre bénéfice aujourd'hui. À confirmer ; si tu veux l'envelopper, c'est un jalon de plus.

**A25. Le RHI s'enregistre auprès des couches du dessous.** Pour supprimer les deux violations restantes (`Profiler` → RHI, `Window` → RHI), le haut dépend du bas par des interfaces définies en bas : `IGpuTimerFactory` (`include/Profiler/GpuTimer.h`) et `IWindowGraphicsBinding` (`include/Graphics/Presentation/WindowGraphicsBinding.h`). `BindGraphicsRuntime` installe les deux ; ils restent installés après `ClearGraphicsRuntime` (sinon `Window::Close` ne libérerait plus le contexte). C'est le même principe que celui prévu pour les backends (`docs/03` §3) ; à confirmer pour les futures couches (renderer, assets).

**A24 et A25 : validés par l'auteur le 2026-11-18.** Port système déclaré dans `core` et défini dans `platform` ; `std::filesystem` reste dans `core` ; les couches du bas définissent l'interface que le RHI installe (principe applicable aux futures couches).

## Décisions prises en M2 (2026-11-18)

**A1 (Vulkan pendant M2-M3) : décidé par l'auteur le 2026-11-18** : l'ancien backend Vulkan (ancienne abstraction `IGraphicsDevice`) est **retiré du build** et conservé en archive ; `--api vulkan` répondra « non disponible avant M4 » ; M4 le réécrit sur le nouveau RHI (VMA, validation). Le retrait est fait au lot B, quand les façades (`Mesh`, `Texture`...) passent sur le RHI.

**A27. Découpage de M2 : décidé par l'auteur le 2026-11-18** : trois lots dans `dev`, arrêt à la fin de chacun. A : interface RHI + backend OpenGL + tests GPU ; B : pipeline de shaders, migration éclairage/texte/UI, images de référence, isolation bloquante, retrait de l'ancien code ; C : `platform::Window`, mode sans fenêtre, DPI. M2 n'est « terminé » qu'après C.

**A26. Repère du RHI (à valider).** Le lot A adopte le **repère OpenGL** (origine du viewport en bas à gauche, Y vers le haut, première ligne lue = bas, profondeur −1..1) pour que la migration du rendu existant (lot B) donne la même image. Vulkan et Metal ont l'origine en haut. Deux options pour la suite : (1) garder ce repère comme **convention du RHI** et faire absorber la différence par les backends Vulkan/Metal (viewport à hauteur négative, `VK_KHR_maintenance1` du noyau 1.1) ; (2) passer au repère Vulkan (origine en haut) et faire absorber la différence par OpenGL (`glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE)`, exige GL 4.5 ou `GL_ARB_clip_control`, ce qui relève le minimum de 4.3). Pour la profondeur, REN-02 (reverse-Z, M3) demande 0..1, donc `GL_ARB_clip_control` de toute façon. **Recommandation : (1) pour les coordonnées d'écran et d'image, et 0..1 pour la profondeur dès M3.** *Décision demandée avant M3 ; sans réponse, le code suit (1).*

**Décision de l'auteur (2026-11-19) : option (2), repère Vulkan** (origine en haut, profondeur 0..1). À appliquer en M3, pas avant : le backend OpenGL passera par `glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE)` (OpenGL 4.5 ou `GL_ARB_clip_control`, à lire dans `Capabilities`, règle 11 ; le minimum 4.3 est relevé ou un repli est écrit), les shaders, les images de référence (`scene.png`, `ui.png`) et les tests de parité devront être régénérés avec la nouvelle orientation, et `docs/RHI.md` et `docs/03` mis à jour. Le lot A à C (M2) suit encore le repère OpenGL : c'est l'état livré.

**A28. Migration du rendu hérité : décidé par l'auteur le 2026-11-18** : **réécriture explicite** des façades (`Mesh`, `Texture`, `ShaderProgram`, `RenderTarget`, `Renderer`, `Sprite`, texte, interface) sur `Device` / `CommandEncoder`, **sans adaptateur d'état implicite** (l'option « adaptateur provisoire » a été écartée : elle n'aurait pas été rattachée à un jalon et risquait de devenir permanente). L'API publique des façades change ; la démo peut être incomplète entre sous-lots ; plan en B1-B6 dans `docs/05`.

**A29. Découpage de M3 (proposé et appliqué par l'IA le 2026-11-19).** M3 regroupe le terrain, le rendu, les outils,
l'interface et la release utilisable. Comme pour M2 (A27), il est découpé en six lots A à F (`docs/05`), dans l'ordre des
dépendances : bruit → tuiles → rendu → export/outils → interface et visualiseur → release. La portée ne change pas ; l'ordre et
le découpage sont une organisation du travail. Conséquence de A26 (repère Vulkan) appliquée au lot C : le backend OpenGL exige
`glClipControl` (OpenGL 4.5 ou `GL_ARB_clip_control`, lu dans les extensions) ; sans lui, la création du périphérique est
refusée avec un message clair (CLAUDE.md règle 11 : refus propre). *À contester si l'auteur préfère un autre ordre.*

**A26 : appliqué en M3 lot C (2026-11-19).** Repère du RHI : origine en haut à gauche (ligne 0 = haut), NDC y = +1 en haut,
profondeur [0, 1] ; OpenGL par `glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE)`, la fenêtre étant traitée à part par le backend ;
caméra en reverse-Z (plan lointain infini par défaut). Précision sur « repère Vulkan » : la profondeur [0, 1] et l'origine en haut
sont celles de Vulkan ; pour le sens de y en NDC, le RHI garde y = +1 en haut (projections usuelles de glm), et le backend Vulkan
retournera le viewport (hauteur négative, cœur 1.1). Les images de référence sont restées identiques au pixel près.


**A13 : choix par défaut appliqués en M3 lot E (2026-11-19), à contester.** Faute de réponse et pour livrer UI-05 : (a) le
compilateur **reste en Rust** (`tools/cui-rs`, déjà construit par le build et la CI ; le passage en C++ reste possible plus tard) ;
(b) un événement s'écrit comme un **modificateur qui nomme une fonction** : `.onClick(state.regenerate)` (appel direct) ou
`.onClick(regenerate)` (action retrouvée par nom dans le registre de l'interface, docs/03 §12) — cohérent avec la grammaire
existante ; jamais une expression ; (c) l'en-tête généré porte le nom du **fichier** (`st.cui` → `st.gen.h`), défaut corrigé pour
les deux cibles ; (d) `docs/UI_LANGUAGE.md` (français) est la spécification de la nouvelle cible `orosmo/ui` ;
`docs/UI_DSL_Spec.md` (anglais) reste celle de la cible héritée `UI::View`, utilisée par la démo.

**A30. Deux modules d'interface pendant la migration (M3 lot E, 2026-11-19).** Le module `orosmo::ui` de `docs/03` (§1 couche
`ui`, §12) est écrit à neuf (`include/orosmo/ui/`, `src/ui/`) : widgets, mise en page, thèmes JSON, événements, liste de dessin. Il
ne remplace pas encore l'interface héritée `UI::` de la démo (`PerformanceView`, parité de `ui.png`) : comme pour le rendu en M2,
l'ancien code reste jusqu'à sa bascule, sous `src/ui/legacy/` (déplacé de `src/UI/` : deux dossiers qui ne diffèrent que par la
casse sont impossibles sous Windows et macOS). Le langage choisit la cible par `import "orosmo/ui"`. *Proposition : basculer la
démo et retirer `UI::` dans un jalon ultérieur (M5, avec le batch 2D R2D-01), à confirmer.*

**A31. API de `platform` et version de la release de M3 (2026-11-19, proposition).** Le lot F demande l'API publique figée et
nommée selon `docs/03` §7. `core`, `rhi`, `renderer`, `world` et `ui` le sont. `platform` (`Window`, `InputManager`) ne l'est pas :
la renommer casse la cible héritée du langage (`Get` + nom déduit par `generator.rs`) et la démo. Proposition : le guide déclare
`platform` **provisoire** (`docs/USING_THE_ENGINE.md` §7), le renommage est fait avec la bascule de la démo vers `orosmo::ui`
(A30), et la release de M3 porte le numéro **v0.3.0** (`mk/config.mk` ; 0.x : un changement mineur peut casser l'API, la liste
est dans `docs/releases/`). *L'étiquette est posée par l'auteur ; à contester.*

*A30 et A31, suite (2026-11-20)* : à la demande de l'auteur, l'interface héritée `UI::` et la cible `UI::View` du langage sont
supprimées, et la démo passe sur `orosmo::ui` et `orosmo::world` (nettoyage « Orosmo », `docs/releases/M3.md`). `platform`
reste provisoire (G96).

**A32. Nom Orosmo : jusqu'où renommer ? (2026-11-20, tranchée).** L'auteur a renommé le projet **Orosmo**. Fait d'abord : nom
affiché (`mk/config.mk`, bannière de l'outil, README, CLAUDE.md, SDK, extension VS Code, dossier de données `Orosmo/`). Proposition
initiale : garder l'espace de noms, le dossier des en-têtes et l'outil `engine`, renommer l'outil seulement à M4. **Décision de
l'auteur (2026-11-20)** : tout renommer tout de suite, aucun projet extérieur n'utilisant encore le moteur : outil `orosmo`
(`orosmo init`, `orosmo update`, fichiers `orosmo-<plateforme>[.exe]` et `orosmo-pkg-<plateforme>.zip`, branche
`chore/rename-cli-orosmo`), espace de noms `orosmo::`, en-têtes `include/orosmo/<module>/` (`#include <orosmo/core/log.hpp>`),
dossier du paquet dans un projet `orosmo/` (`apps/demo/orosmo/` dans le dépôt), import `.cui` `import "orosmo/ui"` (branche
`chore/rename-namespace-orosmo`). Restent nommés « engine » : les variables de build `ENGINE_*` (`ENGINE_DIR`…), le fichier
`make/engine.mk` du SDK et les dossiers d'outils `tools/engine-sdk/`, `tools/engine-init-rs/` (internes).
