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

**A33. `generateRegion` hors des limites d'int64 : région vide ou erreur ? (2026-11-20, proposition).** Revue CodeRabbit 3 de la
PR #8 : une région dont le dernier indice global dépasse int64 est maintenant refusée avant toute allocation
(`fix/M3-review3-world`). Pour ne pas changer l'API figée de 0.3.x, `generateRegion` renvoie alors un `HeightField` vide
(largeur et hauteur 0, documenté dans `export.hpp`), que tous les exports refusent ensuite (`kInvalidArgument`). Plus propre :
`Result<HeightField>` (règle 10), au prix d'un changement incompatible (`terrain-gen`, `terrain-viewer`, une dizaine de tests).
Proposition : garder le champ vide en 0.3.x et passer à `Result` à M4 avec les autres changements d'API. **Décision de l'auteur (2026-11-21) : oui** (`docs/05`, M4).

**A34. Périmètre de la release : moteur générique seul, modules procéduraux hors release + jalon Modules/Addons (2026-11-20, tranchée).**
**Décision de l'auteur (2026-11-20)** :
1. La **release utilisable** (`orosmo-pkg-<plateforme>.zip`, outil `orosmo init`) ne doit contenir **que le moteur générique**
   (`core`, `platform`, `rhi`, `renderer` sans `TerrainRenderer`, `ui`, `thirdparty`, compilateur `cui`, shaders/ressources
   génériques, template de projet générique, exemples `minimal` et `ui_gallery`).
2. La **génération procédurale** déjà développée (`orosmo::world`, `TerrainRenderer`, shaders `terrain.*`, `apps/demo`,
   `apps/terrain_gen`, `apps/terrain_viewer`) reste dans le dépôt pour les démos internes et la CI (via `libworld.a` +
   `libterrain_renderer.a` et `ENGINE_EXTRA_MODULES`), mais n'est **pas** packagée dans la release : le développement procédural
   continuera dans un autre projet.
3. **Pour plus tard** : un nouveau jalon (**M11 · Système de modules et d'addons**, IDs `MOD-01` à `MOD-04` dans `docs/02` et
   `docs/05`) est ajouté afin de pouvoir intégrer facilement des modules d'aide au développement, soit **inclus directement dans
   la release** (`bundled`), soit **distribués en addons séparés** installables sur une release cœur.

**A35. Version de la release M3 bis (2026-11-21, tranchée).** M3 est proposée en `v0.3.0` avec `world` dans le paquet ; M3 bis
retire `world`, `TerrainRenderer` et les exemples terrain du paquet (changement incompatible pour qui les utiliserait, personne à
ce jour). Proposition : `v0.3.1` (aucun utilisateur extérieur ; `v0.4.0` reste pour M4). Les fichiers de version (`mk/config.mk`,
`vcpkg.json`, `Cargo.toml`) restent à `0.3.0` tant que l'auteur n'a pas tranché. **Décision de l'auteur (2026-11-21)** : `v0.3.1`, et **pas de release pour M3** (`v0.3.0` n'est pas publiée : la première
release publiée est celle de M3 bis) ;
fichiers de version passés à 0.3.1 (`fix/M3bis-macos-intel`).

## Décisions de l'auteur du 2026-11-21 (après M3 bis)

