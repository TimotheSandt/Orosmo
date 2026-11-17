# ProceduralGeneration : règles permanentes

Moteur de jeu 3D et 2D en **C++23**, rendu par une couche d'abstraction (**RHI**) avec backends **Vulkan** (principal), **OpenGL 4.3+** et **Metal** (via MoltenVK d'abord, natif ensuite). Le premier produit visé est un générateur de terrain et de planètes procédural. L'interface s'écrit dans un langage maison précompilé en C++, avec thèmes JSON.

Lis `docs/` avant tout travail : `01` vision, `02` checklist, `03` architecture, `04` audit, `05` plan.

## Git (détail dans `docs/06-WORKFLOW_GIT_ET_NETTOYAGE.md`)

- `main` : réservé à l'auteur. **Ne commit, ne fusionne et ne pousse jamais dessus.**
- `dev` : branche d'intégration. Jamais de commit direct dessus.
- Une branche par fonctionnalité (`feat/<ID>-<sujet>`), par bug (`fix/<sujet>`), par nettoyage (`chore/<sujet>`), toujours créée depuis `dev`. Fusion dans `dev` avec `--no-ff`, seulement quand compilation, tests et définition de « terminé » sont verts.
- Fin de jalon : préparer `docs/releases/<jalon>.md` et la description de la merge request, vérifier depuis un clone propre, puis **s'arrêter**. L'auteur ouvre la merge request vers `main`.
- Interdits : `git push --force`, `--no-verify`, réécriture de l'historique de `main` ou `dev`, suppression d'une branche non fusionnée, gros binaires et secrets dans le dépôt.
- Commits petits, au format `type(ID): résumé` ; les déplacements de fichiers (`git mv`) sont dans des commits séparés.
- **Premier travail : M00, nettoyage du projet et du dépôt** (voir `docs/06`), avant l'audit. L'historique actuel est conservé.

## Environnement

Le projet a été **commencé sous Windows** et le développement continue maintenant **sous Linux**. Windows reste une plateforme cible. Conséquences à vérifier dès le premier build :
- **Casse des noms de fichiers** : Linux la distingue ; un `#include "Logger.hpp"` qui vise `logger.hpp` marche sous Windows et échoue ici.
- **Fins de ligne** (CRLF/LF) : normaliser avec un `.gitattributes`, sans réécrire l'historique.
- **Chemins** : séparateurs, chemins absolus ou lettres de lecteur en dur.
- **Dépendances** : vcpkg était utilisé sous Windows ; sous Linux, utiliser vcpkg ou les paquets du système, et documenter la commande exacte.
- **`<stacktrace>`** : peut demander des options de lien selon la version de GCC ou de libstdc++ ; ne pas supprimer le logger pour autant.
- **Compilateur** : C++23 demande une version récente (GCC 13+ ou Clang 17+) ; noter la version utilisée.
- **Pilotes graphiques** : OpenGL 4.3+ et Vulkan (avec les couches de validation) doivent être disponibles ; noter le GPU et le pilote dans `docs/BASELINE/`.

Toute correction de portabilité Windows → Linux est faite sans casser la compilation sous Windows, ou notée dans `docs/GAPS.md` si ce n'est pas vérifiable ici.

## Règles

1. **Preuve avant case cochée.** Une fonctionnalité n'est « Fait » que si tu peux donner (a) le fichier et le symbole, (b) un test automatisé ou une commande de démo, (c) le résultat de ce test. Sinon c'est « Partiel » ou « Absent ». Ne déclare jamais « terminé » de mémoire.
2. **Ne casse pas l'existant.** Le comportement actuel (logger, UI, entrées, éclairage, texte) est conservé. Avant de refactorer, ajoute un test ou une capture de référence qui le fige.
3. **Ne supprime aucune fonctionnalité** sans le dire et sans mon accord. Pas de stub qui prétend marcher : un `TODO` est listé dans `docs/GAPS.md`.
4. **Aucun appel d'API graphique hors de `src/rhi/<backend>/`.** Pas de `gl*`, `Vk*`, `MTL*` ailleurs. Une CI vérifie cette règle (voir `docs/03`).
5. **Respecte les dépendances entre modules** (couches de `docs/03`). Un module ne dépend que de ceux d'en dessous.
6. **Petits commits**, un sujet à la fois, message clair. Chaque commit compile et passe les tests.
7. **Désaccord ou ambiguïté** : écris-le dans `docs/QUESTIONS.md` et demande, au lieu de choisir en silence. Une décision d'architecture non prévue par `docs/03` est proposée, pas appliquée.
8. **Documente ce que tu ajoutes** : un `README.md` court par module (rôle, API publique, dépendances), et la mise à jour de `docs/STATUS.md`.
9. **Déterminisme** : la génération procédurale produit le même résultat pour la même graine, sur toutes les plateformes. Pas de `rand()` ni d'horloge dans la génération.
10. **Erreurs** : pas d'exception pour le flux normal ; utilise `std::expected` et le logger existant.
11. **Capacités optionnelles** (ray tracing, bindless, mesh shaders, etc.) : toujours lues dans `Capabilities`, jamais supposées. Le moteur a un chemin de repli ou refuse proprement.
12. **Le langage d'interface décrit, il ne décide pas.** Aucune logique métier dans les fichiers du langage : les événements nomment des fonctions C++ ; l'apparence vient des thèmes JSON, jamais de valeurs en dur.
13. **Logique de jeu rechargeable** : aucun état persistant, `static` ni pointeur vers du code dans la bibliothèque de jeu ; les systèmes s'enregistrent par nom ; les composants sont des données réfléchies.
14. **Extensions** : voxels et ray tracing s'ajoutent comme plugins du renderer, sans modifier le cœur ni les backends existants.

