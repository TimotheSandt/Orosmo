// Galerie de l'interface (exemple) : tous les widgets, conteneurs, modificateurs, événements et thèmes d'engine::ui, décrits dans
// Gallery.cui. Fenêtre, ou image sans fenêtre (--headless --capture DIR). Codes de retour : 0, 1 (utilisation), 2 (exécution).
#include "Gallery.gen.h"
#include "gallery_state.hpp"

#include <engine/core/file_watcher.hpp>
#include <engine/core/system.hpp>
#include <engine/platform/input_manager.hpp>
#include <engine/platform/window.hpp>
#include <engine/renderer/canvas_renderer.hpp>
#include <engine/renderer/fullscreen_blit.hpp>
#include <engine/renderer/image.hpp>
#include <engine/renderer/pipeline_cache.hpp>
#include <engine/renderer/render_target.hpp>
#include <engine/renderer/shader_library.hpp>
#include <engine/rhi/device.hpp>
#include <engine/ui/font.hpp>
#include <engine/ui/ui.hpp>
#include <engine/ui/window_input.hpp>

#include <engine/core/log.hpp>
#include <engine/ui/text_renderer.hpp>

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

namespace fs = std::filesystem;
namespace rd = engine::renderer;
namespace rhi = engine::rhi;
namespace ui = engine::ui;