- **A33** : champ vide en 0.3.x, `Result<HeightField>` à M4 (voir plus haut ; `docs/05`, M4).
- **A18, suite** : Windows, macOS et Linux arm64 passent en CI, y compris `e2e-init` : ces jobs deviennent **bloquants** (plus de
  `continue-on-error`) et les avertissements y sont des erreurs (`-Werror`) ; il a fallu corriger neuf avertissements de clang
  (destructeur de `FPSCounter` sans `noexcept` dans l'en-tête, `kVersion` inutilisé dans des tests, capture de lambda inutile). Le
  build de release (`build-engine.yml`) garde `WERROR=0` hors Linux x64.
- **A36. Plage d'un champ entier réfléchi (décidée le 2026-11-21).** Pour un champ entier, `range(min, max)` est faite
  **d'entiers**, dans le type : `range(1, 3)`, pas `range(0.5, 3.5)`. Sinon le champ est **refusé** : erreur journalisée et pastille
  « plage invalide » à la place du contrôle, rien ne peut être écrit (`reflectedPanel`, `tests/ui/review3_widgets_tests.cpp`).
  Avant : la plage était corrigée en silence (`ceil` / `floor`).
- **A31, suite** : le renommage de `platform` (`Window`, `InputManager`) se fait **au début de M4** (`docs/05`, G96, G105).
- **Q7** : les 17 stashes sont supprimés (analyse du 2026-11-17 : 2 idées archivées dans `docs/notes/archive/`) ; sauvegarde
  complète hors dépôt (G24). **Les branches ne sont pas supprimées.** Le worktree `release-core-only` est supprimé (sa branche reste).

## Décisions de l'auteur du 2026-11-21 (début de M4)

- **A7** : close (identifiants placés au premier usage, comme proposé).
- **A8** : close (`world` sans `renderer` : fait en M3, `HeightField` sans GPU).
- **A10** : on **garde** `platform::InputManager::getInstance(nativeWindow)` (un gestionnaire par fenêtre native, exigé par les
  rappels C de GLFW) et le `Logger`. Avis de l'agent : d'accord ; le seul gain d'un contexte explicite serait de ne plus passer par
  une table globale, et `Window` donne déjà son gestionnaire. Le code neuf passe par la fenêtre plutôt que par `getInstance`.
  Les autres singletons de la liste (`GraphicsRuntime`, `ShaderLibrary`, `UI::Manager`, `GraphicsRenderState`) ont disparu en M2/M3.
- **A13** : close, le compilateur `cui` **reste en Rust** (`cargo` reste une dépendance de build). Les points (b) à (d) ont été
  tranchés en M2 (`docs/UI_LANGUAGE.md`).
- **A14** (CMake) : **reportée à M9** (portage macOS/Metal), le Makefile reste le build de référence d'ici là. **Précision (v0.4.1)** : un support CMake **optionnel** est livré avec le SDK (`OrosmoConfig.cmake`, `find_package(Orosmo)`), sans migrer le build : le Makefile reste la référence, ce qui est cohérent avec cette décision ; le prototype complet (presets, moteur et tests) reste à M9.
- **A15** : close (les ajustements de jalons ont été intégrés à `docs/05`).
- **A21** : l'auteur demande une explication. En clair : l'interface ne dessine plus elle-même, elle remplit une **liste de
  dessin** (`DrawList` : « rectangle ici, texte là ») que `CanvasRenderer` exécute commande par commande (B5, M2, fait). Le
  **batch 2D** (R2D-01, M5) est une optimisation : regrouper ces commandes en quelques dessins instanciés. A21 demandait seulement
  d'accepter que UI-02 reste `Partiel` jusqu'à M5, puisque la liste est déjà là mais pas encore le batch. **Close** par l'auteur
  (2026-11-21) : UI-02 reste `Partiel` jusqu'au batch 2D de M5.


## Décision de l'auteur du 2026-11-21 : registre d'addons (A38)

- **A38** : le système d'addons de M11 se limite au **mécanisme** (manifeste, empaquetage, installation locale ou par URL, liaison au
  build). L'**hébergement** des addons et l'**interface** pour les parcourir et les installer forment un jalon à part, **M12**
  (`MOD-05` à `MOD-08`, `docs/05`) : c'est un service et une application distincts du moteur. Le manifeste de M11 porte déjà les
  champs d'affichage du registre. Le premier addon visé est le monde procédural archivé (`archive/procedural-world`). Forme du
  registre (dépôt Git avec releases ou service web) : à proposer au début de M12. Version visée : M11 `v1.1.0`, M12 `v1.2.0`.
