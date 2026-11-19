// RHI-12 : pipeline de shaders. Source unique en GLSL 450 « style Vulkan » (`layout(set = N, binding = M)`, `#include`),
// compilée en SPIR-V par shaderc, réfléchie par SPIRV-Cross, puis traduite en GLSL 430 pour OpenGL (liaisons aplaties, push
// constants en UBO). Le backend Vulkan (M4) consomme directement le SPIR-V.
#pragma once

#include "engine/core/error.hpp"
#include "engine/core/file_watcher.hpp"
#include "engine/rhi/device.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::rhi
{

struct ReflectedBinding
{
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    BindingType type = BindingType::kUniformBuffer;
    std::string name;
};

struct ShaderReflection
{
    std::vector<ReflectedBinding> bindings; // triées par (set, binding)
    std::uint32_t pushConstantSize = 0;     // 0 : pas de bloc push_constant
    std::array<std::uint32_t, 3> localSize{0, 0, 0}; // compute
    std::uint32_t colorOutputs = 0;         // fragment : nombre de sorties couleur
};

struct CompiledShader
{
    ShaderStage stage = ShaderStage::kVertex;
    std::vector<std::uint32_t> spirv;
    ShaderReflection reflection;
    std::vector<std::string> includedFiles; // fichiers lus par #include (rechargement à chaud)
};

struct IncludedSource
{
    std::string path;    // chemin résolu (sert de `requester` aux inclusions imbriquées et au rechargement à chaud)
    std::string content;
};

// `requested` : chemin écrit dans #include ; `requester` : chemin résolu du fichier qui l'inclut.
using IncludeResolver = std::function<core::Result<IncludedSource>(std::string_view requested, std::string_view requester)>;

// Résolveur qui lit des fichiers : relatif au fichier demandeur d'abord, puis aux dossiers `roots`.
IncludeResolver fileIncludeResolver(std::vector<std::filesystem::path> roots);

struct CompileOptions
{
    std::string name = "shader"; // nom dans les messages d'erreur (le fichier, de préférence)
    std::vector<std::pair<std::string, std::string>> defines{};
    IncludeResolver includeResolver{}; // vide : #include refusé
};

// Compile du GLSL 450 en SPIR-V et le réfléchit. Erreurs : kParse (message du compilateur avec fichier:ligne), kInvalidArgument
// (liaison hors des trois groupes, textures et échantillonneurs séparés non pris en charge).
core::Result<CompiledShader> compileGlsl(ShaderStage stage, std::string_view source, const CompileOptions &options = {});

// SPIR-V -> GLSL 430 pour OpenGL : (set, binding) -> set * kBindingsPerGroup + binding ; push constants -> UBO
// kPushConstantBinding. Pas de correction d'espace de clipping (repère OpenGL du RHI, QUESTIONS A26).
core::Result<std::string> translateToGlsl430(const CompiledShader &shader);

// Cache de SPIR-V compilés : en mémoire et, si `directory` n'est pas vide, sur disque (RHI-06). La clé est un hachage du
// source prétraité (après #include et macros), de l'étage et de la version du cache : un changement de source, d'include ou de
// macro donne une autre entrée.
class ShaderCache
{
  public:
    explicit ShaderCache(std::filesystem::path directory = {}) : mDirectory(std::move(directory)) {}

    core::Result<CompiledShader> compile(ShaderStage stage, std::string_view source, const CompileOptions &options = {});

    std::size_t hits() const noexcept { return mHits; }
    std::size_t misses() const noexcept { return mMisses; }
    std::size_t diskHits() const noexcept { return mDiskHits; }

  private:
    std::filesystem::path mDirectory;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> mMemory;
    std::size_t mHits = 0;
    std::size_t mMisses = 0;
    std::size_t mDiskHits = 0;
};

// Crée le shader du périphérique à partir d'un source GLSL 450 : OpenGL reçoit le GLSL 430 traduit, Vulkan recevra le SPIR-V.
core::Result<ShaderHandle> createShaderFromGlsl(Device &device, ShaderStage stage, std::string_view source, const CompileOptions &options = {},
                                                ShaderCache *cache = nullptr);

// Même chose à partir d'un shader déjà compilé (rechargement à chaud, cache).
core::Result<ShaderHandle> createShaderFromCompiled(Device &device, const CompiledShader &compiled, const std::string &name);

// Rechargement à chaud : surveille des fichiers shader (et leurs #include), recompile à chaque modification et appelle le
// rappel. Une compilation qui échoue est signalée (le rappel reçoit l'erreur) ; l'appelant garde alors l'ancien shader.
class ShaderHotReloader
{
  public:
    using Callback = std::function<void(const std::filesystem::path &file, const core::Result<CompiledShader> &result)>;

    // `cache` (facultatif, non possédé) sert les compilations, y compris les rechargements : une source déjà vue ne se recompile pas.
    explicit ShaderHotReloader(IncludeResolver resolver = {}, ShaderCache *cache = nullptr) : mResolver(std::move(resolver)), mCache(cache) {}

    // Les rappels des surveillances retiennent `this` : ni copie ni déplacement.
    ShaderHotReloader(const ShaderHotReloader &) = delete;
    ShaderHotReloader &operator=(const ShaderHotReloader &) = delete;
    ShaderHotReloader(ShaderHotReloader &&) = delete;
    ShaderHotReloader &operator=(ShaderHotReloader &&) = delete;

    // Compile `file` tout de suite (premier résultat renvoyé) et la surveille ensuite.
    core::Result<CompiledShader> add(const std::filesystem::path &file, ShaderStage stage, Callback onReload,
                                     std::vector<std::pair<std::string, std::string>> defines = {});

    // À appeler une fois par image : détecte les changements et recompile. Renvoie le nombre de rechargements tentés.
    std::size_t poll();

  private:
    struct Entry
    {
        std::filesystem::path file;
        ShaderStage stage;
        Callback callback;
        std::vector<std::pair<std::string, std::string>> defines;
        std::vector<core::FileWatcher::WatchId> watchIds;
        bool dirty = false;
    };
    core::Result<CompiledShader> build(const Entry &entry) const;
    void watchFiles(std::size_t index, const CompiledShader &shader);

    IncludeResolver mResolver;
    ShaderCache *mCache = nullptr;
    core::FileWatcher mWatcher;
    std::vector<Entry> mEntries;
};

} // namespace engine::rhi
