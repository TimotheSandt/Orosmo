# terrain-gen (TOOL-02)

Génère une région de terrain **sans fenêtre ni GPU** (module `engine::world`) et l'exporte (TER-11).

```bash
make terrain-gen
apps/terrain_gen/bin/terrain-gen --seed 42 --size 4096 --out relief.png      # carte de hauteur 16 bits
apps/terrain_gen/bin/terrain-gen --seed 42 --size 512 --out terrain.stl       # solide fermé, imprimable
apps/terrain_gen/bin/terrain-gen --seed 7 --size 1024 --out h.exr --biomes biomes.png --params preset.json
make terrain-gen-check   # mêmes octets avec 1 et 4 threads, somme de contrôle de référence, codes de retour
```

- Formats (extension de `--out`) : `.png` (hauteurs 16 bits, 0 = min, 65535 = max), `.exr` (float, mètres), `.obj`, `.gltf`, `.stl`.
- `--params` : document JSON `{"version": 1, "data": {...}}` de `TerrainParams` (réflexion CORE-12, champs absents = défauts).
- Sortie standard : graine, taille, bornes des hauteurs, **somme de contrôle** des hauteurs (identique sur tous les systèmes, TEST-03).
- Codes de retour : **0** succès, **1** erreur d'utilisation (argument invalide, format inconnu, paramètres hors plage), **2**
  erreur d'entrée/sortie (fichier de paramètres illisible, écriture impossible).
- Référence : `docs/BASELINE/terrain-gen-seed42-256.txt`, vérifiée en CI sur Linux x64 et arm64, macOS et Windows.