- **A39** (2026-11-21) : **M7 est supprimé** (numéro non réattribué). Les effets de rendu génériques (`REN-23`, `25`, `33`, `14`, `27`,
  `38`, et `24`, `26` à confirmer) deviennent des **modules préinstallés** dans un nouveau jalon **M13**, après le système de modules
  (M11) ; le terrain et les planètes (`TER-05` à `TER-10`) sortent du moteur. Versions : M4 `v0.4.0`, M5 `v0.5.0`, M6 `v0.6.0`,
  M8 `v0.7.0`, M9 `v0.8.0`, **M10 `v1.0.0`**, M11 `v1.1.0`, M12 `v1.2.0`, M13 `v1.3.0` (la version suit l'ordre des jalons livrés, pas leur numéro).


## Décisions de l'auteur du 2026-11-21 : moteur générique et démo unique

- **A37. Suppression du code procédural du dépôt (archivé) et consolidation en une démo unique (2026-11-21, tranchée).**
  1. Tout le code procédural (`orosmo::world`, `TerrainRenderer`, `shaders/rhi/terrain.*`, `apps/terrain_gen`,
     `apps/terrain_viewer`, tests `tests/world/` et `terrain_render_tests.cpp`, baseline terrain) est archivé intact dans la
     branche inutilisée `archive/procedural-world` (issue de `dev` à `78dbb5e`, qui inclut `feat/M4-platform-rename` et
     `feat/M4-region-result`), puis retiré du dépôt principal.
  2. Le dépôt ne conserve qu'**une seule démo** (`apps/demo/`) : diorama 3D statique écrit en C++ (sans `.obj`, en attendant
     l'import d'assets en M6) + vitrine complète d'`orosmo::ui` et des fonctionnalités moteur (`Camera`, `LightManager`,
     `DebugDraw`, `Profiler`, `TaskSystem`, `Reflect`/JSON, `ThemeHost`/`FileWatcher`). `apps/ui_gallery`, `apps/core_demo` et
     `apps/rhi_demo` sont supprimés.
  3. **Ergonomie** : `Échap` (ainsi que `F1`) ouvre et referme l'interface complète (ne ferme plus l'application) ; un bouton
     `Quit` et `File > Quit` dans l'UI ferment l'application.
  4. `minimal` est déplacé dans `tools/engine-sdk/examples/minimal/` (seul exemple dans `dist/examples/minimal/`), et
     `orosmo init` conserve son projet starter générique minimal (`tools/engine-sdk/template/src/`).
- **A40. Frontière cœur vs futurs modules M11 et modèles de démarrage (2026-11-21, tranchée).**
  1. Le cœur du moteur garde le rendu de base (maillages, PBR, ombres, post-traitement, brouillard simple `REN-25`, 2D, UI,
     assets, animation, ECS, logique rechargeable). Les fonctionnalités d'environnement/monde (`REN-23` bruit compute,
     `REN-24` atmosphère planétaire, `REN-26` nuages, `REN-27` eau, `TER-*`, `SIM-02`, `SIM-03`) ne sont pas implémentées pour
     l'instant et sont destinées à devenir des modules utilisables inclus/activables via le système de modules **M11**.
  2. Le jalon **M11** est complété par `MOD-09` : choix entre plusieurs modèles de démarrage à `orosmo init` (`minimal`,
     `starter`, `demo` pour comprendre le moteur) et possibilité pour l'utilisateur de définir un de ses projets comme modèle
     par défaut.

## Question ouverte du 2026-11-21 (fusion de la démo unique)

