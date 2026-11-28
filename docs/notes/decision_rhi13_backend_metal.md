# RHI-13 : backend Metal natif, oui ou non (M9-L4)

**Statut : proposition de l'IA, non appliquée** (`docs/QUESTIONS.md` A92). La décision revient à l'auteur.

## Proposition

**Reporter.** Garder Vulkan sur MoltenVK comme seul chemin macOS, n'écrire aucun code sous `src/rhi/metal/`, et remesurer sur un
vrai Mac avant de décider. RHI-13 reste **Absent**.

**Les mesures de cette note ne permettent aucune décision de performance.** Elles viennent d'un runner GitHub `macos-26`, une
machine virtuelle dont le GPU est « Apple Paravirtual device » : partagé, sans minuteries GPU exploitables (G500), avec au moins un
défaut de dessin propre à ce périphérique (G510). Aucun Mac réel n'a été essayé. Il n'existe pas non plus de point de comparaison :
sans backend natif, le surcoût de MoltenVK ne se mesure pas par différence.

## Mesures du runner (indicatives)

Run [46121707903](https://github.com/TimotheSandt/Orosmo/actions/runs/46121707903) (2026-11-28, sonde temporaire `m9-l4-probe.yml`,
retirée ensuite ; fichiers JSON dans les artefacts `m9-l4-measures-1` et `-2`, conservés le temps que GitHub les garde). Deux
machines virtuelles A et B : macOS 26.7.2, « Apple M1 (Virtual) », 3 coeurs, 7 Gio, GPU « Apple Paravirtual device », MoltenVK 1.4.2,
chargeur Vulkan 1.4.363 (Homebrew). Binaire **release**, validation coupée, 1280x720.

### Build et taille

