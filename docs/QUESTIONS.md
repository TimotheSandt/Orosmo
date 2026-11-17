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
