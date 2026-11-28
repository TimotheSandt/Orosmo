# Backends graphiques

> **Obsolète depuis M2 B6.** L'ancienne abstraction `IGraphicsDevice` et ses backends ont été supprimés (historique git, étiquette
> `archive/pre-M2-B6-legacy-graphics`). Le guide à jour est [RHI.md](RHI.md) ; les détails du backend OpenGL sont dans
> [`src/rhi/README.md`](../src/rhi/README.md).

| Backend | État |
| --- | --- |
| OpenGL 4.3+ | **Disponible** : `src/rhi/opengl/` (RHI), c'est celui de la démo |
| Vulkan | **Pas avant M4** : réécriture sur le RHI avec VMA et couches de validation ; `--api vulkan` répond « not available yet (Vulkan: M4) » |
| Metal | **Absent** : macOS passe par Vulkan et MoltenVK (M9) ; backend natif reporté (proposition, A92) |
