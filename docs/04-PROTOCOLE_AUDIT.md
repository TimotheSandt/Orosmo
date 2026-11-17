# 04 · Protocole d'audit du code existant

But : savoir **exactement** ce qui existe, ce qui manque et ce qui n'est pas portable, avant de coder. L'IA ne modifie aucun fichier source pendant l'audit. Elle ne crée que les rapports listés plus bas.

L'audit se fait **après M00** (nettoyage, `docs/06`), sur le dépôt rangé, depuis `dev`. Les rapports sont ajoutés par une branche `docs/audit` fusionnée dans `dev`, jamais par un commit direct.

## Règles de preuve

Pour chaque identifiant de `docs/02-CHECKLIST_FONCTIONNALITES.md`, l'IA donne :

| Champ | Contenu |
| --- | --- |
| État | `Fait`, `Partiel`, `Absent`, `Incompatible` |
| Où | fichier(s) et symbole(s) précis (`src/ui/UIManager.cpp : UIContainer::layout`) |
| Comment elle sait | ce qu'elle a lu, lancé ou mesuré (build, test, exécution) |
| Écart | ce qui manque par rapport au critère d'acceptation, point par point |
| Portabilité | appels d'API graphique ou système présents dans ce code (`gl*`, `GLFW`, etc.) |

Une fonctionnalité n'est `Fait` que si **tous** les points du critère sont remplis **et** qu'un test ou une commande de démo l'exerce avec succès. Un nom de classe qui existe ne suffit pas. En cas de doute : `Partiel`. En cas d'impossibilité de vérifier (outil absent, système différent) : le dire, ne pas supposer.

## Étapes

1. **Inventaire.** Arborescence, dépendances (vcpkg, `Libraries/`), système de build, versions de compilateur. Lancer le build sur le système disponible (**Linux**, alors que le projet a été commencé sous Windows) et noter les avertissements et les échecs. Chercher en particulier les problèmes de portage décrits dans `CLAUDE.md` § Environnement : casse des noms de fichiers dans les `#include`, fins de ligne, chemins en dur, dépendances, options de lien de `<stacktrace>`, version du compilateur. Lister chaque correction nécessaire dans `docs/PORTABILITY_REPORT.md` sans l'appliquer pendant l'audit.
2. **Exécution.** Lancer l'application existante. Décrire ce qu'elle fait réellement. Ajouter si possible une capture d'écran de référence (nécessaire plus tard pour RHI-02).
3. **Lecture par module.** Pour chaque module existant (logger, UIManager, InputManager, éclairage, TextRenderer, terrain), résumer l'API, les responsabilités, les dépendances réelles, les points forts à conserver et les défauts.
3 bis. **Audit du langage d'interface.** Retrouver le compilateur du langage maison et son format : grammaire réelle (déduite des fichiers sources et du code du compilateur), génération de code, intégration au build, gestion d'erreurs (fichier/ligne/colonne du source ou du C++ généré ?), liaison des événements et des données, widgets et mises en page déjà disponibles, utilisation de `DeferredValue<T>`. Lister ce qui manque pour UI-03 à UI-12. Écrire le résultat dans `docs/UI_LANGUAGE_AUDIT.md` avec des extraits de code réels. Ne pas inventer de syntaxe : si un point n'est pas visible dans le code, le dire.
3 ter. **Audit de l'organisation du rendu actuel.** Où sont décidés l'ordre des passes, les cibles de rendu, les shaders, les lumières, les ressources GPU ? Qui crée les contextes ? Quels états OpenGL sont modifiés globalement ? Ces réponses alimentent `docs/PORTABILITY_REPORT.md`.
4. **Cartographie de la portabilité.** Rechercher tous les usages de `gl*`, `GL_*`, `glfw*`, GLSL spécifique à OpenGL (`gl_VertexID` ok, mais `#version 330`, extensions, `layout(binding)` sans set, etc.), chemins de fichiers en dur, appels Windows ou POSIX. Pour chacun : où, pourquoi, et quelle abstraction (RHI, plateforme, core) le remplacerait.
5. **Vérification fonctionnalité par fonctionnalité.** Remplir le tableau du §« Règles de preuve » pour **chaque** ID, y compris ceux qu'elle pense absents (preuve d'absence : recherche effectuée, mots-clés utilisés).
6. **Écarts avec la spécification.** Lister ce qui, dans `docs/` , est contradictoire, irréaliste ou incomplet, avec une proposition. Ne pas contourner : demander.
7. **Risques.** Dette technique, dépendances obsolètes, code mort, fuites, fils d'exécution non sûrs, champs non initialisés. Classer par gravité.
8. **Proposition d'ajustement du plan** : si l'état réel justifie de changer l'ordre ou la portée des jalons de `docs/05`, le proposer, sans l'appliquer.

## Livrables

| Fichier | Contenu |
| --- | --- |
| `docs/AUDIT_REPORT.md` | Tableau complet par ID (état, où, comment, écart, portabilité) + synthèse chiffrée (Fait / Partiel / Absent / Incompatible par module). |
| `docs/PORTABILITY_REPORT.md` | Tous les appels dépendants d'une API ou d'un système, regroupés par destination (RHI, plateforme, core), avec une estimation d'effort. |
| `docs/GAPS.md` | Tout ce qui manque, trié par priorité, avec le jalon qui le traite. C'est la liste de travail. |
| `docs/UI_LANGUAGE_AUDIT.md` | Grammaire réelle, génération de code, build, erreurs, événements, liaisons, widgets, manques par rapport à UI-03 à UI-12. |
| `docs/STATUS.md` | Version initiale du tableau d'état, mise à jour à chaque jalon. |
| `docs/QUESTIONS.md` | Questions ouvertes, désaccords, choix à valider (ECS maison ou EnTT, CMake ou Makefile, etc.). |
| `docs/BASELINE/` | Captures d'écran de référence et sorties de l'application actuelle. |

## Critères de fin d'audit

- Chaque ID de la checklist a un état et une preuve (ou une preuve d'absence).
- Le build est reproduit, ou la raison de l'échec est documentée.
- Les rapports contiennent des chemins de fichiers réels, pas de généralités.
- L'IA a relu son propre rapport en cherchant ses affirmations non prouvées et les a corrigées.
- Elle s'arrête et attend la validation avant le jalon M0.

## Confirmation de la couverture (deuxième passe)

Après chaque jalon, l'IA refait une passe courte :

1. Relancer les tests et la démo du jalon.
2. Parcourir `docs/STATUS.md` : pour chaque ID passé à `Fait`, retrouver la preuve (fichier, test, résultat). Rétrograder en `Partiel` toute ligne sans preuve.
3. Chercher dans le code les `TODO`, `FIXME`, `stub`, `not implemented` et vérifier qu'ils figurent dans `docs/GAPS.md`.
4. Vérifier les règles automatiques du §8 de `docs/03`.
5. Résumer en dix lignes ce qui est vraiment livré, ce qui ne l'est pas, et pourquoi.
