# Orosmo : règles permanentes

Orosmo est un moteur 3D et 2D générique en **C++23**, rendu par une couche d'abstraction (**RHI**) avec backends **Vulkan** (principal), **OpenGL 4.3+** et **Metal** (via MoltenVK d'abord, natif ensuite). L'interface s'écrit dans un langage maison précompilé en C++, avec thèmes JSON. *(Note A37 : le code procédural `orosmo::world` / `TerrainRenderer` a été archivé dans la branche `archive/procedural-world` et retiré du dépôt principal ; `apps/` ne contient que la démo unique `apps/demo/`.)*

Lis `docs/` avant tout travail : `01` vision, `02` checklist, `03` architecture, `04` audit, `05` plan.

## Git (détail dans `docs/06-WORKFLOW_GIT_ET_NETTOYAGE.md`)

- `main` : versions validées, réservé à l'auteur. **Ne commit, ne fusionne et ne pousse jamais dessus.**
- `dev` : intégration et test. **Ne pousse jamais directement dessus**, même si le compte utilisé le permet : ouvre une PR vers `dev`, **seul l'auteur la fusionne**.
- Une branche depuis `dev` par fonctionnalité (`feat/<ID>-<sujet>`), bug (`fix/<sujet>`), nettoyage (`chore/<sujet>`) ou doc (`docs/<sujet>`). PR ouverte quand compilation, tests et définition de « terminé » sont verts.
- Un gros jalon est découpé en lots (`feat/<jalon>-L<n>-<sujet>`), une PR par lot. Chaque lot compile, passe les tests et laisse `dev` utilisable.
- Validation : l'auteur teste `dev` ; les corrections passent par des branches `fix/<jalon>-<sujet>`, avec leur PR vers `dev`. Pendant la validation, `dev` ne reçoit que des corrections ; le jalon suivant avance sur `milestone/<jalon>`.
- Fin de jalon (jalon validé) : préparer `docs/releases/<jalon>.md` et la description de la PR `dev` → `main`, vérifier depuis un clone propre, puis **s'arrêter**. L'auteur ouvre la PR, la fusionne en **merge commit** (jamais squash) et pose le tag.
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

On suit `docs/03` §2 (arborescence `include/orosmo/<module>/*.hpp`, `src/<module>/`, `src/rhi/<backend>/`, `tests/<module>/`, `apps/<nom>/`) et §7 (types `PascalCase`, fonctions et variables `camelCase`, constantes `kPascalCase`, fichiers `snake_case`, `-Wall -Wextra -Wpedantic -Werror`). Le code existant (`include/<Module>/*.h`, noms `PascalCase`) **migre module par module** quand le jalon du plan le réécrit (M1 core, M2 rhi/platform/renderer…) : tout code neuf suit déjà `docs/03`. Pas de renommage de masse hors jalon.

## Commandes

Linux (Ubuntu/Debian ; g++ 13.3 vérifié). Dépôt sur un disque monté sans bit exécutable : lancer les scripts avec `sh`.

```bash
# 1. Dépendances (une fois). apt pour ce qu'il a ; UNE commande vcpkg (lit vcpkg.json) pour glad v1, msdfgen et GLFW 3.4+ (Wayland natif).
sudo apt install build-essential cmake cargo python3 libglm-dev libstb-dev libshaderc-dev glslc \
    doctest-dev libfreetype-dev libvulkan-dev vulkan-validationlayers libgl-dev libx11-dev \
    autoconf autoconf-archive automake libtool pkg-config libwayland-dev libxkbcommon-dev wayland-protocols \
    libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
~/vcpkg/vcpkg install                   # à la racine du dépôt ; make relie ensuite les paquets apt tout seul

# 2. Build et démo
make all-debug                          # moteur (debug, -Wpedantic -Werror) -> apps/demo/orosmo -> démo unique
make run                                # lance la démo unique (Vulkan par défaut, repli sur OpenGL s'il ne démarre pas ; --api opengl pour forcer OpenGL ; Escape ouvre/ferme l'UI, Quit pour quitter)

# 3. Tests et contrôles
make demo-test                          # doctest : 413 cas (construit les archives release d'abord)
make demo-test SANITIZE=address,undefined   # ASan + UBSan (partir d'un arbre propre : make fclean)
make check                              # dépôt, couches, isolation des API, en-têtes autonomes (bloquant depuis M2)
make demo-test-unit                     # tests sans GPU ni fenêtre ; make demo-test-gpu : seulement l'intégration graphique
make cui-test                           # tests du compilateur .cui
make e2e-init                           # paquet + orosmo init + compilation + exemple minimal
python3 -m unittest discover -s tests/tools # outils Python (compare_png)

# 4. Captures et comparaison d'images
make demo-capture                       # captures hors écran de la démo unifiée (scène, thèmes, onglets)
apps/demo/bin/debug/main --api opengl --headless --capture /chemin/sortie   # scene.png + ui.png
python3 tools/compare_png.py A.png B.png

# 5. Formatage (clang-format requis)
sh tools/format_sources.sh
```

Options utiles : `WERROR=0` (avertissements non bloquants), `V=1` (commandes complètes au lieu de la barre de progression, affichée seulement dans un terminal), options CLI de `apps/demo` (`--ui off|hud|full`, `--theme dark|light|framed`, `--tab scene|widgets|controls|color|windows|layout|themes`, `--size WxH`, `--headless`, `--vsync`, `--capture <dir>`, `--capture-frames N`, `--hover <id>`, `--click <id>`). Windows/macOS : voir `README.md` ; non vérifiés (`docs/GAPS.md`).
