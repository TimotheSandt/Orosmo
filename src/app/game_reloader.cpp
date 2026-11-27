// Rechargement à chaud de la bibliothèque de jeu (GAME-01, A79 lot L5) : développement seulement. Ce fichier est le seul de
// `app` qui charge une bibliothèque dynamique ou observe un fichier ; une version finale statique (OROSMO_GAME_STATIC) n'appelle
// pas `GameModule::load`, et l'éditeur de liens ne prend pas ce fichier objet (GAME-07, vérifié par `nm`).
#include "game_module_impl.hpp"

#include <orosmo/core/file_watcher.hpp>
#include <orosmo/core/log.hpp>
#include <orosmo/platform/dynamic_library.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <utility>

namespace orosmo::app
{
using core::ErrorCode;

namespace
{

// Charge une copie de la bibliothèque et lit sa table (sans l'appeler).
struct LoadedGame
{
    platform::DynamicLibrary library;
    const OrosmoGameApi *api = nullptr;
};

core::Result<LoadedGame> loadGame(const std::filesystem::path &path)
{
    auto library = platform::DynamicLibrary::load(path);
    if (!library)
        return std::unexpected(std::move(library.error()));
    auto entry = library->function<const OrosmoGameApi *()>(kGameApiSymbol);
    if (!entry)
        return std::unexpected(std::move(entry.error()));
    LoadedGame loaded{std::move(*library), (*entry)()};
    if (auto valid = validateGameApi(loaded.api); !valid)
        return std::unexpected(std::move(valid.error()));
    return loaded;
}

class DynamicReloader final : public GameModule::Reloader
{
  public:
    DynamicReloader(const GameModuleSettings &settings, platform::DynamicLibrary library)
        : path_(settings.path), settleDelay_(settings.settleDelay), library_(std::move(library))
    {
        if (settings.watch)
        {
            watcher_.setPollInterval(settings.pollInterval);
            watcher_.watch(path_, [this](const core::FileEvent &event) {
                // Fichier effacé (l'éditeur de liens le recrée) : on attend sa nouvelle version.
                if (event.change != core::FileChange::kRemoved)
                {
                    pending_ = true;
                    changedAt_ = std::chrono::steady_clock::now();
                }
            });
        }
    }

    void poll(GameModule::Impl &impl) override
    {
        watcher_.poll();
        if (!pending_ || std::chrono::steady_clock::now() - changedAt_ < settleDelay_)
            return;
        pending_ = false;
        (void)reloadNow(impl);
    }

    core::Result<> reloadNow(GameModule::Impl &impl) override
    {
        pending_ = false;
        auto loaded = loadGame(path_);
        if (!loaded)
        {
            impl.recordFailure(loaded.error());
            return std::unexpected(std::move(loaded.error()));
        }
        // L'ancienne copie reste chargée pendant l'`init` de la nouvelle ; en cas d'échec, la nouvelle est déchargée à la sortie
        // (ses fonctions ont été détruites par `switchTo`).
        if (auto switched = impl.switchTo(loaded->api); !switched)
        {
            impl.recordFailure(switched.error());
            return switched;
        }
        // Succès : les fonctions de l'ancienne version sont détruites ; son code peut partir (copie effacée).
        library_ = std::move(loaded->library);
        return {};
    }

    std::filesystem::path loadedPath() const override { return library_.loadedPath(); }

    void unloadActive() noexcept override { library_.unload(); }

  private:
    std::filesystem::path path_;
    std::chrono::milliseconds settleDelay_;
    platform::DynamicLibrary library_;
    core::FileWatcher watcher_;
    bool pending_ = false;
    std::chrono::steady_clock::time_point changedAt_{};
};

} // namespace

core::Result<GameModule> GameModule::load(GameModuleSettings settings)
{
    GameTargets targets = settings.targets;
    if (targets.world == nullptr && targets.simulation != nullptr)
        targets.world = &targets.simulation->world();
    if (targets.world == nullptr)
        return core::makeError(ErrorCode::kInvalidArgument, "game module: no world in the targets");
    if (settings.path.empty())
        return core::makeError(ErrorCode::kInvalidArgument, "game module: empty library path");
    settings.path = std::filesystem::absolute(settings.path);

    auto loaded = loadGame(settings.path);
    if (!loaded)
        return std::unexpected(std::move(loaded.error()));

    auto impl = std::make_unique<Impl>(targets);
    if (auto started = impl->switchTo(loaded->api); !started)
        return std::unexpected(std::move(started.error()));
    impl->reloader = std::make_unique<DynamicReloader>(settings, std::move(loaded->library));
    return GameModule(std::move(impl));
}

} // namespace orosmo::app
