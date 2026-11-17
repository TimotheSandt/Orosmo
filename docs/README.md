# Spécification du moteur ProceduralGeneration

Ce dossier dit à l'IA **quoi construire**, **comment l'organiser**, **comment travailler avec Git** et **comment prouver que c'est fait**.
À copier tel quel dans le dépôt, sous `docs/` (et `CLAUDE.md` à la racine).

## Contenu

| Fichier | Rôle |
| --- | --- |
| `CLAUDE.md` | Règles permanentes pour l'IA (à placer à la racine du dépôt). Court, lu à chaque session. |
| `01-VISION_ET_PERIMETRE.md` | Pourquoi le moteur existe, ce qu'il doit faire, ce qu'il ne fait pas, plateformes visées. |
| `02-CHECKLIST_FONCTIONNALITES.md` | **La liste de référence.** Chaque fonctionnalité a un identifiant, une priorité et un critère d'acceptation vérifiable. |
| `03-ARCHITECTURE_ET_PORTABILITE.md` | Modules, règles de dépendance, interface RHI (Vulkan / OpenGL / Metal), shaders, conventions. |
| `04-PROTOCOLE_AUDIT.md` | Comment l'IA analyse le code existant et confirme, preuve à l'appui, ce qui est fait ou non. |
| `05-PLAN_PAR_JALONS.md` | L'ordre de travail (M00, puis M0 à M10), avec une démo et une définition de « terminé » par jalon. |
| `06-WORKFLOW_GIT_ET_NETTOYAGE.md` | Branches (`main`, `dev`, une branche par fonctionnalité), commits, fin de jalon, correction de bugs, et le **nettoyage du projet et du dépôt (M00)**. |

Le dossier `docs/` sera complété par l'IA : `CLEANUP_PLAN.md`, `AUDIT_REPORT.md`, `PORTABILITY_REPORT.md`, `GAPS.md`, `STATUS.md`, `QUESTIONS.md`, `UI_LANGUAGE_AUDIT.md`, `BASELINE/`, `releases/`, puis `UI_LANGUAGE.md` et `ADD_A_BACKEND.md` au fil des jalons.

## Comment l'utiliser

1. Copie les fichiers dans le dépôt : `CLAUDE.md` à la racine, les autres dans `docs/` (y compris ce `README.md`). Le projet vient de Windows et continue sous Linux.
2. **Ordre de travail** : nettoyage (M00) → audit → jalons M0 à M10. Donne à l'IA les messages ci-dessous **un par un**, dans cet ordre, et attends chaque fin avant le suivant.
3. Pendant chaque jalon, l'IA travaille sur des branches de fonctionnalité qu'elle fusionne dans `dev`. **Tu testes sur `dev`**, tu lui signales les bugs, elle les corrige sur des branches `fix/`.
4. Quand un jalon est complet, elle prépare les notes de version et la description de la merge request, puis s'arrête. **Toi seul fusionnes `dev` vers `main`.**
5. Active les protections de branche sur ton hébergeur (voir `docs/06` §5).

## Message 1 · Nettoyage du projet et du dépôt (M00)

> Lis `CLAUDE.md`, puis `docs/06-WORKFLOW_GIT_ET_NETTOYAGE.md`, puis le reste de `docs/`.
> Exécute le jalon M00 : d'abord la sauvegarde (tag, branche, bundle) et la référence de comportement dans `docs/BASELINE/`, puis l'inventaire en lecture seule, puis écris `docs/CLEANUP_PLAN.md`.
> **Arrête-toi après ce plan et attends mon accord** avant de déplacer, supprimer ou toucher à l'historique. Conserve l'historique actuel.

Après mon accord :

> Exécute `docs/CLEANUP_PLAN.md` sur une branche `chore/cleanup-*`, par petits commits (les déplacements avec `git mv` dans des commits séparés), en vérifiant que le projet compile à chaque étape. Crée la branche `dev`, fusionne-y le nettoyage, écris `docs/releases/M00.md`, puis arrête-toi.

## Message 2 · Audit

> Lis `CLAUDE.md`, puis tous les fichiers de `docs/` dans l'ordre (01 à 06). Ne modifie aucun code pour l'instant.
> Exécute le protocole d'audit de `docs/04-PROTOCOLE_AUDIT.md` sur le code existant : pour chaque identifiant de `docs/02-CHECKLIST_FONCTIONNALITES.md`, indique l'état réel (Fait / Partiel / Absent / Incompatible), avec le fichier et le symbole qui le prouvent.
> Audite aussi le langage d'interface maison (grammaire réelle, génération de code, erreurs, événements, liaisons) et écris `docs/UI_LANGUAGE_AUDIT.md`.
> Produis `docs/AUDIT_REPORT.md`, `docs/PORTABILITY_REPORT.md` et `docs/GAPS.md` (avec le jalon qui traite chaque identifiant). Signale tout désaccord avec la spécification au lieu de le contourner.
> Quand c'est fini, arrête-toi et attends mon feu vert avant le jalon M0.

## Message pour chaque jalon (M0 à M10)

> Passe au jalon Mx de `docs/05-PLAN_PAR_JALONS.md`. Une branche par fonctionnalité depuis `dev`, fusion dans `dev` quand tout est vert. À la fin : mets à jour `docs/STATUS.md`, écris `docs/releases/Mx.md` et la description de la merge request, vérifie depuis un clone propre, fournis la commande de démo et le résultat des tests, puis arrête-toi. Ne touche jamais à `main` et ne coche rien sans preuve (voir `CLAUDE.md`).

## Message pour corriger un bug

> Sur `dev`, voici le bug : (décris-le). Crée `fix/<sujet>`, écris d'abord un test qui échoue, corrige, vérifie, fusionne dans `dev`.
