# scene

Couche 6 de `docs/03` §1 : entités, composants, hiérarchie et transformations (ECS-01, ECS-02), sauvegarde de scène et
empreinte d'état (ECS-03, SIM-01 ; lot L3). Archive : `libscene.a`
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
| `scene_file.hpp` | `saveScene` / `loadScene` (JSON), `saveSceneBinary` / `loadSceneBinary` (MessagePack), `saveSceneFile` / `loadSceneFile`, `SceneMigrations`, `SceneLoadReport`, `componentToJson` / `componentFromJson`, `kSceneFormatVersion` (lot L3) |
| `fingerprint.hpp` | `fingerprint(world)`, `hashWorld` : empreinte des champs décrits (lot L3) |
| `component_migration.hpp` | GAME-03 (lot L6) : `MigrationKind` (`kCopied`, `kRestored`), `ComponentMigration`, `classifyMigration`, `migrateComponentValue` (par nom de champ, à partir des descriptions copiées) |
| `world.hpp` | `World` : entités (`create`, `destroy` du sous-arbre, `alive`), registre (`registerComponent`, `unregisterComponent`, `migrateComponent` (L6), `componentId`, `componentType<T>`, `componentDesc`), données (`add` / `get` / `has` / `remove` par identifiant, par nom ou typées), requêtes (`view(a, b).each(fn)`, `forEach` et `forEachComponent` non typés), hiérarchie (`setParent`, `parent`, `children`), table des entités (`entityTable`, `resetEntities`, lot L3), ordre dense (`componentEntities`), transformations (`setTransform`, `editTransform`, `markTransformDirty`, `updateTransforms`) |

Exemple :

```cpp
struct Velocity { orosmo::core::Vec3 value{0.f}; };
template <> struct orosmo::core::Reflect<Velocity> {
    static constexpr auto fields() { return std::make_tuple(field("value", &Velocity::value)); }
};

orosmo::scene::World world;                                   // « Transform » déjà enregistré
auto velocity = world.registerComponent<Velocity>("Velocity"); // Result<ComponentType<Velocity>>
if (!velocity)
    return std::unexpected(velocity.error());
auto ship = world.create();
if (auto placed = world.setTransform(ship, {.position = {1.0e7, 0.0, 0.0}}); !placed)
    return placed;
if (world.add(ship, *velocity, Velocity{{0.f, 0.f, 1.f}}) == nullptr) // typé : nul si l'entité est morte
    return orosmo::core::makeError(orosmo::core::ErrorCode::kNotFound, "ship is gone");
// Autre façon, par nom (sauvegarde, inspecteur) : Result<void *>, kNotFound si l'entité ou le type est inconnu.
//   if (auto added = world.add(ship, "Velocity"); !added) return std::unexpected(added.error());
world.view(world.transformType(), *velocity).each([&](auto e, orosmo::scene::Transform &t, Velocity &v) {
    t.position += orosmo::core::widen(v.value) * dt;
    world.markTransformDirty(e);                              // écriture directe : marquer l'entité
});
world.updateTransforms();                                     // seulement les entités sales et leurs descendants
if (const auto *t = world.transform(ship))
    model = relativeMatrix(*t, cameraPosition);               // float, relatif à la caméra
```

## Règles

