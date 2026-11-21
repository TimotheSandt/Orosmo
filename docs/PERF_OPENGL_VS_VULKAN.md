# Performance comparée OpenGL / Vulkan de la démo (M4)

Mesure du 2026-11-21, démo unique `apps/demo`, binaire **release** (`-O3`, couches de validation coupées), scène par défaut (graine 1).

## Verdict

Sur cette machine et cette démo, **Vulkan est un peu plus rapide qu'OpenGL en headless** (-12 % en frame CPU sans interface, -20 % avec
l'interface complète) et **plus lent d'environ 7 % en fenêtre sans interface** (+0,3 ms par image : copie de l'image de fond vers la
chaîne d'échange, plus coûteuse que le `swapBuffers` du pilote OpenGL) ; avec l'interface complète il reste plus rapide en fenêtre
(-14 %). La démo est **limitée par le processeur** (3,7 à 11 ms par image pour 0,4 à 4,5 ms de travail GPU) : Vulkan ne peut pas montrer ici
son avantage structurel (moins de surcharge du pilote sur de grandes scènes), il ne montre que le coût de son encodage. Ne pas conclure
« Vulkan est X fois plus rapide » de ce tableau.

## Méthode

- `tools/perf_compare.py` : lance la démo `--capture-frames 600` 7 fois par backend et par mode, **en alternance** OpenGL / Vulkan, après une exécution de chauffe écartée par couple ; médiane, minimum, maximum et écart interquartile (IQR) sur les 7 exécutions.
- Modes : `headless` (appareil hors écran, pas de présentation) et `windowed` (fenêtre réelle, chaîne d'échange, sans synchronisation verticale).
- Interface : `--ui off` (scène seule) et `--ui full` (interface complète, onglet scène, thème sombre).
- **Frame CPU** = durée de `Game::run()` / 600 (horloge `steady_clock`, journalisée par `apps/demo/src/main.cpp` : `Perf: loopMs=`). Elle inclut l'attente de l'image précédente (contre-pression du GPU) ; ce n'est pas un temps CPU pur.
- **Temps GPU par passe** : moyennes du `Profiler` (`Render`, `RenderScene`, `Upscale`, `UIUpscale`) mesurées par les requêtes de temps du RHI (`ProfilerGpuTimerFactory`) ; `SwapBuffers` est une mesure CPU. Les valeurs sont celles du journal `Perf: <nom> avgMs=`.
- Résultats bruts : `docs/BASELINE/perf-opengl-vs-vulkan-ui-off.json` et `...-ui-full.json`.

Reproduire (affichage requis pour le mode `windowed`) :

```bash
make all-release                               # moteur + démo release (apps/demo/bin/release/Orosmo)
python3 tools/perf_compare.py --runs 7 --frames 600 --ui off  --out /tmp/perf-off.json
python3 tools/perf_compare.py --runs 7 --frames 600 --ui full --out /tmp/perf-full.json
# sans affichage : --modes headless ; GPU logiciel Vulkan : ENGINE_VULKAN_DEVICE=llvmpipe (OpenGL reste sur son pilote)
```

## Conditions

| Élément | Valeur |
| --- | --- |
| GPU | Intel Arc Graphics (Meteor Lake-P, iGPU), Intel Core Ultra 7 155H, 6 coeurs visibles |
| Pilote | Mesa 26.1.6 (ANV pour Vulkan 1.4.354, Iris pour OpenGL 4.6 core) ; noyau 7.2.15, KMD Xe |
| Système | Pop!_OS 24.04, X11 (`DISPLAY=:1`), gouverneur CPU `powersave` (fréquence variable) |
| Image | 1280x720, 600 images par exécution, 7 exécutions + 1 de chauffe |
| Bruit | un autre agent compilait sur la même machine pendant une partie des mesures ; les IQR le montrent (OpenGL headless sans interface : IQR de 0,3 ms sur 4,4) |

## Résultats (médiane sur 7 exécutions, millisecondes par image)

### Interface coupée (`--ui off`)

| Mode | Métrique | OpenGL | Vulkan | Vulkan / OpenGL |
| --- | --- | ---: | ---: | ---: |
| headless | frame CPU | 4,364 (IQR 0,317) | 3,823 (0,077) | -12 % |
| headless | GPU `Render` | 2,411 | 0,438 | voir réserve |
| headless | GPU `RenderScene` | 0,140 | 0,091 | |
| headless | GPU `Upscale` | 0,097 | 0,148 | +53 % |
| windowed | frame CPU | 4,594 (0,199) | 4,898 (0,069) | +7 % |
| windowed | GPU `Render` | 0,665 | 0,428 | |
| windowed | CPU `SwapBuffers` / présentation | 0,066 | 0,121 | +0,055 ms |

### Interface complète (`--ui full`)

| Mode | Métrique | OpenGL | Vulkan | Vulkan / OpenGL |
| --- | --- | ---: | ---: | ---: |
| headless | frame CPU | 10,261 (0,085) | 8,212 (0,078) | -20 % |
| headless | GPU `Render` | 4,483 | 0,820 | voir réserve |
| headless | GPU `UIUpscale` | 1,989 | 0,385 | |
| windowed | frame CPU | 11,139 (0,244) | 9,525 (0,176) | -14 % |
| windowed | GPU `Render` | 4,692 | 0,818 | voir réserve |
| windowed | GPU `UIUpscale` | 2,028 | 0,387 | |

## Limites et réserves

- **Un seul GPU** (iGPU Intel, mémoire partagée avec le processeur) et un seul pilote : aucune conclusion pour un GPU discret NVIDIA / AMD, où l'écart de surcharge du pilote est différent.
- **Les temps GPU ne sont pas comparables entre backends.** Les requêtes de temps OpenGL (Iris) donnent des valeurs 5 à 6 fois supérieures à celles des horodatages Vulkan pour le même travail (`Render` 4,5 contre 0,8 ms), et la somme des passes OpenGL n'atteint pas `Render` ; ce sont probablement des attentes d'instrumentation du pilote, pas du temps d'exécution. Seule la **frame CPU**, mesurée avec la même horloge des deux côtés, est comparable. Les temps GPU par passe sont donnés pour comparer les passes d'un même backend.
- La charge GPU de la démo est faible (< 1 ms) : la frame CPU est dominée par la génération d'interface, les tuiles et l'encodage. Une scène plus lourde (beaucoup de dessins) est nécessaire pour mesurer l'avantage attendu de Vulkan ; à faire avec le terrain procédural (M5+).
- Les barrières de Vulkan sont conservatrices (« tout contre tout », voir `src/rhi/vulkan/README.md`) : une marge de gain existe côté Vulkan.
- Mesure en fenêtre sans synchronisation verticale ; avec elle, les deux backends sont bornés par l'écran.
- llvmpipe / lavapipe : non mesurés (le temps GPU logiciel n'a pas de sens pour comparer des API) ; la parité d'image a été vérifiée sur lavapipe (voir `docs/BASELINE/README.md`).
