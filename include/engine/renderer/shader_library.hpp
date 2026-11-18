// Programmes de shaders (vertex + fragment, ou compute) chargés par une bibliothèque explicite (pas de singleton) : compilation
// GLSL 450 → SPIR-V → GLSL cible par le pipeline de shaders du RHI, cache mémoire et disque, rechargement à chaud.
#pragma once

#include "engine/core/error.hpp"
#include "engine/rhi/device.hpp"
#include "engine/rhi/shader_compiler.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine::renderer
{

class ShaderLibrary;

// Étages d'un programme et leur réflexion fusionnée. Créé par `ShaderLibrary`, partagé (`shared_ptr`). Le périphérique doit
// survivre au programme.
class ShaderProgram
{
  public:
    ~ShaderProgram();
    ShaderProgram(const ShaderProgram &) = delete;
    ShaderProgram &operator=(const ShaderProgram &) = delete;

    const std::string &name() const noexcept { return name_; }
    bool isCompute() const noexcept { return !compute_.isNull(); }
    rhi::ShaderHandle vertex() const noexcept { return vertex_; }
    rhi::ShaderHandle fragment() const noexcept { return fragment_; }
    rhi::ShaderHandle compute() const noexcept { return compute_; }
    // Liaisons, taille des push constants, sorties couleur, taille de groupe : somme des étages.
    const rhi::ShaderReflection &reflection() const noexcept { return reflection_; }
    std::uint64_t id() const noexcept { return id_; }
    // Augmente à chaque rechargement à chaud réussi : un pipeline construit avec une révision plus ancienne est périmé.
    std::uint64_t revision() const noexcept { return revision_; }

  private:
    friend class ShaderLibrary;
    ShaderProgram(rhi::Device &device, std::string name, std::uint64_t id) : device_(device), name_(std::move(name)), id_(id) {}
    core::Result<> setStage(const rhi::CompiledShader &compiled, rhi::ShaderHandle handle);
    void mergeReflection();

    rhi::Device &device_;
    std::string name_;
    std::uint64_t id_;
    std::uint64_t revision_ = 1;
    rhi::ShaderHandle vertex_;
    rhi::ShaderHandle fragment_;
    rhi::ShaderHandle compute_;
    std::array<rhi::ShaderReflection, 3> stageReflection_{}; // indexé par rhi::ShaderStage
    rhi::ShaderReflection reflection_;
};

using Define = std::pair<std::string, std::string>;

struct ShaderLibraryConfig
{
    std::filesystem::path cacheDirectory; // cache SPIR-V sur disque ; vide : mémoire seulement
    std::vector<std::filesystem::path> includeRoots;
};

class ShaderLibrary
{
  public:
    ShaderLibrary(rhi::Device &device, ShaderLibraryConfig config = {});
    ~ShaderLibrary();
    ShaderLibrary(const ShaderLibrary &) = delete;
    ShaderLibrary &operator=(const ShaderLibrary &) = delete;

    // Fichiers GLSL 450, surveillés pour le rechargement à chaud (eux et leurs #include).
    core::Result<std::shared_ptr<ShaderProgram>> loadGraphics(const std::filesystem::path &vertexFile, const std::filesystem::path &fragmentFile,
                                                              const std::vector<Define> &defines = {});
    core::Result<std::shared_ptr<ShaderProgram>> loadCompute(const std::filesystem::path &computeFile, const std::vector<Define> &defines = {});
    // Sources en mémoire (pas de rechargement).
    core::Result<std::shared_ptr<ShaderProgram>> graphicsFromSource(const std::string &name, std::string_view vertexSource,
                                                                    std::string_view fragmentSource, const std::vector<Define> &defines = {});

    // À appeler une fois par image : recompile les fichiers modifiés. Renvoie le nombre d'étages rechargés avec succès. Un échec est
    // journalisé et conservé dans `lastError()` ; le programme garde alors son ancienne version.
    std::size_t pollHotReload();
    const std::optional<core::Error> &lastError() const noexcept { return lastError_; }

    const rhi::ShaderCache &cache() const noexcept { return cache_; }
    std::size_t programCount() const;

  private:
    core::Result<std::shared_ptr<ShaderProgram>> newProgram(const std::string &name);
    core::Result<rhi::CompiledShader> watch(const std::shared_ptr<ShaderProgram> &program, const std::filesystem::path &file, rhi::ShaderStage stage,
                                            const std::vector<Define> &defines);
    rhi::CompileOptions options(const std::string &name, const std::vector<Define> &defines) const;

    rhi::Device &device_;
    ShaderLibraryConfig config_;
    rhi::ShaderCache cache_;
    rhi::ShaderHotReloader reloader_;
    std::vector<std::weak_ptr<ShaderProgram>> programs_;
    std::optional<core::Error> lastError_;
    std::uint64_t nextId_ = 1;
    std::size_t reloaded_ = 0;
};

} // namespace engine::renderer
