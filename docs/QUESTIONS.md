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

**A2. Arborescence et nommage de `docs/03`.** `docs/03` §2 : `include/engine/<module>/*.hpp`, fichiers en `snake_case`, fonctions en `camelCase`. Le code : `include/<Module>/*.h`, fichiers en `PascalCase`, méthodes en `PascalCase` (`GetInstance`), macros `LOG_*`. `CLAUDE.md` demande de suivre l'existant. Proposition : **garder l'existant**, l'inscrire dans `CLAUDE.md`, et n'ajouter `engine/` que si l'API est publiée à des tiers. *Choix demandé.*

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

**A17. Quand migrer l'arborescence et les noms vers `docs/03` ?** Option 1 (recommandée) : au fil des jalons, comme écrit plus haut. Option 2 : une migration de masse dédiée avant M1 (`include/engine/<module>/*.hpp`, fichiers en `snake_case`, fonctions en `camelCase`), qui touche ~19 000 lignes mécaniquement mais fait passer tous les `#include` et tous les appels ; je la ferais avec les tests comme filet. *Dis-moi si tu préfères l'option 2.*

**A18. Windows et macOS dans la CI.** Les jobs correspondants de `.github/workflows/ci.yml` n'ont jamais tourné (aucune machine disponible pendant M0) : ils sont en `continue-on-error`. Quand tu auras vu un premier run, dis-moi ce qu'il affiche pour que je corrige et retire ce drapeau.

**A19. Disque presque plein.** Pendant M0, `/` était à 100 % (97 Go sur 102) ; `~/vcpkg` pèse 7,5 Go (`buildtrees`, `downloads` supprimables avec `vcpkg` : `rm -rf ~/vcpkg/buildtrees ~/vcpkg/downloads`) et `~/.cache` 9,4 Go. Je n'ai rien supprimé chez toi, seulement mes fichiers temporaires.

## Revue CodeRabbit de la PR #4 : questions sur le plan

**A20. TEST-01 et la clôture de M0.** Le critère de TEST-01 (`docs/02`) inclut « couverture de `core` et `world` », que le plan traite en M1 (core) et M3 (world). M0 livre le framework (doctest), le build avec avertissements en erreur et la CI, pas la couverture. Proposition : dans `docs/05`, M0 clôt **« TEST-01 (framework et exécution en CI) »** ; la couverture de `core` et de `world` est suivie dans M1 et M3 sous les mêmes identifiants (`Partiel` jusque-là). Sinon, scinder TEST-01 en deux identifiants. *Décision demandée.*

**A21. UI-02 et le batch 2D (R2D-01).** UI-02 (M2) demande « un adaptateur les envoie au batch 2D (R2D-01) », mais le batch est planifié en M5. Proposition : en M2, l'interface produit ses **listes de dessin** et un adaptateur provisoire les dessine par le renderer actuel ; l'adaptateur vers le batch R2D-01 est livré avec R2D-01 en M5. UI-02 reste `Partiel` entre les deux. *Décision demandée ; aucun jalon n'est ajouté.*

**A22. REPO-01 rétrogradé en `Partiel`** (remarque CodeRabbit, juste : « comportement identique » exigeait des captures d'avant M00, qui n'existent pas, et l'exception `third_party/` n'est pas validée). Pour le repasser à `Fait` : valider l'absence de `third_party/` (rien de copié) et accepter que la preuve d'identité soit tests + journal de démarrage + capture reproductible. *Ton accord demandé.*

**A23. Dédoublonnage de la CI et validation du commit de fusion.** CodeRabbit (2e passage) objecte que `pull_request` teste le **commit de fusion** `dev` + `main`, pas la tête de `dev` : le dédoublonnage que tu m'as demandé retire cette validation. Dans ton flux, `main` ne reçoit que des fusions de `dev`, donc le commit de fusion est identique à la tête de `dev` tant que `main` n'a pas divergé ; le risque est faible et le gain est de 12 minutes de macOS par push. **Je garde le dédoublonnage** ; si `main` reçoit un jour autre chose que `dev`, retire la condition `if:` des trois jobs de `.github/workflows/ci.yml`. *Ton avis demandé.*
