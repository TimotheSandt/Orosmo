---
title: Options de la démo
description: Toutes les options de ligne de commande de apps/demo, avec leur valeur par défaut, leurs bornes et les combinaisons refusées.
sidebar:
  order: 1
---

La démo est le binaire `apps/demo/bin/debug/main` (ou `bin/release/<nom>`), construit par `make all-debug`. Syntaxe : `--option valeur`
ou `--option=valeur` ; `-h` équivaut à `--help`. Une option inconnue arrête la démo avec `Unknown argument: …` et rappelle
l'usage. Pour passer des options, lancez le binaire (voir [Explorer la démo](/demarrer/demo/)). Source :
[`apps/demo/src/launch_options.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/launch_options.cpp).

Les valeurs de `--api`, `--ui`, `--hud`, `--theme`, `--tab`, `--post`, `--aa`, `--lighting`, `--shadow-quality`, `--gpu-timings`, `--graph-cache`, `--vsync` et
`--direct-present` ignorent la casse. Celles de `--shadows`, `--gpu-driven`, `--scene`, `--debug-view` et `--transparent-shadow` doivent
être écrites en minuscules.

## Backend

| Option | Valeurs | Défaut | Effet |
|---|---|---|---|
| `--api`, `--backend` | `vulkan` (`vk`), `opengl` (`gl`), `metal` | `vulkan` | API graphique. Si Vulkan ne démarre pas, la raison est écrite et OpenGL est utilisé. `metal` est refusé (Absent). |
| `--strict-backend` | (aucune) | repli permis | Interdit le repli sur OpenGL : sortie en erreur si Vulkan ne démarre pas. |
| `--choose-api` | (aucune) | non | Menu interactif au démarrage (Entrée choisit Vulkan). |
| `--list-apis` | (aucune) | non | Écrit l'état des trois API et quitte. |
| `--help`, `-h` | (aucune) | non | Écrit l'usage et quitte. |

## Cadence

| Option | Valeurs | Défaut | Effet |
|---|---|---|---|
| `--vsync` | `on`, `off`, ou sans valeur (= `on`) | `on` | Synchronisation verticale. `off` : pas de limite. Sans effet avec `--headless`. |
| `--fps-limit` | `0`, ou 10 à 1000 | `0` (aucune) | Limite à N images par seconde et coupe la vsync. |
| `--direct-present` | `on`, `off` | `on` | Vulkan dessine directement dans l'image de la fenêtre. `off` revient à la copie. Sans effet sur OpenGL et avec `--headless`. |
| `--gpu-timings` | `auto`, `on`, `off` | `auto` | Minuteries GPU. `auto` : seulement quand le panneau de performances affiche des temps GPU (`--hud full` ou `graph`). |
| `--frames-in-flight` | 1 à 3 | `2` | Nombre d'images que le processeur prépare d'avance sur le GPU. |
| `--graph-cache` | `on`, `off` | `on` | Garde le graphe de passes d'une image à l'autre tant que plugins, signatures de leurs passes, cibles, tailles et modes de rendu ne changent pas (un plugin sans signature, comme les post-traitements, le GPU-driven et les vues, le fait reconstruire à chaque image) ; `off` le reconstruit à chaque image. |

## Capture et mode sans fenêtre

| Option | Valeurs | Défaut | Effet |
|---|---|---|---|
| `--headless` | (aucune) | non | Aucune fenêtre : périphérique hors écran, pas de temps fixe (1/60 s), aucune entrée. Rend `--capture-frames` images puis quitte. |
| `--capture` | dossier | aucun | Après `--capture-frames` images, écrit `scene.png` et `ui.png` dans le dossier, puis quitte. Un chemin relatif part du dossier de lancement. |
| `--capture-frames` | 1 à 100000 | `10` | Nombre d'images rendues avant la capture ou la sortie. |
| `--hover` | identifiant | aucun | Place le pointeur simulé sur l'élément d'interface portant cet `id` pendant la capture. |
| `--click` | identifiant | aucun | Clique sur cet élément pendant la capture. |
| `--focus` | identifiant | aucun | Donne le focus clavier à cet élément ; l'anneau de focus est dessiné (au moins 2 images). |
| `--size` | `WxH` | `1280x720` | Taille de la vue. Minimum 640x400, maximum 16384 par côté. |

## Interface

| Option | Valeurs | Défaut | Effet |
|---|---|---|---|
| `--ui` | `off`, `hud`, `full` | `hud` | Aucune interface, panneau de performances seul, ou interface complète. |
| `--hud` | `off`, `fps`, `full`, `graph` | `full` | Niveau du panneau de performances. `Ctrl+F` passe au suivant : off, fps, full, graph. |
| `--theme` | `dark`, `light`, `framed` | `dark` | Thème de l'interface. |
| `--tab` | `scene`, `widgets`, `controls`, `color`, `windows`, `layout`, `themes` | `scene` | Onglet ouvert de l'interface complète. |

## Rendu

| Option | Valeurs | Défaut | Effet |
|---|---|---|---|
| `--shadows` | `on`, `off` | `off` | Ombres en cascade avec cache. |
| `--shadow-quality` | `low`, `medium`, `high` | `high` | Préréglage des ombres (résolution et nombre des cascades, taille de l'atlas, résolution des projecteurs, rayon du PCF, portée, cadence de mise à jour). `high` est égal aux réglages du moteur (`ShadowSettings{}`). Demande `--shadows on`. |
| `--transparent-shadow` | `none`, `full`, `masked`, `dithered` | absent | Ombre des vitres de `--scene mixed`. Demande `--shadows on`. Absent : les vitres ne projettent pas d'ombre. |
| `--post` | `none`, `hdr`, `bloom`, `full` | `none` | `hdr` : scène HDR et tonemapping ACES. `bloom` : + bloom. `full` : + exposition automatique, étalonnage par LUT, vignettage. |
| `--aa` | `none`, `fxaa`, `smaa`, `taa` | `none` | Anticrénelage. |
| `--lighting` | `forward`, `forwardplus` (ou `forward+`) | `forward` | `forwardplus` : éclairage clusterisé ; retombe sur forward sans compute. |
| `--lights` | 0 à 100000 | `0` | Nombre de lumières ponctuelles déterministes, en plus du soleil. |

## Scène

| Option | Valeurs | Défaut | Effet |
|---|---|---|---|
| `--scene` | `default`, `mixed`, `anim` | `default` | `mixed` : transparents triés et 2D dans le monde. `anim` : personnage glTF animé et foule de 1000 agents ; sa texture est rechargée à chaud. |
| `--instances` | 0 à 1000000 | `0` | N objets instanciés autour de la cour (culling, LOD, un draw call par lot). |
| `--gpu-driven` | `on`, `off` | `off` | Culling des `--instances` sur le GPU (multi-draw indirect, occlusion Hi-Z). Retombe sur le CPU sans compute. |

## Debug

| Option | Valeurs | Défaut | Effet |
|---|---|---|---|
| `--debug-tools` | (aucune) | non | Ouvre les [outils de debug](/guides/interface/outils-debug/) au démarrage (F12 les ouvre et ferme). |
| `--debug-tab` | `console`, `widgets`, `entities`, `profiler` | `widgets` | Onglet des outils de debug ; ouvre les outils. |
| `--debug-view` | `none`, `normals`, `albedo`, `overdraw`, `lod`, `wireframe`, `motion` | `none` | Mode d'affichage de debug. `lod` n'a d'effet qu'avec `--instances`. |
| `--freeze-frustum` | (aucune) | non | Fige le frustum de culling des objets instanciés à la première image et le dessine (demande `--instances`). |

## Simulation

| Option | Valeurs | Défaut | Effet |
|---|---|---|---|
| `--sim-agents` | 1 à 1000000 | aucun | Fait tourner N agents sur la [simulation à pas fixe](/guides/scene/simulation/), sans fenêtre ni périphérique graphique, écrit l'empreinte de l'état et quitte. |
| `--seed` | entier non signé de 64 bits | `42` | Graine de la simulation. Demande `--sim-agents`. |
| `--sim-steps` | 0 à 10000000 | `1000` | Nombre de pas joués. Demande `--sim-agents`. |

Même graine et même nombre de pas : même empreinte à chaque lancement (`tests/demo/agent_simulation_tests.cpp` la compare
à une valeur figée).

## Combinaisons refusées

| Combinaison | Message | Raison |
|---|---|---|
| `--click` avec `--capture-frames` < 3 | `--click requires --capture-frames 3 or more` | le clic a besoin de quelques images |
| `--hover` avec `--capture-frames` < 2 | `--hover requires --capture-frames 2 or more` | idem |
| `--focus` avec `--capture-frames` < 2 | `--focus requires --capture-frames 2 or more` | idem |
| `--seed` ou `--sim-steps` sans `--sim-agents` | `--seed and --sim-steps require --sim-agents` | |
| `--sim-agents` avec `--headless` ou `--capture` | `--sim-agents runs without window nor graphics device: it cannot be combined with --headless or --capture` | la simulation n'ouvre aucun périphérique |
| `--fps-limit` > 0 avec `--vsync on` explicite | `--fps-limit turns the vertical synchronization off: it cannot be combined with --vsync on` | deux cadences contradictoires |
| `--aa taa` avec `--instances` > 0 | `--aa taa is not supported with --instances: the instanced objects write no motion vectors; anti-aliasing disabled` | pas une erreur : la démo continue sans anticrénelage |
| `--api metal` | refus avec message du backend | Metal n'existe pas encore |
| `--size` sous 640x400 | `Invalid value for --size (expected WxH, at least 640x400): …` | taille minimale |
| `--gpu-driven`, `--shadows` autre que `on` ou `off` | `Invalid value for --gpu-driven (expected on or off)` | |

Les messages sont ceux du code, en anglais.

## Exemples

```bash
apps/demo/bin/debug/main --api opengl --headless --capture /tmp/out
apps/demo/bin/debug/main --scene mixed --shadows on --transparent-shadow dithered
apps/demo/bin/debug/main --shadows on --shadow-quality low
apps/demo/bin/debug/main --sim-agents 500 --seed 42 --sim-steps 1000
apps/demo/bin/debug/main --debug-tools --debug-tab entities
apps/demo/bin/debug/main --instances 10000 --gpu-driven on --hud graph
apps/demo/bin/debug/main --ui full --theme light --tab widgets --headless --click fruit --capture /tmp/dropdown --capture-frames 5
```
