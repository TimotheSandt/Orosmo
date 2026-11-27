# scene

Couche 6 de `docs/03` §1 : entités, composants, hiérarchie et transformations (ECS-01, ECS-02). Archive : `libscene.a`
(`src/scene/`), API publique `include/orosmo/scene/`, namespace `orosmo::scene`. Dépend seulement de `core` (réflexion,
erreurs, mathématiques). Créé en M8 (A79 lot L2).

**ECS maison** (A79 choix 1) : les composants du jeu seront définis dans une bibliothèque rechargée (GAME-01, lot L5). Le monde
ne garde donc aucun type C++, aucune table virtuelle ni identifiant de type issu de cette bibliothèque : un type de composant est
enregistré **par nom** avec sa taille, son alignement, sa version, sa description de champs (`core::FieldDesc` avec décalage,
taille et sous-champs) et les octets de sa valeur par défaut, tous copiés côté moteur. Les composants sont des données
**trivialement copiables** (A79 choix 2, concept `ComponentData`) : le moteur les déplace par `memcpy`, sans constructeur ni
destructeur.

| En-tête | Rôle |
| --- | --- |
| `entity.hpp` | `Entity` (indice + génération), `kNullEntity` ; réfléchi (index, generation) |
| `component.hpp` | `ComponentDesc` (nom, taille, alignement, version, champs, valeur par défaut ; `sameLayout`), `validateComponentDesc`, `ComponentId` (emplacement + génération), `ComponentType<T>`, `describeComponent<T>(nom)` |
| `component_pool.hpp` | `ComponentPool` : ensemble clairsemé, octets denses contigus et alignés (jusqu'à 4096), retrait par échange avec le dernier |
| `transform.hpp` | `Transform` (local en double : position, rotation `DQuat`, échelle ; cache monde : `worldPosition`, `worldLinear`), `composeWorld`, `relativeMatrix(t, origine)` (float relatif à la caméra), `worldPosition(t)` |
| `world.hpp` | `World` : entités (`create`, `destroy` du sous-arbre, `alive`), registre (`registerComponent`, `unregisterComponent`, `componentId`, `componentType<T>`, `componentDesc`), données (`add` / `get` / `has` / `remove` par identifiant, par nom ou typées), requêtes (`view(a, b).each(fn)`, `forEach` et `forEachComponent` non typés), hiérarchie (`setParent`, `parent`, `children`), transformations (`setTransform`, `editTransform`, `markTransformDirty`, `updateTransforms`) |

Exemple :

```cpp
struct Velocity { orosmo::core::Vec3 value{0.f}; };
template <> struct orosmo::core::Reflect<Velocity> {
    static constexpr auto fields() { return std::make_tuple(field("value", &Velocity::value)); }
};

orosmo::scene::World world;                                   // « Transform » déjà enregistré
auto velocity = world.registerComponent<Velocity>("Velocity"); // Result<ComponentType<Velocity>>
auto ship = world.create();
world.setTransform(ship, {.position = {1.0e7, 0.0, 0.0}});
world.add(ship, *velocity, Velocity{{0.f, 0.f, 1.f}});
world.add(ship, "Velocity");                                  // par nom (sauvegarde, inspecteur) : kNotFound si inconnu
world.view(world.transformType(), *velocity).each([&](auto e, orosmo::scene::Transform &t, Velocity &v) {
    t.position += orosmo::core::widen(v.value) * dt;
    world.markTransformDirty(e);                              // écriture directe : marquer l'entité
});
world.updateTransforms();                                     // seulement les entités sales et leurs descendants
auto model = relativeMatrix(*world.transform(ship), cameraPosition); // float, relatif à la caméra
```

## Règles

- **Identifiants** : une entité détruite incrémente la génération de son indice ; tout ancien `Entity` est refusé (`alive`,
  `get`, `add`…), même après réutilisation de l'indice. Même principe pour `ComponentId` après `unregisterComponent`.
- **Enregistrement** : même nom et même disposition (taille, alignement, version, champs) : sans effet, rend l'identifiant
  existant (prévu pour le rechargement, L5) ; même nom, autre disposition : `kAlreadyExists` (la migration est le lot L6).
  Description invalide (champ hors de la structure, chaîne ou tableau dynamique, alignement…) : `kInvalidArgument` qui nomme le
  champ. `componentType<T>(nom)` vérifie taille, alignement et champs de T contre la description enregistrée.
- **Pointeurs** : un pointeur de composant reste valide jusqu'au prochain ajout ou retrait du même type. Aucun changement de
  structure pendant un `each` / `forEach` (ENGINE_ASSERT en debug) ; enregistrer un nouveau type ou en retirer un y rend
  `kBusy`. Une `View` garde les identifiants de ses types : valable après d'autres enregistrements, invalide (`valid()`
  faux, `each` sans effet) si l'un de ses types est retiré.
