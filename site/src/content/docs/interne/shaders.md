---
title: Pipeline de shaders
description: GLSL 450 source unique, compilation SPIR-V, réflexion, traduction pour OpenGL, cache et rechargement à chaud.
sidebar:
  order: 4
---

*(Sources : `docs/03` §5, `src/rhi/README.md` « Pipeline de shaders », `src/renderer/README.md`.)*

## Source unique en GLSL 450

Les shaders sont écrits **une fois**, en GLSL 450 « style Vulkan » : `layout(set = N, binding = M)`,
`layout(push_constant)`, `gl_VertexIndex`, `#include`, textures combinées (`sampler2D`). Ils sont dans `shaders/rhi/`
(maillages, ombres, clusters, culling GPU, 2D, interface, `post/` pour les post-traitements) et la boîte à outils partagée
dans `shaders/include/orosmo/` (`math.glsl`, `coords.glsl`, `depth.glsl`, `lights.glsl`, `shadow.glsl`, `noise.glsl`,
`pbr.glsl`), incluse par `#include "orosmo/math.glsl"`.

## Chaîne de compilation

```text
GLSL 450 ──shaderc──▶ SPIR-V ──SPIRV-Cross (réflexion)──▶ liaisons, push constants, taille de groupe, sorties
                         │
                         ├──▶ Vulkan : SPIR-V utilisé tel quel
                         └──▶ OpenGL : GLSL 430 régénéré par SPIRV-Cross, liaison = set × 8 + binding,
                                       push constants dans l'UBO 24
```

La réflexion refuse une liaison hors des 3 groupes × 8, des textures et échantillonneurs séparés, ou des ressources qui se
chevauchent. Le choix « SPIRV-Cross plutôt que `GL_ARB_gl_spirv` » vient du minimum OpenGL 4.3 ; `GL_ARB_gl_spirv` pourra
s'ajouter derrière `Capabilities::spirvShaders`.

## Variantes

Les variantes d'un shader sont choisies par macros de préprocesseur (`OROSMO_SHADOWS`, `OROSMO_FORWARD_PLUS`,
`OROSMO_TRANSPARENT`, AOV…), sélectionnées par l'application ou le plugin qui en a besoin.

## Cache et rechargement à chaud

- **`ShaderCache`** : clé = hachage du source *prétraité* (includes et macros développés) + étage + version du cache ; en
  mémoire puis sur disque (`<dossier>/<hash>.spv`, écriture atomique par renommage ; un fichier corrompu est recompilé).
- **Cache de pipelines** Vulkan sur disque (un fichier d'un autre GPU est ignoré).
- **Rechargement à chaud** (`ShaderHotReloader`, `ShaderLibrary::pollHotReload`) : surveille le fichier et ses
  `#include`, recompile, et l'appelant recrée le pipeline ; si la compilation échoue, l'ancien pipeline est gardé.

## Garantie

Aucune différence de comportement selon le backend n'est acceptable pour un même shader : les tests tournent sur les deux
backends, et des images de référence comparent OpenGL et Vulkan (`make demo-vulkan-check`).
