#include "orosmo/renderer/shader_library.hpp"

#include <orosmo/core/log.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <atomic>
#include <iterator>

namespace orosmo::renderer
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

core::Result<> ShaderProgram::setStages(std::vector<StageUpdate> updates)
{
    auto destroyCandidates = [&]()
    {
        for (const StageUpdate &u : updates)
        {
            device_.destroy(u.handle);
        }
    };
    // Ensemble final : étages gardés + étages remplacés. Une même liaison ne peut pas changer de type d'un étage à l'autre.
    std::array<rhi::ShaderReflection, 3> tentative = stageReflection_;
    for (const StageUpdate &u : updates)
    {
        tentative[stageIndex(u.compiled.stage)] = u.compiled.reflection;
    }
    for (std::size_t first = 0; first < tentative.size(); ++first)
    {
        for (std::size_t second = first + 1; second < tentative.size(); ++second)
        {
            for (const rhi::ReflectedBinding &a : tentative[first].bindings)
            {
                for (const rhi::ReflectedBinding &b : tentative[second].bindings)
                {
                    if (a.set == b.set && a.binding == b.binding && a.type != b.type)
                    {
                        destroyCandidates();
                        return makeError(ErrorCode::kInvalidArgument, name_ + ": (set " + std::to_string(a.set) + ", binding " +
                                                                          std::to_string(a.binding) + ") has a different type in two stages ('" +
                                                                          a.name + "' and '" + b.name + "')");
                    }
                }
            }
        }
    }
    for (const StageUpdate &u : updates)
    {
        rhi::ShaderHandle *slot = u.compiled.stage == rhi::ShaderStage::kVertex     ? &vertex_
                                  : u.compiled.stage == rhi::ShaderStage::kFragment ? &fragment_
                                                                                   : &compute_;
        device_.destroy(*slot);
        *slot = u.handle;
    }
    stageReflection_ = std::move(tentative);
    mergeReflection();
    return {};
}

core::Result<> ShaderProgram::setStage(const rhi::CompiledShader &compiled, rhi::ShaderHandle handle)
{
    std::vector<StageUpdate> one;
    one.push_back({compiled, handle});
    return setStages(std::move(one));
}

namespace
{
// Identifiant de programme unique dans le processus : deux bibliothèques sur le même périphérique ne donnent jamais le même
// identifiant à deux programmes (le cache de pipelines s'en sert comme clé).
std::uint64_t nextProgramId()
{
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1);
}
} // namespace

ShaderLibrary::ShaderLibrary(rhi::Device &device, ShaderLibraryConfig config)
    : device_(device), config_(std::move(config)), cache_(config_.cacheDirectory),
      reloader_(rhi::fileIncludeResolver(config_.includeRoots), &cache_)
{
}

ShaderLibrary::~ShaderLibrary() = default;

std::size_t ShaderLibrary::programCount() const
{
    return static_cast<std::size_t>(std::ranges::count_if(programs_, [](const Tracked &t) { return !t.program.expired(); }));
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
    std::shared_ptr<ShaderProgram> program(new ShaderProgram(device_, name, nextProgramId()));
    programs_.push_back({program, program->id(), {}, {}, false});
    return program;
}

void ShaderLibrary::forget(std::uint64_t programId)
{
    std::erase_if(programs_,
                  [&](const Tracked &t)
                  {
                      if (t.id != programId)
                      {
                          return false;
                      }
                      for (rhi::ShaderHotReloader::EntryId entry : t.entries)
                      {
                          reloader_.remove(entry);
                      }
                      return true;
                  });
}

void ShaderLibrary::reportFailure(const core::Error &error)
{
    if (!batchError_)
    {
        batchError_ = error;
    }
}

core::Result<rhi::CompiledShader> ShaderLibrary::watch(const std::shared_ptr<ShaderProgram> &program, const std::filesystem::path &file,
                                                       rhi::ShaderStage stage, const std::vector<Define> &defines)
{
    std::weak_ptr<ShaderProgram> weak = program;
    const std::uint64_t id = program->id();
    auto first = reloader_.add(
        file, stage,
        [this, weak, id](const std::filesystem::path &changed, const core::Result<rhi::CompiledShader> &result)
        {
            const auto live = weak.lock();
            if (!live)
            {
                return; // libéré depuis le dernier poll : ses entrées sont retirées au prochain
            }
            if (!result)
            {
                reportFailure(result.error());
                failedThisPoll_.push_back(id);
                LOG_ERROR(1, "Shader reload failed (", changed.generic_string(), "), keeping the previous version: ", result.error().message);
                return;
            }
            auto handle = rhi::createShaderFromCompiled(device_, *result, changed.generic_string());
            if (!handle)
            {
                reportFailure(handle.error());
                failedThisPoll_.push_back(id);
                LOG_ERROR(1, "Shader reload failed (", changed.generic_string(), "): ", handle.error().message);
                return;
            }
            // Pas d'installation ici : les étages du même programme modifiés dans ce poll sont validés ensemble ensuite.
            auto it = std::ranges::find_if(pending_, [&](const auto &p) { return p.first == id; });
            if (it == pending_.end())
            {
                pending_.push_back({id, Pending{weak, {}}});
                it = std::prev(pending_.end());
            }
            it->second.updates.push_back({*result, *handle});
        },
        defines);
    if (auto it = std::ranges::find_if(programs_, [&](const Tracked &t) { return t.id == id; }); it != programs_.end())
    {
        it->entries.push_back(reloader_.lastAdded());
        it->sources.push_back({file, stage, defines});
    }
    return first;
}

