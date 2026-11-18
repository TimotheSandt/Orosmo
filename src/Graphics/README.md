# Graphics

Rendu 2D/3D et couche d'abstraction graphique actuelle (OpenGL, Vulkan, Metal en squelette).

- **Rôle** : fenêtre (`Window`), caméra, maillages, textures, shaders (`ShaderProgram`, `ShaderLibrary`), cibles de rendu, rendus `Renderer2D`/`Renderer3D`, upscaling, et les backends (`Backends/OpenGL`, `Backends/Vulkan`, `Backends/Metal`).
- **API publique** : `include/Graphics/` (`Core/` : `GraphicsBackend`, `GraphicsDevice`, `RenderState`, `WindowContext`…).
- **Dépendances** : GLFW, glad, Vulkan, shaderc, stb, glm ; modules `Utilities`, `Profiler`, `UI` (voir l'avertissement).
- **Couches (M1 bis)** : `tools/check_layers.py --strict` ne trouve plus aucune violation. `Window` (couche platform) n'inclut plus aucun en-tête du RHI : elle passe par `IWindowGraphicsBinding` (`include/Graphics/Presentation/WindowGraphicsBinding.h`), que `BindGraphicsRuntime` installe (`GraphicsWindowContext::RuntimeBinding()`, `src/Graphics/Core/WindowContext.cpp`). `Window` garde le binding avec lequel elle a été initialisée et s'en sert à `Close()`. Le binding reste installé après `ClearGraphicsRuntime`, mais il choisit son API d'après le runtime actif : **fermer la fenêtre avant `ClearGraphicsRuntime`**, sinon un contexte Vulkan ne serait pas libéré (comportement d'avant M1 bis, G65). De même, le `Profiler` n'inclut plus `Graphics` : il reçoit un fournisseur de minuteries GPU installé au même endroit. Tests : `tests/suites/window_graphics_binding_tests.cpp`, `profiler_gpu_timer_tests.cpp`.
- Les appels `gl*`/`Vk*` sont tous sous `Backends/` à ce jour.
- Détail des backends : [docs/GRAPHICS_BACKENDS.md](../../docs/GRAPHICS_BACKENDS.md).
