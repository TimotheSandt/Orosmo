---
title: RHI, la couche graphique abstraite
description: Handles opaques, modèle de liaison en trois groupes, backends Vulkan et OpenGL, repère commun et validation.
sidebar:
  order: 2
---

Le RHI (`orosmo::rhi`, couche 2) est la seule porte vers le GPU. Son API publique (`include/orosmo/rhi/`) ne contient
**aucun** type d'API graphique ; les backends vivent dans `src/rhi/opengl/` et `src/rhi/vulkan/`, seuls endroits où `gl*`
et `Vk*` ont le droit d'exister. *(Sources : `docs/RHI.md`, `src/rhi/README.md`, `src/rhi/vulkan/README.md`,
`docs/03` §3 à §6.)*

## Principes

- **Style moderne** : état explicite, pas d'état global caché. Vulkan et Metal y sont naturels, OpenGL est une traduction.
  Chaque passe déclare ses attachements, chaque pipeline porte tout son état fixe.
- **Handles opaques** (`Handle<Tag>` : indice + génération) : un handle périmé est refusé, jamais déréférencé.
  Destruction **différée** jusqu'à ce que le GPU n'utilise plus l'objet.
- **Erreurs** : `Result<T>`, jamais d'exception. Une commande invalide est refusée avant tout appel au pilote, et la
  première erreur est renvoyée par `Device::submit`.
- **Barrières** calculées par le RHI, pas par l'appelant.
- **Capacités** : `Capabilities` donne les limites et les fonctions optionnelles ; celles qui ne sont pas encore
  implémentées (ray query, bindless, mesh shaders…) valent `false`.

## Une image, de bout en bout

```cpp
auto created = rhi::createDevice({.surface = {.nativeWindow = window.getWindow()}});
if (!created) { return fail(created.error()); }
std::unique_ptr<rhi::Device> device = std::move(*created);

auto frame = device->beginFrame();                 // kBusy si la fenêtre est réduite : sauter l'image
auto encoder = device->createCommandEncoder();
encoder->beginRenderPass({.colors = {{.texture = *frame, .clearColor = {0, 0, 0, 1}}}});
encoder->setPipeline(pipeline);
encoder->draw(3);
encoder->endRenderPass();
auto done = device->submit(std::move(encoder));    // première erreur d'usage, sinon succès
device->present();
```

Exemple complet et compilé : `apps/demo/src/Game/Game.cpp`.

## Modèle de liaison

Trois groupes fixes pour tout le projet, de 8 liaisons chacun :

| Groupe | Contenu | Fréquence |
| --- | --- | --- |
| 0 | données par image : caméra, lumières, `FrameData` | une fois par image |
| 1 | matériau ou passe : textures, échantillonneurs, constantes | par matériau ou par passe |
| 2 | données par objet | par objet |

Vulkan utilise un ensemble de descripteurs par groupe ; OpenGL aplatit en **emplacement = groupe × 8 + binding**. Les
push constants (128 octets) sont émulés par un UBO sous OpenGL.

## Les backends

| Backend | État | Points clés |
| --- | --- | --- |
| **Vulkan 1.3** (`src/rhi/vulkan/`) | Fait (Linux) | chargé à l'exécution par volk, mémoire par VMA, rendu dynamique, `synchronization2`, sémaphore à chronologie, cache de pipelines sur disque, couches de validation dont la validation de synchronisation |
| **OpenGL 4.3+** (`src/rhi/opengl/`) | Fait | contexte de debug `GL_KHR_debug`, `glClipControl` pour le repère commun, barrières conservatrices, stockage persistant mappé quand le pilote l'offre |
| **Metal** | Absent (M9) | reconnu et refusé avec un message |

Le choix se fait au lancement (`--api vulkan|opengl`) ; si Vulkan ne démarre pas, la raison s'affiche et le programme
retombe sur OpenGL. Les mêmes tests (`tests/rhi/rhi_device_tests.cpp`) tournent sur les deux backends et exigent
`validationErrorCount() == 0`. `make demo-vulkan-check` compare des captures OpenGL et Vulkan.

## Repère commun

Origine en haut à gauche pour toutes les cibles ; NDC avec y = +1 en haut et profondeur dans [0, 1] ; caméra en
**reverse-Z** ; un triangle anti-horaire est la face avant. OpenGL l'obtient par `glClipControl`, Vulkan par une hauteur de
viewport négative. L'utilisateur ne voit que la convention.

## Ajouter un backend

Le guide `docs/ADD_A_BACKEND.md` détaille la marche : implémenter `Device` et `CommandEncoder`, la table des formats, le
modèle de liaison, puis passer les tests `tests/rhi/` et les images de référence.
