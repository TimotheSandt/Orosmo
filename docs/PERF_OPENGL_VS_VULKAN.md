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
- **Temps GPU par passe** : moyennes du `Profiler` (`Render`, `RenderScene`, `Upscale`, `UIUpscale`) mesurées par les requêtes de temps du RHI (`ProfilerGpuTimerFactory`) ; `SwapBuffers` est une mesure CPU. Les valeurs sont celles du journal `Perf: <nom> avgMs=`. Les minuteries GPU ne tournent que si la démo les demande (`--gpu-timings auto|on|off`, `auto` = seulement quand le panneau de performances affiche des temps GPU) : `tools/perf_compare.py` passe `--gpu-timings on`, à faire de même pour toute mesure à la main (sinon `Render` et `RenderGraph/...` sont des temps CPU).
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

## Coût CPU par image et premier affichage (fix/M5-ui-perf, 2026-11-22)

Symptôme : moins de 1 ms de GPU mais 150 à 200 FPS, et un gel d'environ une seconde à la première ouverture de l'interface complète.
Mesures : binaire release, Intel Arc (MTL), Mesa 26.1.6 ; machine chargée par d'autres compilations (charge 7 à 15 sur 6 coeurs) :
les valeurs absolues sont bruitées, les écarts restent nets.

Causes trouvées (minuteurs temporaires) :

