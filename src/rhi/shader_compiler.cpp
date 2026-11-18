#include "engine/rhi/shader_compiler.hpp"

#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_glsl.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace engine::rhi
{

using core::ErrorCode;
using core::makeError;

namespace
{

constexpr std::uint32_t kCacheVersion = 1; // à incrémenter quand la compilation ou la réflexion change de résultat
constexpr std::uint32_t kSpirvMagic = 0x07230203u;

shaderc_shader_kind kindOf(ShaderStage stage)
{
    switch (stage)
    {
        case ShaderStage::kVertex: return shaderc_vertex_shader;
        case ShaderStage::kFragment: return shaderc_fragment_shader;
        case ShaderStage::kCompute: return shaderc_compute_shader;
    }
    return shaderc_vertex_shader;
}

std::uint64_t fnv1a(std::uint64_t hash, const void *data, std::size_t size)
{
    const auto *bytes = static_cast<const unsigned char *>(data);
    for (std::size_t i = 0; i < size; ++i)
    {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}

class Includer final : public shaderc::CompileOptions::IncluderInterface
{
  public:
    Includer(const IncludeResolver &resolver, std::vector<std::string> &included) : mResolver(resolver), mIncluded(included) {}

    shaderc_include_result *GetInclude(const char *requested, shaderc_include_type, const char *requester, std::size_t) override
    {
        auto *holder = new Holder();
        if (!mResolver)
        {
            holder->content = "#include is not available: no include resolver was given";
            holder->failed = true;
        }
        else if (auto r = mResolver(requested, requester != nullptr ? requester : ""); r)
        {
            holder->name = r->path;
            holder->content = std::move(r->content);
            if (std::ranges::find(mIncluded, holder->name) == mIncluded.end())
            {
                mIncluded.push_back(holder->name);
            }
        }
        else
        {
            holder->content = r.error().message;
            holder->failed = true;
        }
        holder->result = {holder->name.c_str(), holder->name.size(), holder->content.c_str(), holder->content.size(), holder};
        if (holder->failed)
        {
            holder->result.source_name = "";
            holder->result.source_name_length = 0;
        }
        return &holder->result;
    }

    void ReleaseInclude(shaderc_include_result *data) override { delete static_cast<Holder *>(data->user_data); }

  private:
    struct Holder
    {
        std::string name;
        std::string content;
        bool failed = false;
        shaderc_include_result result{};
    };
    const IncludeResolver &mResolver;
    std::vector<std::string> &mIncluded;
};

shaderc::CompileOptions baseOptions(const CompileOptions &options, std::vector<std::string> &included)
{
    shaderc::CompileOptions o;
    o.SetSourceLanguage(shaderc_source_language_glsl);
    // Cible « Vulkan » : `set`/`binding`, `push_constant` et `gl_VertexIndex` sont ceux du GLSL commun ; SPIRV-Cross les
    // retraduit pour OpenGL. Pas d'optimisation : SPIR-V stable, noms conservés pour le débogage.
    o.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    o.SetOptimizationLevel(shaderc_optimization_level_zero);
    for (const auto &[key, value] : options.defines)
    {
        o.AddMacroDefinition(key, value);
    }
    o.SetIncluder(std::make_unique<Includer>(options.includeResolver, included));
    return o;
}

core::Result<std::string> preprocess(ShaderStage stage, std::string_view source, const CompileOptions &options, std::vector<std::string> &included)
{
    shaderc::Compiler compiler;
    auto result = compiler.PreprocessGlsl(std::string(source), kindOf(stage), options.name.c_str(), baseOptions(options, included));
    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
    {
        return makeError(ErrorCode::kParse, result.GetErrorMessage());
    }
    return std::string(result.cbegin(), result.cend());
}

core::Result<std::vector<std::uint32_t>> toSpirv(ShaderStage stage, const std::string &preprocessed, const CompileOptions &options)
{
    shaderc::Compiler compiler;
    std::vector<std::string> unused;
    CompileOptions noInclude = options;
    noInclude.includeResolver = {}; // déjà développé par le prétraitement
    auto result = compiler.CompileGlslToSpv(preprocessed, kindOf(stage), options.name.c_str(), baseOptions(noInclude, unused));
    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
    {
        return makeError(ErrorCode::kParse, result.GetErrorMessage());
    }
    return std::vector<std::uint32_t>(result.cbegin(), result.cend());
}

core::Result<ShaderReflection> reflect(const std::vector<std::uint32_t> &spirv, ShaderStage stage, const std::string &name)
{
    ShaderReflection out;
    try
    {
        spirv_cross::Compiler compiler(spirv);
        const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        std::string problem;
        auto add = [&](const spirv_cross::SmallVector<spirv_cross::Resource> &list, BindingType type)
        {
            for (const spirv_cross::Resource &r : list)
            {
                ReflectedBinding b;
                b.set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                b.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
                b.type = type;
                b.name = r.name;
                if (b.set >= kGroupCount || b.binding >= kBindingsPerGroup)
                {
                    problem = "'" + r.name + "' uses set " + std::to_string(b.set) + ", binding " + std::to_string(b.binding) + ": the RHI has " +
                              std::to_string(kGroupCount) + " groups of " + std::to_string(kBindingsPerGroup) + " bindings";
                }
                out.bindings.push_back(std::move(b));
            }
        };
        add(resources.uniform_buffers, BindingType::kUniformBuffer);
        add(resources.storage_buffers, BindingType::kStorageBuffer);
        add(resources.sampled_images, BindingType::kSampledTexture);
        add(resources.storage_images, BindingType::kStorageImage);
        if (!resources.separate_images.empty() || !resources.separate_samplers.empty())
        {
            problem = "separate textures and samplers are not supported: declare a combined 'sampler2D' (RHI binding model)";
        }
        if (!problem.empty())
        {
            return makeError(ErrorCode::kInvalidArgument, name + ": " + problem);
        }
        std::ranges::sort(out.bindings, [](const ReflectedBinding &a, const ReflectedBinding &b)
                          { return a.set != b.set ? a.set < b.set : a.binding < b.binding; });
        for (std::size_t i = 1; i < out.bindings.size(); ++i)
        {
            if (out.bindings[i].set == out.bindings[i - 1].set && out.bindings[i].binding == out.bindings[i - 1].binding)
            {
                return makeError(ErrorCode::kInvalidArgument, name + ": '" + out.bindings[i].name + "' and '" + out.bindings[i - 1].name +
                                                                   "' share set " + std::to_string(out.bindings[i].set) + ", binding " +
                                                                   std::to_string(out.bindings[i].binding));
            }
        }
        if (!resources.push_constant_buffers.empty())
        {
            const auto &pc = resources.push_constant_buffers.front();
            out.pushConstantSize = static_cast<std::uint32_t>(compiler.get_declared_struct_size(compiler.get_type(pc.base_type_id)));
        }
        if (stage == ShaderStage::kCompute)
        {
            for (std::uint32_t i = 0; i < 3; ++i)
            {
                out.localSize[i] = compiler.get_execution_mode_argument(spv::ExecutionModeLocalSize, i);
            }
        }
        if (stage == ShaderStage::kFragment)
        {
            out.colorOutputs = static_cast<std::uint32_t>(resources.stage_outputs.size());
        }
    }
    catch (const spirv_cross::CompilerError &e)
    {
        return makeError(ErrorCode::kParse, name + ": SPIR-V reflection failed: " + e.what());
    }
    return out;
}

core::Result<CompiledShader> finish(ShaderStage stage, std::vector<std::uint32_t> spirv, std::vector<std::string> included, const std::string &name)
{
    auto reflection = reflect(spirv, stage, name);
    if (!reflection)
    {
        return std::unexpected(reflection.error());
    }
    CompiledShader out;
    out.stage = stage;
    out.spirv = std::move(spirv);
    out.reflection = std::move(*reflection);
    out.includedFiles = std::move(included);
    return out;
}

std::string hex(std::uint64_t v)
{
    char buffer[17];
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(v));
    return buffer;
}

} // namespace

IncludeResolver fileIncludeResolver(std::vector<std::filesystem::path> roots)
{
    return [roots = std::move(roots)](std::string_view requested, std::string_view requester) -> core::Result<IncludedSource>
    {
        namespace fs = std::filesystem;
        std::vector<fs::path> candidates;
        if (!requester.empty())
        {
            candidates.push_back(fs::path(requester).parent_path() / requested);
        }
        for (const fs::path &root : roots)
        {
            candidates.push_back(root / requested);
        }
        for (const fs::path &candidate : candidates)
        {
            std::error_code ec;
            if (!fs::is_regular_file(candidate, ec))
            {
                continue;
            }
            std::ifstream in(candidate, std::ios::binary);
            std::ostringstream text;
            text << in.rdbuf();
            return IncludedSource{fs::weakly_canonical(candidate, ec).generic_string(), text.str()};
        }
        return makeError(ErrorCode::kNotFound, "include not found: " + std::string(requested));
    };
}

core::Result<CompiledShader> compileGlsl(ShaderStage stage, std::string_view source, const CompileOptions &options)
{
    std::vector<std::string> included;
    auto text = preprocess(stage, source, options, included);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    auto spirv = toSpirv(stage, *text, options);
    if (!spirv)
    {
        return std::unexpected(spirv.error());
    }
    return finish(stage, std::move(*spirv), std::move(included), options.name);
}

core::Result<std::string> translateToGlsl430(const CompiledShader &shader)
{
    try
    {
        spirv_cross::CompilerGLSL glsl(shader.spirv);
        spirv_cross::CompilerGLSL::Options o;
        o.version = 430;
        o.es = false;
        o.vulkan_semantics = false;
        o.emit_push_constant_as_uniform_buffer = true;
        glsl.set_common_options(o);
        const spirv_cross::ShaderResources resources = glsl.get_shader_resources();
        auto remap = [&](const spirv_cross::SmallVector<spirv_cross::Resource> &list)
        {
            for (const spirv_cross::Resource &r : list)
            {
                const std::uint32_t set = glsl.get_decoration(r.id, spv::DecorationDescriptorSet);
                const std::uint32_t binding = glsl.get_decoration(r.id, spv::DecorationBinding);
                glsl.unset_decoration(r.id, spv::DecorationDescriptorSet);
                glsl.set_decoration(r.id, spv::DecorationBinding, set * kBindingsPerGroup + binding);
            }
        };
        remap(resources.uniform_buffers);
        remap(resources.storage_buffers);
        remap(resources.sampled_images);
        remap(resources.storage_images);
        for (const spirv_cross::Resource &r : resources.push_constant_buffers)
        {
            glsl.set_decoration(r.id, spv::DecorationBinding, kPushConstantBinding);
        }
        return glsl.compile();
    }
    catch (const spirv_cross::CompilerError &e)
    {
        return makeError(ErrorCode::kParse, std::string("SPIR-V to GLSL 430 failed: ") + e.what());
    }
}

// ── Cache ───────────────────────────────────────────────────────────────────────────────────────────────────────────

core::Result<CompiledShader> ShaderCache::compile(ShaderStage stage, std::string_view source, const CompileOptions &options)
{
    std::vector<std::string> included;
    auto text = preprocess(stage, source, options, included);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    std::uint64_t key = 14695981039346656037ull;
    const std::uint32_t salt[2] = {kCacheVersion, static_cast<std::uint32_t>(stage)};
    key = fnv1a(key, salt, sizeof salt);
    key = fnv1a(key, text->data(), text->size());

    if (auto it = mMemory.find(key); it != mMemory.end())
    {
        ++mHits;
        return finish(stage, it->second, std::move(included), options.name);
    }
    const std::filesystem::path file = mDirectory.empty() ? std::filesystem::path{} : mDirectory / (hex(key) + ".spv");
    if (!file.empty())
    {
        std::ifstream in(file, std::ios::binary | std::ios::ate);
        if (in)
        {
            const auto bytes = static_cast<std::size_t>(in.tellg());
            std::vector<std::uint32_t> words(bytes / 4);
            in.seekg(0);
            in.read(reinterpret_cast<char *>(words.data()), static_cast<std::streamsize>(words.size() * 4));
            if (bytes % 4 == 0 && !words.empty() && words[0] == kSpirvMagic && in)
            {
                ++mDiskHits;
                ++mHits;
                mMemory.emplace(key, words);
                return finish(stage, std::move(words), std::move(included), options.name);
            }
        }
    }
    ++mMisses;
    auto spirv = toSpirv(stage, *text, options);
    if (!spirv)
    {
        return std::unexpected(spirv.error());
    }
    mMemory.emplace(key, *spirv);
    if (!file.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(mDirectory, ec);
        const std::filesystem::path temp = file.string() + ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(spirv->data()), static_cast<std::streamsize>(spirv->size() * 4));
        }
        std::filesystem::rename(temp, file, ec); // échec d'écriture : le cache disque est facultatif
    }
    return finish(stage, std::move(*spirv), std::move(included), options.name);
}

