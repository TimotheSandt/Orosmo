---
title: "Index des en-têtes publics"
description: "Tous les en-têtes de include/orosmo/, module par module, avec leurs types principaux et la page qui les explique."
sidebar:
  order: 9
---

Les en-têtes publics sont dans `include/orosmo/<module>/` et s'incluent par `#include <orosmo/<module>/<fichier>.hpp>`. Chaque
en-tête se compile seul (`make check-headers`). Les modules sont rangés en couches : un module n'inclut que ceux d'en dessous
(`core` → `platform` → `rhi` → `renderer` → `assets` → `anim` → `scene` → `ui` → `sim` → `app`, voir [Couches et modules](/interne/couches/)).

Espaces de noms : `orosmo::core`, `orosmo::platform`, `orosmo::rhi`, `orosmo::renderer`, `orosmo::assets`, `orosmo::anim`,
`orosmo::ui`, `orosmo::scene`, `orosmo::sim`, `orosmo::app`.

## Logique de jeu

| En-tête | Contenu | Page |
| --- | --- | --- |
| `orosmo/game.hpp` | en-tête unique et léger de la bibliothèque de jeu : contexte, monde, composants, systèmes (`sim/system.hpp`), types mathématiques | [Compiler](/reference/construction/#logique-de-jeu-rechargeable) |

## core

| En-tête | Types et fonctions principaux | Page |
| --- | --- | --- |
| `error.hpp` | `Error`, `ErrorCode`, `Result<T>`, `makeError`, `logError`, `ENGINE_ASSERT` | [Erreurs et journal](/guides/noyau/erreurs-journal/) |
| `log.hpp` | `Logger`, `LogLevel`, macros `LOG_*`, `SET_LOG_FILE`, `FLUSH_LOG_TO_FILE` | [Erreurs et journal](/guides/noyau/erreurs-journal/) |
| `config.hpp` | `Config` | [Configuration](/guides/noyau/configuration/) |
| `reflection.hpp` | `Reflect<T>`, `field`, `range`, `EnumNames<E>`, `describeFields`, `forEachField` | [Configuration](/guides/noyau/configuration/) |
| `serialization.hpp` | `Json`, `saveDocument`, `loadDocument`, `MigrationTable`, `parseJson`, `toBinary`, `fromBinary` | [Configuration](/guides/noyau/configuration/) |
| `tasks.hpp` | `TaskSystem`, `TaskHandle`, `TaskGroup` | [Tâches](/guides/noyau/taches/) |
| `time.hpp` | `IClock`, `SteadyClock`, `ManualClock`, `FrameTimer`, `FixedStepper` | [Temps et événements](/guides/noyau/temps-evenements/) |
| `events.hpp` | `EventBus`, `Subscription` | [Temps et événements](/guides/noyau/temps-evenements/) |
| `math.hpp` | `Vec2`…`Mat4`, `Quat`, `WorldPosition`, `toLocal`, `toWorld`, `narrow`, `widen` | [Temps et événements](/guides/noyau/temps-evenements/) |
| `math_types.hpp` | les types seuls (`Vec3`, `Quat`, `WorldPosition`), sans les fonctions de glm | — |
| `vfs.hpp` | `Vfs` | [Fichiers et ressources](/guides/noyau/fichiers-ressources/) |
| `paths.hpp` | `GET_RESOURCE_PATH`, `setWorkingDirectoryToExecutable`, `validateAssets` | [Fichiers et ressources](/guides/noyau/fichiers-ressources/) |
| `resources.hpp` | `Handle<T>`, `ResourceCache<T>`, `ResourceState` | [Fichiers et ressources](/guides/noyau/fichiers-ressources/) |
| `file_watcher.hpp` | `FileWatcher`, `FileEvent`, `FileChange` | [Fichiers et ressources](/guides/noyau/fichiers-ressources/) |
| `profiler.hpp`, `gpu_timer.hpp` | `Profiler`, `IGpuTimer`, `IGpuTimerFactory` | [Mesurer](/guides/noyau/profilage/) |
| `trace.hpp` | `TraceRecorder`, `ScopedTrace`, `ENGINE_TRACE_SCOPE` | [Mesurer](/guides/noyau/profilage/) |
| `benchmark.hpp` | `bench::Scenario`, `ScenarioRegistry`, `Runner`, `BenchmarkConfig`, `Statistics` | [Mesurer](/guides/noyau/profilage/) |
| `fps_counter.hpp`, `ring_buffer.hpp` | `FPSCounter`, `RingBuffer<T>` | [Mesurer](/guides/noyau/profilage/) |
| `memory.hpp` | `ArenaAllocator`, `ObjectPool<T>` | — |
| `parse.hpp` | `parseFloat`, `parseDouble` (indépendants de la locale) | — |
| `system.hpp` | services du système (chemin de l'exécutable, dossier de données) déclarés ici, définis par `platform` | — |
| `random.hpp` | `Random` (PCG32, même suite sur toutes les plateformes) | [Simulation à pas fixe](/guides/scene/simulation/) |
| `hash.hpp` | `Fnv` (FNV-1a 64 bits, empreinte stable) | [Sauvegarder une scène](/guides/scene/sauvegarde/#empreinte) |
| `spsc_queue.hpp` | `SpscQueue<T>` (un producteur, un consommateur, sans verrou) | [Simulation à pas fixe](/guides/scene/simulation/#thread-de-simulation) |

## platform

| En-tête | Types et fonctions principaux | Page |
| --- | --- | --- |
| `window.hpp` | `Window`, `WindowParameters`, `WindowState` | [Fenêtre, clavier et souris](/guides/application/fenetre-entrees/) |
| `input_manager.hpp` | `InputManager`, `InputAction`, `InputEvent` | [Fenêtre, clavier et souris](/guides/application/fenetre-entrees/) |
| `gamepad.hpp` | `Gamepads`, `GamepadState`, `GamepadButton`, `GamepadAxis` | [Naviguer au clavier et à la manette](/guides/interface/navigation/) |
| `dynamic_library.hpp` | `DynamicLibrary` (copie sous un nom unique, `dlopen` ou `LoadLibraryW`) | [Modules scene, sim et app](/interne/scene-sim-app/) |
| `process.hpp` | `runCommand`, `CommandResult` (commande du système, bloquante) | [Vues rechargées à chaud](/guides/interface/rechargement-vues/) |

## rhi

| En-tête | Types et fonctions principaux | Page |
| --- | --- | --- |
| `device.hpp` | `Device`, `CommandEncoder`, `DeviceDesc`, `SurfaceDesc`, `createDevice`, `prepareWindowHints` | [Choisir le backend](/guides/application/peripherique/), [RHI](/interne/rhi/) |
| `capabilities.hpp` | `Capabilities` | [RHI](/interne/rhi/) |
| `handle.hpp` | `Handle<Tag>`, `BufferHandle`, `TextureHandle`…, `HandlePool` | [RHI](/interne/rhi/) |
| `types.hpp` | `Backend`, `Format`, descriptions (`BufferDesc`, `TextureDesc`, `BindGroupDesc`, `RenderPassDesc`…) | [RHI](/interne/rhi/) |
| `shader_compiler.hpp` | `compileGlsl`, `ShaderCache`, `ShaderHotReloader`, `ShaderReflection` | [Chaîne des shaders](/interne/shaders/) |

## renderer

| En-tête | Types principaux | Page |
| --- | --- | --- |
| `scene_renderer.hpp` | `SceneRenderer`, `SceneRendererConfig`, `FrameOptions` | [Dessiner une scène](/guides/rendu-3d/scene/) |
| `camera.hpp`, `mesh.hpp` | `Camera`, `Mesh`, `MeshData`, `TextureBinding` | [Dessiner une scène](/guides/rendu-3d/scene/) |
| `render_context.hpp`, `mesh_builder.hpp` | `RenderContext`, `MeshBuilder`, `MeshVertex` | [Dessiner une scène](/guides/rendu-3d/scene/#contexte-de-rendu-depuis-m8) |
| `material.hpp` | `PbrMaterial`, `PbrDefaults`, `PbrShaderCache`, `PbrParams` | [Matériaux PBR](/guides/rendu-3d/materiaux-pbr/) |
| `light.hpp`, `forward_plus.hpp` | `Light`, `LightManager`, `ForwardPlusLighting`, `enableForwardPlus` | [Lumières](/guides/rendu-3d/lumieres/) |
| `ibl.hpp` | `IblFeature`, `ProbeSet`, `EquirectImage` | [Environnement et sondes](/guides/rendu-3d/environnement-ibl/) |
| `shadow_feature.hpp`, `shadows.hpp`, `transparent_shadow.hpp` | `ShadowFeature`, `ShadowSettings`, `TransparentShadow` | [Ombres](/guides/rendu-3d/ombres/) |
| `transparent_feature.hpp`, `blend_mode.hpp` | `TransparentFeature`, `BlendMode` | [Transparence](/guides/rendu-3d/transparence/) |
| `post_effects.hpp`, `tonemap.hpp` | `TonemapFeature`, `BloomFeature`, `AutoExposureFeature`, `ColorGradingFeature`, `VignetteFeature` | [Post-traitement](/guides/rendu-3d/post-traitement/) |
| `antialiasing.hpp`, `upscaling.hpp` | `FxaaFeature`, `SmaaFeature`, `TaaFeature`, `IUpscaleMode`, `IPostProcessPass` | [Anticrénelage](/guides/rendu-3d/anticrenelage/) |
| `instance_renderer.hpp`, `instance_batch.hpp`, `object_set.hpp`, `culling.hpp` | `InstanceRenderer`, `ObjectSet`, `Frustum`, `Bvh`, `Aabb` | [Instancing et culling](/guides/rendu-3d/instancing-culling/) |
| `lod.hpp` | `LodSettings`, `LodSelection` | [LOD](/guides/rendu-3d/lod/) |
| `gpu_driven.hpp`, `gpu_scene.hpp` | `GpuDrivenFeature`, `GpuScene` | [Culling sur le GPU](/guides/rendu-3d/rendu-gpu/) |
| `debug_draw.hpp`, `debug_view.hpp`, `aov.hpp` | `DebugDraw`, `DebugView`, `Aov`, `AovSet` | [Debug visuel](/guides/rendu-3d/debug-visuel/) |
| `renderer2d.hpp`, `camera2d.hpp` | `Renderer2D`, `Sprite`, `Camera2D` | [Sprites et formes](/guides/rendu-2d/sprites-formes/) |
| `canvas_renderer.hpp`, `draw_list.hpp` | `CanvasRenderer`, `DrawList` | [Texte et polices](/guides/rendu-2d/texte/) |
| `layer2d_feature.hpp`, `scene_view.hpp` | `Layer2DFeature`, `SceneView`, `ViewsFeature` | [Mélanger 2D et 3D](/guides/rendu-2d/melange-2d-3d/) |
| `render_graph.hpp`, `render_feature.hpp`, `frame_data.hpp` | `RenderGraph`, `RenderFeature`, `RenderGraphBuilder`, `FrameData` | [Plugin de rendu](/guides/avance/plugin-de-rendu/), [Renderer](/interne/renderer/) |
| `shader_library.hpp`, `pipeline_cache.hpp` | `ShaderLibrary`, `ShaderProgram`, `PipelineCache` | [Shaders](/guides/avance/shaders/) |
| `gpu_buffer.hpp`, `texture.hpp`, `render_target.hpp`, `fullscreen_blit.hpp` | `GpuBuffer`, `Texture`, `RenderTarget`, `FullscreenBlit` | [Renderer](/interne/renderer/) |
| `frame_ring.hpp`, `batch_arena.hpp` | `FrameRing`, `BatchArena` | [Renderer](/interne/renderer/) |
| `image.hpp`, `frame_pacing.hpp`, `gpu_timer.hpp` | `Image`, `savePng`, `compareImages`, `FramePacer`, `GpuTimer`, `ProfilerGpuTimerFactory` | [Cadence et captures](/guides/application/cadence-captures/), [Mesurer](/guides/noyau/profilage/) |

## assets

| En-tête | Types principaux | Page |
| --- | --- | --- |
| `model.hpp` | `Model`, `MeshPrimitive`, `MaterialData`, `AnimationData`, `loadGltf` | [Importer un glTF](/guides/contenus/gltf/) |
| `model_loader.hpp` | `ModelLoader` | [Importer un glTF](/guides/contenus/gltf/#plus-court--modelloader-depuis-m8) |
| `render_model.hpp` | `RenderModel`, `RenderModelOptions` | [Importer un glTF](/guides/contenus/gltf/) |
| `texture_cache.hpp`, `image_loader.hpp`, `ktx2.hpp` | `TextureCache`, `TextureRequest`, `DecodedImage` | [Textures](/guides/contenus/textures/) |
| `bake.hpp` | `BakeOptions`, `BakedPrimitive` | [Cuire les maillages](/guides/contenus/cuisson/) |

## anim

| En-tête | Types principaux | Page |
| --- | --- | --- |
| `skinning.hpp`, `pose.hpp` | `SkinnedModel`, `Pose`, `Transform` | [Animer un personnage](/guides/animation/squelettes/) |
| `clip.hpp`, `state_machine.hpp` | `Clip`, `StateMachine`, `AnimState`, `AnimTransition` | [Animer un personnage](/guides/animation/squelettes/) |
| `curves.hpp` | `Curve<T>`, `PropertyAnimation` | [Animer des propriétés](/guides/animation/proprietes/) |
| `crowd.hpp` | `CrowdRenderer`, `CrowdOptions` | [Foules instanciées](/guides/animation/foules/) |
| `animated_model.hpp` | `AnimatedModel`, `AnimatedModelOptions`, `PlayOptions` | [Animer un personnage](/guides/animation/squelettes/#en-cinq-lignes--animatedmodel) |

## ui

| En-tête | Types principaux | Page |
| --- | --- | --- |
| `ui.hpp`, `widget.hpp` | `Ui`, `Widget`, `ActionRegistry`, `Event` | [Construire une interface](/guides/interface/premiere-interface/) |
| `widgets.hpp`, `controls.hpp`, `color.hpp`, `menus.hpp` | `Button`, `Label`, `Checkbox`, `Dropdown`, `SpinBox`, `ColorEditor`, `Dialog`, `MenuBar` | [Catalogue des widgets](/guides/interface/widgets/) |
| `layout.hpp` | `VBox`, `HBox`, `Grid`, `Panel`, `ScrollView` | [Catalogue des widgets](/guides/interface/widgets/) |
| `reflect.hpp` | panneau automatique d'une structure réfléchie | [Catalogue des widgets](/guides/interface/widgets/) |
| `theme.hpp`, `nine_slice.hpp` | `Theme`, `ThemeHost`, `Style` | [Thèmes JSON](/guides/interface/themes/) |
| `font.hpp`, `text_renderer.hpp` | `Font`, `TextRenderer`, `TextRendererFont`, `FixedFont` | [Texte et polices](/guides/rendu-2d/texte/) |
| `window_input.hpp` | `readWindowInput`, `useSystemClipboard` | [Construire une interface](/guides/interface/premiere-interface/) |
| `gamepad_navigation.hpp` | `GamepadNavigator`, `GamepadNavigationSettings` | [Naviguer au clavier et à la manette](/guides/interface/navigation/) |
| `debug_tools.hpp`, `debug_console.hpp`, `frame_history.hpp` | `debug::DebugTools`, `debug::Console`, `FrameHistory` | [Outils de debug](/guides/interface/outils-debug/) |
| `view_entry.hpp`, `view_state.hpp` | `CuiViewEntry`, `ViewState`, `replaceRoot` (rechargement des vues) | [Vues rechargées à chaud](/guides/interface/rechargement-vues/) |

## scene

| En-tête | Types et fonctions principaux | Page |
| --- | --- | --- |
| `world.hpp`, `entity.hpp` | `World`, `View`, `Entity`, `kNullEntity` | [Entités et composants](/guides/scene/entites-composants/) |
| `component.hpp`, `component_pool.hpp` | `ComponentDesc`, `ComponentId`, `ComponentType<T>`, `describeComponent<T>`, `ComponentPool` | [Entités et composants](/guides/scene/entites-composants/) |
| `transform.hpp` | `Transform`, `relativeMatrix`, `worldPosition` | [Entités et composants](/guides/scene/entites-composants/#transformations) |
| `scene_file.hpp`, `fingerprint.hpp` | `saveScene`, `loadScene`, `saveSceneFile`, `loadSceneFile`, `SceneMigrations`, `fingerprint` | [Sauvegarder une scène](/guides/scene/sauvegarde/) |
| `component_migration.hpp` | `migrateComponentValue`, `classifyMigration`, `MigrationKind` | [Logique de jeu rechargeable](/guides/scene/logique-rechargeable/) |

## sim

| En-tête | Types et fonctions principaux | Page |
| --- | --- | --- |
| `simulation.hpp`, `system.hpp` | `Simulation`, `SimulationSettings`, `SystemDesc`, `StepContext`, `Phase`, `Input`, `Event`, `Snapshot`, `Replay` | [Simulation à pas fixe](/guides/scene/simulation/) |
| `simulation_thread.hpp`, `snapshot_buffer.hpp` | `SimulationThread`, `ThreadSettings`, `Cadence`, `RenderSnapshot`, `SnapshotBuffer<T>` | [Simulation à pas fixe](/guides/scene/simulation/#thread-de-simulation) |

## app

| En-tête | Types et fonctions principaux | Page |
| --- | --- | --- |
| `application.hpp` | `Application`, `ApplicationSettings`, `FrameTime`, `RenderFrame`, `GameSource` | [La façade app::Application](/guides/application/facade/) |
| `render_components.hpp`, `scene_render_system.hpp`, `resource_pool.hpp` | `MeshRenderer`, `LightComponent`, `CameraComponent`, `AnimatedModelComponent`, `DrawMode`, `SceneRenderSystem`, `ResourcePool<T>` | [La façade app::Application](/guides/application/facade/#entités-et-composants-de-rendu) |
| `default_game.hpp` | `defaultGame`, `defaultViews`, `defaultSettings` (un seul fichier du programme l'inclut) | [La façade app::Application](/guides/application/facade/) |
| `game_api.hpp`, `game_context.hpp`, `game_module.hpp` | `OrosmoGameApi`, `OROSMO_GAME_EXPORT`, `GameContext`, `GameModule` | [Logique de jeu rechargeable](/guides/scene/logique-rechargeable/) |
| `params_host.hpp` | `ParamsFile`, `ParamsHost<T>`, `Params<T>` | [Logique de jeu rechargeable](/guides/scene/logique-rechargeable/#paramètres-json) |
| `view_host.hpp` | `ViewHost`, `ViewHostSettings` | [Vues rechargées à chaud](/guides/interface/rechargement-vues/) |

Le détail de chaque module est dans son `README.md` : `src/core/README.md`, `src/platform/README.md`, `src/rhi/README.md`,
`src/renderer/README.md`, `src/assets/README.md`, `src/anim/README.md`, `src/ui/README.md`, `src/scene/README.md`,
`src/sim/README.md`, `src/app/README.md`.
