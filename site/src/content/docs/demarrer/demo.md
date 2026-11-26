---
title: Explorer la démo
description: Lancer la démo du dépôt et activer chaque fonction du moteur par une option de ligne de commande.
sidebar:
  order: 5
---

La démo (`apps/demo/`) est la seule application du dépôt. Elle montre le moteur et la bibliothèque d'interface ; son code
(`apps/demo/src/Game/Game.cpp`) est aussi la meilleure source d'exemples. Il faut d'abord
[compiler le moteur](/demarrer/compiler-le-moteur/) ; la démo n'est pas dans le paquet d'une release.

## Lancer

```bash
make run                                   # sans option (`make run` ne transmet aucun argument)
apps/demo/bin/debug/main --shadows on      # avec options : lancer le binaire
```

Par défaut la démo affiche la scène et le panneau de performances. Les touches :

| Touche | Effet |
|---|---|
| `Échap` ou `F1` | ouvre l'interface complète ; la ferme si elle est ouverte. N'arrête jamais la démo (bouton `Quit`) |
| `Ctrl+F` | niveau suivant du panneau de performances : off, fps, full, graph |
| `Z` `Q` `S` `D`, `Espace`, `Maj gauche` | avance, gauche, recule, droite, monte, descend. La démo suppose un clavier AZERTY : sur QWERTY, ce sont les touches W, A, S, D qui sont aux mêmes places que Z, Q, S, D de l'AZERTY, mais `kZ` désigne la touche qui porte la lettre Z (celle du bas à gauche). Pour lier W A S D, voir [Fenêtre, clavier et souris](/guides/application/fenetre-entrees/) |
| clic droit maintenu | oriente la caméra (le curseur est capturé) |
| `A` / `E` | caméra plus rapide / plus lente tant qu'elles sont tenues |
| `F11` | plein écran sans bordure |

L'interface complète a des onglets (`scene`, `widgets`, `controls`, `color`, `windows`, `layout`, `themes`), un choix de
thème, de cadence d'images et de minuteries GPU, un journal d'événements et un bouton « Capture PNG ». Un onglet ou un
thème s'ouvre directement avec `--tab` et `--theme`.

## Une option par fonction

| Pour voir | Commande (après `apps/demo/bin/debug/main`) | Guide |
|---|---|---|
| Ombres en cascade | `--shadows on` | [Ombres](/guides/rendu-3d/ombres/) |
| HDR, bloom, étalonnage | `--post full` | [Post-traitement](/guides/rendu-3d/post-traitement/) |
| Anticrénelage | `--aa smaa` (ou `fxaa`, `taa`) | [Anticrénelage](/guides/rendu-3d/anticrenelage/) |
| 64 lumières, forward+ | `--lighting forwardplus --lights 64` | [Lumières](/guides/rendu-3d/lumieres/) |
| 10 000 objets instanciés | `--instances 10000` | [Instancing et culling](/guides/rendu-3d/instancing-culling/) |
| Culling sur le GPU | `--instances 10000 --gpu-driven on` | [Culling sur le GPU](/guides/rendu-3d/rendu-gpu/) |
| Transparents et 2D dans le monde | `--scene mixed` | [Transparence](/guides/rendu-3d/transparence/) |
| Personnage animé et foule de 1000 agents | `--scene anim` | [Squelettes](/guides/animation/squelettes/) |
| Normales, surdessin, filaire… | `--debug-view normals` | [Debug visuel](/guides/rendu-3d/debug-visuel/) |

`--aa taa` est refusé avec `--instances` : l'anticrénelage est alors désactivé et un message le dit. Toutes les valeurs sont
dans [Options de la démo](/reference/options-demo/).

## Captures hors écran

```bash
apps/demo/bin/debug/main --api opengl --headless --capture /tmp/captures    # scene.png et ui.png
apps/demo/bin/debug/main --headless --ui off --shadows on --capture /tmp/ombres --capture-frames 5
```

`--headless` n'ouvre aucune fenêtre ; la démo rend `--capture-frames` images (10 par défaut), écrit `scene.png` (la scène)
et `ui.png` (scène et interface), puis quitte. `make demo-capture` rend toute la série de captures de référence. Voir
[Cadence d'images, mode sans fenêtre, captures](/guides/application/cadence-captures/).

## Limites

`--direct-present` (Vulkan) peut faire scintiller l'image à très haute cadence sans vsync ; `--direct-present off` revient
à la copie. Voir [`docs/GAPS.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/GAPS.md).
