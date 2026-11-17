# Graphics

Rendu 2D/3D et couche d'abstraction graphique actuelle (OpenGL, Vulkan, Metal en squelette).

- **Rôle** : fenêtre (`Window`), caméra, maillages, textures, shaders (`ShaderProgram`, `ShaderLibrary`), cibles de rendu, rendus `Renderer2D`/`Renderer3D`, upscaling, et les backends (`Backends/OpenGL`, `Backends/Vulkan`, `Backends/Metal`).
- **API publique** : `include/Graphics/` (`Core/` : `GraphicsBackend`, `GraphicsDevice`, `RenderState`, `WindowContext`…).
- **Dépendances** : GLFW, glad, Vulkan, shaderc, stb, glm ; modules `Utilities`, `Profiler`, `UI` (voir l'avertissement).
- **Avertissement** : les sources de `Graphics` incluent `UI` (`Renderer2D.cpp`) et `Profiler` (`Window.cpp`), et `Profiler.h` et `UI` incluent `Graphics` : il y a des cycles. Ce n'est pas corrigé en M00 (rangement seulement) ; à traiter à l'audit puis en M2 (`docs/03`, §1).
- Les appels `gl*`/`Vk*` sont tous sous `Backends/` à ce jour.
- Détail des backends : [docs/GRAPHICS_BACKENDS.md](../../docs/GRAPHICS_BACKENDS.md).
