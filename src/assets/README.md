# assets

Couche 4 de `docs/03` §1 : import et préparation des contenus (images, glTF, cuisson, rechargement). Archive : `libassets.a`
(`src/assets/`), API publique `include/orosmo/assets/`. Dépend de `renderer`, `rhi`, `platform` et `core`. Namespace
`orosmo::assets`. Créé en M6 (lot A, `docs/05`, A70).

| En-tête | Rôle |
| --- | --- |
| `image_loader.hpp` | `decodeImage` (octets en mémoire) et `loadImage` (fichier) : PNG, JPG, TGA, BMP décodés par stb en RGBA8, première ligne = haut (A26) ; `TextureRole` (`kColor`, `kData`, `kNormal`) → `colorSpaceOf`, `textureFormatFor` (couleur en `kRGBA8Srgb`, données en `kRGBA8Unorm`) |
| `texture_cache.hpp` | `createTexture` (image décodée → `renderer::Texture` au format de l'usage, mips générés sur le GPU) ; `TextureCache` : une texture partagée par (chemin normalisé, usage, filtre, adressage), `loadFromMemory` pour les images incluses dans un fichier, `purgeUnused` |

Exemple :

```cpp
orosmo::assets::TextureCache textures(device);
auto albedo = textures.load("res/brick.png");                                      // sRGB, mips
auto normal = textures.load("res/brick_n.png", {orosmo::assets::TextureRole::kNormal}); // linéaire
```

Mips : générés par le backend (`CommandEncoder::generateMips`) ; une texture sRGB est filtrée dans l'espace linéaire (vérifié sur
OpenGL et Vulkan : `tests/assets/texture_loading_tests.cpp`, damier noir et blanc → mip 188 en sRGB, 128 en linéaire).

Limites : images 8 bits par canal seulement (les PNG 16 bits sont réduits à 8 bits par stb) ; pas de HDR ici (IBL, lot D) ni de
KTX2 (lot G).
