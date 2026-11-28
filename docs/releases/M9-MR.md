# M9 : macOS et Metal (`milestone/M9` → `dev`)

**Ne pas fusionner avant la validation de M8 (PR #37, `dev` → `main`)** : la tête de la PR #37 est `dev`, une fusion de M9
l'y ajouterait.

## Résumé

macOS passe de « CI verte sans rien rendre » à « Vulkan par MoltenVK exécuté, validé et comparé en CI » : tests GPU et de
fenêtre exigés (955 / 955, 1 ignoré), six captures à 0,1 % au plus des références, paquet installé et lancé par
`make e2e-init`, chargeur trouvé sans variable d'environnement, `orosmo doctor`, plus de repli OpenGL trompeur (run
46123557575 ; PR #43, #44, #45, #47, #49 et la PR de L5). Détail : `docs/releases/M9.md`.

**La démo « visualiseur sur un Mac » est ABSENTE : rien n'a été essayé sur un vrai Mac.** La preuve vient d'une machine
virtuelle de CI (GPU « Apple Paravirtual device »). **macOS Intel : rendu absent** (G523). Aucun backend Metal natif. Aucun
identifiant de `docs/02` ne passe à « Fait ». Pas de changement de version, ni tag, ni release.

## À confirmer par l'auteur

1. **Contournement G510 choisi par le nom du GPU** (MoltenVK et « Paravirtual ») : les dessins indexés y passent en indirect.
   Cause non établie (MoltenVK ou pilote virtuel) ; un auto-test à la création a été écarté parce qu'il pourrait réussir par
   hasard. Un vrai GPU n'est pas contourné et n'est pas essayé.
2. **Un message de MoltenVK 1.4.2 est journalisé sans être compté** comme erreur de validation (mélange sur un format non
   mélangeable, faux positif, G504). À retirer avec une version corrigée.
3. **A91** : le SDK n'embarque ni MoltenVK ni le chargeur (A76) ; un joueur doit installer Homebrew et deux paquets.
   Proposition : l'embarquer dans le `.app` au moment de distribuer un jeu (M15). Aussi : version minimale de macOS et image de
   release arm64 à épingler.
4. **A92 (RHI-13)** : backend Metal natif **reporté**, à remesurer sur un vrai Mac. Seuils C1 à C3 de
   `docs/notes/decision_rhi13_backend_metal.md` à accepter ou changer ; avez-vous (ou pouvez-vous emprunter) un Mac ?
5. **Plus de repli OpenGL sur macOS** (OpenGL s'arrête à 4.1) : si Vulkan ne démarre pas, le programme échoue avec la
   commande d'installation.
6. **Changements d'API et de comportement** : ajouts `Capabilities::validation`, `Capabilities::srgbStorageImages`,
   `rhi::fallbackBackend`, `rhi::kHostIsMacOS`, `rhi::kNoFallbackOnMacOS` ; `timestampQueries` peut être faux sous MoltenVK ;
   fonctions de `VK_KHR_portability_subset` activées en bloc (G505) ; rpaths macOS dans `engine.mk` et `OrosmoConfig.cmake` ;
   variable `ENGINE_VULKAN_INDEXED_VIA_INDIRECT` ; image de CI macOS épinglée à `macos-26`, paquets Homebrew non épinglés
   (G502) ; `build-engine.yml` modifié et non exécuté (G524).
7. **Exception à la règle de conduite 5 de `docs/05`** : les lots ont fusionné dans `milestone/M9`, pas dans `dev`.
8. **`CLAUDE.md`, « Commandes »** (non modifié) : la ligne « Windows/macOS : voir `README.md` ; non vérifiés » est périmée
   pour macOS. Proposition : « Windows : voir `README.md`, non vérifié. macOS : voir `README.md`, vérifié par la CI sur
   machine virtuelle seulement (`docs/GAPS.md`, G521). »

## À essayer sur un Mac

Mac Apple Silicon, depuis un clone propre de `milestone/M9` :

```bash
xcode-select --install
brew install vulkan-loader molten-vk vulkan-validationlayers
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
~/vcpkg/vcpkg install --triplet=arm64-osx --x-install-root=./vcpkg_installed
make -j6 all-debug VCPKG_TRIPLET=arm64-osx VCPKG_INSTALLED_ROOT=./vcpkg_installed
make -j6 demo-test VCPKG_TRIPLET=arm64-osx VCPKG_INSTALLED_ROOT=./vcpkg_installed
apps/demo/bin/debug/main --api vulkan --strict-backend        # fenêtre visible, souris, clavier, manette, Retina
python3 tools/demo_reference_check.py --api vulkan --out build/ref
```

À vérifier : maillages visibles, la ligne « indexed draws are recorded as one-command indirect draws » **absente** du
journal, netteté et position de la souris à l'échelle 2, redimensionnement, plein écran, changement d'écran ; puis un paquet
installé par `install.sh`, `orosmo doctor`, `orosmo init`, `orosmo run`. Mesures de performance : commandes de la note
RHI-13. La couche de validation de Homebrew peut demander `VK_LAYER_PATH` et un manifeste au chemin absolu (`ci.yml`).

## Tests

CI macOS arm64 et Linux, Windows : run 46123557575, tous les jobs verts. Local (Linux) : description de la PR de L5. La CI
de cette merge request doit être verte avant fusion.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