- **Identifiants** : une entité détruite incrémente la génération de son indice ; tout ancien `Entity` est refusé (`alive`,
  `get`, `add`…), même après réutilisation de l'indice. Même principe pour `ComponentId` après `unregisterComponent` ou
  `migrateComponent`. Les générations de types sont uniques dans le processus (« Transform » : 0) : après `a = b`, un
  identifiant ou une `View` d'un type que `b` n'a pas est refusé au lieu de lire un autre type ; une copie du même registre
  (restauration d'une capture) garde ses identifiants valides.
- **Enregistrement** : même nom et même disposition (taille, alignement, version, champs) : sans effet, rend l'identifiant
  existant (prévu pour le rechargement, L5) ; même nom, autre disposition : `kAlreadyExists` (la migration est le lot L6).
  Description invalide (champ hors de la structure, chaîne ou tableau dynamique, alignement, deux champs de même nom au même
  niveau, version négative…) : `kInvalidArgument` qui nomme le champ. La sauvegarde et la restauration se font champ par
  champ, par nom : deux structures distinctes peuvent avoir chacune leur champ `x`, pas une même structure. **Migration
  (L6)** : `migrateComponent(desc)` remplace la disposition d'un type et relit ses valeurs par nom de champ (ordre dense gardé,
  nouvelle génération : anciens identifiants refusés) ; c'est `app::GameModule` qui l'appelle au rechargement. Chaque champ
  et chaque sous-champ d'une structure réfléchie migre indépendamment : même type et même taille, valeur copiée telle quelle
  (sans réappliquer la plage de la nouvelle version) ; type changé, valeur convertie et plage vérifiée, sinon défaut et
  avertissement ; champ ou sous-champ retiré, avertissement. Une structure opaque devenue réfléchie (ou l'inverse) est une
  restauration (`kRestored`), sa valeur reste au défaut. `componentType<T>(nom)` vérifie taille, alignement et champs de T contre la description enregistrée.
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
- **Copie** : `World` est copiable (copie profonde ; le compteur des parcours en cours n'est pas copié, une copie faite dans
  un `each` est libre) ; remplacer un monde pendant l'un de ses parcours est refusé (ENGINE_ASSERT_ALWAYS). Non thread-safe
  (un propriétaire).

## Sauvegarde et empreinte (lot L3)

- **Format** : `{"version": kSceneFormatVersion, "data": {"entities": {"generations", "free"}, "hierarchy": [[enfant, parent]...],
  "components": [{"name", "version", "entities", "values"}]}}`. Table des entités entière (mêmes identifiants après chargement,
  références `Entity` valides, mêmes `create` ensuite), ordre des enfants, ordre dense des stockages (ordre de parcours gardé).
- **Valeurs** : écrites champ par champ à partir de `ComponentDesc::fields` (ni le remplissage entre champs décrits, ni le
  cache monde de `Transform`). Réels au plus court qui se relit à l'identique ; NaN et infinis en `"nan"`, `"inf"`, `"-inf"` ;
  quaternions `[w, x, y, z]` ; énumérations en entier signé de leur taille. Exception : un champ **opaque** (structure non
  réfléchie) est écrit et haché à partir de sa représentation mémoire, en octets hexadécimaux, remplissage interne compris :
  il n'est pas portable si sa disposition change, et ce remplissage entre dans l'empreinte.
- **Chargement** : par nom, dans un monde où les types sont enregistrés ; champ manquant = défaut du type ; champ inconnu et type
  non enregistré ignorés avec un avertissement (journal + `SceneLoadReport::warnings`) ; version plus ancienne : migrations du
  type (`SceneMigrations::components[nom][v]`) ; plus récente : `kVersionMismatch` ; version mal formée (même d'un type
  inconnu) ou deux valeurs du même type pour une entité : `kParse` ; plage d'un champ (`range`) vérifiée. Tout ou rien
  (chargé dans une copie). `componentFromJson` aussi : en cas d'erreur, la valeur et les avertissements sont inchangés. Le cache monde est recalculé (`updateTransforms`).
- **Empreinte** : FNV-1a 64 bits (`core/hash.hpp`) de la table des entités, de la hiérarchie et des valeurs des champs décrits
  des types non vides, par nom de type croissant, dans l'ordre dense ; réels par motif de bits, sauf NaN (tous hachés comme
  le NaN silencieux canonique, relu de `"nan"` : l'aller-retour JSON garde l'empreinte). `saveSceneFile` écrit un temporaire
  créé de façon exclusive (`O_EXCL`, `_wsopen_s(_O_CREAT | _O_EXCL)` sous Windows : un fichier existant, même créé au même
  instant par un autre processus, n'est jamais ouvert ni tronqué, nouvel essai sous un autre nom), nommé comme la destination
  dont la fin est remplacée par `~<8 chiffres hexadécimaux>` (identifiant du processus et compteur) : jamais plus long que la
  destination dès qu'elle a 9 caractères, il tient sous `MAX_PATH` (260, Windows) et 255 octets si elle y tient ; le ferme et le vérifie (une erreur de vidange, disque plein, rend `kIo`), puis le renomme sur la destination : un échec
  laisse la dernière sauvegarde intacte et supprime le temporaire. Un pointeur dans un composant n'est pas refusé et
  rendrait l'empreinte dépendante d'une adresse (G289).

## Pour les lots suivants (notes de L2, faites depuis)

- **L3, sauvegarde** : fait (section ci-dessus).
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
tailles de `FieldDesc`) ; `make demo-test-unit` (sans GPU ni fenêtre). Démo : la scène est rendue par la façade
`app::Application` (lot L13 ; projet d'`orosmo init`) et simulée sans fenêtre par `apps/demo --sim-agents` (lot L3).
Guides du site : `site/src/content/docs/guides/scene/` ; architecture : `site/src/content/docs/interne/scene-sim-app.md`.

Limites : voir `docs/GAPS.md` (G260 à G264, G280 à G288).

Tests du lot L3 : `tests/scene/scene_file_tests.cpp` (aller-retour JSON et MessagePack exact, migration v1 -> v2, documents
invalides, empreinte).