| Mesure | A | B |
| --- | ---: | ---: |
| `make -j3 all-release` depuis un clone neuf, sans ccache, dépendances vcpkg déjà installées | 116 s | 152 s |
| Démo release (symboles / après `strip`) | 19,2 Mo / 14,6 Mo | identique |
| `libMoltenVK.dylib` / `libvulkan.1.dylib` (Homebrew, à côté de l'exécutable) | 5,3 Mo / 0,68 Mo | identique |

L'installation des dépendances vcpkg à froid a pris en plus environ 13 minutes.

### Rendu de la version release

Capture de la scène comparée à la référence OpenGL (tolérance 8) : **0,0011 %** de pixels différents avec le contournement de G510,
**16,45 %** avec `ENGINE_VULKAN_INDEXED_VIA_INDIRECT=0` (maillages absents), sur A et B. Le défaut de G510 existe donc aussi sans
validation et en `-O3`.

### Premier affichage (processus entier, une image ; médiane de 7 lancements après le premier)

| Mode | Interface | A (ms) | B (ms) |
| --- | --- | ---: | ---: |
| hors écran | coupée | 1 795 (1 418 à 2 090) | 1 727 (1 618 à 1 893) |
| hors écran | complète | 2 282 (1 822 à 3 687) | 2 251 (2 104 à 2 857) |
| fenêtre | coupée | 2 665 (2 556 à 2 827) | 2 849 (2 546 à 3 419) |
| fenêtre | complète | 2 166 (2 058 à 2 488) | 3 250 (2 793 à 4 250) |

Hors écran, la première image seule dure environ 0,15 à 0,17 s sans interface et 0,53 à 0,56 s avec (médianes). **Aucun lancement
n'est vraiment à froid** : les caches de pipelines (moteur et système) avaient été remplis par les étapes précédentes.

### Temps CPU par image (médiane de 7 répétitions de 300 images, une de chauffe écartée)

« Image » = médiane de la durée d'une image ; « travail » = la même moins les attentes (GPU, écran). Millisecondes, A / B.

| Scène | Mode | Image | Travail CPU |
| --- | --- | ---: | ---: |
| scène, interface coupée | hors écran | 0,47 / 0,49 | 0,07 / 0,09 |
| scène, interface coupée | fenêtre | 16,42 / 16,38 | 0,45 / 0,40 |
| interface complète | hors écran | 0,62 / 0,95 | 0,35 / 0,53 |
| interface complète | fenêtre | 16,38 / 16,42 | 2,08 / 1,78 |
| 2 000 instances, GPU-driven | hors écran | 0,60 / 0,62 | 0,11 / 0,12 |
| 2 000 instances, GPU-driven | fenêtre | 16,41 / 16,36 | 0,69 / 0,58 |
| sans contournement G510, interface coupée (image fausse) | hors écran | 0,47 / 0,38 | 0,06 / 0,07 |
| sans contournement G510, interface coupée (image fausse) | fenêtre | 16,44 / 16,34 | 0,48 / 0,41 |

À lire avec ces réserves :

- **En fenêtre**, la présentation du runner est cadencée à environ 60 Hz même sans `--vsync` (14 à 16 ms d'attente par image) : la
  durée d'image ne mesure pas le moteur.
- **Pas de temps GPU** : `timestampQueries` est faux (G500) ; les lignes `RenderGraph/...` de l'outil sont des temps CPU.
- **Dispersion** : jusqu'à un facteur 1,5 entre A et B (interface complète hors écran).
- **Contournement indexé vers indirect** : sans lui la scène n'est pas dessinée, la charge n'est pas la même ; l'écart (0,1 ms au
  plus) ne dit rien de son coût sur un GPU sain.

### Compteurs de MoltenVK (interface complète, hors écran, 300 images, 2 lancements par machine)

`MVK_CONFIG_PERFORMANCE_TRACKING=1`. Les moyennes incluent le premier envoi (36 à 57 ms, création des pipelines) ; « dernier » est la
valeur de la dernière image, seul aperçu du régime établi que ce résumé donne (une valeur, pas une statistique).

| Compteur | Moyenne | Dernier | Maximum |
| --- | ---: | ---: | ---: |
| `vkQueueSubmit()` encodage vers Metal | 0,16 à 0,26 ms | environ 0,04 ms | 36 à 58 ms |
| exécution d'un `MTLCommandBuffer` sur le GPU | 1,7 à 2,8 ms | 1,0 à 1,5 ms | 90 à 168 ms |
| compilation MSL vers `MTLLibrary` (17) | 0,08 à 0,16 ms | | 1,4 ms |
| compilation d'un pipeline Metal (6) | 6 à 10 ms | | 57 ms |

La conversion SPIR-V vers MSL compte **0 occurrence** (MSL déjà dans le cache de pipelines) : son coût à froid n'est pas mesuré.

## Ce qui ferait pencher vers un backend natif

Seuils **proposés** ; à mesurer sur un vrai Mac, binaire release, dans la configuration visée par un jeu. Un seul critère vérifié
et non corrigeable autrement suffit à rouvrir la question ; aucun n'est vérifié ni réfuté aujourd'hui.

| # | Critère | Seuil proposé | Remède à essayer avant |
| --- | --- | --- | --- |
| C1 | Coût processeur de la traduction (encodage et soumission de MoltenVK) | plus de 20 % du travail CPU d'une image, ou plus de 2 ms par image sur la scène visée | réglages `MVK_CONFIG_*`, moins de dessins et de liaisons côté moteur |
| C2 | Cible d'images non tenue alors que la même charge la tient en Metal natif | 60 images/s manquées ; capture Xcode montrant des encodeurs, copies ou synchronisations ajoutés par la traduction | barrières moins conservatrices dans `src/rhi/vulkan/` |
| C3 | Premier affichage et à-coups de compilation (SPIR-V vers MSL puis pipelines) | plus de 2 s avant la première image à chaud, ou images de plus de 50 ms en cours de jeu | cache de pipelines sur disque (déjà là), préchauffage |
| C4 | Défaut bloquant de MoltenVK sur du matériel réel | pas de contournement raisonnable (G510 reproduit sur un vrai GPU Apple, par exemple) | contournement dans le backend Vulkan, rapport à MoltenVK |
| C5 | Fonction exigée par un jalon et absente sous MoltenVK | ray tracing, mesh shaders, fonction hors de `VK_KHR_portability_subset` (G505) | chemin de repli par `Capabilities` (règle 11) |
| C6 | Cible nouvelle que MoltenVK sert mal (iOS, visionOS) | décision de l'auteur d'ajouter la cible | MoltenVK sur la cible |

La dépendance à Homebrew pour les joueurs (A91) **n'est pas** un argument à elle seule : embarquer le chargeur et MoltenVK dans le
paquet d'un jeu la règle sans écrire de backend.

## À mesurer sur un vrai Mac

Mac Apple Silicon (au moins une puce de base, branché sur secteur), écran allumé, aucune autre charge ; si possible un second Mac
Intel (G523). Depuis un clone propre, les paquets de `README.md` installés :

```bash
brew install vulkan-loader molten-vk vulkan-validationlayers
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
~/vcpkg/vcpkg install --triplet=arm64-osx --x-install-root=./vcpkg_installed
sw_vers && system_profiler SPDisplaysDataType && brew list --versions molten-vk vulkan-loader

# 1. Correction d'abord : tests, puis captures comparées, avec et sans le contournement de G510
make -j6 all-debug VCPKG_TRIPLET=arm64-osx VCPKG_INSTALLED_ROOT=./vcpkg_installed
make -j6 demo-test VCPKG_TRIPLET=arm64-osx VCPKG_INSTALLED_ROOT=./vcpkg_installed
ENGINE_VULKAN_INDEXED_VIA_INDIRECT=0 python3 tools/demo_reference_check.py --api vulkan --out build/ref-direct
ENGINE_VULKAN_INDEXED_VIA_INDIRECT=1 python3 tools/demo_reference_check.py --api vulkan --out build/ref-indirect

# 2. Build release (chronométré) ; PROJECT_NAME est nécessaire (G531)
make fclean && make demo-fclean
time make -j6 all-release PROJECT_NAME=Orosmo VCPKG_TRIPLET=arm64-osx VCPKG_INSTALLED_ROOT=./vcpkg_installed
ls -l apps/demo/bin/release/Orosmo

# 3. Temps par image : 7 répétitions de 600 images, hors écran et en fenêtre
python3 tools/perf_compare.py --api vulkan --runs 7 --frames 600 --ui off  --out perf-off.json
python3 tools/perf_compare.py --api vulkan --runs 7 --frames 600 --ui full --out perf-full.json
python3 tools/perf_compare.py --api vulkan --runs 7 --frames 600 --ui off --extra="--instances 20000 --gpu-driven on" --out perf-instances.json
python3 tools/perf_compare.py --api vulkan --runs 7 --frames 600 --ui off --extra="--lighting forwardplus --lights 1000 --shadows on --post full" --out perf-lourd.json

# 4. Coût propre de MoltenVK (conversion SPIR-V vers MSL, compilation, encodage, soumission)
MVK_CONFIG_PERFORMANCE_TRACKING=1 MVK_CONFIG_ACTIVITY_PERFORMANCE_LOGGING_STYLE=2 MVK_CONFIG_LOG_LEVEL=3 \
  apps/demo/bin/release/Orosmo --api vulkan --strict-backend --ui full --capture /tmp/mvk --capture-frames 600

# 5. Premier affichage : à froid (cache de pipelines du moteur vide), puis à chaud
rm -rf /tmp/pc-vide
ENGINE_VULKAN_PIPELINE_CACHE_DIR=/tmp/pc-vide time apps/demo/bin/release/Orosmo --api vulkan --strict-backend --capture /tmp/f1 --capture-frames 1
ENGINE_VULKAN_PIPELINE_CACHE_DIR=/tmp/pc-vide time apps/demo/bin/release/Orosmo --api vulkan --strict-backend --capture /tmp/f1 --capture-frames 1

# 6. Où part le temps (Xcode installé) : encodeurs, GPU, attentes
xcrun xctrace record --template 'Metal System Trace' --output orosmo.trace --launch -- \
  "$PWD/apps/demo/bin/release/Orosmo" --api vulkan --strict-backend --ui full --capture /tmp/trace --capture-frames 600
```

Les commandes 1 à 4 sont celles de la sonde ou de la CI (sur le runner) ; les commandes 5 et 6 n'ont pas été essayées. À relever en plus : `Capabilities::timestampQueries` (si vrai, les lignes `RenderGraph/...` de `perf_compare.py` sont des temps
GPU ; sinon des temps CPU), la ligne « indexed draws are recorded as one-command indirect draws » (elle ne doit **pas** apparaître,
G510), la fenêtre en Retina (G520) avec et sans `--vsync`.

