# ThirdParty

Pas de code tiers copié ici. `StbImage.cpp` instancie l'implémentation de `stb_image` (`STB_IMAGE_IMPLEMENTATION`) pour que l'en-tête `stb_image.h`, fourni par le paquet `libstb-dev` (apt) ou vcpkg, soit lié une seule fois.

Les dépendances sont gérées par apt/vcpkg : voir [README.md](../../README.md) et `vcpkg.json`. Aucun dossier `third_party/` n'est nécessaire tant qu'aucune bibliothèque n'est copiée dans le dépôt.