## Définition de « terminé » (pour toute tâche)

- Compile sans avertissement sur Windows, Linux et macOS (ou la limite est notée dans `docs/GAPS.md`).
- Tests unitaires ajoutés et verts ; les sanitizers (ASan/UBSan) ne signalent rien sur les tests.
- Les couches de validation graphique (Vulkan validation, `GL_KHR_debug`) ne signalent aucune erreur sur la démo.
- Le module est documenté ; `docs/STATUS.md` est à jour ; la commande de démo est donnée.

## Conventions de code (décision de l'auteur, 2026-11-17)

On suit `docs/03` §2 (arborescence `include/engine/<module>/*.hpp`, `src/<module>/`, `src/rhi/<backend>/`, `tests/<module>/`, `apps/<nom>/`) et §7 (types `PascalCase`, fonctions et variables `camelCase`, constantes `kPascalCase`, fichiers `snake_case`, `-Wall -Wextra -Wpedantic -Werror`). Le code existant (`include/<Module>/*.h`, noms `PascalCase`) **migre module par module** quand le jalon du plan le réécrit (M1 core, M2 rhi/platform/renderer…) : tout code neuf suit déjà `docs/03`. Pas de renommage de masse hors jalon.

## Commandes

Linux (Ubuntu/Debian ; g++ 13.3 vérifié). Dépôt sur un disque monté sans bit exécutable : lancer les scripts avec `sh`.

```bash
# 1. Dépendances (une fois). apt d'abord, vcpkg pour ce qui n'existe pas dans apt (glad v1, msdfgen).
sudo apt install build-essential cmake cargo python3 libglfw3-dev libglm-dev libstb-dev libshaderc-dev glslc \
    doctest-dev libfreetype-dev libvulkan-dev vulkan-validationlayers libgl-dev libx11-dev \
    autoconf autoconf-archive automake libtool
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
~/vcpkg/vcpkg install glad msdfgen --triplet x64-linux
sh tools/setup-linux-deps.sh            # relie apt + vcpkg dans vcpkg_installed/x64-linux

# 2. Build et démo
make all-debug                          # moteur (debug, -Wpedantic -Werror) -> apps/demo/engine -> démo
make run                                # lance la démo (OpenGL par défaut ; --api vulkan non fiable avant M4)

# 3. Tests et contrôles
make demo-test                          # doctest : 117 cas (construit les archives release d'abord)
make demo-test SANITIZE=address,undefined   # ASan + UBSan (partir d'un arbre propre : make fclean)
make check                              # dépôt, couches, isolation des API, en-têtes autonomes (mode avertissement)
python3 -m unittest discover -s tests/tools # outils Python (compare_png)

# 4. Images de référence
apps/demo/bin/debug/main --api opengl --capture /chemin/sortie        # scene.png (reproductible) + ui.png (FPS affichés)
python3 tools/compare_png.py docs/BASELINE/reference/scene-opengl-seed1-1280x720.png /chemin/sortie/scene.png

# 5. Formatage (clang-format requis)
sh tools/format_sources.sh
```

Options utiles : `WERROR=0` (avertissements non bloquants), `--seed N` (terrain de la démo), `--capture-frames N`. Windows/macOS : voir `README.md` ; non vérifiés (`docs/GAPS.md`).