- `DemoUiState::tick` recalculait la prévisualisation 96x96 et la boussole 64x64 pixel par pixel à chaque image, interface fermée
  comprise (~2,3 ms CPU par image). Elles ne sont plus dessinées que visibles (onglet Windows de l'interface complète) et modifiées.
- Premier affichage : génération des atlas MSDF (msdfgen, 5 à 10 ms par glyphe, 0,7 à 1,1 s par taille de police ; l'interface
  complète en demande 3 de plus que le HUD, l'onglet Themes 2 dont un étendu de 4 s), puis la scène (~0,5 s : pipelines, pilote).
  Corrections : glyphes calculés en parallèle (`TaskSystem`, ~2,5x ici), cache SPIR-V sur disque
  (`userDataDirectory()/Orosmo/shader_cache`), préchauffe avant la boucle en fenêtre (scène, HUD, interface complète sur l'onglet
  courant ; ~1,3 à 1,5 s à charge normale, 2,5 à 4,5 s sous forte charge). Les autres onglets restent paresseux (0,3 à 1,5 s chacun).

`tools/perf_compare.py --runs 3 --frames 600` (`frameCpuMs` = boucle / images, premier affichage compris ; `frameMedian` : médiane
du temps CPU par image, journal `Perf: frameMedianMs=`, nouveau) :

| Mode | `--ui` | API | avant `frameCpuMs` | après `frameCpuMs` | après `frameMedian` |
| --- | --- | --- | ---: | ---: | ---: |
| headless | off | OpenGL | 4,33 | 1,61 | 0,69 |
| headless | off | Vulkan | 4,75 | 1,70 | 0,45 |
| headless | hud | OpenGL | 6,03 | 2,43 | 0,87 |
| headless | hud | Vulkan | 6,28 | 2,08 | 0,60 |
| headless | full | OpenGL | 9,01 | 5,21 | 1,67 |
| headless | full | Vulkan | 9,83 | 8,66 (bruit) | 1,45 |
| fenêtre | off | OpenGL | 4,80 | 2,02 | 1,07 |
| fenêtre | off | Vulkan | 5,15 | 1,80 | 0,81 |
| fenêtre | hud | OpenGL | 6,10 | 1,84 | 1,10 |
| fenêtre | hud | Vulkan | 6,51 | 2,08 | 1,15 |
| fenêtre | full | OpenGL | 9,52 | 5,35 | 2,45 |
| fenêtre | full | Vulkan | 9,88 | 4,75 | 2,26 |

Première image (fenêtre, préchauffe faite) : 1 à 12 ms au lieu de 0,6 à 3,3 s ; aucun pic au-dessus de 50 ms dans la boucle hors
écriture des captures PNG (exclue du temps d'image). Coûts CPU restants par image : exécution du graphe et encodage (`CPU/Graph`,
0,5 à 1,8 ms, interface comprise), présentation (`SwapBuffers`, 0,1 à 0,4 ms en fenêtre), mise à jour de l'interface complète
(0,2 à 0,3 ms). Le panneau de performances (`--hud`) coûte 0,1 à 0,3 ms (mise à jour + liste de dessin) plus 0,1 à 0,3 ms
d'encodage 2D aux niveaux `full` et `graph`.

## M6 : skinning et foules (2026-11-24)

Mesure du coût GPU du skinning (ANIM-01, compute) et des foules (ANIM-04, skinning dans le vertex shader, un draw call par
primitive skinnée) sur le personnage de la démo `--scene anim` agrandi à une taille réaliste : 10 147 sommets (tronc de 200 anneaux
de 50 sommets + coiffe), 8 os, 2 primitives skinnées. Machine : la même que ci-dessus (Intel Arc Graphics Meteor Lake-P, Mesa 26.1.6 :
ANV Vulkan 1.4.354, Iris OpenGL 4.6 ; noyau 7.2.15, KMD Xe ; gouverneur `powersave`). Binaire de tests **release**, couches de
validation actives (Vulkan), image 1280x720, minuteries GPU (`renderer::GpuTimer`) ; médiane de 30 images après 3 de chauffe, deux
exécutions (la plage donne les deux médianes).

```bash
make demo-test TEST_ARGS='-ts=apps/demo-anim'                                  # construit les tests (le cas mesuré est ignoré)
cd apps/demo && ./bin/tests/tests -ts=apps/demo-anim -tc='*GPU cost*' --no-skip  # tests/apps/demo_anim_scene_tests.cpp
```

| Mesure (GPU, ms) | OpenGL | Vulkan |
| --- | ---: | ---: |
| Skinning en compute d'un personnage (10 147 sommets) | 0,0004 (voir réserve) | 0,054 à 0,056 |
| Image : personnage seul (skinning + passe de scène) | 0,15 à 0,22 | 0,26 à 0,30 |
| Image : personnage + 1000 agents (2 draw calls, 2000 instances, 10,1 M sommets skinnés) | 14,7 à 17,8 | 15,1 à 18,5 |

Lecture : la foule coûte environ 15 ms GPU par image pour 10 millions de sommets skinnés (environ 1,5 ns par sommet), sur les deux
backends à 5 % près ; elle est limitée par le traitement des sommets (aucun culling par agent ni LOD, G199). Pour 60 images par
seconde sur ce GPU intégré, viser environ 1 million de sommets skinnés par image (ex. 1000 agents de 1000 sommets). Le personnage
de la démo par défaut (361 sommets, 1000 agents, 0,36 M sommets) tient sous 1,3 ms GPU pour toute l'image (`--ui hud`, Vulkan).

Réserves : sur OpenGL, les horodatages qui encadrent seulement un dispatch de compute rendent une durée quasi nulle (0,4 µs, non
crédible pour 10 000 sommets) : le pilote Iris ne sérialise pas l'horodatage avec le compute ; la mesure Vulkan (barrières
comprises) est la référence. Les palettes des agents sont calculées sur le CPU (`CrowdRenderer::setAgents`, G199) : ce coût CPU
n'est pas dans ce tableau.

## M8 : images en vol et graphe gardé (A61 G et C, lot L16, 2026-11-26)

Même machine (Intel Arc Graphics Meteor Lake-P, Mesa 26.1.6 : ANV Vulkan 1.4.354, Iris OpenGL 4.6). Binaires **release** :
« avant » = `origin/dev` à `850cdf7f` ; « après » = branche `feat/M8-L16-perf-a61`, par défaut (2 images en vol, graphe
gardé), `--graph-cache off` (graphe reconstruit à chaque image, comportement d'avant dans le même binaire) et
`--frames-in-flight 3`. 7 exécutions de 600 images par configuration (plus une de chauffe écartée), `--vsync off`.
Charge de la machine (moyenne sur 1 min) entre 1,6 et 3,9 pendant les mesures (d'autres agents compilaient avant ; mesures
lancées une fois la charge retombée). Scène « légère » : démo par défaut, `--ui off` ; « lourde » : `--ui full --scene mixed
--shadows on` (plugins ombres, transparents, couche 2D du monde, interface, tous gardables).

```bash
python3 tools/perf_compare.py --runs 7 --frames 600 --modes headless,windowed --extra="--vsync off"                  # avant / après
python3 tools/perf_compare.py --runs 7 --frames 600 --modes headless,windowed --extra="--vsync off --graph-cache off"
python3 tools/perf_compare.py --runs 7 --frames 600 --modes headless,windowed --extra="--vsync off --frames-in-flight 3"
python3 tools/perf_compare.py --runs 7 --frames 600 --modes headless,windowed --ui full --extra="--vsync off --scene mixed --shadows on"   # scène lourde
```

Médianes (ms par image ; `CPU/Graph` = temps processeur de `SceneRenderer::endFrame`, absent du binaire « avant ») :

| Scène, mode, API | Avant : image | Après : image | Après : CPU/Graph | Graphe reconstruit : image / CPU/Graph | 3 images en vol : image |
| --- | ---: | ---: | ---: | ---: | ---: |
| légère, hors écran, OpenGL | 0,578 | 0,579 | 0,067 | 0,579 / 0,066 | 0,506 |
| légère, hors écran, Vulkan | 0,364 | 0,362 | 0,053 | 0,316 / 0,051 | 0,363 |
| légère, fenêtre, OpenGL | 0,421 | 0,525 | 0,069 | 0,514 / 0,064 | 0,384 |
| légère, fenêtre, Vulkan | 0,474 | 0,478 | 0,094 | 0,470 / 0,099 | 0,426 |
| lourde, hors écran, OpenGL | 1,604 | 1,632 | 0,496 | 1,637 / 0,599 | 1,606 |
| lourde, hors écran, Vulkan | 0,883 | 0,865 | 0,573 | 0,888 / 0,589 | 0,856 |
| lourde, fenêtre, OpenGL | 1,504 | 1,438 | 0,617 | 1,620 / 0,354 | 1,450 |
| lourde, fenêtre, Vulkan | 1,034 | 1,203 | 0,431 | 1,206 / 0,424 | 1,183 |

Reconstructions du graphe sur 600 images : 2 à 3 avec le graphe gardé (première image, cache d'ombres qui s'établit, capture
finale), 600 sans. Lecture :

- **Graphe gardé (A61 C)** : gain réel mais petit, au plus ~0,1 ms de processeur par image sur la scène lourde (OpenGL hors
  écran : 0,599 → 0,496 ms pour `CPU/Graph`), rien de mesurable sur la scène légère (graphe de 3 à 4 passes). La durée d'une
  image est limitée par le GPU et par l'attente : elle ne change pas au-delà du bruit. Gardé activé par défaut (aucune
  régression mesurée, captures identiques).
- **Trois images en vol (A61 G)** : pas de gain stable. Les écarts (−0,07 ms ou +0,1 ms selon la configuration) sont du même
  ordre que la différence entre deux séries identiques (par exemple « après » et « graphe reconstruit » en scène lourde
  fenêtrée Vulkan : 1,203 et 1,206 contre 1,034 pour « avant ») ; les distributions sont bimodales (fréquence du GPU intégré,
  gouverneur `powersave`), voir min/max des JSON. **Deux images restent le défaut** (A79, L16), trois sont disponibles par
  `DeviceDesc::framesInFlight` et `--frames-in-flight 3`.

Réserves : un seul GPU (intégré) ; aucune mesure de latence d'entrée (G344) ; Windows et macOS non mesurés (G343).
