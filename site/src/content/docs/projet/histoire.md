---
title: Histoire et méthode de développement
description: D'où vient Orosmo, pourquoi le projet s'est arrêté, et comment il est développé aujourd'hui avec Claude sous la direction de l'auteur.
sidebar:
  order: 4
---

Le même texte, plus court, figure dans le `README.md` du dépôt (section « Origine et méthode de développement »).

## Les débuts

Orosmo a été commencé par son auteur, avec OpenGL, en août 2025. Les premières briques (le logger, le gestionnaire
d'entrées, l'éclairage, le texte et un début d'interface dans un langage maison précompilé en C++) datent de cette
période. Une partie existe encore dans le moteur actuel ; le logger, par exemple, a été repris tel quel.

## Une première utilisation de l'IA, sans cadre

L'auteur a utilisé l'IA dès son apparition, mais sans plan ni structure clairs : il lui demandait « fais tel truc » tout
en continuant à coder lui-même. Sans architecture écrite, sans règles ni critère pour dire qu'une chose était finie, le code
s'est accumulé sans cohérence. Le projet a été mis en pause, puis arrêté.

## La reprise avec Claude

Le projet a repris récemment avec Claude, l'assistant de programmation d'Anthropic, cette fois dans un cadre écrit.

- **L'auteur décide.** Il fixe la vision, le périmètre et l'architecture, et tranche les questions ouvertes. Un choix non
  prévu est proposé dans `docs/QUESTIONS.md`, pas appliqué en silence.
- **L'auteur fixe les règles.** `CLAUDE.md` et les documents de `docs/` (vision, checklist, architecture, audit, plan par
  jalons, workflow Git) disent ce que l'IA peut faire et comment.
- **L'IA écrit du code et des tests,** sur une branche par sujet, en petits commits. Chaque commit écrit avec elle porte une
  ligne `Co-Authored-By`.
- **Tout est vérifié.** La CI compile sous Linux, Windows et macOS avec les avertissements bloquants, lance les tests (avec
  ASan et UBSan), contrôle les couches et l'isolation des API graphiques, et compare des images de référence.
- **Preuve avant case cochée.** Une fonction n'est « Fait » qu'avec le fichier et le symbole, un test ou une commande de
  démo, et le résultat. Sinon elle reste « Partiel » ou « Absent » et ce qui manque est écrit dans `docs/GAPS.md`.
- **L'auteur relit et fusionne.** En fin de jalon, l'IA prépare les notes de version et la description de la merge request,
  vérifie depuis un clone propre et s'arrête ; l'auteur teste, signale les bugs et fusionne vers `main`.

## Vérifier

Tout est public dans le dépôt : l'historique Git avec les lignes `Co-Authored-By`, `CLAUDE.md`, le plan
(`docs/05-PLAN_PAR_JALONS.md`), les décisions (`docs/QUESTIONS.md`), l'état et ses preuves (`docs/STATUS.md`) et les notes de
chaque jalon (`docs/releases/`). Ce site a été écrit de la même façon.