- **A41. Effets de rendu : cœur ou modules préinstallés ?** Deux décisions de l'auteur se recoupent. D'après A39 (message à l'agent) :
  **tous** les effets (`REN-23`, `24`, `25`, `26`, `27`, `14`, `33`, `38`) deviennent des modules préinstallés (M13), M7 est supprimé.
  D'après A40 (rapport de la session de Gemini) : `REN-25` (brouillard), `REN-14` (AO), `REN-33` (particules) et `REN-38`
  (réfraction) **restent dans le cœur** ; seuls `REN-23`, `24`, `26`, `27` et `TER-*` / `SIM-02` / `SIM-03` deviennent des modules.
  `docs/05` applique A39. **Tranchée par l'auteur (2026-11-21) : modules préinstallés dans M13** (tous les effets, y compris `REN-25`, `14`, `33`, `38`) ; A40, point 1, est remplacé en conséquence.
- **A42** (2026-11-21) : pour l'interface, en M5 : aucun dessin pour un élément caché ou hors de sa zone visible, et cache du rendu
  par couche pour qu'une interface inchangée ne soit pas redessinée (`UI-13`, `docs/05` M5). Le sélecteur de couleur avec roue au clic
  (UI-03) est aussi reporté à M5 (branche `feat/UI-03-color-popup`, commencée).

## Décisions et propositions de M5 (à confirmer par l'auteur)

Choix faits pendant le jalon M5 (`docs/releases/M5.md`). Aucun n'est une décision de l'auteur : chacun est **à confirmer par l'auteur**. Numérotation : A44 à A66 (A43 est pris ; A57 à A60 viennent de la revue de la PR #12 ; A61 à A69 : performances).

- **A44. Sémantique du graphe de passes (`docs/03` §11).** `RenderGraph` : `read` voit la version laissée par le dernier écrivain déclaré avant la passe, et l'écrivain suivant attend cette passe (écriture après lecture) ; sans écrivain avant elle, la passe voit le contenu final ; `readFinal` voit toujours le contenu final (après tous les écrivains de l'image) ; une ressource importée sans écrivain est lue telle quelle. Ce comportement est dans `render_graph.hpp` et `src/renderer/README.md`, pas dans `docs/03` : à y ajouter ou à corriger (G152).
- **A45. Dessins enregistrés puis joués à `endFrame`.** `SceneRenderer::drawMesh` enregistre ; les dessins sont joués dans la passe `scene` à `endFrame` (les tampons d'un maillage doivent rester valides jusque-là). `drawCustom` et `sceneDrawInfo` sont le point d'extension provisoire des dessins qui ne sont pas des `Mesh` (`InstanceRenderer`, `GpuDrivenFeature`). Garder, ou remplacer par une liste de dessins du graphe ?
- **A46. Liaisons réservées aux plugins.** `RenderGraphBuilder::addFrameBinding` : liaisons 3 à 7 du groupe 0 (forward+ : 3 et 4) ; ombres : liaisons 6 et 7 du groupe 1 des matériaux récepteurs. Le RHI n'a que 3 groupes (`rhi::kGroupCount`) : il faut une règle d'attribution si d'autres plugins arrivent.
- **A47. Pré-passe de profondeur dans `SceneRenderer`** (`setDepthPrepass`), pas dans un plugin : elle doit précéder le plugin de clusters et partage les maillages de la scène. Convention des push constants de `mesh.frag` : le premier entier (`wireframe`) vaut 2 pour la pré-passe (couleur masquée, éclairage sauté), 1 pour le filaire.
- **A48. Variantes de shaders par macros** (`OROSMO_SHADOWS`, AOV, `OROSMO_FORWARD_PLUS`, `OROSMO_TRANSPARENT`), choisies par l'application (`meshDefines`, `receiverProgramWith`, `aovShaderDefines`, `forwardPlusDefines`). Le renderer devrait-il les sélectionner seul à partir des plugins actifs ?
- **A49. Points d'extension de `RenderFeature`.** `wantsJitter()` (nouveau : un plugin demande le jitter de Halton) ; HDR demandé par un réglage explicite (`setSceneColorFormat`, `PostEffects::sync`) plutôt que par un `requiresHdrSceneColor()` du plugin (qui n'existe pas) ; ordre des plugins = ordre d'enregistrement (`addTemporalAntiAliasing` avant `addPostEffects`, `addEdgeAntiAliasing` après), sans API d'insertion à une position donnée. À décider.
- **A50. Ajouts au RHI** (deux faits en M5, pendant la revue de la PR #12 : `Capabilities::drawIndirectFirstInstance` et `CommandEncoder::id()`, voir A57 ; le reste est à faire) : `alphaToCoverage` et nombre d'échantillons (`GraphicsPipelineDesc`, `TextureDesc`) et résolution de cible (MSAA, REN-19, REN-37) ; `drawIndexedIndirectCount` (REN-29, G154) ; un numéro d'image dont la soumission est confirmée (G172) ; `depthBias` (REN-08) ; copie texture à texture (cache d'ombres) ; libération différée par série d'encodeur (G131, G155). À planifier (M8 ?).
- **A51. `shaders/include/` : nouvelle racine d'inclusion** (`shaders/include/orosmo/*.glsl`, `#include "orosmo/math.glsl"`), à ajouter aux `includeRoots` de `ShaderLibrary` des projets utilisateurs ; à documenter dans `docs/USING_THE_ENGINE.md` si acceptée.
- **A52. `InstanceMaterial` provisoire** (`instance_batch.hpp`) : à remplacer par le futur type `Material` de `docs/03` (REN-10, M6).
- **A53. `ModelBlock` de 128 à 208 octets** (`src/renderer/mesh.cpp`, `static_assert`) : ajout de `prevModel` pour les vecteurs de mouvement par objet (REN-21). Coût : 80 octets par objet et par dessin.
- **A54. UI-13 et A42 : cache par couche de l'interface.** Recommandation : **reporter à M6**. Le batch 2D a ramené l'interface de 143 à 18 draw calls (onglet widgets, `--ui full`), l'élimination des éléments cachés et hors découpage est faite et testée ; le gain restant d'un cache est faible face au risque d'invalidation (liaisons, survol, animation, thème). UI-13 reste Partiel jusque-là (G165).
- **A55. Taille du jalon et version.** M5 est un jalon de grande taille (26 identifiants de rendu et de 2D, plus UI-13 et le sélecteur de couleur de UI-03) livré en **une seule merge request** ; version **`v0.5.0`**.
- **A56. Critère de REN-22 (`docs/02`).** Le critère cite l'atmosphère ; elle est le module REN-24 (M13, A39). Proposition : retirer « atmosphère » du critère de REN-22, qui passerait alors à Fait (preuves : `tests/renderer/shader_toolbox_tests.cpp`). En attendant, REN-22 est Partiel (G168).
- **A57. Ajouts d'API faits pendant la revue de la PR #12.** Nécessaires aux correctifs, non prévus par `docs/03` §11 : `RenderFeature::frameEnded()` (appelé à la fin de chaque image, réussie ou non) ; `FrameResources::modelGroup` et `SceneRenderer::recordModelSlice` (tranche du modèle par dessin) ; `SceneRenderer::frameSerial()` (compteur des `beginFrame` réussis) ; `CullView::forward` (taille à l'écran mesurée à la profondeur de vue, identique sur CPU et GPU) ; `GpuScene::unsentRanges()` ; `ObjectSet` n'est plus réaffectable ; le bloc `GpuCullParams` passe de 240 à 256 octets ; les `IPostProcessPass` sont déclarés après les plugins (ils s'exécutent après tous les écrivains de la scène). **Dans le RHI** : `Capabilities::drawIndirectFirstInstance` (Vulkan : la fonction du périphérique ; OpenGL : toujours vraie), qui ferme G145 ; `CommandEncoder::id()`, identifiant unique dans le processus et jamais réutilisé (code commun `src/rhi/device.cpp`, aucun backend modifié) : l'arène des lots 2D (`BatchArena`) reconnaissait un nouvel encodeur à son adresse, que l'allocateur redonne souvent à l'encodeur de l'image suivante ; elle ne repartait alors jamais du début et son tampon grandissait sans fin. À confirmer.
- **A58. Cache disque des atlas MSDF.** Le gel du premier affichage venait de la génération des atlas de glyphes (0,7 à 1,1 s par taille de police) ; elle est maintenant parallèle et faite par la préchauffe de la démo, mais coûte encore 1,3 à 1,5 s au démarrage (G177). Proposition : un cache disque des atlas, clé = hachage de la police, taille, jeu de glyphes et version de msdfgen ; à défaut, préparer les atlas en tâche de fond (il faut un `TextRenderer` sûr entre threads). À planifier (M6 ?).
- **A59. Ombre des matériaux transparents par défaut.** `defaultTransparentShadowMode` renvoie `kDithered` pour `Blend`, `Premultiplied` et `Transmission` (ombre partielle proportionnelle à l'opacité). CodeRabbit propose `kFull` ou `kNone`. Le comportement actuel est conservé (code, test et documentation concordent) : garder `kDithered` ?
- **A60. Budget de `make demo-vulkan-check` pour le cas `hud`.** Le panneau de performances affiche des valeurs mesurées et le nom du pilote : environ 6,6 % de pixels diffèrent entre OpenGL et Vulkan. Le budget de ce seul cas passe de 2 % à 10 % (`tools/demo_parity.py`) ; la scène et les autres cas restent à 2 %. L'autre solution serait de comparer ce cas avec `--hud off`, mais le rendu du panneau ne serait plus vérifié. Garder 10 % ?
- **A61. Optimisations de performance (décision de l'auteur, 2026-11-22).** Mesures et liste : analyse de fin de M5 (release, Intel Arc). Retenus : **A** synchronisation verticale activée par défaut, avec limite de FPS optionnelle, désactivable, et une fonction pour changer le mode à chaud, reliée à l'interface de la démo ; **B** données par image en mémoire écrite par le CPU (`MemoryKind::kUpload`), anneau par image en vol et décalages dynamiques ; **E** minuteries GPU actives seulement quand le panneau affiche des temps GPU, avec une fonction pour les forcer ; plus les optimisations internes sans changement de comportement (transferts Vulkan permanents, barrières groupées, une soumission par image, pools de commandes par image, surveillance des thèmes sur l'horloge réelle, OpenGL sans réécriture d'un tampon en vol, copies plein écran inutiles supprimées, clés du Profiler précalculées). **F** : `make run` garde son comportement (lance le binaire compilé le plus récemment, debug ou release ; `make run-release` pour la release). **Reportés** : **G** trois images en vol (plus tard, M8) ; **C** graphe de passes mis en cache (M8, contrat de `RenderFeature`, A44/A49) ; **D** cache de l'interface par couche (M6, A54).
- **A62. API ajoutée par les optimisations (A61), à confirmer.** `renderer::FramePacer` et `FramePacing` (cadence : vsync, limite, sans limite ; placé dans `renderer`, découplé de la fenêtre et du périphérique par deux fonctions, `docs/03` ne le prévoyait pas) ; `core::Profiler::Key`, `MakeKey`, `SetGpuTimingsEnabled`, `GpuTimingsEnabled` ; `core::FileWatcher::setPollInterval`, `setFullScanEvery`, `stats` ; `renderer::FrameRing`, `kDefaultFramesInFlight`, `SceneRendererConfig::framesInFlight`, `SceneRenderer::framesInFlight`, `frameRingBuffer` ; `Mesh::prepareTransform` ; constructeur `BatchArena(framesInFlight)`. **Comportements changés** : `SceneRenderer::setCamera` n'écrit plus au GPU (envoi à `endFrame`) ; `drawMesh` n'écrit plus le tampon propre du maillage (un appelant qui le lie lui-même appelle `uploadTransform`) ; `FrameResources::buffer` change d'une image à l'autre ; `updateBuffer` sur un tampon `kUpload` écrit directement en mémoire quand le GPU ne l'utilise plus (contrat dans `include/orosmo/rhi/types.hpp`). Démo : vsync activée par défaut (`--vsync on|off`, `--fps-limit N`), minuteries GPU automatiques (`--gpu-timings auto|on|off`).
- **A63. Rendre la scène directement dans la sortie (fait à la demande de l'auteur, 2026-11-23)** : `SceneRenderer::setRenderIntoOutput(bool)`, désactivé par défaut, supprime la copie `upscale` 1:1 seulement quand rien d'autre n'a besoin de la cible de scène (pas de mise à l'échelle ni de mode, d'historique, de génération d'images, de post-traitement ni d'AOV ; sortie de même format et taille). La couleur de scène vue par les plugins est alors la sortie (même ressource du graphe) ; `saveColorPng` est refusé pour une telle image. La démo l'active quand l'image se compose dans le backbuffer, hors capture, en Vulkan ou sans fenêtre (le framebuffer de fenêtre OpenGL refuse la profondeur de la scène). À confirmer : la couleur de scène des plugins devenant la sortie (un plugin qui lit la scène avec `readFinal` passe maintenant après les écrivains de la sortie, interface comprise).
- **A64. `glBufferStorage` hors de `Capabilities`** : la disponibilité du stockage persistant OpenGL (cœur 4.4 ou `GL_ARB_buffer_storage`) est lue dans le backend, comme un détail d'implémentation de `kUpload` sans effet visible (repli `glBufferSubData` ; `ENGINE_GL_BUFFER_STORAGE=0` le force). Règle 11 : garder ainsi, ou l'exposer dans `Capabilities` ?
- **A65. Suivi de `kUpload` par plage** (proposition) : suivre le dernier usage GPU par plage plutôt que par tampon permettrait l'écriture directe dans un tampon unique découpé par image (G183).
- **A66. Allocation par image ouverte aux plugins** (proposition) : une allocation dans l'anneau de `SceneRenderer` (décalage dynamique ou groupe par zone) pour les tampons par image des plugins (G184).
- **A67. Vsync sous XWayland (tranchée par l'auteur, 2026-11-23 : voie a)** : GLFW 3.4 et plus vient de vcpkg sur toutes les plateformes, avec Wayland natif sous Linux (`vcpkg.json`, `glfw3[wayland]`, 3.5.1 vérifié) ; le paquet apt `libglfw3-dev` n'est plus utilisé. Conséquences : pas de position de fenêtre sous Wayland (non lue), la rotation de la caméra de la démo capture le curseur (`CursorMode::kDisabled`, mouvement relatif) au lieu de le replacer au centre, les tests passent par X11 quand XWayland est là (G191).
- **A68. Taille du tampon d'image lue par le RHI dans `platform`** (à confirmer) : `VulkanDevice::beginFrame` et `GlDevice::beginFrame` lisent la taille par `platform::framebufferSizeOf` (gardée par le rappel GLFW de `Window`) au lieu de `glfwGetFramebufferSize`, un aller-retour X11 par image. Le RHI (couche 2) dépend déjà de `platform` (couche 1) ; une fenêtre native sans `Window` est relue chez GLFW comme avant.
- **A69. Image de fond Vulkan = image de la chaîne (G185, demandé par l'auteur le 2026-11-23)** (à confirmer) : quand la surface propose RGBA8 UNORM, `beginFrame` rend l'image acquise de la chaîne ; son contenu ne survit pas d'une image à l'autre (avant : texture hors écran gardée). Le contrat du RHI ne promettait rien là-dessus ; le moteur et la démo redessinent toute l'image. `ENGINE_VK_DIRECT_SWAPCHAIN=0` rend l'ancien chemin. Les surfaces BGRA seules gardent la copie (des pipelines BGRA seraient une autre décision).
- **A43. Jalon M14 : site vitrine et wiki Astro, Docker et CI/CD (2026-11-21, tranchée sur le principe ; détails DevOps à trancher au début de M14).**
  1. Le numéro **M7** restant non réattribué (A39), le nouveau jalon est ajouté en **M14** (`WEB-01` à `WEB-05`).
  2. Portée : un **wiki** créé avec **Astro** (utilisation du moteur + explication de son fonctionnement interne) et un **site vitrine** présentant Orosmo.
  3. **Calendrier** : peut être commencé **avant M6** (aucune modification du code moteur), mais **publié après M6** puis mis à jour à chaque jalon.
  4. **CI/CD et Docker** : une CI construit l'image Docker et l'enregistre sur un registre pour un déploiement manuel ou automatique sur serveur ; la publication en production se déclenche lors de la publication d'un **tag sur `main`**.
  5. **À trancher au début de M14** : choix de l'hébergement et du registre en fonction des prix, mode de déploiement serveur (manuel vs automatique), et décision + spécifications sur le déploiement de **staging sur sous-domaine de test** à l'ouverture d'une PR (`WEB-05`).

  6. **Transparence (auteur, 2026-11-21)** : le site explique honnêtement que le moteur a été commencé par l'auteur avec OpenGL, puis que l'auteur a utilisé l'IA dès son apparition mais mal (sans plan ni structure, par demandes ponctuelles, en codant aussi lui-même), d'où une pause puis un arrêt du projet, repris récemment avec Claude, utilisé correctement (WEB-02) ; la même mention figure dans le `README.md` du dépôt.
