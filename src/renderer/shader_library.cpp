#include "engine/renderer/shader_library.hpp"

#include "Logger.h"

#include <algorithm>

namespace engine::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{
std::size_t stageIndex(rhi::ShaderStage stage) { return static_cast<std::size_t>(stage); }
} // namespace

ShaderProgram::~ShaderProgram()
{
    device_.destroy(vertex_);
    device_.destroy(fragment_);
    device_.destroy(compute_);
}

void ShaderProgram::mergeReflection()
{
    rhi::ShaderReflection merged;
    for (const rhi::ShaderReflection &stage : stageReflection_)
    {
        for (const rhi::ReflectedBinding &b : stage.bindings)
        {
            const bool known = std::ranges::any_of(merged.bindings, [&](const rhi::ReflectedBinding &m) { return m.set == b.set && m.binding == b.binding; });
            if (!known)
            {
                merged.bindings.push_back(b);
            }
        }
        merged.pushConstantSize = std::max(merged.pushConstantSize, stage.pushConstantSize);
        merged.colorOutputs = std::max(merged.colorOutputs, stage.colorOutputs);
        for (std::size_t i = 0; i < 3; ++i)
        {
            merged.localSize[i] = std::max(merged.localSize[i], stage.localSize[i]);
        }
    }
    std::ranges::sort(merged.bindings, [](const rhi::ReflectedBinding &a, const rhi::ReflectedBinding &b)
                      { return a.set != b.set ? a.set < b.set : a.binding < b.binding; });
    reflection_ = std::move(merged);
}

core::Result<> ShaderProgram::setStage(const rhi::CompiledShader &compiled, rhi::ShaderHandle handle)
{
    // Une même liaison ne peut pas changer de type d'un étage à l'autre.
    for (std::size_t other = 0; other < stageReflection_.size(); ++other)
    {
        if (other == stageIndex(compiled.stage))
        {
            continue;
        }
        for (const rhi::ReflectedBinding &a : stageReflection_[other].bindings)
        {
            for (const rhi::ReflectedBinding &b : compiled.reflection.bindings)
            {
                if (a.set == b.set && a.binding == b.binding && a.type != b.type)
                {
                    device_.destroy(handle);
                    return makeError(ErrorCode::kInvalidArgument, name_ + ": (set " + std::to_string(a.set) + ", binding " + std::to_string(a.binding) +
                                                                      ") has a different type in two stages ('" + a.name + "' and '" + b.name + "')");
                }
            }
        }
    }
    rhi::ShaderHandle *slot = compiled.stage == rhi::ShaderStage::kVertex     ? &vertex_
                              : compiled.stage == rhi::ShaderStage::kFragment ? &fragment_
                                                                             : &compute_;
    device_.destroy(*slot);
    *slot = handle;
    stageReflection_[stageIndex(compiled.stage)] = compiled.reflection;
    mergeReflection();
    return {};
}

ShaderLibrary::ShaderLibrary(rhi::Device &device, ShaderLibraryConfig config)
    : device_(device), config_(std::move(config)), cache_(config_.cacheDirectory),
      reloader_(rhi::fileIncludeResolver(config_.includeRoots), &cache_)
{
}

ShaderLibrary::~ShaderLibrary() = default;

std::size_t ShaderLibrary::programCount() const
{
    return static_cast<std::size_t>(std::ranges::count_if(programs_, [](const std::weak_ptr<ShaderProgram> &p) { return !p.expired(); }));
}

rhi::CompileOptions ShaderLibrary::options(const std::string &name, const std::vector<Define> &defines) const
{
    rhi::CompileOptions o;
    o.name = name;
    o.defines = defines;
    o.includeResolver = rhi::fileIncludeResolver(config_.includeRoots);
    return o;
}

core::Result<std::shared_ptr<ShaderProgram>> ShaderLibrary::newProgram(const std::string &name)
{
    std::shared_ptr<ShaderProgram> program(new ShaderProgram(device_, name, nextId_++));
    std::erase_if(programs_, [](const std::weak_ptr<ShaderProgram> &p) { return p.expired(); });
    programs_.push_back(program);
    return program;
}

