// UI-08 (A79 lot L8) : rechargement à chaud des vues `.cui` (développement seulement). Seul fichier de `ViewHost` qui cherche une
// vue dans la bibliothèque de jeu, observe des fichiers et lance une commande ; la forme liée de `setRoot` (version finale) ne
// l'appelle pas, et l'éditeur de liens ne prend pas ce fichier objet.
#include "view_host_impl.hpp"

#include <orosmo/app/game_module.hpp>
#include <orosmo/core/file_watcher.hpp>
#include <orosmo/core/log.hpp>
#include <orosmo/platform/process.hpp>
#include <orosmo/ui/ui.hpp>
#include <orosmo/ui/view_entry.hpp>
#include <orosmo/ui/view_state.hpp>

#include <bit>
#include <chrono>
#include <future>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace orosmo::app
{
using core::ErrorCode;

namespace
{

// Lignes d'erreur d'une sortie de construction (cui : « fichier:ligne:colonne: error: … » ; compilateur : « … error: … » ;
// make : « *** … Error 2 »), au plus `limit` ; toute la sortie si aucune ne ressemble à une erreur (dernières lignes).
std::string errorSummary(const std::string &output, std::size_t limit = 4)
{
    std::istringstream in(output);
    std::string line;
    std::string summary;
    std::string last;
    std::size_t kept = 0;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        last = line;
        if (kept < limit && (line.find("error") != std::string::npos || line.find("Error") != std::string::npos))
        {
            summary += (summary.empty() ? "" : "\n") + line;
            ++kept;
        }
    }
    return summary.empty() ? last : summary;
}

// Construit l'arbre de la vue avec la version active de la bibliothèque de jeu.
core::Result<> buildFromLibrary(ViewHost::Impl &impl)
{
    GameModule &game = *impl.settings.game;
    impl.builtVersion = game.version(); // même en cas d'échec : on attend la version suivante
    const std::string symbol = std::string(ui::kCuiViewSymbolPrefix) + impl.name;
    auto address = game.findSymbol(symbol);
    if (!address)
        return core::makeError(address.error().code, "view " + impl.name + ": " + address.error().message +
                                                         " (is the .cui compiled into the game library?)");
    const ui::CuiViewEntry *entry = std::bit_cast<const ui::CuiViewEntry *(*)()>(*address)();
    if (entry == nullptr || entry->abi != ui::kCuiViewAbi || entry->build == nullptr)
        return core::makeError(ErrorCode::kVersionMismatch, "view " + impl.name + ": entry point of another cui version");
    if (entry->bindings != impl.bindings || entry->size != impl.size)
        return core::makeError(ErrorCode::kVersionMismatch,
                               "view " + impl.name + ": its fields changed (@Mutable, @Snapshot or @cui-cpp) since the program was "
                                                     "built: previous view kept, rebuild and restart the program");
    std::shared_ptr<const void> code = game.retainCode(); // retenu avant de construire : l'arbre garde de son code
    std::unique_ptr<ui::Widget> root(entry->build(impl.view.get(), *impl.ui));
    auto installed = impl.install(std::move(root), code);
    if (!installed)
        impl.builtVersion = 0; // kBusy : nouvel essai à l'image suivante
    return installed;
}

class DevReloader final : public ViewHost::Reloader
{
  public:
    explicit DevReloader(const ViewHostSettings &settings)
        : settleDelay_(settings.settleDelay), command_(settings.buildCommand), directory_(settings.buildDirectory),
          startBuild_(settings.startBuild)
    {
        if (!startBuild_)
            startBuild_ = [](const std::string &command, const std::filesystem::path &directory) {
                return std::async(std::launch::async, [command, directory] { return platform::runCommand(command, directory); });
            };
        if (!settings.sources.empty() && !command_.empty())
        {
            watcher_.setPollInterval(settings.pollInterval);
            watcher_.watch(
                settings.sources,
                [this](const core::FileEvent &) {
                    pending_ = true;
                    changedAt_ = std::chrono::steady_clock::now();
                },
                {".cui"});
        }
    }

    ~DevReloader() override
    {
        if (build_.valid())
            build_.wait();
    }

    bool building() const noexcept override { return build_.valid(); }

    void poll(ViewHost::Impl &impl) override
    {
        watcher_.poll();
        finishBuild(impl);
        // Pendant une construction, la bibliothèque peut déjà être liée et chargée sans que la construction ait réussi : on
        // attend sa fin avant de recréer l'arbre.
        GameModule *game = impl.settings.game;
        if (!build_.valid() && game != nullptr && game->running() && game->version() != impl.builtVersion)
        {
            if (auto rebuilt = buildFromLibrary(impl); rebuilt)
            {
                ++impl.rebuilds;
                LOG_INFO("ui view " + impl.name + ": rebuilt from game library version " + std::to_string(game->version()) + " (" +
                         std::to_string(impl.ui->root() ? ui::countWidgets(*impl.ui->root()) : 0) + " widgets)");
            }
            else if (rebuilt.error().code != ErrorCode::kBusy)
                impl.fail(rebuilt.error());
        }
        if (pending_ && !build_.valid() && std::chrono::steady_clock::now() - changedAt_ >= settleDelay_)
        {
            pending_ = false;
            ++impl.builds;
            LOG_INFO("ui view: .cui changed, running `" + command_ + "`");
            build_ = startBuild_(command_, directory_);
        }
    }

  private:
    void finishBuild(ViewHost::Impl &impl)
    {
        if (!build_.valid() || build_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        core::Result<platform::CommandResult> result = build_.get();
        // Échec : la version du jeu chargée pendant la construction ne remplace pas la vue.
        const auto rejectLoadedVersion = [&impl] {
            if (const GameModule *game = impl.settings.game; game != nullptr && game->running())
                impl.builtVersion = game->version();
        };
        if (!result)
        {
            rejectLoadedVersion();
            impl.fail(result.error());
            return;
        }
        impl.lastBuildOutput = std::move(result->output);
        if (result->exitCode != 0)
        {
            rejectLoadedVersion();
            LOG_WARNING("ui view: build output:\n" + impl.lastBuildOutput);
            impl.fail(core::makeError(ErrorCode::kCancelled, "ui build failed, previous view kept:\n" + errorSummary(impl.lastBuildOutput))
                          .error());
            return;
        }
        // Réussite : la bibliothèque reconstruite est rechargée par le module de jeu (`GameModule::update`), l'arbre recréé ensuite.
        LOG_INFO("ui view: build done, waiting for the game library reload");
    }

    std::chrono::milliseconds settleDelay_;
    std::string command_;
    std::filesystem::path directory_;
    decltype(ViewHostSettings::startBuild) startBuild_;
    core::FileWatcher watcher_;
    bool pending_ = false;
    std::chrono::steady_clock::time_point changedAt_{};
    std::future<core::Result<platform::CommandResult>> build_;
};

} // namespace

core::Result<> ViewHost::mountFromLibrary(const char *name, std::uint64_t bindings, std::size_t size, std::shared_ptr<const void> view)
{
    Impl &impl = *impl_;
    if (impl.ui->updating())
        return core::makeError(ErrorCode::kBusy, "ViewHost::setRoot during Ui::update");
    if (impl.settings.game == nullptr || !impl.settings.game->running())
        return core::makeError(ErrorCode::kInvalidArgument, std::string("view ") + name +
                                                                ": compiled into the game library, but no game module is running");
    // Vue précédente gardée jusqu'au nouvel arbre, et remise si la nouvelle ne se construit pas. Sans vue précédente, la vue
    // demandée reste retenue : une version suivante de la bibliothèque la montera.
    const std::uint32_t previousBuiltVersion = impl.builtVersion;
    std::shared_ptr<const void> previous = std::exchange(impl.view, std::move(view));
    std::string previousName = std::exchange(impl.name, name);
    const std::uint64_t previousBindings = std::exchange(impl.bindings, bindings);
    const std::size_t previousSize = std::exchange(impl.size, size);
    if (!impl.reloader)
        impl.reloader = std::make_unique<DevReloader>(impl.settings);
    auto built = buildFromLibrary(impl);
    if (!built)
    {
        if (!previousName.empty())
        {
            impl.view = std::move(previous);
            impl.name = std::move(previousName);
            impl.bindings = previousBindings;
            impl.size = previousSize;
            impl.builtVersion = previousBuiltVersion;
        }
        impl.fail(built.error());
    }
    return built;
}

} // namespace orosmo::app
