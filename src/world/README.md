# world

Couche `world` de `docs/03` : bruit, terrain, export. Archive : `libworld.a` (`src/world/`), API publique `include/engine/world/`.
Ne dépend **pas** de `renderer` : la génération tourne sans GPU (`terrain-gen`, tests). Le calcul sur GPU passe par le RHI et est
isolé dans `src/world/gpu/`.

| En-tête | Rôle |
| --- | --- |
| `noise.hpp` | TER-01 : `perlin2/3`, `simplex2/3`, `worley2/3` (f1, f2), `fbm2/3` (`FbmParams` : base, octaves, fréquence, lacunarité, gain), `warpedFbm2` (distorsion de domaine, `WarpParams`) ; hachage entier `hashMix`, `hash2`, `hash3`, `deriveSeed` |
| `checksum.hpp` | TEST-03 : `checksum64` (FNV-1a 64 bits, octets en petit-boutiste) des sorties de génération |

## Déterminisme (règle 9, TEST-03)

- Même graine, mêmes paramètres → **mêmes octets** sur tous les systèmes. Aucune horloge, aucun `rand()`.
- Gradients et points caractéristiques tirés d'un **hachage d'entiers** (pas de table de permutation remplie au hasard).
- Calculs en `float` IEEE avec `+ - * / floor sqrt` seulement (correctement arrondis partout) ; pas de `sin`, `exp`, `pow` de la
  bibliothèque. Le moteur et le kit compilent avec `-ffp-contract=off` (Clang fusionnerait sinon `a*b+c` en FMA sur arm64).
- Sommes de contrôle de référence versionnées : `tests/world/noise_tests.cpp` ; la CI les vérifie sur Linux x64 et arm64,
  Windows (MinGW) et macOS arm64.

## Exemple

```cpp
#include <engine/world/noise.hpp>
namespace w = engine::world;
const w::FbmParams relief{.basis = w::NoiseBasis::kPerlin, .octaves = 6, .frequency = 0.01f};
const float h = w::warpedFbm2(x, z, seed, relief, {.amplitude = 20.0f, .frequency = 0.004f});
```

## Limites

- Domaine : |coordonnée| < 2^23 (précision du float). Les planètes (coordonnées mondiales en `double`, CORE-09) passeront par
  des coordonnées locales à la tuile.
- Le bruit de l'ancien module `Noise` (`include/Noise/`) reste utilisé par la démo historique (image de référence) ; le nouveau
  code utilise `engine::world`.
