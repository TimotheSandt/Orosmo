# GAPS : manques connus et limites

Alimenté par M00 ; l'audit (`docs/04`) le complétera avec la table de rattachement des identifiants aux jalons.

| # | Sujet | Détail | Traité en |
| --- | --- | --- | --- |
| G1 | Windows non vérifié | Les chemins du Makefile, de la CI et de l'installeur NSIS ont été adaptés à `apps/demo/`, `res/`, `shaders/` sans pouvoir tester sous Windows (branches `powershell`/`mklink`, `installers/windows/installer.nsi`). | M0 (CI Windows) |
| G2 | macOS non vérifié | Aucun test sous macOS. | M0 / M9 |
| G3 | Installeur NSIS | `apps/demo/installers/windows/installer.nsi` référence encore `..\..\res\*` : il doit aussi inclure `..\..\..\..\res` et `..\..\..\..\shaders` (copiés vers `res\shader`). | M0 |
| G4 | Installeur .deb | `create_linux_installer` copie `SHADER_DIR` vers `usr/share/<nom>/shader` ; non exécuté. | M0 |
| G5 | Cycles entre modules | `Graphics` ↔ `UI` ↔ `Profiler` (voir `src/Graphics/README.md`). Contraire à la règle 5 de `CLAUDE.md`. | audit, M2 |
| G6 | `UI.h`, `PresentationController.h` | Aucun fichier ne les inclut. Conservés (décision de l'auteur) ; usage à trancher à l'implémentation de l'UI. | UI (M3/M8) |
| G7 | Module `Benchmark` | Repris tel quel de `dev` (non commité à l'origine), sans test ni démonstration vérifiée. | à décider |
| G8 | Dépendances non épinglées | `vcpkg.json` sans `builtin-baseline`. Sous Linux, les versions apt sont celles de la distribution. | M0 |
| G9 | Tests | Framework maison, pas de CI de tests, pas d'ASan/UBSan. Les 102 tests ne sont lancés qu'en release. | M0 |
| G10 | Licence | Aucun fichier `LICENSE` (Apache-2.0 envisagée). Licence de la police Roboto non documentée dans le dépôt. | décision de l'auteur |
| G11 | Référence visuelle | Pas de capture d'écran de référence (capture X11 impossible dans la session). Vulkan non lancé. | M0 |
| G12 | Casse du nom `utilities.h` / `Utilities.cpp` | Incohérence interne (corrigée seulement dans l'`#include` fautif). | M1 |
| G13 | `backend` : dossier d'en-têtes `Graphics/Backend/` (singulier) contre `Backends/` pour les sources et les sous-dossiers | Non renommé : cela change des `#include`. | M2 |
| G14 | Espaces en fin de ligne | 3 fichiers du code ; non touchés (changement de contenu). | M0 (formatage) |
| G15 | Installeurs restés dans `apps/demo/installers/` | Le plan prévoyait `tools/installers/` ; le Makefile de la démo les référence en relatif. | M0 |