core::Result<ShaderProgram::StageUpdate> ShaderLibrary::compileStage(const StageSource &source)
{
    std::ifstream in(source.file, std::ios::binary);
    if (!in)
    {
        return makeError(ErrorCode::kIo, "cannot read " + source.file.generic_string());
    }
    std::ostringstream text;
    text << in.rdbuf();
    auto compiled = cache_.compile(source.stage, text.str(), options(source.file.generic_string(), source.defines));
    if (!compiled)
    {
        return std::unexpected(compiled.error());
    }
    auto handle = rhi::createShaderFromCompiled(device_, *compiled, source.file.generic_string());
    if (!handle)
    {
        return std::unexpected(handle.error());
    }
    return ShaderProgram::StageUpdate{std::move(*compiled), *handle};
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
        core::Result<> installed;
        if (compiled)
        {
            auto handle = rhi::createShaderFromCompiled(device_, *compiled, s.file.generic_string());
            installed = handle ? (*program)->setStage(*compiled, *handle) : core::Result<>(std::unexpected(handle.error()));
        }
        if (!compiled || !installed)
        {
            forget((*program)->id()); // échec du chargement : aucune surveillance sans consommateur
            return std::unexpected(!compiled ? compiled.error() : installed.error());
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
    core::Result<> installed;
    if (compiled)
    {
        auto handle = rhi::createShaderFromCompiled(device_, *compiled, computeFile.generic_string());
        installed = handle ? (*program)->setStage(*compiled, *handle) : core::Result<>(std::unexpected(handle.error()));
    }
    if (!compiled || !installed)
    {
        forget((*program)->id());
        return std::unexpected(!compiled ? compiled.error() : installed.error());
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
    // Programmes libérés : leurs fichiers ne sont plus surveillés ni recompilés.
    std::vector<std::uint64_t> expired;
    for (const Tracked &t : programs_)
    {
        if (t.program.expired())
        {
            expired.push_back(t.id);
        }
    }
    for (std::uint64_t id : expired)
    {
        forget(id);
    }

    const std::size_t before = reloaded_;
    batchError_.reset();
    pending_.clear();
    failedThisPoll_.clear();
    reloader_.poll();
    auto destroyAll = [&](const std::vector<ShaderProgram::StageUpdate> &updates)
    {
        for (const ShaderProgram::StageUpdate &u : updates)
        {
            device_.destroy(u.handle);
        }
    };
    // Un programme dont un étage n'a pas compilé ne reçoit aucun de ses autres candidats : à reprendre en entier plus tard.
    for (std::uint64_t id : failedThisPoll_)
    {
        if (auto t = std::ranges::find_if(programs_, [&](const Tracked &x) { return x.id == id; }); t != programs_.end())
        {
            t->retry = true;
        }
    }
    for (auto &[id, pending] : pending_)
    {
        const auto program = pending.program.lock();
        auto tracked = std::ranges::find_if(programs_, [&](const Tracked &x) { return x.id == id; });
        if (!program || std::ranges::find(failedThisPoll_, id) != failedThisPoll_.end())
        {
            destroyAll(pending.updates);
            continue;
        }
        // Après un échec, tous les étages du programme sont recompilés depuis leurs fichiers et validés ensemble : une édition
        // qui touchait deux étages se reprend quand on corrige l'un d'eux.
        if (tracked != programs_.end() && tracked->retry)
        {
            bool complete = true;
            for (const StageSource &source : tracked->sources)
            {
                const bool present = std::ranges::any_of(pending.updates, [&](const ShaderProgram::StageUpdate &u) { return u.compiled.stage == source.stage; });
                if (present)
                {
                    continue;
                }
                auto update = compileStage(source);
                if (!update)
                {
                    reportFailure(update.error());
                    LOG_ERROR(1, "Shader reload of '", program->name(), "' failed (", source.file.generic_string(), "): ", update.error().message);
                    complete = false;
                    break;
                }
                pending.updates.push_back(std::move(*update));
            }
            if (!complete)
            {
                destroyAll(pending.updates);
                continue;
            }
        }
        const std::size_t count = pending.updates.size();
        if (auto r = program->setStages(std::move(pending.updates)); !r)
        {
            reportFailure(r.error());
            if (tracked != programs_.end())
            {
                tracked->retry = true;
            }
            LOG_ERROR(1, "Shader reload of '", program->name(), "' refused, keeping the previous version: ", r.error().message);
            continue;
        }
        if (tracked != programs_.end())
        {
            tracked->retry = false;
        }
        ++program->revision_;
        reloaded_ += count;
    }
    pending_.clear();
    // Le premier échec du lot reste visible ; sans échec, un rechargement réussi efface l'ancien.
    if (batchError_)
    {
        lastError_ = batchError_;
    }
    else if (reloaded_ != before)
    {
        lastError_.reset();
    }
    return reloaded_ - before;
}

} // namespace orosmo::renderer
