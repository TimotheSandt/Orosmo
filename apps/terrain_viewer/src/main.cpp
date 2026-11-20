// TOOL-01 : terrain-viewer. Codes de retour : 0 succès, 1 erreur d'utilisation, 2 erreur d'exécution (pas de périphérique
// graphique, ressource manquante, capture ratée).
#include "viewer.hpp"

#include <engine/core/system.hpp>

#include "Logger.h"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

namespace
{
constexpr int kUsage = 1;
constexpr int kRuntime = 2;

void usage(std::ostream &out)
{
    out << "terrain-viewer [options]\n"
           "  --seed N              graine du terrain (défaut 1)\n"
           "  --preset NAME         préréglage de presets/ (hills, mountains, islands, dunes, custom)\n"
           "  --theme dark|light|framed  thème de l'interface (changeable dans le panneau ; framed : cadres en images)\n"
           "  --res DIR             ressources (fonts/, themes/, shader/rhi/, presets/) ; défaut : res à côté de l'exécutable\n"
           "  --capture DIR         écrit DIR/viewer.png après --capture-frames images (défaut 30), puis quitte\n"
           "  --capture-frames N\n"
           "  --headless            sans fenêtre (image hors écran ; avec --capture)\n"
           "  --size WxH            taille de la vue (défaut 1280x720)\n"
           "  --backend opengl      seul backend disponible avant M4 (vulkan : refusé)\n"
           "Caméra : clic droit + souris pour tourner, ZQSD (AZERTY) ou WASD, flèches ; Espace / Maj : monter, descendre ; A (Q en QWERTY) : vite, E : lent.\n";
}

template <typename T> std::optional<T> number(std::string_view s)
{
    T v{};
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    return ec == std::errc() && p == s.data() + s.size() ? std::optional<T>(v) : std::nullopt;
}
} // namespace

int main(int argc, char **argv)
{
    ViewerOptions options;
    // Ressources à côté du binaire, trouvé par le système (lancé par le PATH, argv[0] n'est que le nom de la commande) ; --res
    // les remplace. Aucune résolution de chemin ne lève d'exception (dossier courant supprimé : message et code 2).
    std::filesystem::path exe = engine::core::system::executablePath();
    std::error_code pathError;
    if (exe.empty())
        exe = std::filesystem::absolute(argv[0], pathError);
    options.resources = exe.parent_path() / "res";
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        auto value = [&]() -> std::optional<std::string_view> { return i + 1 < argc ? std::optional<std::string_view>(argv[++i]) : std::nullopt; };
        auto bad = [&](std::string_view what)
        {
            std::cerr << "terrain-viewer: " << what << "\n";
            usage(std::cerr);
            return kUsage;
        };
        if (arg == "--help" || arg == "-h")
        {
            usage(std::cout);
            return EXIT_SUCCESS;
        }
        else if (arg == "--headless")
            options.headless = true;
        else if (arg == "--seed" || arg == "--capture-frames")
        {
            auto v = value();
            auto n = v ? number<std::uint32_t>(*v) : std::nullopt;
            if (!n)
                return bad(std::string(arg) + " expects a non-negative integer");
            (arg == "--seed" ? options.seed : options.captureFrames) = *n;
        }
        else if (arg == "--preset" || arg == "--theme" || arg == "--res" || arg == "--capture" || arg == "--backend" || arg == "--size")
        {
            auto v = value();
            if (!v)
                return bad(std::string(arg) + " expects a value");
            if (arg == "--preset")
                options.preset = *v;
            else if (arg == "--theme")
                options.theme = *v;
            else if (arg == "--res")
                options.resources = std::string(*v);
            else if (arg == "--capture")
            {
                options.captureDirectory = std::filesystem::absolute(std::string(*v), pathError);
                if (pathError)
                {
                    std::cerr << "terrain-viewer: cannot resolve " << *v << ": " << pathError.message() << "\n";
                    return kRuntime;
                }
            }
            else if (arg == "--backend")
            {
                if (*v == "vulkan")
                {
                    std::cerr << "terrain-viewer: the Vulkan backend of the RHI arrives in M4; use --backend opengl\n";
                    return kRuntime;
                }
                if (*v != "opengl")
                    return bad("unknown backend (opengl)");
            }
            else
            {
                const auto x = v->find('x');
                auto wv = x == std::string_view::npos ? std::nullopt : number<int>(v->substr(0, x));
                auto hv = x == std::string_view::npos ? std::nullopt : number<int>(v->substr(x + 1));
                if (!wv || !hv || *wv < 64 || *hv < 64 || *wv > 8192 || *hv > 8192)
                    return bad("--size expects WxH (64 to 8192)");
                options.width = *wv;
                options.height = *hv;
            }
        }
        else
            return bad("unknown option " + std::string(arg));
    }
    if (options.headless && options.captureDirectory.empty())
    {
        std::cerr << "terrain-viewer: --headless needs --capture DIR (nothing would be shown)\n";
        return kUsage;
    }

    SET_LOG_FILE("logs/terrain-viewer.log");
    auto viewer = Viewer::create(options);
    if (!viewer)
    {
        std::cerr << "terrain-viewer: " << viewer.error().message << "\n";
        LOG_ERROR(1, viewer.error().message);
        FLUSH_LOG_TO_FILE;
        return kRuntime;
    }
    const int code = (*viewer)->run();
    viewer->reset();
    Window::Terminate();
    FLUSH_LOG_TO_FILE;
    return code;
}