// ── Création côté périphérique ─────────────────────────────────────────────────────────────────────────────────────────

core::Result<ShaderHandle> createShaderFromCompiled(Device &device, const CompiledShader &compiled, const std::string &name)
{
    ShaderDesc desc;
    desc.stage = compiled.stage;
    desc.debugName = name;
    std::string glsl;
    if (device.capabilities().backend == Backend::kOpenGL)
    {
        auto translated = translateToGlsl430(compiled);
        if (!translated)
        {
            return std::unexpected(translated.error());
        }
        glsl = std::move(*translated);
        desc.language = ShaderLanguage::kGlsl;
        desc.code = std::as_bytes(std::span(glsl));
    }
    else
    {
        desc.language = ShaderLanguage::kSpirv;
        desc.code = std::as_bytes(std::span(compiled.spirv));
    }
    return device.createShader(desc);
}

core::Result<ShaderHandle> createShaderFromGlsl(Device &device, ShaderStage stage, std::string_view source, const CompileOptions &options,
                                                ShaderCache *cache)
{
    auto compiled = cache != nullptr ? cache->compile(stage, source, options) : compileGlsl(stage, source, options);
    if (!compiled)
    {
        return std::unexpected(compiled.error());
    }
    return createShaderFromCompiled(device, *compiled, options.name);
}

