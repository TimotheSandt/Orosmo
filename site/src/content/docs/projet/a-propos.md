---
title: À propos
description: "Orosmo, moteur de jeu 2D et 3D en C++23 développé par Timothé Sandt : pourquoi il existe, à quoi il servira, choix techniques et licence."
sidebar:
  order: 1
---

Orosmo est un moteur de jeu 2D et 3D écrit en C++23. C'est un logiciel open source sous licence Apache-2.0, développé
par Timothé Sandt ([`TimotheSandt`](https://github.com/TimotheSandt) sur GitHub). Le code est dans le dépôt
[`TimotheSandt/Orosmo`](https://github.com/TimotheSandt/Orosmo).

Les quatre sections suivantes sont écrites par l'auteur.

## Pourquoi ce moteur

J'ai commencé Orosmo parce que je voulais comprendre comment fonctionne un moteur, et parce que j'aime créer les choses
à partir de zéro. Je voulais aussi un outil que je comprends, pour faire d'autres projets en 3D avec un moteur simple,
sans passer par Unity ou Unreal : ce sont des outils lourds à utiliser, et ils ne fonctionnent pas toujours de la
manière que je veux.

Je le développe aussi pour que n'importe quel autre développeur puisse s'en servir s'il le souhaite.

## À quoi il servira

Les projets que je veux faire avec Orosmo sont surtout de la simulation et de la génération procédurale. Je veux aussi
m'en servir pour un jeu de voxels, ou pour n'importe quel autre type de jeu si je le souhaite.

## Ce qui le distingue

Je ne me suis pas comparé aux moteurs moins connus. Par rapport à Unity ou Unreal, Orosmo est plus léger. Il intègre un
langage pour décrire l'interface, le `.cui`, inspiré de SwiftUI, dont j'apprécie la simplicité.

La simplicité d'utilisation est prévue, même si elle n'est pas encore atteinte partout aujourd'hui. La page [État du moteur](/projet/etat/) dit ce qui est livré et ce qui manque.

## Choix techniques

- C++23 : j'aime le C++, et la plupart de mes projets sont écrits dans ce langage, à part ce qui touche au web.
- OpenGL : j'ai appris la programmation graphique avec OpenGL, l'API la plus simple parmi celles qui sont performantes.
- Vulkan : c'est l'avenir ; l'API offre plus de fonctions et elle est plus efficace.
- Metal : prévu plus tard, pour la compatibilité avec les Mac. Il n'existe pas encore.
- Plusieurs backends à la fois : pour que le moteur fonctionne sur le plus de machines possible. Le code du moteur
  passe par une couche commune (le RHI) ; voir [Choisir le backend graphique](/guides/application/peripherique/).

## Licence

Orosmo est distribué sous licence [Apache-2.0](https://github.com/TimotheSandt/Orosmo/blob/main/LICENSE). Ce fichier
donne les conditions d'utilisation, de modification et de redistribution.

## Voir aussi

- [Fonctionnalités](/projet/fonctionnalites/) : ce que le moteur fait aujourd'hui, et ce qui est prévu.
- [Histoire et méthode de développement](/projet/histoire/) : les débuts du projet, son arrêt, et la reprise avec l'aide d'une IA.
- [Contribuer](/projet/contribuer/) : proposer un changement au dépôt.
