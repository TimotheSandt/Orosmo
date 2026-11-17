# Profiler

Mesures par image : `FPSCounter`, `Profiler`, `RingBuffer`.

- **API publique** : `include/Profiler/`.
- **Dépendances** : `Utilities` ; `Profiler.h` inclut `Graphics/Core/GraphicsRuntime.h` (voir l'avertissement sur les cycles dans `src/Graphics/README.md`).
- Code spécifique Windows sous `#ifdef _WIN32` (`FPSCounter`).
