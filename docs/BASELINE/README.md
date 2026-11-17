# BASELINE : référence de comportement avant nettoyage (M00)

Date : 2026-11-16. État de référence : tag `pre-cleanup` (= `899c349`, branche `dev`) + arbre de travail non commité (voir `docs/CLEANUP_PLAN.md` §1.1).

## Statut : INCOMPLET — le build Linux n'est pas possible dans l'état actuel

Aucune capture d'écran ni sortie d'exécution n'a pu être enregistrée. Raison, prouvée :

```
$ make debug     # dans une copie propre de l'arbre de travail, hors dépôt
g++ -std=c++23 -Wall -Wextra ... -I./vcpkg_installed/x64-linux/include -c Libraries/src/Graphics/Camera.cpp
Libraries/includes/Graphics/Camera.h:5:10: fatal error: GLFW/glfw3.h: Aucun fichier ou dossier de ce nom
make: *** [Makefile:61 : debug] Erreur 2
```

Causes :
- `vcpkg` n'est pas installé sur cette machine (`which vcpkg` vide, pas de `~/vcpkg` ni `/opt/vcpkg`).
- `vcpkg_installed/` ne contient que des triplets Windows (`x64-windows`, `x64-mingw-static`), pas `x64-linux`.
- GLFW, glad, glm, stb, shaderc ne sont pas dans les paquets système (seuls FreeType et le loader Vulkan le sont).
- Les binaires existants (`demo/bin/*/main.exe`, `ProceduralGeneration.exe`, `vulkan-1.dll`) sont des binaires Windows, non exécutables ici. Ils ne sont ni copiés ni modifiés.

Pour compléter la référence, il faut installer vcpkg et les dépendances (réseau, plusieurs gigaoctets, compilation de shaderc). Je ne l'ai pas fait sans ton accord : voir `docs/QUESTIONS.md` Q1.

## Environnement mesuré

| Élément | Valeur |
| --- | --- |
| OS | Linux 7.2.15, Pop!_OS 24.04 LTS, x86_64 |
| Compilateur | g++ 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) ; clang++ absent |
| GPU | Intel Arc Graphics (Meteor Lake-P, MTL), pilote Mesa 26.1.6 (KMD Xe : « support expérimental ») |
| Vulkan | instance 1.3.280, appareil 1.4.354 (`vulkaninfo --summary`) |
| OpenGL | rendu accéléré, profil core, Mesa 26.1.6 (`glxinfo -B`) ; version exacte du contexte à relever quand le build marchera |
| Système de fichiers du dépôt | NTFS via `fuseblk` (`/mnt/data`), sensible à la casse côté Linux, mais `git config core.ignorecase` vaut `true` (hérité de Windows) |
| Python | 3.x présent ; `cargo` présent (outils Rust `cui-rs`, `engine-init-rs`) |

## Ce qui est figé malgré tout

- Historique complet : tag `pre-cleanup`, branches `backup/2026-11-16-pre-cleanup` et `backup/2026-11-16-pre-cleanup-worktree`, bundle hors dépôt.
- Comportement par le code : la suite de tests existante est `demo/tests/` (framework maison, 12 fichiers). Elle servira de référence dès qu'elle compile.

## À faire dès que le build Linux existe

1. `make install-deps-all && make all-debug`.
2. Construire et lancer `demo/tests` ; enregistrer la sortie dans `docs/BASELINE/tests.txt`.
3. Lancer la démo (OpenGL puis Vulkan), enregistrer captures et journal (`logs/`) dans ce dossier.
4. Après le nettoyage, rejouer et comparer.
