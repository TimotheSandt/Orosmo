---
title: Histoire et méthode de développement
description: D'où vient Orosmo, pourquoi le projet s'est arrêté, et comment il est développé aujourd'hui avec Claude sous la direction de l'auteur.
---

Cette page dit honnêtement comment Orosmo a été écrit. Le même texte, plus court, figure dans le `README.md` du dépôt
(section « Origine et méthode de développement »).

## Les débuts : un moteur OpenGL écrit par l'auteur

Orosmo a été **commencé par son auteur, avec OpenGL**. Les premières briques — le logger, le gestionnaire d'entrées,
l'éclairage, le texte et un début d'interface écrite dans un langage maison précompilé en C++ — viennent de cette période,
et une partie existe encore dans le moteur actuel (le logger, par exemple, a été repris tel quel).

## L'IA, mal utilisée au début

L'auteur a utilisé l'IA **dès son apparition**, mais **mal** à l'époque : **sans plan ni structure clairs**, en lui demandant
simplement « fais tel truc », tout en continuant à coder lui-même un peu. Sans architecture écrite, sans règles ni critère pour
dire qu'une chose était finie, le code s'est accumulé sans cohérence. Le projet a été **mis en pause, puis arrêté**.

## La reprise : Claude, utilisé correctement

Le projet a **repris récemment**, avec **Claude** (assistant de programmation d'Anthropic), utilisé cette fois
**correctement**, sous la direction de l'auteur. La répartition des rôles est nette :

- **L'auteur décide.** Il fixe la direction, la vision et le périmètre, choisit l'architecture et tranche les questions
  ouvertes (`docs/QUESTIONS.md` : chaque choix non prévu est proposé, pas appliqué en silence).
- **L'auteur fixe les règles.** `CLAUDE.md` et les documents de `docs/` (vision, checklist, architecture, protocole
  d'audit, plan par jalons, workflow Git) disent ce que l'IA a le droit de faire et comment.
- **L'IA écrit du code et des tests.** Elle travaille sur une branche par sujet, en petits commits ; chaque commit écrit
  avec elle porte la ligne `Co-Authored-By`.
- **Tout est vérifié.** La CI compile sous Linux, Windows et macOS avec les avertissements bloquants, lance les tests (et les
  sanitizers ASan et UBSan), contrôle les couches et l'isolation des API graphiques, et compare des images de référence.
- **Preuve avant case cochée.** Aucune fonctionnalité n'est déclarée « Fait » sans le fichier et le symbole, un test
  automatisé ou une commande de démo, et le résultat de ce test (`docs/STATUS.md`). Sinon elle reste « Partiel » ou
  « Absent », et ce qui manque est écrit dans `docs/GAPS.md`.
- **L'auteur relit et fusionne.** En fin de jalon, l'IA prépare les notes de version et la description de la merge
  request, vérifie depuis un clone propre, puis s'arrête : c'est l'auteur qui teste, signale les bugs et fusionne vers
  `main`.

## Comment le vérifier

Tout est public dans le dépôt : l'historique Git (avec les lignes `Co-Authored-By`), les règles (`CLAUDE.md`), le plan
(`docs/05-PLAN_PAR_JALONS.md`), les décisions (`docs/QUESTIONS.md`), l'état et ses preuves (`docs/STATUS.md`) et les notes
de chaque jalon (`docs/releases/`). Ce site lui-même (jalon M14) a été écrit de la même façon.
