// RHI-12 : pipeline de shaders GLSL 450 -> SPIR-V -> GLSL 430, réflexion, inclusions, cache, rechargement à chaud.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "rhi/gpu_fixture.hpp"

#include <engine/rhi/shader_compiler.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{

constexpr const char *kVertex = R"(#version 450
layout(location = 0) out vec2 uv;
void main() { vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); })";

constexpr const char *kFragment = R"(#version 450
layout(set = 0, binding = 0, std140) uniform Frame { vec4 tint; } frame;
layout(set = 1, binding = 2) uniform sampler2D albedo;
layout(push_constant) uniform Push { vec4 scale; } pc;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(location = 1) out uint objectId;
void main() { color = texture(albedo, uv) * frame.tint * pc.scale; objectId = 3u; })";

constexpr const char *kCompute = R"(#version 450
layout(local_size_x = 8, local_size_y = 4, local_size_z = 2) in;
layout(set = 0, binding = 1, std430) buffer Out { uint data[]; };
layout(set = 2, binding = 0, rgba8) uniform image2D img;
void main() { data[gl_GlobalInvocationID.x] = 1u; })";

struct TempDir
{
    fs::path path;
    TempDir()
    {
        path = fs::temp_directory_path() / ("engine_shader_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    void write(const std::string &name, const std::string &text) const
    {
        fs::create_directories((path / name).parent_path());
        std::ofstream(path / name, std::ios::trunc | std::ios::binary) << text;
    }
};

} // namespace

TEST_SUITE("rhi/shader")
{
    TEST_CASE("compile and reflect: bindings by (set, binding), push constants, outputs, compute group size")
    {
        auto fs = compileGlsl(ShaderStage::kFragment, kFragment, {.name = "frag"});
        REQUIRE(fs.has_value());
        CHECK(fs->spirv.size() > 10);
        CHECK(fs->spirv[0] == 0x07230203u);
        REQUIRE(fs->reflection.bindings.size() == 2);
        CHECK(fs->reflection.bindings[0].set == 0);
        CHECK(fs->reflection.bindings[0].binding == 0);
        CHECK((fs->reflection.bindings[0].type == BindingType::kUniformBuffer));
        CHECK(fs->reflection.bindings[1].set == 1);
        CHECK(fs->reflection.bindings[1].binding == 2);
        CHECK((fs->reflection.bindings[1].type == BindingType::kSampledTexture));
        CHECK(fs->reflection.bindings[1].name == "albedo");
        CHECK(fs->reflection.pushConstantSize == 16);
        CHECK(fs->reflection.colorOutputs == 2);

        auto cs = compileGlsl(ShaderStage::kCompute, kCompute, {.name = "comp"});
        REQUIRE(cs.has_value());
        CHECK((cs->reflection.localSize == std::array<std::uint32_t, 3>{8, 4, 2}));
        REQUIRE(cs->reflection.bindings.size() == 2);
        CHECK((cs->reflection.bindings[0].type == BindingType::kStorageBuffer));
        CHECK((cs->reflection.bindings[1].type == BindingType::kStorageImage));
        CHECK(cs->reflection.bindings[1].set == 2);
    }

    TEST_CASE("translation to GLSL 430 flattens (set, binding) and turns push constants into a uniform block")
    {
        auto fs = compileGlsl(ShaderStage::kFragment, kFragment, {.name = "frag"});
        REQUIRE(fs.has_value());
        auto glsl = translateToGlsl430(*fs);
        REQUIRE(glsl.has_value());
        INFO(*glsl);
        CHECK(glsl->find("#version 430") != std::string::npos);
        CHECK(glsl->find("binding = 0") != std::string::npos);  // set 0, binding 0
        CHECK(glsl->find("binding = 10") != std::string::npos); // set 1, binding 2 -> 1 * 8 + 2
        CHECK(glsl->find("binding = 24") != std::string::npos); // push constants
        CHECK(glsl->find("set =") == std::string::npos);
        CHECK(glsl->find("push_constant") == std::string::npos);

        auto vs = compileGlsl(ShaderStage::kVertex, kVertex, {.name = "vert"});
        REQUIRE(vs.has_value());
        auto vglsl = translateToGlsl430(*vs);
        REQUIRE(vglsl.has_value());
        CHECK(vglsl->find("gl_VertexID") != std::string::npos); // gl_VertexIndex est la forme Vulkan
    }

    TEST_CASE("errors name the file and line; unsupported binding layouts are refused")
    {
        auto bad = compileGlsl(ShaderStage::kVertex, "#version 450\nvoid main() {\n  gl_Position = nonsense;\n}\n", {.name = "broken.vert"});
        REQUIRE_FALSE(bad.has_value());
        CHECK((bad.error().code == engine::core::ErrorCode::kParse));
        CHECK(bad.error().message.find("broken.vert:3") != std::string::npos);

        const char *outOfGroups = R"(#version 450
layout(set = 3, binding = 0, std140) uniform U { vec4 v; } u;
layout(location = 0) out vec4 c;
void main() { c = u.v; })";
        auto r1 = compileGlsl(ShaderStage::kFragment, outOfGroups, {.name = "groups"});
        REQUIRE_FALSE(r1.has_value());
        CHECK((r1.error().code == engine::core::ErrorCode::kInvalidArgument));
        CHECK(r1.error().message.find("set 3") != std::string::npos);

        const char *highBinding = R"(#version 450
layout(set = 0, binding = 8, std140) uniform U { vec4 v; } u;
layout(location = 0) out vec4 c;
void main() { c = u.v; })";
        CHECK_FALSE(compileGlsl(ShaderStage::kFragment, highBinding, {.name = "binding"}).has_value());

