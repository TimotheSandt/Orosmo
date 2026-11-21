// REN-01 (graphe de passes, plugins de rendu) et REN-03 (données par image) : ordre déduit, passes inutiles écartées, ressources
// transitoires créées puis réutilisées (et partagées entre vies disjointes), cycle refusé, plugin qui ajoute une passe, profilage
// GPU par passe, bloc FrameData lu par un shader (disposition C++/GLSL et valeurs de SceneRenderer). Sur chaque backend.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"
#include "support/test_support.hpp"

#include <orosmo/core/profiler.hpp>
#include <orosmo/renderer/frame_data.hpp>
#include <orosmo/renderer/fullscreen_blit.hpp>
#include <orosmo/renderer/gpu_timer.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/pipeline_cache.hpp>
#include <orosmo/renderer/render_feature.hpp>
#include <orosmo/renderer/render_graph.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/shader_library.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace rd = orosmo::renderer;
namespace core = orosmo::core;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

bool contains(const std::vector<std::string> &names, const std::string &name) { return std::find(names.begin(), names.end(), name) != names.end(); }

std::ptrdiff_t position(const std::vector<std::string> &names, const std::string &name)
{
    return std::distance(names.begin(), std::find(names.begin(), names.end(), name));
}

// Périphérique, bibliothèque de shaders (avec shaders/rhi pour les #include) et cache de pipelines.
struct GraphGpu
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::FullscreenBlit> blit;

    explicit GraphGpu(Backend backend) : gpu(backend, {16, 16})
    {
        if (!gpu.device)
        {
            return;
        }
        library = std::make_unique<rd::ShaderLibrary>(*gpu.device, rd::ShaderLibraryConfig{.cacheDirectory = {}, .includeRoots = {shaderDir()}});
        pipelines = std::make_unique<rd::PipelineCache>(*gpu.device);
        auto created = rd::FullscreenBlit::create(*gpu.device, *library, *pipelines, shaderDir());
        INFO((created ? std::string() : created.error().message));
        REQUIRE(created.has_value());
        blit = std::move(*created);
    }
    ~GraphGpu()
    {
        blit.reset();
        pipelines.reset();
        library.reset();
    }
};

// Passe générique qui ne fait que noter son exécution.
rd::PassExecute note(std::vector<std::string> &log, std::string name)
{
    return [&log, name](rd::RenderPassContext &) -> core::Result<>
    {
        log.push_back(name);
        return {};
    };
}

// Triangle plein écran qui lit le bloc FrameData (frame_data.glsl, groupe 0 binding 2).
constexpr const char *kFrameVs = R"(#version 450
void main() { vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); })";

// Chaque champ k de FrameData vaut, composante i (ordre mémoire, colonne par colonne pour une matrice), k * 100 + i : vérifie la
// première, une intermédiaire et la dernière composante de chaque champ. Vert si tout correspond, sinon rouge avec le champ fautif.
constexpr const char *kLayoutFs = R"(#version 450
#include "frame_data.glsl"
layout(location = 0) out vec4 color;
int bad = -1;
void checkMat(mat4 m, int k) { float b = float(k) * 100.0; if (bad < 0 && (m[0][0] != b || m[1][2] != b + 6.0 || m[3][3] != b + 15.0)) bad = k; }
void checkVec(vec4 v, int k) { float b = float(k) * 100.0; if (bad < 0 && (v.x != b || v.z != b + 2.0 || v.w != b + 3.0)) bad = k; }
void main()
{
    checkMat(frame.view, 0); checkMat(frame.proj, 1); checkMat(frame.viewProj, 2); checkMat(frame.invView, 3);
    checkMat(frame.invProj, 4); checkMat(frame.invViewProj, 5); checkMat(frame.prevViewProj, 6);
    checkVec(frame.cameraRelativeOrigin, 7); checkVec(frame.cameraWorldHigh, 8); checkVec(frame.cameraWorldLow, 9);
    checkVec(frame.jitter, 10); checkVec(frame.viewport, 11); checkVec(frame.time, 12); checkVec(frame.sunDirection, 13);
    checkVec(frame.sunColor, 14); checkVec(frame.planet, 15); checkVec(frame.scatteringRayleigh, 16); checkVec(frame.scatteringMie, 17);
    checkVec(frame.exposure, 18);
    color = bad < 0 ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, float(bad) / 32.0, 0.0, 1.0);
})";

