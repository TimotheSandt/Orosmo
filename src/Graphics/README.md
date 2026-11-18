# Graphics

Ce qui reste de l'ancienne couche graphique après M2 B6 : la **fenêtre** et le choix de l'API en ligne de commande. Tout le rendu est
passé sur le RHI (`src/rhi/`, `include/engine/rhi/`) et la couche `engine::renderer` (`src/renderer/`).

| Fichier | Rôle |
| --- | --- |
| `Window.cpp` / `include/Graphics/Window.h` | fenêtre GLFW : taille, plein écran, bordure, compteur d'images, entrées. **Ne connaît aucune API graphique** : `Init(beforeCreate)` appelle un rappel juste avant la création (l'application y pose `rhi::prepareWindowHints`), puis le périphérique RHI adopte la fenêtre (`SurfaceDesc::nativeWindow`) et la présente. Migrera en `platform::Window` au lot C |
| `Backends/GraphicsAPI.cpp` / `include/Graphics/Backends/GraphicsAPI.h` | `--api`, `--list-apis`, `--choose-api`, `--capture`, `--seed` : analyse de la ligne de commande. OpenGL disponible ; Vulkan « pas avant M4 », Metal « pas avant M9 » (RHI-04) |

- **Supprimé en B6** (historique git, étiquette `archive/pre-M2-B6-legacy-graphics`) : `IGraphicsDevice` et ses backends OpenGL, Vulkan et Metal,
  `GraphicsRenderState`, `Buffer`, `Texture`, `RenderTarget`, `ShaderProgram`, `ShaderLibrary`, `Mesh`, `Sprite`, `Light`, `Camera`,
  `Renderer`, `Renderer2D`, `Renderer3D`, les modes d'upscale, `Screenshot`, `GPUTimestampQuery`, les shaders d'origine. Leurs remplaçants :
  voir `src/renderer/README.md` et `docs/RHI.md`.
- **Dépendances** : GLFW, glm ; modules `Utilities`, `Profiler`.
- **Isolation des API** : plus aucun `gl*`, `GL_*`, `Vk*`, `MTL*` hors de `src/rhi/` (`tools/check_api_isolation.py --graphics-only --strict`).
  GLFW reste dans `Window`, `InputManager` et la démo jusqu'au lot C (baseline `tools/baseline/api_isolation.txt`).