        const char *separate = R"(#version 450
layout(set = 0, binding = 0) uniform texture2D t;
layout(set = 0, binding = 1) uniform sampler s;
layout(location = 0) out vec4 c;
void main() { c = texture(sampler2D(t, s), vec2(0.5)); })";
        auto r2 = compileGlsl(ShaderStage::kFragment, separate, {.name = "separate"});
        REQUIRE_FALSE(r2.has_value());
        CHECK(r2.error().message.find("combined") != std::string::npos);
    }

    TEST_CASE("#include: relative and root lookups, nested files, macros, missing file")
    {
        TempDir dir;
        dir.write("lib/color.glsl", "#extension GL_GOOGLE_include_directive : enable\n#include \"common.glsl\"\nvec4 shade() { return vec4(TINT, 1.0); }\n");
        dir.write("lib/common.glsl", "const vec3 TINT = vec3(0.25, 0.5, 0.75);\n");
        const std::string main = "#version 450\n#extension GL_GOOGLE_include_directive : enable\n#include \"lib/color.glsl\"\n"
                                 "layout(location = 0) out vec4 c;\nvoid main() { c = shade(); }\n";
        CompileOptions options;
        options.name = (dir.path / "main.frag").generic_string();
        options.includeResolver = fileIncludeResolver({dir.path});
        auto ok = compileGlsl(ShaderStage::kFragment, main, options);
        INFO((ok ? std::string() : ok.error().message));
        REQUIRE(ok.has_value());
        CHECK(ok->includedFiles.size() == 2); // color.glsl (via la racine) et common.glsl (relatif à color.glsl)

        options.includeResolver = {};
        auto none = compileGlsl(ShaderStage::kFragment, main, options);
        REQUIRE_FALSE(none.has_value());
        CHECK(none.error().message.find("no include resolver") != std::string::npos);

        options.includeResolver = fileIncludeResolver({dir.path});
        const std::string absent = "#version 450\n#extension GL_GOOGLE_include_directive : enable\n#include \"absent.glsl\"\nvoid main() {}\n";
        auto missing = compileGlsl(ShaderStage::kFragment, absent, options);
        REQUIRE_FALSE(missing.has_value());
        CHECK(missing.error().message.find("include not found") != std::string::npos);

        CompileOptions macros;
        macros.name = "macro";
        macros.defines = {{"TINT_R", "0.5"}};
        const char *src = "#version 450\nlayout(location = 0) out vec4 c;\nvoid main() { c = vec4(TINT_R); }\n";
        CHECK(compileGlsl(ShaderStage::kFragment, src, macros).has_value());
        CHECK_FALSE(compileGlsl(ShaderStage::kFragment, src, {.name = "nomacro"}).has_value()); // TINT_R non défini
    }

    TEST_CASE("cache: memory and disk hits; a changed source, macro or include is a miss (RHI-06)")
    {
        TempDir dir;
        dir.write("inc.glsl", "const float K = 1.0;\n");
        const std::string src = "#version 450\n#extension GL_GOOGLE_include_directive : enable\n#include \"inc.glsl\"\n"
                                "layout(location = 0) out vec4 c;\nvoid main() { c = vec4(K);\n#ifdef FANCY\n c *= 2.0;\n#endif\n}\n";
        CompileOptions options;
        options.name = (dir.path / "a.frag").generic_string();
        options.includeResolver = fileIncludeResolver({dir.path});
        {
            ShaderCache cache(dir.path / "cache");
            REQUIRE(cache.compile(ShaderStage::kFragment, src, options).has_value());
            CHECK(cache.misses() == 1);
            REQUIRE(cache.compile(ShaderStage::kFragment, src, options).has_value());
            CHECK(cache.hits() == 1);
            CHECK(cache.diskHits() == 0);
        }
        ShaderCache fresh(dir.path / "cache"); // autre processus simulé : seul le disque survit
        auto fromDisk = fresh.compile(ShaderStage::kFragment, src, options);
        REQUIRE(fromDisk.has_value());
        CHECK(fresh.diskHits() == 1);
        CHECK(fresh.misses() == 0);
        CHECK(fromDisk->spirv[0] == 0x07230203u);

        dir.write("inc.glsl", "const float K = 0.5;\n"); // l'include change : autre clé
        REQUIRE(fresh.compile(ShaderStage::kFragment, src, options).has_value());
        CHECK(fresh.misses() == 1);
        options.defines = {{"FANCY", "1"}};                // une macro change : autre clé
        REQUIRE(fresh.compile(ShaderStage::kFragment, src, options).has_value());
        CHECK(fresh.misses() == 2);

        // Un fichier de cache corrompu est ignoré, puis recréé.
        for (const auto &entry : fs::directory_iterator(dir.path / "cache"))
        {
            std::ofstream(entry.path(), std::ios::trunc) << "not spirv";
        }
        ShaderCache again(dir.path / "cache");
        REQUIRE(again.compile(ShaderStage::kFragment, src, options).has_value());
        CHECK(again.diskHits() == 0);
        CHECK(again.misses() == 1);

        // Bon mot magique mais SPIR-V invalide (la réflexion échoue) : traité comme une absence, on recompile (revue CodeRabbit).
        for (const auto &entry : fs::directory_iterator(dir.path / "cache"))
        {
            const std::uint32_t garbage[] = {0x07230203u, 0xdeadbeefu, 0xdeadbeefu, 0u};
            std::ofstream(entry.path(), std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char *>(garbage), sizeof(garbage));
        }
        ShaderCache broken(dir.path / "cache");
        auto recovered = broken.compile(ShaderStage::kFragment, src, options);
        REQUIRE(recovered.has_value());
        CHECK(broken.diskHits() == 0);
        CHECK(broken.misses() == 1);
        // ... et la compilation suivante est servie sans erreur depuis la mémoire.
        CHECK(broken.compile(ShaderStage::kFragment, src, options).has_value());
        CHECK(broken.hits() == 1);
    }

    TEST_CASE("ShaderHotReloader is neither copyable nor movable (its watch callbacks capture this)")
    {
        static_assert(!std::is_copy_constructible_v<ShaderHotReloader>);
        static_assert(!std::is_move_constructible_v<ShaderHotReloader>);
        static_assert(!std::is_copy_assignable_v<ShaderHotReloader>);
        static_assert(!std::is_move_assignable_v<ShaderHotReloader>);
    }

    TEST_CASE("hot reload: a modified file or include is recompiled; a broken edit reports the error and keeps going")
    {
        TempDir dir;
        dir.write("inc.glsl", "const float K = 0.25;\n");
        const std::string head = "#version 450\n#extension GL_GOOGLE_include_directive : enable\n#include \"inc.glsl\"\nlayout(location = 0) out vec4 c;\n";
        dir.write("s.frag", head + "void main() { c = vec4(K); }\n");

        int reloads = 0;
        bool lastOk = false;
        std::size_t lastSize = 0;
        ShaderHotReloader reloader(fileIncludeResolver({dir.path}));
        auto first = reloader.add(dir.path / "s.frag", ShaderStage::kFragment,
                                  [&](const fs::path &, const engine::core::Result<CompiledShader> &r)
                                  {
                                      ++reloads;
                                      lastOk = r.has_value();
                                      lastSize = r ? r->spirv.size() : 0;
                                  });
        REQUIRE(first.has_value());
        CHECK(reloader.poll() == 0); // rien n'a changé

        dir.write("s.frag", head + "void main() { c = vec4(K) * vec4(K) + vec4(1.0); }\n");
        CHECK(reloader.poll() == 1);
        CHECK(reloads == 1);
        CHECK(lastOk);
        CHECK(lastSize != first->spirv.size());

        dir.write("s.frag", head + "void main() { c = nonsense; }\n"); // édition cassée
        CHECK(reloader.poll() == 1);
        CHECK(reloads == 2);
        CHECK_FALSE(lastOk);

        dir.write("s.frag", head + "void main() { c = vec4(K); }\n"); // corrigée
        CHECK(reloader.poll() == 1);
        CHECK(lastOk);
        CHECK(lastSize == first->spirv.size());

        dir.write("inc.glsl", "const float K = 0.75; // modifié\n"); // l'include est surveillé aussi
        CHECK(reloader.poll() == 1);
        CHECK(lastOk);
        CHECK(reloader.poll() == 0);
    }
}

