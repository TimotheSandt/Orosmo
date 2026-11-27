// GAME-05 (A79 lot L6) : app::ParamsFile, paramètres de jeu JSON relus à chaud, dernière valeur valide gardée.
#include <orosmo/app/params_host.hpp>

#include <orosmo/core/file_watcher.hpp>
#include <orosmo/core/log.hpp>
#include <orosmo/core/serialization.hpp>
#include <orosmo/scene/scene_file.hpp>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <sstream>
#include <string>
#include <utility>

namespace orosmo::app
{
using core::ErrorCode;

namespace
{
struct AlignedDelete
{
    std::size_t alignment = 1;
    void operator()(std::byte *p) const noexcept { ::operator delete(p, std::align_val_t{alignment}); }
};
using AlignedBytes = std::unique_ptr<std::byte, AlignedDelete>;

AlignedBytes allocate(const scene::ComponentDesc &desc)
{
    const std::size_t size = desc.size == 0 ? 1 : desc.size;
    auto *p = static_cast<std::byte *>(::operator new(size, std::align_val_t{desc.alignment}));
    std::memset(p, 0, size);
    if (desc.defaultValue.size() == desc.size && desc.size != 0)
        std::memcpy(p, desc.defaultValue.data(), desc.size);
    return AlignedBytes(p, AlignedDelete{desc.alignment});
}
} // namespace

struct ParamsFile::State
{
    std::filesystem::path path;
    scene::ComponentDesc desc;
    AlignedBytes value;
    AlignedBytes scratch; // lecture en cours : la valeur n'est remplacée que si tout le fichier est valide
    std::uint64_t revision = 0;
    std::optional<core::Error> lastError;
    std::vector<std::string> warnings;
    core::FileWatcher watcher;
    bool pending = false;

    core::Result<> read()
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return core::makeError(ErrorCode::kIo, "params " + path.string() + ": cannot open the file");
        std::ostringstream text;
        text << in.rdbuf();
        auto json = core::parseJson(text.str());
        if (!json)
            return core::makeError(ErrorCode::kParse, "params " + path.string() + ": invalid JSON");
        if (!json->is_object())
            return core::makeError(ErrorCode::kParse, "params " + path.string() + ": expected an object {field: value}");
        // Champ absent : valeur par défaut (et non la valeur précédente) : le fichier seul décide.
        if (desc.size != 0)
        {
            if (desc.defaultValue.size() == desc.size)
                std::memcpy(scratch.get(), desc.defaultValue.data(), desc.size);
            else
                std::memset(scratch.get(), 0, desc.size);
        }
        std::vector<std::string> found;
        if (auto decoded = scene::componentFromJson(desc, scratch.get(), *json, found); !decoded)
            return core::makeError(decoded.error().code, "params " + path.string() + ": " + decoded.error().message);
        if (desc.size != 0)
            std::memcpy(value.get(), scratch.get(), desc.size);
        warnings = std::move(found);
        for (const std::string &w : warnings)
            LOG_WARNING("params " + path.string() + ": " + w);
        ++revision;
        return {};
    }

    core::Result<> reload()
    {
        pending = false;
        if (auto ok = read(); !ok)
        {
            lastError = ok.error();
            LOG_WARNING(ok.error().describe() + "; last valid values kept");
            return ok;
        }
        lastError.reset();
        LOG_INFO("params " + path.string() + ": loaded (revision " + std::to_string(revision) + ")");
        return {};
    }
};

core::Result<ParamsFile> ParamsFile::open(std::filesystem::path path, scene::ComponentDesc desc, ParamsSettings settings)
{
    if (auto valid = scene::validateComponentDesc(desc); !valid)
        return std::unexpected(std::move(valid.error()));
    auto state = std::make_unique<State>();
    state->path = std::move(path);
    state->value = allocate(desc);
    state->scratch = allocate(desc);
    state->desc = std::move(desc);
    if (settings.watch)
    {
        state->watcher.setPollInterval(settings.pollInterval);
        // L'adresse de `State` ne change pas (tenu par pointeur) : la capture reste valide après un déplacement de ParamsFile.
        // Fichier retiré aussi (revue PR #37) : la relecture signale kIo dans lastError, les dernières valeurs valides restent.
        state->watcher.watch(state->path, [s = state.get()](const core::FileEvent &) { s->pending = true; });
    }
    (void)state->reload(); // absent ou invalide : valeurs par défaut, erreur gardée dans lastError
    return ParamsFile(std::move(state));
}

ParamsFile::ParamsFile(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}
ParamsFile::ParamsFile(ParamsFile &&) noexcept = default;
ParamsFile &ParamsFile::operator=(ParamsFile &&) noexcept = default;
ParamsFile::~ParamsFile() = default;

bool ParamsFile::poll()
{
    if (!state_)
        return false;
    state_->watcher.poll();
    if (!state_->pending)
        return false;
    return state_->reload().has_value();
}

core::Result<> ParamsFile::reload()
{
    if (!state_)
        return core::makeError(ErrorCode::kInvalidArgument, "params: moved-from object");
    return state_->reload();
}

const void *ParamsFile::data() const noexcept { return state_->value.get(); }
const scene::ComponentDesc &ParamsFile::desc() const noexcept { return state_->desc; }
const std::filesystem::path &ParamsFile::path() const noexcept { return state_->path; }
std::uint64_t ParamsFile::revision() const noexcept { return state_->revision; }
const std::optional<core::Error> &ParamsFile::lastError() const noexcept { return state_->lastError; }
const std::vector<std::string> &ParamsFile::warnings() const noexcept { return state_->warnings; }

} // namespace orosmo::app