core::Result<rhi::CompiledShader> ShaderLibrary::watch(const std::shared_ptr<ShaderProgram> &program, const std::filesystem::path &file,
                                                       rhi::ShaderStage stage, const std::vector<Define> &defines)
{
    std::weak_ptr<ShaderProgram> weak = program;
    return reloader_.add(
        file, stage,
        [this, weak, stage](const std::filesystem::path &changed, const core::Result<rhi::CompiledShader> &result)
        {
            const auto program = weak.lock();
            if (!program)
            {
                return;
            }
            if (!result)
            {
                lastError_ = result.error();
                LOG_ERROR(1, "Shader reload failed (", changed.generic_string(), "), keeping the previous version: ", result.error().message);
                return;
            }
            auto handle = rhi::createShaderFromCompiled(device_, *result, changed.generic_string());
            if (!handle)
            {
                lastError_ = handle.error();
                LOG_ERROR(1, "Shader reload failed (", changed.generic_string(), "): ", handle.error().message);
                return;
            }
            if (auto r = program->setStage(*result, *handle); !r)
            {
                lastError_ = r.error();
                LOG_ERROR(1, "Shader reload failed (", changed.generic_string(), "): ", r.error().message);
                return;
            }
            ++program->revision_;
            ++reloaded_;
            lastError_.reset();
            (void)stage;
        },
        defines);
}

core::Result<std::shared_ptr<ShaderProgram>> ShaderLibrary::loadGraphics(const std::filesystem::path &vertexFile, const std::filesystem::path &fragmentFile,
                                                                         const std::vector<Define> &defines)
{
    auto program = newProgram(vertexFile.filename().generic_string() + "+" + fragmentFile.filename().generic_string());
    if (!program)
    {
        return program;
    }
    struct Stage
    {
        const std::filesystem::path &file;
        rhi::ShaderStage stage;
    };
    for (const Stage s : {Stage{vertexFile, rhi::ShaderStage::kVertex}, Stage{fragmentFile, rhi::ShaderStage::kFragment}})
    {
        // Le rechargeur compile une première fois (par le cache) puis surveille ; son rappel met le programme à jour.
        auto compiled = watch(*program, s.file, s.stage, defines);
        if (!compiled)
        {
            return std::unexpected(compiled.error());
        }
        auto handle = rhi::createShaderFromCompiled(device_, *compiled, s.file.generic_string());
        if (!handle)
        {
            return std::unexpected(handle.error());
        }
        if (auto r = (*program)->setStage(*compiled, *handle); !r)
        {
            return std::unexpected(r.error());
        }
    }
    return program;
}

core::Result<std::shared_ptr<ShaderProgram>> ShaderLibrary::loadCompute(const std::filesystem::path &computeFile, const std::vector<Define> &defines)
{
    auto program = newProgram(computeFile.filename().generic_string());
    if (!program)
    {
        return program;
    }
    auto compiled = watch(*program, computeFile, rhi::ShaderStage::kCompute, defines);
    if (!compiled)
    {
        return std::unexpected(compiled.error());
    }
    auto handle = rhi::createShaderFromCompiled(device_, *compiled, computeFile.generic_string());
    if (!handle)
    {
        return std::unexpected(handle.error());
    }
    if (auto r = (*program)->setStage(*compiled, *handle); !r)
    {
        return std::unexpected(r.error());
    }
    return program;
}

core::Result<std::shared_ptr<ShaderProgram>> ShaderLibrary::graphicsFromSource(const std::string &name, std::string_view vertexSource,
                                                                               std::string_view fragmentSource, const std::vector<Define> &defines)
{
    auto program = newProgram(name);
    if (!program)
    {
        return program;
    }
    struct Stage
    {
        std::string_view source;
        rhi::ShaderStage stage;
        const char *suffix;
    };
    for (const Stage s : {Stage{vertexSource, rhi::ShaderStage::kVertex, ".vert"}, Stage{fragmentSource, rhi::ShaderStage::kFragment, ".frag"}})
    {
        const std::string stageName = name + s.suffix;
        auto compiled = cache_.compile(s.stage, s.source, options(stageName, defines));
        if (!compiled)
        {
            return std::unexpected(compiled.error());
        }
        auto handle = rhi::createShaderFromCompiled(device_, *compiled, stageName);
        if (!handle)
        {
            return std::unexpected(handle.error());
        }
        if (auto r = (*program)->setStage(*compiled, *handle); !r)
        {
            return std::unexpected(r.error());
        }
    }
    return program;
}

std::size_t ShaderLibrary::pollHotReload()
{
    const std::size_t before = reloaded_;
    reloader_.poll();
    return reloaded_ - before;
}

} // namespace engine::renderer
