# M0 : audit et base solide (`dev` → `main`)

## Résumé

Base de qualité avant le noyau : tests sur doctest, avertissements en erreur, ASan/UBSan propres, contrôles d'architecture (couches, isolation des API, en-têtes autonomes), capture d'écran PNG reproductible (OpenGL) avec outil de comparaison et image de référence, CI écrite. Détail et preuves : `docs/releases/M0.md`.

## Identifiants

TEST-01, TEST-02, TEST-03 (partiel), BLD-01 (partiel), BLD-02 (partiel, **CI jamais exécutée**), TOOL-03 (passé de P2 à P0 ; partiel : OpenGL seulement), DOC-01 (partiel), RHI-14 (préparé : outil et image de référence). Aucune case n'est passée à `Fait` sans preuve (`docs/STATUS.md`).

## Contenu

- `feat/TEST-01-doctest` : 110 cas migrés, framework maison supprimé.
- `chore/M0-warnings` : `-Wall -Wextra -Wpedantic -Werror`, `-isystem`, `SANITIZE=`, 0 avertissement.
- `feat/M0-architecture-checks` : `make check` (couches : 5 violations, isolation : 13 fichiers, en-têtes : 1), baselines.
- `feat/TOOL-03-capture`, `feat/TOOL-03-compare` : `--capture`, `--seed`, `tools/compare_png.py`.
- `fix/M0-sanitizer-build` : comportement indéfini corrigé dans `PCGRandom` (séquence inchangée, test d'or), 2 faux positifs GCC 13.
- `feat/BLD-02-ci`, `fix/M0-ci-yaml` : `.github/workflows/ci.yml`.
- `docs/M0`, `docs/M0-release` : `CLAUDE.md` (commandes, conventions), README, GAPS, STATUS, QUESTIONS, notes de version.

## Commande de démo

```bash
sh tools/setup-linux-deps.sh && make all-debug
make run
apps/demo/bin/debug/main --api opengl --capture /tmp/capture
python3 tools/compare_png.py docs/BASELINE/reference/scene-opengl-seed1-1280x720.png /tmp/capture/scene.png
```

## Résultat des tests (clone propre de `dev`, Linux, g++ 13.3)

- `make demo-test` : 118 cas, 647 assertions, 0 échec ; build sans avertissement.
- `make demo-test SANITIZE=address,undefined` : 118 cas verts, aucun rapport.
- `python3 -m unittest discover -s tests/tools` : 6 tests OK.
- `make check` : OK en mode avertissement ; 0 violation hors baseline.
- Capture vs référence : 0 pixel de différence.

## Non fait / à surveiller avant de fusionner

- **CI** : Linux (build, tests, ASan/UBSan, capture) et Windows verts ; macOS rouge au dernier run (correctif d'édition de liens poussé, à reconfirmer). Windows/macOS non bloquants et sans `-Werror`.
- **Revue CodeRabbit** : triée dans `docs/GAPS.md` ; corrigé : fuite de jeton Git, cache CI, macOS, NSIS, Benchmark ; reporté : constats sur l'ancien code (jalons M2-M4).
- Windows et macOS non vérifiés ; Vulkan non touché (`--capture` le refuse jusqu'en M4).
- Dépendance à installer : `sudo apt install doctest-dev`.
- Décisions attendues : `docs/QUESTIONS.md` A17 (migration vers `docs/03`), A19 (disque), A20 (TEST-01), A21 (UI-02 et batch 2D), A22 (REPO-01).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