- **Ordre** : l'ordre de parcours est l'ordre dense du plus petit stockage, fixé par la suite des ajouts et retraits
  (déterministe) ; indices libérés réutilisés du dernier libéré au premier.
- **Hiérarchie** : enfants dans l'ordre d'ajout ; `destroy` détruit les descendants ; cycle refusé. Une entité sans
  `Transform` transmet à ses enfants la transformation de son plus proche ancêtre qui en a une.
- **Transformations** : la partie monde est recalculée par `updateTransforms` pour les entités sales (ajout de `Transform`,
  `setTransform`, `editTransform`, `markTransformDirty`, `setParent`, retrait de `Transform`) et leurs descendants seulement.
  Composition en double sans fonction transcendante (`mat3_cast` du quaternion, produits) : même résultat partout avec
  `-ffp-contract=off` (règle 9). Le passage en float pour le GPU se fait par `relativeMatrix` (soustraction en double avant
  l'arrondi, `core::toLocal`).
- **Copie** : `World` est copiable (copie profonde) ; non thread-safe (un propriétaire).

## Pour les lots suivants (non faits ici)

- **L3, sauvegarde** : écrire les champs décrits (`ComponentDesc::fields`, décalage et taille, sous-champs) et non les octets
  bruts (le remplissage d'une valeur fournie par l'appelant n'est pas initialisé) ; le cache monde de `Transform` n'est pas
  décrit : le recalculer après chargement (`setTransform` ou `markTransformDirty`). Les références `Entity` dans un composant
  supposent que la restauration garde les indices et générations (capture de la table d'entités). `World` copiable sert la
  capture et restauration ; l'empreinte doit hacher les champs, pas les octets.
- **L5, bibliothèque de jeu** : enregistrement par `registerComponent(ComponentDesc)` (rien ne pointe dans la bibliothèque) ;
  un nouvel enregistrement identique après rechargement garde données et identifiant. `unregisterComponent` détruit les
  données : le retrait au déchargement est une décision de L5/L6.
- **L10, inspecteur** : `forEachComponent` + `componentDesc(id)->fields` donnent nom, type, plage, décalage de chaque champ.

## Mesures

Hors de la suite doctest (aucune horloge dans les tests) : scénarios `scene-view` et `scene-transforms` de
`apps/demo/benchmark/scene_benchmarks.hpp`, lancés par `make -C apps/demo ENGINE_DIR=orosmo TEST_DIR=../../tests benchmark`
(rapport `apps/demo/benchmark_report.json`). Les tests vérifient seulement les comptes et les valeurs (100 000 entités).
Mesure du 2026-11-26 (build release `-O3`, g++ 13.3, Linux x64, Intel Xe), machine chargée par d'autres compilations : ordre
de grandeur, pas une référence.

| Mesure | Temps |
| --- | --- |
| `scene-view` : `view<Position>` sur 100 000 entités puis `view<Position, Velocity>` sur 50 000 | 0,47 ms (moyenne, 6 382 échantillons) |
| `scene-transforms` : `updateTransforms`, 100 000 entités (1 000 racines × 99 enfants, toutes sales) | 3,3 ms (moyenne, 918 échantillons) |

## Tests

`tests/scene/world_tests.cpp`, `tests/scene/hierarchy_tests.cpp`, `tests/core/reflection_layout_tests.cpp` (décalages et
tailles de `FieldDesc`) ; `make demo-test-unit` (sans GPU ni fenêtre). Pas encore de démo : la scène sera rendue par la façade
d'application (lot L13).

Limites : voir `docs/GAPS.md` (G260 à G264).
