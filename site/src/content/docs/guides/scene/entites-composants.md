---
title: "Entités, composants et transformations"
description: "Créer des entités dans scene::World, enregistrer des composants par nom, les parcourir avec view, construire une hiérarchie et lire les transformations en double précision."
sidebar:
  order: 1
---

Le monde d'une scène est un `scene::World` : des entités (un indice et une génération), des composants rangés par type et
une hiérarchie parent/enfant. Les types de composants sont enregistrés **par nom**, avec une description de leurs champs
copiée côté moteur : c'est ce qui permet la sauvegarde, l'inspecteur et le rechargement de la logique de jeu.

En-têtes : `orosmo/scene/world.hpp`, `orosmo/scene/transform.hpp`, `orosmo/scene/component.hpp`. Bibliothèque :
`libscene.a` (ne dépend que de `core`).

Avec la façade [`app::Application`](/guides/application/facade/), le monde existe déjà : `a.world()`, et `a.spawn(...)`
crée une entité avec un composant de rendu et une transformation.

## Exemple minimal

```cpp
#include <orosmo/scene/world.hpp>
namespace scene = orosmo::scene;

struct Velocity { orosmo::core::Vec3 value{0.f}; };     // données simples, copiables par memcpy
template <> struct orosmo::core::Reflect<Velocity> {     // champs décrits : sauvegarde, inspecteur, migration
    static constexpr auto fields() { return std::make_tuple(field("value", &Velocity::value)); }
};

scene::World world;                                       // le type « Transform » est déjà enregistré
auto velocity = world.registerComponent<Velocity>("Velocity");   // Result<ComponentType<Velocity>>
if (!velocity)
    return std::unexpected(velocity.error());
const scene::Entity ship = world.create();
if (auto placed = world.setTransform(ship, {.position = {1.0e7, 0.0, 0.0}}); !placed)
    return std::unexpected(placed.error());
world.add(ship, *velocity, Velocity{{0.f, 0.f, 1.f}});

world.view(world.transformType(), *velocity).each([&](scene::Entity e, scene::Transform &t, Velocity &v) {
    t.position += orosmo::core::widen(v.value) * dt;
    world.markTransformDirty(e);                          // écriture directe : marquer l'entité
});
world.updateTransforms();                                 // recalcule seulement les entités sales et leurs descendants
```

## Entités

- `create()` rend une `Entity` ; `destroy(e)` détruit l'entité **et ses descendants** ; `alive(e)` dit si elle existe.
- Une entité détruite incrémente la génération de son indice : un ancien identifiant est refusé partout (`get`, `add`,
  `alive`…), même quand l'indice a été réutilisé.
- `kNullEntity` est l'entité nulle.

## Composants

Un composant est une structure **trivialement copiable** (concept `ComponentData`) : pas de `std::string`, pas de
`std::vector`, pas de pointeur vers du code. Le moteur la déplace par `memcpy`.

| Appel | Effet |
| --- | --- |
| `registerComponent<T>(nom)` | enregistre le type (taille, alignement, champs de `core::Reflect<T>`, valeur par défaut) ; même nom et même disposition : rend l'identifiant existant ; autre disposition : `kAlreadyExists` |
| `componentType<T>(nom)` | retrouve un type déjà enregistré (par exemple par la logique de jeu), après vérification de la disposition |
| `add(e, type, valeur)`, `get(e, type)`, `has`, `remove` | données typées ; `add(e, "Nom")` par nom (`kNotFound` si inconnu) |
| `view(a, b…).each(fn)` | parcourt les entités qui ont tous les types, dans l'ordre dense du plus petit stockage |
| `forEachComponent(e, fn)` | tous les composants d'une entité, sans type C++ (inspecteur, sauvegarde) |

Un pointeur de composant reste valide jusqu'au prochain ajout ou retrait **du même type**. Pendant un `each`, n'ajoutez ni ne
retirez de composant (assertion en debug) ; enregistrer un nouveau type y rend `kBusy`.

## Hiérarchie

`setParent(enfant, parent)` rattache l'enfant en dernier ; un parent nul le détache. Un cycle est refusé
(`kInvalidArgument`). `parent(e)`, `children(e)`, `firstChild(e)` et `nextSibling(e)` lisent l'arbre ; les enfants gardent
l'ordre d'ajout. Une entité sans `Transform` transmet à ses enfants la transformation de son plus proche ancêtre qui en a une.

## Transformations

`Transform` garde la partie locale en double précision (position, rotation `DQuat`, échelle) et un cache de la partie monde.

- `setTransform(e, local)` écrit et marque l'entité sale (ajoute le composant s'il manque) ; `editTransform(e)` rend un
  pointeur modifiable et marque l'entité ; après une écriture par `get` ou `view`, appelez `markTransformDirty(e)`.
- `updateTransforms()` recompose la partie monde des entités sales et de leurs descendants seulement. Avec la simulation
  ([Simulation à pas fixe](/guides/scene/simulation/)), il est appelé après chaque pas.
- Pour le GPU, `relativeMatrix(t, origine)` rend une matrice en float relative à la caméra (soustraction en double avant
  l'arrondi) : un objet à 10⁷ m de l'origine ne tremble pas.

La composition n'utilise aucune fonction transcendante : même résultat sur toutes les plateformes avec `-ffp-contract=off`
(options du moteur et du gabarit).

## Limites

- Un seul propriétaire : `World` n'est pas thread-safe (le [thread de simulation](/guides/scene/simulation/#thread-de-simulation)
  en est le seul utilisateur entre `start` et `stop`).
- Pas de requête par exclusion (« sans tel composant ») ni de tri des vues.
- Un pointeur dans un composant n'est pas refusé, mais rend l'empreinte dépendante d'une adresse.

Liste complète : `docs/GAPS.md` (G260 à G264, G280 à G289).

## Voir aussi

- [Sauvegarder et charger une scène](/guides/scene/sauvegarde/)
- [Logique de jeu rechargeable](/guides/scene/logique-rechargeable/)
- [Modules scene, sim et app](/interne/scene-sim-app/)
