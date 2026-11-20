# M2 lot A : interface RHI et backend OpenGL (`dev`)

Premier des trois lots de M2 (découpage décidé par l'auteur, `docs/QUESTIONS.md` A27). Ce lot **ajoute** le RHI à côté de l'existant : aucun code de rendu actuel n'est modifié, la démo et l'image de référence sont inchangées. La migration de l'éclairage, du texte et de l'interface est le lot B ; la fenêtre et le mode sans fenêtre, le lot C. M2 n'est pas terminé et **n'est pas proposé à `main`**.

## Ce qui est livré

| Élément | Où |
| --- | --- |
| Interface publique (aucun type d'API graphique) | `include/engine/rhi/{handle,types,capabilities,device}.hpp` |
| Handles opaques indice + génération, table qui détecte les handles périmés | `handle.hpp` : `Handle<Tag>`, `HandlePool` |
| `Device` : buffers, textures (2D, tableau, cube, 3D, mips), échantillonneurs, shaders, pipelines graphique et calcul, groupes de liaison, pools d'horodatages, destruction différée, lectures/écritures synchrones, `beginFrame`/`present`/`resize` | `device.hpp`, `src/rhi/opengl/gl_device.cpp` |
| `CommandEncoder` : passes MRT (`LoadOp`/`StoreOp` par cible), pipeline, groupes, sommets, index, push constants, viewport, ciseau, dessins instanciés et indirects, calcul direct et indirect, copies, mips, groupes de debug, horodatages | `device.hpp`, `src/rhi/opengl/gl_encoder.cpp` |
| `Capabilities` lues du pilote, capacités optionnelles réservées à `false` (RHI-18) | `capabilities.hpp` |
| Modèle de liaison à trois groupes aplati pour OpenGL (`groupe × 8 + binding`, UBO 24 pour les push constants) | `src/rhi/README.md` |
| Backend OpenGL 4.3 hors écran, validation `GL_KHR_debug` comptée, noms de debug | `src/rhi/opengl/` |
| Archive `librhi.a`, règle de couche `rhi`, tests GPU exécutés sous xvfb + Mesa en CI | `mk/engine.mk`, `tools/layers.json`, `.github/workflows/ci.yml` |
| Démo `make rhi-demo` | `apps/rhi_demo/main.cpp` |
| Documentation du module | `src/rhi/README.md` |

## Résultat

| Contrôle | Avant | Après |
| --- | --- | --- |
| `make demo-test` | 224 cas, 3348 assertions | **256 cas, 3882 assertions**, 0 échec (32 cas de plus : 8 sans GPU, 24 sur un contexte OpenGL) |
| ASan + UBSan (`fclean` d'abord, G66) | 224 cas verts | **256 cas verts**, aucun rapport |
| `make all-debug` | 0 avertissement | **0 avertissement** (`-Wall -Wextra -Wpedantic -Werror`) |
| `check_layers --strict` | 0 violation | **0 violation** (172 fichiers) |
| `check_headers --strict` | 0 | **0** (102 en-têtes, dont les 4 nouveaux) |
| `check_api_isolation` | 11 fichiers (baseline) | 11 fichiers : inchangé, le RHI n'ajoute aucune infraction (`src/rhi/` est une zone autorisée) |
| Capture OpenGL graine 1 | identique | **identique à la référence** (0 pixel hors tolérance, écart max 0/255) |
| Messages de validation GL sur les tests et la démo | | **0** |

Mesuré sous Linux (g++ 13.3, Mesa 26.1.6, Intel Arc, OpenGL 4.6 core), depuis un **clone propre de `dev`**.

## Décisions à valider

- **A26** (nouvelle) : le RHI suit le repère OpenGL (origine en bas à gauche) pour que la migration donne la même image ; Vulkan/Metal s'y adapteront. Profondeur 0..1 à décider avant M3 (reverse-Z). Recommandation et alternative dans `docs/QUESTIONS.md`.
- **A1** (réponse enregistrée) : l'ancien backend Vulkan est retiré du build au lot B, archivé, réécrit en M4.
- **A27** : découpage de M2 en trois lots.

## Limites connues (voir `docs/GAPS.md`, G67 à G72)

- **Windows et macOS** : le backend n'y est pas exercé. macOS plafonne à OpenGL 4.1 (`createDevice` renvoie `kUnsupported`), le runner Windows n'a qu'un OpenGL 1.1 ; les tests s'arrêtent avec un avertissement. La CI vérifie seulement que le code **compile** sur ces systèmes (G67).
- Surface hors écran seulement (la fenêtre arrive au lot C) ; shaders GLSL 430 écrits à la main (le pipeline GLSL 450 → SPIR-V arrive au lot B) ; pas de cache de pipelines sur disque ; noms de debug appelés mais non relus (RenderDoc) ; barrières conservatrices ; pas de multi-draw avec compteur.

## Commande de démo

```bash
make rhi-demo                                          # calcul -> image de stockage, passe MRT, 3 triangles instanciés, horodatages ; écrit rhi-demo.png
make rhi-demo RHI_DEMO_ARGS="--out /chemin/image.png"  # code de sortie 0 seulement si 0 message de validation et identifiant d'objet = 7
```

Sortie attendue (Intel Arc, Mesa 26.1.6) : `RHI opengl | Mesa Intel(R) Arc(tm) Graphics (MTL) | 4.6 ...`, `identifiant d'objet au centre : 7`, `messages de validation : 0`.

## Résultat des tests (Linux, clone propre de `dev`)

```
make demo-test                                -> 256 cas, 3882 assertions, 0 échec (docs/BASELINE/tests-M2-lotA.txt)
make fclean demo-fclean && make demo-test SANITIZE=address,undefined  -> 256 cas verts, aucun rapport ASan/UBSan
make all-debug                                -> 0 avertissement
make check                                    -> couches 0, en-têtes 0, isolation : baseline inchangée
capture OpenGL graine 1                       -> identique à la référence
make rhi-demo                                 -> 0 message de validation, identifiant d'objet 7
```

## Étape suivante (sur ton accord)

**Lot B** : pipeline de shaders (GLSL 450 → SPIR-V → GLSL 430, réflexion, inclusions, rechargement à chaud, cache), migration de l'éclairage, du texte et de l'interface sur le RHI (listes de dessin, UI-02), tests d'images de référence (RHI-14), règle d'isolation des API en mode bloquant, retrait de l'ancien backend Vulkan et de l'ancienne abstraction. C'est le lot le plus gros ; je proposerai son découpage en branches avant de commencer.