## Coût estimé d'un backend natif

D'après `docs/ADD_A_BACKEND.md`. **Estimation, non mesurée.** Repères du dépôt : backend Vulkan 5 828 lignes dans
`src/rhi/vulkan/` et 1 330 lignes de tests propres (`tests/rhi/vulkan_*.cpp`) ; backend OpenGL 3 069 lignes.

| Poste | Contenu | Ordre de grandeur |
| --- | --- | --- |
| Backend | `src/rhi/metal/` : `Device`, `CommandEncoder`, ressources, pipelines, surface (`CAMetalLayer`), table des formats | 4 000 à 6 000 lignes |
| Shaders | sortie MSL par SPIRV-Cross dans `src/rhi/shader_compiler.cpp`, valeur nouvelle de `ShaderLanguage` (à soumettre à l'auteur), liaisons 3 groupes x 8 et push constants, mêmes images (repère A26) | 300 à 600 lignes |
| Build | Objective-C++ (`.mm`, absent de `mk/engine.mk`) ou metal-cpp, frameworks, `vcpkg.json`, `engine.mk` et CMake du SDK | 1 lot court |
| Tests et CI | Metal dans `tests/rhi/backends.hpp`, tests propres, validation Metal à 0 message, captures comparées | 1 000 à 1 500 lignes |
| Total | | **un jalon entier, 5 à 7 lots** |
| Récurrent | chaque évolution du RHI à écrire et tester trois fois ; un chemin que l'auteur ne peut pas essayer sans Mac | permanent |

Deux risques propres à la situation : sans Mac, la seule preuve serait le même GPU paravirtualisé ; et le défaut de G510, dont la
cause n'est pas établie, peut aussi toucher un backend natif sur ce périphérique.

## Limites de cette note

- Aucun Mac réel, aucune écoute thermique ni batterie, aucune fenêtre visible par un humain, pas de Retina, pas de Mac Intel.
- Deux machines virtuelles d'une même exécution, une seule date, versions Homebrew non épinglées (G502).
- Scène de démo légère : elle ne représente pas un jeu.