// Valeurs de SceneRenderer : largeur de rendu / 16, delta de temps, couleur de la lumière principale.
constexpr const char *kValuesFs = R"(#version 450
#include "frame_data.glsl"
layout(location = 0) out vec4 color;
void main() { color = vec4(frame.viewport.x / 16.0, frame.time.y, frame.sunColor.x, frame.sunDirection.w); })";

// Pipeline plein écran d'un programme, une cible du format donné.
PipelineHandle fullscreenPipeline(GraphGpu &g, const std::shared_ptr<rd::ShaderProgram> &program, Format format)
{
    rd::GraphicsState state;
    state.raster.cull = CullMode::kNone;
    state.colorTargets = {ColorTargetState{.format = format}};
    state.debugName = program->name();
    auto pipeline = g.pipelines->graphics(*program, state);
    INFO((pipeline ? std::string() : pipeline.error().message));
    REQUIRE(pipeline.has_value());
    return *pipeline;
}

} // namespace

TEST_SUITE("renderer/render_graph")
{
    TEST_CASE("RenderGraph (REN-01): the order is deduced from reads and writes, not from declaration; an unused pass is culled")
    {
        FOR_EACH_BACKEND(backend)
        {
            GraphGpu g(backend);
            REQUIRE(g.gpu.device);
            const TextureHandle out = g.gpu.target(Format::kRGBA8Unorm, {4, 4}, "out");
            rd::RenderGraph graph(*g.gpu.device);
            std::vector<std::string> log;
            const rd::RgTexture output = graph.importTexture("output", out, {.size = {4, 4}});
            const rd::RgTexture a = graph.createTexture("a", {.size = {4, 4}});
            const rd::RgTexture b = graph.createTexture("b", {.size = {4, 4}});
            const rd::RgTexture unused = graph.createTexture("unused", {.size = {4, 4}});
            // Déclarées à l'envers : present lit b, make-b lit a, make-a n'attend rien.
            graph.addPass("present", rd::PassType::kGeneric, note(log, "present")).read(b).write(output);
            graph.addPass("make-b", rd::PassType::kGeneric, note(log, "make-b")).read(a).write(b);
            graph.addPass("orphan", rd::PassType::kGeneric, note(log, "orphan")).read(a).write(unused); // personne ne lit `unused`
            graph.addPass("make-a", rd::PassType::kRaster, note(log, "make-a")).writeColor(a, LoadOp::kClear, {1, 0, 0, 1});
            REQUIRE(graph.compile().has_value());
            CHECK(graph.executionOrder() == std::vector<std::string>{"make-a", "make-b", "present"});
            CHECK(graph.culledPasses() == std::vector<std::string>{"orphan"});
            g.gpu.run([&](CommandEncoder &e) { REQUIRE(graph.execute(e).has_value()); });
            CHECK(log == std::vector<std::string>{"make-a", "make-b", "present"}); // la passe écartée n'est pas exécutée
            CHECK(graph.physicalTexture(unused).isNull());                          // ni sa ressource allouée
            CHECK_FALSE(graph.physicalTexture(a).isNull());

            // Plusieurs écrivains d'une ressource : ordre de déclaration ; un lecteur voit le contenu final (après le dernier).
            graph.reset();
            log.clear();
            const rd::RgTexture out2 = graph.importTexture("output", out, {.size = {4, 4}});
            graph.addPass("read-final", rd::PassType::kGeneric, note(log, "read-final")).read(out2).sideEffect();
            graph.addPass("write-1", rd::PassType::kRaster, note(log, "write-1")).writeColor(out2);
            graph.addPass("write-2", rd::PassType::kRaster, note(log, "write-2")).writeColor(out2, LoadOp::kLoad);
            REQUIRE(graph.compile().has_value());
            CHECK(graph.executionOrder() == std::vector<std::string>{"write-1", "write-2", "read-final"});
            g.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("RenderGraph (REN-01): read sees the version declared before it, readFinal the final one; a writer waits for earlier readers")
    {
        FOR_EACH_BACKEND(backend)
        {
            GraphGpu g(backend);
            REQUIRE(g.gpu.device);
            const TextureHandle out = g.gpu.target(Format::kRGBA8Unorm, {4, 4}, "out");
            const TextureHandle resultTexture = g.gpu.target(Format::kRGBA8Unorm, {4, 4}, "result");
            rd::RenderGraph graph(*g.gpu.device);
            std::vector<std::string> log;

            // Chaîne de post-traitement : extraire la couleur dans une transitoire puis la réécrire. La consommatrice (readFinal),
            // déclarée AVANT la chaîne, la voit quand même finie ; la chaîne ne forme pas de cycle (extract lit la version de src).
            const rd::RgTexture color = graph.importTexture("color", out, {.size = {4, 4}});
            const rd::RgTexture result = graph.importTexture("result", resultTexture, {.size = {4, 4}});
            const rd::RgTexture tmp = graph.createTexture("tmp", {.size = {4, 4}});
            const rd::RgTexture dead = graph.createTexture("dead", {.size = {4, 4}});
            graph.addPass("src", rd::PassType::kRaster, note(log, "src")).writeColor(color);
            graph.addPass("consume", rd::PassType::kGeneric, note(log, "consume")).readFinal(color).write(result);
            graph.addPass("extract", rd::PassType::kGeneric, note(log, "extract")).read(color).write(tmp);
            graph.addPass("unused-peek", rd::PassType::kGeneric, note(log, "unused-peek")).read(color).write(dead); // écartée
            graph.addPass("apply", rd::PassType::kGeneric, note(log, "apply")).read(tmp).write(color);
            REQUIRE(graph.compile().has_value());
            CHECK(graph.executionOrder() == std::vector<std::string>{"src", "extract", "apply", "consume"});
            // L'écriture après lecture ordonne sans garder : un lecteur inutile reste écarté.
            CHECK(graph.culledPasses() == std::vector<std::string>{"unused-peek"});

            // Écriture après lecture : `overwrite` ne dépend que de src, mais `reader` lit la version de src ; reader attend
            // make-b (déclarée après lui) : sans l'arête lecture -> écriture, overwrite passerait avant reader.
            graph.reset();
            const rd::RgTexture a = graph.importTexture("color", out, {.size = {4, 4}});
            const rd::RgTexture b = graph.createTexture("b", {.size = {4, 4}});
            graph.addPass("src", rd::PassType::kRaster, note(log, "src")).writeColor(a);
            graph.addPass("reader", rd::PassType::kGeneric, note(log, "reader")).read(a).read(b).sideEffect();
            graph.addPass("overwrite", rd::PassType::kRaster, note(log, "overwrite")).writeColor(a, LoadOp::kLoad);
            graph.addPass("make-b", rd::PassType::kRaster, note(log, "make-b")).writeColor(b);
            REQUIRE(graph.compile().has_value());
            CHECK(graph.executionOrder() == std::vector<std::string>{"src", "make-b", "reader", "overwrite"});
            g.gpu.run([&](CommandEncoder &e) { REQUIRE(graph.execute(e).has_value()); });
            g.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("RenderGraph (REN-01): a cycle is refused with a clear error; invalid declarations are refused; a failing pass stops the frame")
    {
        FOR_EACH_BACKEND(backend)
        {
            GraphGpu g(backend);
            REQUIRE(g.gpu.device);
            rd::RenderGraph graph(*g.gpu.device);
            std::vector<std::string> log;
            const rd::RgTexture x = graph.createTexture("x", {.size = {4, 4}});
            const rd::RgTexture y = graph.createTexture("y", {.size = {4, 4}});
            graph.addPass("first", rd::PassType::kGeneric, note(log, "first")).read(x).write(y).sideEffect();
            graph.addPass("second", rd::PassType::kGeneric, note(log, "second")).read(y).write(x);
            auto compiled = graph.compile();
            REQUIRE_FALSE(compiled.has_value());
            CHECK((tests::errorOf(compiled) == ErrorCode::kInvalidArgument));
            const std::string message = compiled.error().message;
            INFO(message);
            CHECK(message.find("cycle") != std::string::npos);
            CHECK(message.find("'first'") != std::string::npos);
            CHECK(message.find("'second'") != std::string::npos);
            g.gpu.run([&](CommandEncoder &e) { CHECK_FALSE(graph.execute(e).has_value()); });
            CHECK(log.empty()); // rien n'est exécuté

            // Déclarations invalides.
            graph.reset();
            graph.addPass("same", rd::PassType::kGeneric, note(log, "same")).sideEffect();
            graph.addPass("same", rd::PassType::kGeneric, note(log, "same")).sideEffect();
            CHECK((tests::errorOf(graph.compile()) == ErrorCode::kAlreadyExists));
            graph.reset();
            const rd::RgTexture small = graph.createTexture("small", {.size = {2, 2}});
            const rd::RgTexture big = graph.createTexture("big", {.size = {4, 4}});
            graph.addPass("mixed", rd::PassType::kRaster, note(log, "mixed")).writeColor(small).writeColor(big).sideEffect();
            CHECK((tests::errorOf(graph.compile()) == ErrorCode::kInvalidArgument)); // attachements de tailles différentes
            graph.reset();
            graph.addPass("no-attachment", rd::PassType::kRaster, note(log, "no-attachment")).sideEffect();
            CHECK((tests::errorOf(graph.compile()) == ErrorCode::kInvalidArgument));
            graph.reset();
            graph.addPass("bad-handle", rd::PassType::kGeneric, note(log, "bad-handle")).read(rd::RgTexture{}).sideEffect();
            CHECK((tests::errorOf(graph.compile()) == ErrorCode::kInvalidArgument));

            // Une passe en erreur arrête l'image : la passe de rendu ouverte par le graphe est refermée, la suivante ne tourne pas.
            graph.reset();
            log.clear();
            const TextureHandle out = g.gpu.target(Format::kRGBA8Unorm, {4, 4}, "out");
            const rd::RgTexture output = graph.importTexture("output", out, {.size = {4, 4}});
            graph.addPass("fails", rd::PassType::kRaster, [](rd::RenderPassContext &) -> core::Result<> { return core::makeError(ErrorCode::kUnknown, "boom"); })
                .writeColor(output);
            graph.addPass("after", rd::PassType::kGeneric, note(log, "after")).read(output).sideEffect();
            g.gpu.run(
                [&](CommandEncoder &e)
                {
                    auto r = graph.execute(e);
                    REQUIRE_FALSE(r.has_value());
                    CHECK(r.error().message.find("pass 'fails': boom") != std::string::npos);
                });
            CHECK(log.empty());
            g.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("RenderGraph (REN-01): transient textures are created once, reused by the next frame, and shared by disjoint lifetimes")
    {
        FOR_EACH_BACKEND(backend)
        {
            GraphGpu g(backend);
            REQUIRE(g.gpu.device);
            const TextureHandle outRed = g.gpu.target(Format::kRGBA8Unorm, {4, 4}, "out.red");
            const TextureHandle outGreen = g.gpu.target(Format::kRGBA8Unorm, {4, 4}, "out.green");
            rd::RenderGraph graph(*g.gpu.device);
            rd::RgTexture first;
            rd::RgTexture second;
            auto declare = [&]()
            {
                graph.reset();
                const rd::RgTexture red = graph.importTexture("out.red", outRed, {.size = {4, 4}});
                const rd::RgTexture green = graph.importTexture("out.green", outGreen, {.size = {4, 4}});
                first = graph.createTexture("first", {.size = {4, 4}});
                second = graph.createTexture("second", {.size = {4, 4}});
                // Deux chaînes indépendantes : `first` ne sert plus quand `second` naît (même description) : une seule texture.
                graph.addPass("clear-first", rd::PassType::kRaster, [](rd::RenderPassContext &) -> core::Result<> { return {}; })
                    .writeColor(first, LoadOp::kClear, {1, 0, 0, 1});
                graph
                    .addPass("copy-first", rd::PassType::kGeneric,
                             [&, first = first, red](rd::RenderPassContext &ctx)
                             { return g.blit->copy(ctx.encoder(), ctx.texture(first), g.blit->nearestSampler(), ctx.texture(red), Format::kRGBA8Unorm); })
                    .read(first)
                    .write(red);
                graph.addPass("clear-second", rd::PassType::kRaster, [](rd::RenderPassContext &) -> core::Result<> { return {}; })
                    .writeColor(second, LoadOp::kClear, {0, 1, 0, 1});
                graph
                    .addPass("copy-second", rd::PassType::kGeneric,
                             [&, second = second, green](rd::RenderPassContext &ctx)
                             { return g.blit->copy(ctx.encoder(), ctx.texture(second), g.blit->nearestSampler(), ctx.texture(green), Format::kRGBA8Unorm); })
                    .read(second)
                    .write(green);
            };
            declare();
            g.gpu.run([&](CommandEncoder &e) { REQUIRE(graph.execute(e).has_value()); });
            CHECK(graph.transientTexturesCreated() == 1);
            CHECK(graph.physicalTexture(first) == graph.physicalTexture(second)); // alias : vies disjointes
            const TextureHandle physical = graph.physicalTexture(first);
            CHECK(rgba(g.gpu.pixel(outRed, 1, 1)) == std::array<std::uint8_t, 4>{255, 0, 0, 255}); // le partage n'a rien mélangé
            CHECK(rgba(g.gpu.pixel(outGreen, 1, 1)) == std::array<std::uint8_t, 4>{0, 255, 0, 255});

            declare(); // image suivante : la réserve sert, rien n'est créé
            g.gpu.run([&](CommandEncoder &e) { REQUIRE(graph.execute(e).has_value()); });
            CHECK(graph.transientTexturesCreated() == 1);
            CHECK(graph.physicalTexture(first) == physical);
            CHECK(graph.pooledTextureCount() == 1);

            // Une autre taille : une nouvelle texture ; l'ancienne, inutilisée, est libérée après kTransientKeepFrames images.
            for (std::uint32_t frame = 0; frame <= rd::RenderGraph::kTransientKeepFrames; ++frame)
            {
                graph.reset();
                const rd::RgTexture red = graph.importTexture("out.red", outRed, {.size = {4, 4}});
                const rd::RgTexture other = graph.createTexture("other", {.size = {2, 2}});
                graph.addPass("clear", rd::PassType::kRaster, [](rd::RenderPassContext &) -> core::Result<> { return {}; }).writeColor(other);
                graph.addPass("use", rd::PassType::kGeneric, [](rd::RenderPassContext &) -> core::Result<> { return {}; }).read(other).write(red);
                g.gpu.run([&](CommandEncoder &e) { REQUIRE(graph.execute(e).has_value()); });
            }
            CHECK(graph.transientTexturesCreated() == 2);
            CHECK(graph.pooledTextureCount() == 1);

            // Tampons transitoires : même réserve.
            graph.reset();
            const rd::RgBuffer scratch = graph.createBuffer("scratch", {.size = 256, .usage = BufferUsage::kStorage});
            graph.addPass("fill", rd::PassType::kGeneric, [](rd::RenderPassContext &) -> core::Result<> { return {}; }).write(scratch);
            graph.addPass("consume", rd::PassType::kGeneric, [](rd::RenderPassContext &) -> core::Result<> { return {}; }).read(scratch).sideEffect();
            g.gpu.run([&](CommandEncoder &e) { REQUIRE(graph.execute(e).has_value()); });
            CHECK(graph.transientBuffersCreated() == 1);
            CHECK_FALSE(graph.physicalBuffer(scratch).isNull());
            graph.releaseTransients();
            CHECK(graph.pooledTextureCount() == 0);
            CHECK(graph.pooledBufferCount() == 0);
            g.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("RenderGraph (REN-01): every executed pass is timed by the Profiler with a GPU timer; a culled pass is not")
    {
        FOR_EACH_BACKEND(backend)
        {
            GraphGpu g(backend);
            REQUIRE(g.gpu.device);
            if (!g.gpu->capabilities().timestampQueries)
            {
                MESSAGE("no timestamp queries on this device: CPU timing only");
            }
            // Fournisseur qui note les minuteurs demandés et délègue au fournisseur du renderer (horodatages dans l'encodeur courant).
            struct Observed final : core::IGpuTimer
            {
                std::unique_ptr<core::IGpuTimer> inner;
                bool *readySeen;
                Observed(std::unique_ptr<core::IGpuTimer> timer, bool *seen) : inner(std::move(timer)), readySeen(seen) {}
                void Begin() override { inner->Begin(); }
                void End() override { inner->End(); }
                bool IsReady() const override
                {
                    const bool ready = inner->IsReady();
                    *readySeen = *readySeen || ready;
                    return ready;
                }
                std::chrono::nanoseconds GetElapsedTime() const override { return inner->GetElapsedTime(); }
            };
            struct Recording final : core::IGpuTimerFactory
            {
                rd::ProfilerGpuTimerFactory &inner;
                mutable std::vector<std::string> names;
                mutable bool readySeen = false;
                explicit Recording(rd::ProfilerGpuTimerFactory &f) : inner(f) {}
                std::unique_ptr<core::IGpuTimer> CreateTimer(const std::string &name) const override
                {
                    names.push_back(name);
                    return std::make_unique<Observed>(inner.CreateTimer(name), &readySeen);
                }
            };
            rd::ProfilerGpuTimerFactory timers(*g.gpu.device);
            Recording recording(timers);
            core::Profiler::SetGpuTimerFactory(&recording);

            const TextureHandle out = g.gpu.target(Format::kRGBA8Unorm, {16, 16}, "out");
            rd::RenderGraph graph(*g.gpu.device);
            graph.setProfilePrefix("test.graph/");
            for (int frame = 0; frame < 3; ++frame)
            {
                graph.reset();
                const rd::RgTexture output = graph.importTexture("output", out, {.size = {16, 16}});
                const rd::RgTexture dead = graph.createTexture("dead", {.size = {16, 16}});
                graph.addPass("clear", rd::PassType::kRaster, [](rd::RenderPassContext &) -> core::Result<> { return {}; })
                    .writeColor(output, LoadOp::kClear, {0, 0, 1, 1});
                graph.addPass("dead", rd::PassType::kRaster, [](rd::RenderPassContext &) -> core::Result<> { return {}; }).writeColor(dead);
                auto encoder = g.gpu->createCommandEncoder();
                timers.setEncoder(encoder.get());
                REQUIRE(graph.execute(*encoder).has_value());
                timers.setEncoder(nullptr);
                REQUIRE(g.gpu->submit(std::move(encoder)).has_value());
                REQUIRE(g.gpu->present().has_value());
            }
            g.gpu->waitIdle();
            CHECK(std::count(recording.names.begin(), recording.names.end(), "test.graph/clear") == 3);
            CHECK_FALSE(contains(recording.names, "test.graph/dead"));
            core::Profiler::Process(); // relève les minuteries terminées
            if (g.gpu->capabilities().timestampQueries)
            {
                CHECK(recording.readySeen); // au moins une mesure GPU de passe terminée et relevée par le Profiler
            }
            core::Profiler::ClearGpuQueries();
            core::Profiler::SetGpuTimerFactory(nullptr);
            g.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("FrameData (REN-03): the C++ block and frame_data.glsl share one std140 layout, read by a shader through the graph")
    {
        FOR_EACH_BACKEND(backend)
        {
            GraphGpu g(backend);
            REQUIRE(g.gpu.device);
            std::array<float, sizeof(rd::FrameData) / sizeof(float)> floats{};
            const std::size_t fieldFloats[19] = {16, 16, 16, 16, 16, 16, 16, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
            std::size_t at = 0;
            for (std::size_t k = 0; k < 19; ++k)
            {
                for (std::size_t i = 0; i < fieldFloats[k]; ++i)
                {
                    floats[at++] = static_cast<float>(k * 100 + i);
                }
            }
            REQUIRE(at == floats.size());
            rd::FrameData data;
            std::memcpy(static_cast<void *>(&data), floats.data(), sizeof data); // trivialement copiable : même disposition, sans aliasing
            CHECK(data.time.x == 1200.0f);                  // champ 12, première composante : la disposition C++ suit le GLSL
            const BufferHandle buffer = g.gpu.buffer(BufferUsage::kUniform, bytesOf(data), 0, "frame");
            BindGroupDesc groupDesc;
            groupDesc.entries.push_back({.binding = rd::kFrameDataBinding, .type = BindingType::kUniformBuffer, .buffer = buffer, .size = sizeof(rd::FrameData)});
            auto group = g.gpu->createBindGroup(groupDesc);
            REQUIRE(group.has_value());
            rd::FrameResources resources{.group = *group, .buffer = buffer, .data = &data};

            auto program = g.library->graphicsFromSource("frame.layout", kFrameVs, kLayoutFs);
            INFO((program ? std::string() : program.error().message));
            REQUIRE(program.has_value());
            const PipelineHandle pipeline = fullscreenPipeline(g, *program, Format::kRGBA8Unorm);

            const TextureHandle out = g.gpu.target(Format::kRGBA8Unorm, {4, 4}, "out");
            rd::RenderGraph graph(*g.gpu.device);
            graph.setFrameResources(&resources);
            const rd::RgTexture output = graph.importTexture("output", out, {.size = {4, 4}});
            graph
                .addPass("read-frame", rd::PassType::kRaster,
                         [pipeline](rd::RenderPassContext &ctx) -> core::Result<>
                         {
                             REQUIRE(ctx.frame() != nullptr);
                             ctx.encoder().setPipeline(pipeline);
                             ctx.encoder().setBindGroup(0, ctx.frame()->group);
                             ctx.encoder().draw(3);
                             return {};
                         })
                .writeColor(output, LoadOp::kClear, {0, 0, 0, 0});
            g.gpu.run([&](CommandEncoder &e) { REQUIRE(graph.execute(e).has_value()); });
            const auto px = rgba(g.gpu.pixel(out, 2, 2));
            INFO("first mismatching field: ", static_cast<int>(px[1]) * 32 / 255);
            CHECK(px == std::array<std::uint8_t, 4>{0, 255, 0, 255});
            g.gpu->destroy(*group);
            g.gpu->destroy(buffer);
            g.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("FrameData (REN-03): pure helpers are deterministic")
    {
        const rd::SplitDouble split = rd::splitDouble(1.0e7 + 0.123456789);
        CHECK(static_cast<double>(split.high) + static_cast<double>(split.low) == doctest::Approx(1.0e7 + 0.123456789).epsilon(1e-12));
        CHECK(split.low != 0.0f); // la partie perdue par le float haut est gardée
        CHECK(rd::frameNoiseSeed(42) == rd::frameNoiseSeed(42));
        CHECK(rd::frameNoiseSeed(1) != rd::frameNoiseSeed(2));
        for (std::uint64_t i = 0; i < 64; ++i)
        {
            const float seed = rd::frameNoiseSeed(i);
            CHECK((seed >= 0.0f && seed < 1.0f));
        }
    }

    TEST_CASE("SceneRenderer (REN-01, REN-03): a feature adds passes without touching the core; its shader reads the per-frame data")
    {
        FOR_EACH_BACKEND(backend)
        {
            GraphGpu g(backend);
            REQUIRE(g.gpu.device);
            auto created = rd::SceneRenderer::create(*g.gpu.device, *g.library, *g.pipelines, {.shaderDirectory = shaderDir()});
            INFO((created ? std::string() : created.error().message));
            REQUIRE(created.has_value());
            std::unique_ptr<rd::SceneRenderer> renderer = std::move(*created);

            auto program = g.library->graphicsFromSource("frame.values", kFrameVs, kValuesFs);
            INFO((program ? std::string() : program.error().message));
            REQUIRE(program.has_value());
            const PipelineHandle pipeline = fullscreenPipeline(g, *program, Format::kRGBA8Unorm);

            // Plugin : une passe « ombre » lue par la scène (exécutée avant elle), une passe orpheline (écartée), et une
            // superposition qui écrit la sortie après la mise à l'échelle avec un shader qui lit FrameData.
            struct Overlay final : rd::RenderFeature
            {
                PipelineHandle pipeline;
                int prepared = 0;
                float preparedDelta = 0.0f;
                std::string name() const override { return "overlay"; }
                core::Result<> prepare(const rd::FrameData &frame) override
                {
                    ++prepared;
                    preparedDelta = frame.time.y;
                    return {};
                }
                core::Result<> declarePasses(rd::RenderGraphBuilder &builder) override
                {
                    rd::RenderGraph &graph = builder.graph();
                    const rd::RgTexture shadow = graph.createTexture("shadow", {.format = Format::kRGBA8Unorm, .size = {8, 8}});
                    graph.addPass("shadow", rd::PassType::kRaster, [](rd::RenderPassContext &) -> core::Result<> { return {}; }).writeColor(shadow);
                    builder.addSceneInput(shadow);
                    const rd::RgTexture orphan = graph.createTexture("orphan", {.size = {8, 8}});
                    graph.addPass("orphan", rd::PassType::kRaster, [](rd::RenderPassContext &) -> core::Result<> { return {}; }).writeColor(orphan);
                    // Post-traitement de la scène (REN-17/18) : lit la couleur, écrit une transitoire, la réécrit dans la scène ;
                    // déclaré après la mise à l'échelle, il s'exécute avant elle (elle lit le contenu final de la scène).
                    const rd::RgTexture scene = builder.resources().sceneColor;
                    const rd::RgTexture blurred = graph.createTexture("post.tmp", {.size = builder.resources().renderSize});
                    graph.addPass("post.extract", rd::PassType::kRaster, [](rd::RenderPassContext &) -> core::Result<> { return {}; })
                        .read(scene)
                        .writeColor(blurred);
                    graph.addPass("post.apply", rd::PassType::kGeneric, [](rd::RenderPassContext &) -> core::Result<> { return {}; })
                        .read(blurred)
                        .write(scene);
                    graph
                        .addPass("overlay", rd::PassType::kRaster,
                                 [pipeline = pipeline](rd::RenderPassContext &ctx) -> core::Result<>
                                 {
                                     ctx.encoder().setPipeline(pipeline);
                                     ctx.encoder().setBindGroup(0, ctx.frame()->group);
                                     ctx.encoder().draw(3);
                                     return {};
                                 })
                        .writeColor(builder.resources().output, LoadOp::kLoad);
                    return {};
                }
            };
            struct Unsupported final : rd::RenderFeature
            {
                std::string name() const override { return "needs-ray-query"; }
                bool supports(const orosmo::rhi::Capabilities &caps) const noexcept override { return caps.maxColorAttachments > 1000; }
                core::Result<> declarePasses(rd::RenderGraphBuilder &) override { return {}; }
            };
            CHECK((tests::errorOf(renderer->addFeature(std::make_unique<Unsupported>())) == ErrorCode::kUnsupported));
            auto overlay = std::make_unique<Overlay>();
            overlay->pipeline = pipeline;
            auto added = renderer->addFeature(std::move(overlay));
            REQUIRE(added.has_value());
            auto *feature = static_cast<Overlay *>(*added);
            CHECK((tests::errorOf(renderer->addFeature(std::make_unique<Overlay>())) == ErrorCode::kAlreadyExists));
            CHECK(renderer->featureNames() == std::vector<std::string>{"overlay"});

            auto lights = rd::LightManager::create(*g.gpu.device);
            REQUIRE(lights.has_value());
            lights->addLight(rd::directionalLight({0.0f, 2.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 0.5f));
            REQUIRE(lights->upload().has_value());
            renderer->setLights(&*lights);
            renderer->setOutputResolution(8, 8);
            renderer->setDeltaTime(0.25f);
            rd::Camera camera;
            camera.setPosition({1.0f, 2.0f, 3.0f});
            const TextureHandle out = g.gpu.target(Format::kRGBA8Unorm, {8, 8}, "output");
            auto frame = [&]()
            {
                g.gpu.run(
                    [&](CommandEncoder &e)
                    {
                        REQUIRE(renderer->beginFrame(e).has_value());
                        REQUIRE(renderer->setCamera(camera).has_value());
                        auto ended = renderer->endFrame({out, Format::kRGBA8Unorm, {8, 8}});
                        INFO((ended ? std::string() : ended.error().message));
                        REQUIRE(ended.has_value());
                    });
            };
            frame();
            const std::vector<std::string> order = renderer->renderGraph().executionOrder();
            INFO("order: ", order.size());
            CHECK(position(order, "shadow") < position(order, "scene"));
            CHECK(position(order, "upscale") < position(order, "overlay"));
            CHECK(position(order, "scene") < position(order, "post.extract"));
            CHECK(position(order, "post.extract") < position(order, "post.apply"));
            CHECK(position(order, "post.apply") < position(order, "upscale"));
            CHECK(renderer->renderGraph().culledPasses() == std::vector<std::string>{"orphan"});
            CHECK(feature->prepared == 1);
            CHECK(feature->preparedDelta == 0.25f);
            const auto px = rgba(g.gpu.pixel(out, 4, 4));
            CHECK(near(px[0], 128)); // largeur de rendu 8 / 16
            CHECK(near(px[1], 64));  // delta 0,25
            CHECK(near(px[2], 128)); // rouge × intensité 0,5
            CHECK(px[3] == 255);     // une lumière principale existe

            // Valeurs CPU : temps cumulé, numéro d'image, matrice de l'image précédente, position scindée.
            const rd::FrameData first = renderer->frameData();
            CHECK(first.time.x == doctest::Approx(0.25f));
            CHECK(first.time.z == 0.0f);
            CHECK(first.cameraWorldHigh.x == 1.0f);
            CHECK(first.cameraRelativeOrigin.z == 3.0f);
            CHECK(first.sunDirection.y == doctest::Approx(1.0f)); // normalisée
            CHECK(first.viewport == glm::vec4(8.0f, 8.0f, 0.125f, 0.125f));
            camera.setPosition({0.0f, 0.0f, 10.0f});
            frame();
            const rd::FrameData second = renderer->frameData();
            CHECK(second.time.x == doctest::Approx(0.5f));
            CHECK(second.time.z == 1.0f);
            CHECK(second.prevViewProj == first.viewProj);
            CHECK(second.viewProj != first.viewProj);
            CHECK(renderer->frameIndex() == 2);

            renderer->removeFeature("overlay");
            frame();
            CHECK_FALSE(contains(renderer->renderGraph().executionOrder(), "overlay"));
            g.gpu.expectNoValidationErrors();
        }
    }
}