TEST_SUITE("rhi/shader/opengl")
{
    TEST_CASE("a GLSL 450 shader pair renders on OpenGL: set/binding remapped, push constants and MRT working")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        ShaderCache cache;
        auto vs = createShaderFromGlsl(*gpu.device, ShaderStage::kVertex, kVertex, {.name = "vert"}, &cache);
        auto fs = createShaderFromGlsl(*gpu.device, ShaderStage::kFragment, kFragment, {.name = "frag"}, &cache);
        REQUIRE(vs.has_value());
        INFO((fs ? std::string() : fs.error().message));
        REQUIRE(fs.has_value());

        const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {4, 4}, "color");
        const TextureHandle ids = gpu.target(Format::kR32Uint, {4, 4}, "ids");
        const PipelineHandle pipe = gpu.pipeline(*vs, *fs, {ColorTargetState{.format = Format::kRGBA8Unorm}, ColorTargetState{.format = Format::kR32Uint}});

        const float tint[4] = {1.0f, 0.5f, 0.25f, 1.0f};
        const BufferHandle frame = gpu.buffer(BufferUsage::kUniform, std::as_bytes(std::span(tint)), 0, "frame");
        BindGroupDesc g0;
        g0.entries = {{.binding = 0, .type = BindingType::kUniformBuffer, .buffer = frame}};
        auto group0 = gpu->createBindGroup(g0);
        REQUIRE(group0.has_value());

        TextureDesc td;
        td.size = {1, 1, 1};
        td.usage = TextureUsage::kSampled | TextureUsage::kCopyDst;
        auto albedo = gpu->createTexture(td);
        REQUIRE(albedo.has_value());
        const std::uint8_t white[4] = {255, 255, 255, 255};
        REQUIRE(gpu->updateTexture(*albedo, {}, std::as_bytes(std::span(white))).has_value());
        auto sampler = gpu->createSampler({});
        REQUIRE(sampler.has_value());
        BindGroupDesc g1;
        g1.entries = {{.binding = 2, .type = BindingType::kSampledTexture, .texture = *albedo, .sampler = *sampler}};
        auto group1 = gpu->createBindGroup(g1);
        REQUIRE(group1.has_value());

        const float scale[4] = {0.5f, 1.0f, 1.0f, 1.0f};
        gpu.run(
            [&](CommandEncoder &e)
            {
                RenderPassDesc pass;
                pass.colors = {{.texture = color}, {.texture = ids}};
                e.beginRenderPass(pass);
                e.setPipeline(pipe);
                e.setBindGroup(0, *group0);
                e.setBindGroup(1, *group1);
                e.setPushConstants(std::as_bytes(std::span(scale)));
                e.draw(3);
                e.endRenderPass();
            });
        const auto px = gpu.pixel(color, 1, 1);
        CHECK(static_cast<std::uint8_t>(px[0]) == 128); // 1.0 * 1.0 * 0.5
        CHECK(static_cast<std::uint8_t>(px[1]) == 128); // 1.0 * 0.5 * 1.0
        CHECK(static_cast<std::uint8_t>(px[2]) == 64);  // 1.0 * 0.25 * 1.0
        std::uint32_t id = 0;
        const auto idPx = gpu.pixel(ids, 1, 1);
        std::memcpy(&id, idPx.data(), 4);
        CHECK(id == 3);
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("a GLSL 450 compute shader runs on OpenGL with its storage buffer at (set 0, binding 1)")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        const char *cs = R"(#version 450
layout(local_size_x = 16) in;
layout(set = 0, binding = 1, std430) buffer Out { uint data[]; };
void main() { data[gl_GlobalInvocationID.x] = gl_GlobalInvocationID.x + 100u; })";
        auto shader = createShaderFromGlsl(*gpu.device, ShaderStage::kCompute, cs, {.name = "comp"});
        REQUIRE(shader.has_value());
        auto pipe = gpu->createComputePipeline({.compute = *shader});
        REQUIRE(pipe.has_value());
        const BufferHandle ssbo = gpu.buffer(BufferUsage::kStorage | BufferUsage::kCopySrc, {}, 16 * 4);
        BindGroupDesc g;
        g.entries = {{.binding = 1, .type = BindingType::kStorageBuffer, .buffer = ssbo}};
        auto group = gpu->createBindGroup(g);
        REQUIRE(group.has_value());
        gpu.run(
            [&](CommandEncoder &e)
            {
                e.bindComputePipeline(*pipe);
                e.setBindGroup(0, *group);
                e.dispatch(1, 1, 1);
            });
        auto data = gpu->readBuffer(ssbo, 0, 64);
        REQUIRE(data.has_value());
        std::uint32_t words[16];
        std::memcpy(words, data->data(), 64);
        for (std::uint32_t i = 0; i < 16; ++i)
        {
            CHECK(words[i] == 100 + i);
        }
        gpu.expectNoValidationErrors();
    }
}