namespace
{
struct Options
{
    fs::path resources;
    fs::path capture;
    std::uint32_t frames = 5;
    bool headless = false;
    std::string theme = "dark";
    std::string tab;
    std::string hover; // capture : pointeur posé sur ce widget (survol, infobulle)
    std::string click; // capture : clic sur ce widget (liste déroulante ouverte, case cochée…)
    int width = 1280;
    int height = 800;
};

void usage(std::ostream &out)
{
    out << "ui-gallery [--theme dark|light|framed] [--tab widgets|controls|color|windows|layout|parameters|themes] [--res DIR]\n"
           "           [--capture DIR [--capture-frames N] [--headless] [--hover ID] [--click ID]] [--size WxH]\n"
           "  --hover ID / --click ID : during a capture, the pointer rests on / clicks the widget with this id\n"
           "                            (a tooltip appears after 0.6 s: use --capture-frames 60; --click needs 3 frames at least)\n"
           "  --size WxH : 640x400 at least\n";
}

int run(const Options &o)
{
    Window window("UI gallery", o.width, o.height);
    rhi::DeviceDesc desc;
    desc.backend = rhi::Backend::kOpenGL;
    desc.validation = true;
    if (o.headless)
        desc.surface.size = {static_cast<std::uint32_t>(o.width), static_cast<std::uint32_t>(o.height)};
    else
    {
        if (window.Init([&] { (void)rhi::prepareWindowHints(desc); }) != 0)
        {
            std::cerr << "ui-gallery: cannot open a window (no display?); try --headless --capture DIR\n";
            return 2;
        }
        desc.surface.nativeWindow = window.GetWindow();
    }
    auto device = rhi::createDevice(desc);
    if (!device)
    {
        std::cerr << "ui-gallery: graphics device: " << device.error().message << "\n";
        return 2;
    }
    const fs::path shaderDir = o.resources / "shader" / "rhi";
    rd::ShaderLibrary shaders(**device);
    rd::PipelineCache pipelines(**device);
    auto canvas = rd::CanvasRenderer::create(**device, shaders, pipelines, shaderDir);
    auto blit = rd::FullscreenBlit::create(**device, shaders, pipelines, shaderDir);
    if (!canvas || !blit)
    {
        std::cerr << "ui-gallery: renderer: " << (!canvas ? canvas.error().message : blit.error().message) << "\n";
        return 2;
    }

    ui::TextRenderer text;
    if (!text.init(static_cast<unsigned>(o.width), static_cast<unsigned>(o.height)) ||
        !text.loadFont((o.resources / "fonts" / "Roboto-Regular.ttf").string(), "default", 48))
    {
        std::cerr << "ui-gallery: font not found in " << (o.resources / "fonts").generic_string() << "\n";
        return 2;
    }
    text.setActiveFont("default");
    ui::TextRendererFont font(text);
    engine::core::FileWatcher watcher; // avant l'interface : son ThemeHost s'en désinscrit à sa destruction
    ui::Ui interface(font);
    if (!o.headless)
        ui::useSystemClipboard(interface, window); // champs de texte : Ctrl+C / X / V

    GalleryState state;
    state.ui = &interface;
    const auto themes = state.themes();
    const auto found = std::find(themes.begin(), themes.end(), o.theme);
    if (found == themes.end())
    {
        std::cerr << "ui-gallery: unknown theme '" << o.theme << "'\n";
        return 1;
    }
    state.theme = static_cast<int>(found - themes.begin());
    auto loadTheme = [&]
    {
        const auto path = o.resources / "themes" / (themes[static_cast<std::size_t>(state.theme)] + ".json");
        if (auto r = interface.themes().load(path); !r)
            state.record("theme error: " + r.error().message);
        else
            state.record("theme " + themes[static_cast<std::size_t>(state.theme)]);
        interface.themes().watch(watcher); // rechargé à chaud quand le fichier ou une de ses images change
    };
    loadTheme();
    const std::vector<std::string> tabs = {"widgets", "controls", "color", "windows", "layout", "parameters", "themes"};
    if (!o.tab.empty())
    {
        const auto t = std::find(tabs.begin(), tabs.end(), o.tab);
        if (t == tabs.end())
        {
            std::cerr << "ui-gallery: unknown tab '" << o.tab << "'\n";
            return 1;
        }
        state.tab = static_cast<int>(t - tabs.begin());
    }
    // Une action NOMMÉE (le bouton « Named action » nomme `galleryAction`, qui n'est pas un champ de la vue).
    interface.actions().add("galleryAction", [&](const ui::Event &) { state.record("action  galleryAction (ActionRegistry)"); });
    interface.setRoot(cui::buildGallery(interface, state));
    for (const std::string &missing : interface.actions().missing())
        std::cerr << "ui-gallery: unregistered action " << missing << "\n";

    rd::RenderTarget target;
    std::uint32_t frames = 0;
    double watchClock = 0.0;
    const bool capturing = !o.capture.empty();
    bool written = false;
    while (o.headless || !window.ShouldClose())
    {
        if (!o.headless && !window.NewFrame())
            continue;
        const float dt = capturing || o.headless ? 1.0f / 60.0f : static_cast<float>(1.0 / std::max(window.GetFPS(), 1.0));
        int w = o.width, h = o.height, pw = o.width, ph = o.height;
        if (!o.headless && !capturing)
        {
            w = window.GetWidth();
            h = window.GetHeight();
            window.GetFramebufferSize(pw, ph);
        }
        if ((watchClock += dt) > 0.5)
        {
            watchClock = 0.0;
            watcher.poll();
        }
        state.tick(dt);
        // Échap appartient à l'interface si un champ de texte, une fenêtre surgissante ou une fenêtre modale l'attend (état AVANT
        // la mise à jour : le champ qui annule sa saisie perd le focus pendant celle-ci).
        const bool uiTakesEscape = interface.wantsKeyboard() || interface.popup() != nullptr || interface.modalOpen();
        ui::InputFrame input = o.headless ? ui::InputFrame{} : ui::readWindowInput(window, dt);
        if (capturing)
        {
            // Entrées simulées (images de démonstration reproductibles) : le pointeur sur un widget, un clic à la 2e image.
            input = ui::InputFrame{};
            input.deltaTime = dt;
            const std::string &id = !o.click.empty() ? o.click : o.hover;
            ui::Widget *target = id.empty() ? nullptr : interface.find(id);
            if (!id.empty() && target == nullptr && frames >= 1)
            {
                std::cerr << "ui-gallery: no widget with id '" << id << "' (--hover / --click)\n";
                return 2;
            }
            if (target != nullptr && frames >= 1)
            {
                input.mouse = target->rect().position + target->rect().size * 0.5f;
                input.mouseDown = !o.click.empty() && frames == 1;
            }
        }
        interface.update(input, {static_cast<float>(w), static_cast<float>(h)});
        if (state.themeRequested)
        {
            state.themeRequested = false;
            loadTheme();
        }
        if (!o.headless && !uiTakesEscape && !interface.wantsKeyboard() && interface.popup() == nullptr && !interface.modalOpen() &&
            InputManager::GetInstance(window.GetWindow()).IsKeyJustPressed(KeyButton::ESCAPE))
            window.RequestClose();

        auto backbuffer = (*device)->beginFrame();
        if (!backbuffer || pw <= 0 || ph <= 0)
            continue;
        const rhi::Extent2D size{static_cast<std::uint32_t>(pw), static_cast<std::uint32_t>(ph)};
        if (!target.valid())
        {
            auto t = rd::RenderTarget::create(**device, {.size = size, .depth = false, .debugName = "gallery"});
            if (!t)
                return 2;
            target = std::move(*t);
        }
        else if (!target.resize(size.width, size.height))
            return 2;
        rd::DrawList list;
        interface.draw(list);
        auto encoder = (*device)->createCommandEncoder();
        auto drawn = (*canvas)->render(*encoder, list, {target.color(0), rhi::Format::kRGBA8Unorm, size}, false);
        if (drawn)
            drawn = (*blit)->copy(*encoder, target.color(0), (*blit)->linearSampler(), *backbuffer, rhi::Format::kRGBA8Unorm, "present");
        auto submitted = (*device)->submit(std::move(encoder));
        if (!drawn || !submitted)
        {
            std::cerr << "ui-gallery: frame: " << (!drawn ? drawn.error().message : submitted.error().message) << "\n";
            return 2;
        }
        if (capturing && ++frames >= o.frames)
        {
            std::error_code ec;
            fs::create_directories(o.capture, ec);
            const auto out = o.capture / "gallery.png";
            if (auto saved = rd::savePng(**device, target.color(0), out); !saved)
            {
                std::cerr << "ui-gallery: " << saved.error().message << "\n";
                return 2;
            }
            std::cout << "ui-gallery: " << out.generic_string() << "\n";
            written = true;
            (void)(*device)->present();
            break;
        }
        (void)(*device)->present();
    }
    (*device)->waitIdle();
    if (capturing && !written)
    {
        std::cerr << "ui-gallery: the window closed before the capture was written\n";
        return 2;
    }
    return 0;
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
    Options o;
    // Ressources à côté du binaire trouvé par le système (pas argv[0], qui peut n'être que le nom de la commande) ; --res les
    // remplace. Résolution sans exception.
    fs::path exe = engine::core::system::executablePath();
    std::error_code pathError;
    if (exe.empty())
        exe = fs::absolute(argv[0], pathError);
    o.resources = exe.parent_path() / "res";
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view a = argv[i];
        const bool hasValue = i + 1 < argc;
        if (a == "--help" || a == "-h")
        {
            usage(std::cout);
            return 0;
        }
        if (a == "--headless")
            o.headless = true;
        else if (hasValue && a == "--theme")
            o.theme = argv[++i];
        else if (hasValue && a == "--tab")
            o.tab = argv[++i];
        else if (hasValue && a == "--hover")
            o.hover = argv[++i];
        else if (hasValue && a == "--click")
            o.click = argv[++i];
        else if (hasValue && a == "--res")
            o.resources = argv[++i];
        else if (hasValue && a == "--capture")
        {
            o.capture = fs::absolute(argv[++i], pathError);
            if (pathError)
            {
                std::cerr << "ui-gallery: cannot resolve " << argv[i] << ": " << pathError.message() << "\n";
                return 2;
            }
        }
        else if (hasValue && a == "--capture-frames" && number<std::uint32_t>(argv[i + 1]))
            o.frames = *number<std::uint32_t>(argv[++i]);
        else if (hasValue && a == "--size")
        {
            const std::string_view v = argv[++i];
            const auto x = v.find('x');
            auto w = x == std::string_view::npos ? std::nullopt : number<int>(v.substr(0, x));
            auto h = x == std::string_view::npos ? std::nullopt : number<int>(v.substr(x + 1));
            if (!w || !h || *w < 640 || *h < 400) // la colonne latérale (300 px) et les onglets doivent tenir
            {
                usage(std::cerr);
                return 1;
            }
            o.width = *w;
            o.height = *h;
        }
        else
        {
            std::cerr << "ui-gallery: unknown option " << a << "\n";
            usage(std::cerr);
            return 1;
        }
    }
    if (o.headless && o.capture.empty())
    {
        std::cerr << "ui-gallery: --headless needs --capture DIR\n";
        return 1;
    }
    if (!o.click.empty() && o.frames < 3)
    {
        // Le clic agit au relâchement (image 2) : la capture doit venir après.
        std::cerr << "ui-gallery: --click needs --capture-frames 3 or more\n";
        return 1;
    }
    SET_LOG_FILE("logs/ui-gallery.log");
    const int code = run(o);
    Window::Terminate();
    FLUSH_LOG_TO_FILE;
    return code;
}
