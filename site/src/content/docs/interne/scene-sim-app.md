---
title: "Modules scene, sim et app"
description: "Architecture des trois modules ajoutés en M8 : le monde d'entités maison, la simulation déterministe et son thread, la façade d'application, le module de jeu rechargeable et l'hôte des vues."
sidebar:
  order: 4
---

M8 ajoute trois modules au-dessus du rendu et de l'interface. Sources : `src/scene/README.md`, `src/sim/README.md`,
`src/app/README.md`, `docs/03-ARCHITECTURE_ET_PORTABILITE.md` §1 et §13, et les choix du découpage de M8
(`docs/QUESTIONS.md`, A79).

| Module | Niveau | Archive | Dépend de |
| --- | --- | --- | --- |
| `scene` | 6 | `libscene.a` | `core` |
| `sim` | 9 | `libsim.a` | `scene`, `core` |
| `app` | 10 (entre `sim` et `game`, **provisoire, à confirmer par l'auteur**) | `libapp.a` | tous les modules du dessous |

## scene : un ECS maison

Les composants du jeu sont définis dans une bibliothèque qui peut être déchargée. Le monde ne garde donc **aucun type C++,
aucune table virtuelle ni identifiant de type** venant d'elle : un type est enregistré par nom avec sa taille, son
alignement, sa version, sa description de champs (`core::FieldDesc` avec décalage, taille et sous-champs) et les octets de
sa valeur par défaut, tous copiés côté moteur. Une bibliothèque ECS du commerce (EnTT) garderait des identifiants issus de
la bibliothèque déchargée ; c'est la raison du choix (A79, choix 1).

- Stockage : un `ComponentPool` par type, ensemble clairsemé, octets denses et alignés, retrait par échange avec le dernier.
  Les composants sont trivialement copiables et déplacés par `memcpy`.
- Identifiants : `Entity` et `ComponentId` portent une génération ; un identifiant périmé est refusé.
- Transformations : locales en double, composées sans fonction transcendante (même résultat partout avec
  `-ffp-contract=off`) ; seules les entités sales et leurs descendants sont recalculées.
- Sauvegarde : champ par champ d'après les descriptions, en JSON ou MessagePack ; empreinte FNV-1a pour les tests de
  rejouabilité.
- Migration (`migrateComponent`) : chaque valeur est écrite avec l'ancienne description et relue par nom de champ dans la
  nouvelle ; c'est le chemin unique de GAME-03.

## sim : déterminisme et thread

`Simulation` possède le monde, un `core::Random` (PCG32, même suite partout) et les systèmes, triés par phase, ordre et nom.
L'état rejouable est : monde, générateur, numéro de pas, entrées en attente. Les systèmes n'en font pas partie.
`tools/check_deterministic_math.py` (dans `make check`) refuse horloge, `rand()` et fonctions transcendantes dans les
fichiers rejoués.

`SimulationThread` déplace la boucle sur un thread dédié. Un seul point d'échange vers le rendu : un `RenderSnapshot`
immuable, publié par `SnapshotBuffer` (trois emplacements, sans verrou). Les entrées et les événements passent par deux
`core::SpscQueue`. Aucune structure non sûre entre threads (`EventBus`, `Vfs`, `Config`, `FileWatcher`) n'est partagée ;
l'arène de pas appartient au thread de simulation. Le test concurrent tourne sous ThreadSanitizer en CI
(`make demo-test-unit SANITIZE=thread`).

## app : assembler

`app` existe parce qu'aucune couche ne pouvait assembler `platform`, `renderer`, `assets`, `anim`, `scene`, `ui` et `sim`
sans dépendre d'une couche supérieure (A79, choix 3). Si l'auteur refuse ce niveau, `GameModule` irait dans `sim` et la
façade attendrait M10.

| Partie | Rôle |
| --- | --- |
| `Application` | façade : crée et détruit dans l'ordre fenêtre, périphérique, renderers, interface, simulation, jeu, hôte de vue ; boucle d'image |
| `SceneRenderSystem`, `render_components.hpp` | composants de rendu définis par le moteur (ressources par `core::Handle<T>`), dessinés seuls, instanciés ou en foule |
| `GameModule`, `GameContext`, `game_api.hpp` | bibliothèque de jeu : chargement (copie sous un nom unique), rechargement surveillé, migration, retour en arrière par copie du monde, version statique |
| `ParamsFile`, `ParamsHost<T>`, `Params<T>` | paramètres JSON relus à chaud |
| `ViewHost` | vues `.cui` compilées dans la bibliothèque de jeu (A79, choix 5), reconstruites et recréées à l'enregistrement |

### Liaison du moteur et de la bibliothèque de jeu

La bibliothèque de jeu ne contient **pas** de copie du moteur : elle appelle celui de l'exécutable, qui exporte ses
symboles (Linux : `-rdynamic` ; macOS : bibliothèque liée avec `-undefined dynamic_lookup` ; Windows MinGW : exécutable
avec `--export-all-symbols` et bibliothèque d'import, limite de 65 535 symboles). Le journal, les registres et le monde vus
par le jeu sont donc ceux de l'hôte. `make check` (`tools/check_game_static.py`) vérifie sur le binaire que la bibliothèque
n'a aucun état (règle 13).

### Version finale

Avec `OROSMO_GAME_STATIC`, `OROSMO_GAME_EXPORT` déclare une fonction ordinaire, `GameModule::fromApi` démarre la table liée
et `view_reloader.cpp` n'est pas compilé : l'exécutable final n'a aucun symbole de `DynamicLibrary`, `GameModule::load`
ni de rechargement des vues (vérifié par `nm` dans `make demo-test` et `make e2e-init`).

## Points à confirmer par l'auteur

- Le niveau `app` lui-même (choix 3).
- Les vues rechargées par la bibliothèque de jeu plutôt qu'un interpréteur du langage (choix 5).

## Voir aussi

- [Couches et modules](/interne/couches/)
- [Entités, composants et transformations](/guides/scene/entites-composants/), [Simulation à pas fixe](/guides/scene/simulation/),
  [Logique de jeu rechargeable](/guides/scene/logique-rechargeable/), [La façade app::Application](/guides/application/facade/)
