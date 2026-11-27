# M8 : scène, simulation, logique de jeu rechargeable, API simplifiée (`dev` → `main`)

## Résumé

Jalon M8, version **`v0.7.0`** (fichiers de version à jour ; ni tag ni release : l'auteur publie). Trois **modules neufs** :
`scene` (ECS maison, hiérarchie, transformations en double, sauvegarde JSON et MessagePack, empreinte), `sim` (simulation à
pas fixe déterministe et rejouable, thread de simulation optionnel à cadence réglable, instantané sans verrou) et `app`
(façade `app::Application`, module de jeu rechargeable `GameModule`, paramètres JSON, hôte des vues `ViewHost`, composants
de rendu). La **logique de jeu** se recharge sans redémarrage ni perte d'état (migration des composants, repli
sauvegarde/restauration, version finale statique) ; les **vues `.cui`** se rechargent à l'enregistrement ; l'interface gagne
la **navigation au clavier et à la manette** et des **outils de debug** (console, inspecteurs, profileur). **API simplifiée**
(A77) : programme de départ d'`orosmo init` **403 → 59** lignes, tutoriel 3D **459 → 170**. Performances (A61) : images en
vol réglables, graphe de passes gardé. Démo : `--shadow-quality`, `--sim-agents`, `--debug-tools`, `--frames-in-flight`,
`--graph-cache`. 18 lots (PR #17 à #35, plan en #16, et la PR de L15). Détail : `docs/releases/M8.md`.

## Identifiants

15 identifiants passent à **Fait** : ECS-01, ECS-02, ECS-03, SIM-01, UI-04, UI-08, UI-09, UI-10, GAME-01 à GAME-07 ; PLAT-03
passe d'Absent à **Partiel** (manettes pour la navigation seulement, G223). Totaux (167 lignes, comptés par script sur
`docs/02`) : **Fait 68, Partiel 45, Absent 35**, Archivé 11, Hors moteur 7, Incompatible 1. Avant M8 : Fait 53, Partiel 44,
Absent 51.

## Changements incompatibles

- `platform::Window::init` rend `core::Result<>` au lieu d'un `int` : `window.init() != 0` ne compile plus (G270).
- `core/reflection.hpp` et `scene/transform.hpp` n'incluent plus que les types de glm : inclure `core/math.hpp` pour les
  fonctions (G333) ; `sim::StepContext::inputs` est un `ConstView<Input>`.
- Gabarit d'`orosmo init` : `make` construit en debug quand `src/game/` existe (G317) ; Vulkan par défaut avec repli sur
  OpenGL ; le cube tourne par la logique de jeu (immobile si la bibliothèque manque, G372).
- Pas de nouvelle dépendance vcpkg. `tools/layers.json` : niveaux `scene`, `sim`, `app`, `game` ; archives `libscene.a`,
  `libsim.a`, `libapp.a` à lier (le `engine.mk` du paquet les ajoute).
- Démo et captures de référence inchangées (les nouveautés s'activent par option).

## À confirmer par l'auteur

- **Module `app`** et son niveau entre `sim` et `game` (A79 choix 3) ; si refusé, `GameModule` irait dans `sim` et la façade
  attendrait M10.
- **Vues rechargées par la bibliothèque de jeu** (A79 choix 5), sans interpréteur du langage ; un changement des champs liés
  demande un redémarrage (G361).
- **Changements d'API cassants** ci-dessus (G270, G333) et nouveau défaut de `make` du gabarit (G317).
- **Choix pris sur les ambiguïtés**, lot par lot, sous A79 dans `docs/QUESTIONS.md` (L1 à L16, validation de l'agent
  validateur, choix 1 à 12) : ECS maison, composants trivialement copiables, sérialisation par le moteur, mode sans
  périphérique de `--sim-agents`, deux images en vol par défaut (L16), lecture de « changement de hiérarchie » (L15)…
- **F10** pour le plein écran dans les builds debug de la démo (F12 réservé aux outils de debug, A79 L10 point 7).
- **A80** : qualité d'ombre selon l'importance des objets, planifiée en M10, fonctionnement à confirmer avant d'être validé
  (G290).

## Commande de démo

```bash
make all-debug
apps/demo/bin/debug/main --sim-agents 500 --seed 42 --sim-steps 1000   # sans fenêtre ni GPU ; même empreinte à chaque lancement
apps/demo/bin/debug/main --debug-tools --debug-tab entities           # F12 : console, inspecteurs, profileur
make e2e-init      # projet orosmo init : logique modifiée et rechargée sans perte d'état, .cui modifié et vue recréée
```

## Tests

Clone propre de `feat/M8-L15-finalisation` (`8b255e6`), Linux x64, g++ 13.3, Mesa, Intel Arc : `make all-debug` sans avertissement ;
`make demo-test` **924 / 924** ; ASan + UBSan **924 / 924**, 0 message ; TSan (`demo-test-unit`) **562 / 562**, 0 rapport ;
`make check`, `make cui-test`, tests Python, `make e2e-init` verts (logique rechargée en 1,80 s, vue en 2,65 s) ; démo OpenGL et
Vulkan sans message de validation, captures identiques ; site : build, liens et ancres verts. Détail et critères de
`docs/05` un par un : `docs/releases/M8.md`.

## Non prouvé

Windows (MinGW) et macOS (clang `-Werror`) : par la CI de cette MR seulement ; un seul GPU (Intel, Mesa) ; boucle fenêtrée
de la façade essayée à la main (G350, G374) ; tutoriel 3D compilé mais pas lancé par un test (G370) ; job `site.yml` non
observé sur les pages neuves (G383).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
