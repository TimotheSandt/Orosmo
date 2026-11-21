# M3 : premier générateur de terrain et interface de base (`dev` → `main`)

## Résumé

Premier jalon présenté comme **utilisable** (release proposée : `v0.3.0`). Le moteur génère un terrain **déterministe** (mêmes
octets pour la même graine, CPU, et GPU dans une tolérance fixée), le découpe en tuiles à niveaux de détail sans fissure,
le charge en tâche de fond sous un budget mémoire, le dessine (pente/altitude, triplanaire, reverse-Z dans le repère Vulkan du RHI)
et l'exporte (PNG 16 bits, EXR, OBJ, glTF, STL imprimable, biomes). Un nouveau module **`orosmo::ui`** (widgets, mise en page,
thèmes JSON rechargés à chaud, paramètres réfléchis affichés automatiquement) est piloté par le **langage d'interface**, dont le
compilateur produit maintenant du code `orosmo::ui` avec événements nommés, liaisons dans les deux sens et erreurs
`fichier:ligne:colonne`. Deux outils : `terrain-gen` (sans fenêtre) et le **visualiseur** (panneau `.cui`, préréglages, thème
clair/sombre en direct, export). Le paquet de release contient shaders, ressources et exemples ; `orosmo init` est vérifié de bout
en bout. Détail : `docs/releases/M3.md`.

M2 (PR #7) est déjà fusionné dans `main` ; la 3e passe de CodeRabbit sur M2 est relevée dans `docs/GAPS.md` (G104), non
traitée.

## Identifiants

`docs/STATUS.md`, section « M3 » : 15 identifiants passent à **Fait** (TER-01, TER-02, TER-03, TER-07, TER-11, TER-12, TER-13,
TOOL-02, REN-02, REN-34, UI-03, UI-05, UI-06, UI-11, UI-12) ; TER-04 et PLAT-05 passent à Partiel. Totaux : Fait 31, Partiel 43,
Absent 77, Incompatible 1.

## Décisions

- Appliquées : A26 (repère Vulkan du RHI), A29 (M3 en lots A à F).
- Choix par défaut à confirmer : A13 (compilateur en Rust ; événements `.onClick(nom)` ; en-tête nommé d'après le fichier) ; A30
  (deux modules d'interface pendant la migration, `src/ui/legacy/`) ; A31 (`platform` provisoire, version `v0.3.0`).

## Commande de démo

```bash
make terrain-gen && apps/terrain_gen/bin/terrain-gen --seed 42 --size 4096 --out relief.png
make viewer-run ARGS="--seed 42 --preset mountains"
```

## Tests

Depuis un clone propre de `dev` (Linux x64) : `make demo-test` (doctest, `docs/BASELINE/tests-M3.txt`), ASan + UBSan propres,
`make check`, `make cui-test`, `make terrain-gen-check`, `make viewer-capture`, `make e2e-init`, capture de la démo identique à la
référence. Résultats chiffrés : `docs/releases/M3.md`.

## À vérifier à la relecture

- Le visualiseur à la main (caméra, export, « Save », thème) : G109.
- Les jobs Windows et macOS de la CI (jamais observés verts ; macOS sans rendu avant M4) : G110.
- La 3e passe de CodeRabbit sur M2 (G104) : à corriger avant ou après la fusion ?

🤖 Generated with [Claude Code](https://claude.com/claude-code)
