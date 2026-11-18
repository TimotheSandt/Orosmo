# Profiler

Mesures par image : `FPSCounter`, `Profiler`, `RingBuffer`.

- **API publique** : `include/Profiler/`.
- **Dépendances** : `Utilities`, `engine/core/system.hpp`. Plus aucune dépendance vers `Graphics` (M1 bis).
- **Minuteries GPU** : `Profiler::ProfileGPU` utilise `IGpuTimerFactory` (`include/Profiler/GpuTimer.h`). Le runtime graphique en installe une quand on lui lie un backend (`BindGraphicsRuntime`) ; sans fournisseur, ou sans périphérique, la mesure se fait avec l'horloge CPU. `Profiler::SetGpuTimerFactory` / `GetGpuTimerFactory`.
- Plus de code spécifique à un système dans ce module : l'attente précise et la résolution du minuteur Windows passent par `engine/core/system.hpp`. Tests : `tests/suites/profiler_gpu_timer_tests.cpp`.
