# terrain-viewer (TOOL-01, TER-12)

Visualiseur de terrain : caméra libre, tuiles générées en tâche de fond (`engine::world::TileStreamer`, niveaux de détail cousus)
et dessinées par `TerrainRenderer`, **panneau de réglage écrit dans le langage d'interface** (`src/TerrainPanel.cui`, compilé par
`cui` vers `engine::ui`) : graine, préréglages (chargés et enregistrés), tous les paramètres du terrain affichés automatiquement
depuis leur réflexion (UI-12), thème clair ou sombre changé en direct (et rechargé à chaud si `bin/res/themes/*.json` est modifié),
export d'une carte de hauteur 16 bits.

```bash
make viewer-run ARGS="--seed 42 --preset mountains"     # à la racine du dépôt
make viewer-capture                                      # sans fenêtre : build/viewer-capture/{dark,light}/viewer.png
apps/terrain_viewer/bin/terrain-viewer --help
```

| Fichier | Rôle |
| --- | --- |
| `src/TerrainPanel.cui` | la vue : structure, liaisons (`$state.seed`), événements nommés (`.onClick(state.randomizeSeed)`) |
| `src/viewer_state.hpp/.cpp` | l'état lié par la vue et sa logique (préréglages, demandes de régénération, d'export, de thème) |
| `src/viewer.hpp/.cpp` | fenêtre, RHI, rendu, streaming, entrées, capture |
| `presets/*.json` | préréglages (`{"version": 1, "data": TerrainParams}`) ; « Save » écrit `bin/res/presets/custom.json` |

- Caméra : clic droit + souris pour tourner ; ZQSD / WASD ou flèches ; Espace / Ctrl : monter, descendre ; Maj : plus vite. Échap
  quitte (sauf pendant une saisie). La souris sur le panneau ne bouge pas la caméra (`Ui::wantsMouse`).
- Régénération : à chaque changement de graine ou de préréglage, et 0,2 s après le dernier changement d'un paramètre (« Live
  update »). Le jeu de tuiles affiché change d'un coup quand toute la nouvelle sélection est prête (pas de trou).
- Export : `exports/heightmap_seed<graine>.png`, 1024 × 1024 échantillons autour de la caméra (TER-11).
- Codes de retour : 0, 1 (utilisation), 2 (exécution : pas de périphérique graphique, ressource manquante, capture ratée).
- `--backend vulkan` est refusé jusqu'à M4 (RHI-04).
