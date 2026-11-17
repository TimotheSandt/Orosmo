# STATUS

Mis à jour après l'audit (2026-11-17). États détaillés et preuves : [AUDIT_REPORT.md](AUDIT_REPORT.md). Liste de travail : [GAPS.md](GAPS.md).

| Jalon | État | Preuve |
| --- | --- | --- |
| M00 Nettoyage du projet et du dépôt | **Fait**, en attente de ta validation | `docs/releases/M00.md` ; `tools/check-repo.sh` : OK ; build + 102/102 tests depuis un clone propre |
| M0 Audit et base solide | À faire | |

Commandes (Linux) : voir [README.md](../README.md). Démo : `make all-debug && make run`. Tests : `make demo-test`.

## État des identifiants de `docs/02` (audit du 2026-11-17)

| État | Nombre |
| --- | ---: |
| Fait | 1 (REPO-01) |
| Partiel | 46 |
| Absent | 102 |
| Incompatible | 3 (RHI-01, RHI-18, EXT-03) |
| Total | 152 |

Aucune case n'est cochée sans preuve : voir la colonne « Comment » de `AUDIT_REPORT.md`. Prochain jalon : **M0** (en attente du feu vert de l'auteur après lecture de l'audit).