// ── Rechargement à chaud ───────────────────────────────────────────────────────────────────────────────────────────────

core::Result<CompiledShader> ShaderHotReloader::build(const Entry &entry) const
{
    std::ifstream in(entry.file, std::ios::binary);
    if (!in)
    {
        return makeError(ErrorCode::kIo, "cannot read " + entry.file.generic_string());
    }
    std::ostringstream text;
    text << in.rdbuf();
    CompileOptions options;
    options.name = entry.file.generic_string();
    options.defines = entry.defines;
    options.includeResolver = mResolver;
    // Les includes relatifs se résolvent par rapport au fichier : on l'indique comme demandeur initial via le nom.
    return mCache != nullptr ? mCache->compile(entry.stage, text.str(), options) : compileGlsl(entry.stage, text.str(), options);
}

void ShaderHotReloader::watchFiles(std::size_t index, const CompiledShader &shader)
{
    Entry &entry = mEntries[index];
    for (core::FileWatcher::WatchId id : entry.watchIds)
    {
        mWatcher.unwatch(id);
    }
    entry.watchIds.clear();
    auto watch = [&](const std::filesystem::path &path)
    { entry.watchIds.push_back(mWatcher.watch(path, [this, index](const core::FileEvent &) { mEntries[index].dirty = true; })); };
    watch(entry.file);
    for (const std::string &include : shader.includedFiles)
    {
        watch(include);
    }
}

core::Result<CompiledShader> ShaderHotReloader::add(const std::filesystem::path &file, ShaderStage stage, Callback onReload,
                                                    std::vector<std::pair<std::string, std::string>> defines)
{
    mEntries.push_back(Entry{file, stage, std::move(onReload), std::move(defines), {}, false});
    const std::size_t index = mEntries.size() - 1;
    auto first = build(mEntries[index]);
    watchFiles(index, first ? *first : CompiledShader{});
    return first;
}

std::size_t ShaderHotReloader::poll()
{
    mWatcher.poll();
    std::size_t attempts = 0;
    for (std::size_t i = 0; i < mEntries.size(); ++i)
    {
        if (!mEntries[i].dirty)
        {
            continue;
        }
        mEntries[i].dirty = false;
        ++attempts;
        auto result = build(mEntries[i]);
        if (result)
        {
            watchFiles(i, *result); // les includes ont pu changer
        }
        mEntries[i].callback(mEntries[i].file, result);
    }
    return attempts;
}

} // namespace engine::rhi
