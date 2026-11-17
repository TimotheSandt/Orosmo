# STATUS

Mis à jour à la fin de M0 (2026-11-17). États détaillés et preuves de l'audit : [AUDIT_REPORT.md](AUDIT_REPORT.md) (instantané du 2026-11-17, non régénéré). Liste de travail : [GAPS.md](GAPS.md).

## Jalons

| Jalon | État | Preuve |
| --- | --- | --- |
| M00 Nettoyage du projet et du dépôt | Fait | `docs/releases/M00.md` |
| Audit | Fait | `docs/AUDIT_REPORT.md`, `PORTABILITY_REPORT.md`, `UI_LANGUAGE_AUDIT.md` |
| **M0 Audit et base solide** | **Livré côté code ; CI jamais observée sur GitHub** | `docs/releases/M0.md` : clone propre de `dev`, build sans avertissement, 118 cas doctest verts, ASan+UBSan propres, contrôles, capture identique à la référence |
| M1 Noyau | À faire (en attente du feu vert) | |

## Identifiants de `docs/02` touchés par M0

Aucune case n'est passée à `Fait` par M0 : un identifiant n'est `Fait` que si tout son critère est exercé, et la CI (qui exerce TEST-01, BLD-02, TEST-02) n'a jamais tourné sur GitHub.

| ID | Avant (audit) | Maintenant | Preuve |
| --- | --- | --- | --- |
| TEST-01 | Partiel (framework maison) | Partiel | `tests/support/test_support.hpp`, `tests/suites/*.cpp` ; `make demo-test` : 118 cas / 647 assertions verts (`docs/BASELINE/tests-M0.txt`) ; manque : exécution en CI, couverture de `core` |
| TEST-02 | Partiel | Partiel | `mk/engine.mk` (`WERROR`, `SANITIZE`) ; build `-Wall -Wextra -Wpedantic -Werror` : 0 avertissement ; `make demo-test SANITIZE=address,undefined` : verts, aucun rapport ASan/UBSan (leaks désactivés) ; manque : clang-tidy / clang-format, CI |
| BLD-02 | Partiel | Partiel | `.github/workflows/ci.yml` (YAML valide ; étapes Linux rejouées localement) ; jamais exécuté sur GitHub |
| TOOL-03 | Absent (P2) | Partiel (**P0**) | `src/Graphics/Screenshot.cpp`, `Renderer::SaveColorPng`, `main --capture` ; 3 captures identiques ; `tools/compare_png.py` (6 tests Python) ; OpenGL seulement |
| TEST-03 | Partiel | Partiel | séquence PCG figée (`noise_tests.cpp`) + image de terrain reproductible ; pas de checksum multi-OS |
| RHI-14 | Absent | Absent | outil de comparaison et image de référence prêts ; pas de test automatisé par backend |
| BLD-01 | Partiel | Partiel | build Linux documenté dans `CLAUDE.md` et `README.md` ; Windows/macOS non vérifiés |
| DOC-01 | Partiel | Partiel | README racine, README par module, `CLAUDE.md` (commandes) ; manquent schéma des modules et `ADD_A_BACKEND.md` |
| REPO-01 | Fait | Fait | `sh tools/check-repo.sh` : OK |

Total (inchangé) : Fait 1, Partiel 46, Absent 102, Incompatible 3 (voir l'audit ; TOOL-03 passe d'Absent à Partiel : Fait 1, Partiel 47, Absent 101, Incompatible 3).

Commandes (Linux) : voir `CLAUDE.md` § Commandes. Démo : `make all-debug && make run`. Tests : `make demo-test`. Contrôles : `make check`.
