#include "orosmo/ui/debug_tools.hpp"

#include "orosmo/core/profiler.hpp"
#include "orosmo/scene/world.hpp"
#include "orosmo/ui/layout.hpp"
#include "orosmo/ui/reflect.hpp"
#include "orosmo/ui/widgets.hpp"

#include <algorithm>
#include <cstdio>
#include <string_view>
#include <typeinfo>
#include <utility>

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#include <cstdlib>
#define OROSMO_HAS_CXXABI 1
#endif

namespace orosmo::ui::debug
{

namespace
{
// Ligne cliquable (arbre des widgets, liste des entités) : un libellé qui reçoit le survol et le clic (style « debugRow »).
class Row : public Label
{
  public:
    explicit Row(std::function<std::string()> text) : Label({}, "debugRow") { bindText(std::move(text)); }
    bool interactive() const override { return true; }
};

// Saisie de la console : Entrée exécute la ligne et vide le champ ; la perte du focus n'exécute rien.
class ConsoleInput : public TextField
{
  public:
    explicit ConsoleInput(Console &console)
        : TextField(Binding<std::string>{[] { return std::string(); }, {}}, "debugInput"), console_(&console)
    {
    }
    void onKey(Ui &ui, int key) override
    {
        submitting_ = key == kKeyEnter;
        TextField::onKey(ui, key);
        submitting_ = false;
    }

  protected:
    // Faux : le champ reprend la valeur liée (vide). La commande est exécutée à la fin de l'image (après les autres écritures).
    bool commit(Ui &ui, const std::string &text) override
    {
        if (submitting_ && !text.empty())
            ui.defer([console = console_, text] { (void)console->execute(text); });
        return false;
    }

  private:
    Console *console_;
    bool submitting_ = false;
};

const char *lineStyle(ConsoleLineKind kind)
{
    switch (kind)
    {
    case ConsoleLineKind::kInput: return "debugInputLine";
    case ConsoleLineKind::kWarning: return "debugWarning";
    case ConsoleLineKind::kError: return "debugError";
    case ConsoleLineKind::kOutput: break;
    }
    return "debugText";
}

std::string formatMs(const char *pattern, double a, double b)
{
    char buffer[160];
    const int n = std::snprintf(buffer, sizeof buffer, pattern, a, b);
    return n <= 0 ? std::string() : std::string(buffer, static_cast<std::size_t>(std::min(n, static_cast<int>(sizeof buffer) - 1)));
}

std::string number(float v)
{
    char buffer[32];
    const int n = std::snprintf(buffer, sizeof buffer, "%g", static_cast<double>(v));
    return n <= 0 ? std::string() : std::string(buffer, static_cast<std::size_t>(std::min(n, static_cast<int>(sizeof buffer) - 1)));
}

void describeInto(const Widget &w, int depth, bool visible, std::size_t maxRows, std::vector<WidgetInfo> &out)
{
    if (out.size() >= maxRows)
        return;
    const bool shown = visible && w.visible();
    out.push_back(WidgetInfo{depth, widgetTypeName(w), w.id(), w.styleName(), w.rect(), w.measured(), shown});
    for (const auto &child : w.children())
        describeInto(*child, depth + 1, shown, maxRows, out);
}

// Signature des composants d'une entité (reconstruction du panneau quand elle change).
std::string componentSignature(const scene::World &world, scene::Entity entity)
{
    std::string out;
    if (!world.alive(entity))
        return out;
    for (const scene::ComponentId id : world.componentIds())
    {
        if (world.has(entity, id))
        {
            out += std::to_string(id.index) + "." + std::to_string(id.generation) + ";";
        }
    }
    return out;
}
} // namespace

// ── Fonctions libres ───────────────────────────────────────────────────────────────────────────────────────────────────

std::string widgetTypeName(const Widget &widget)
{
    const char *raw = typeid(widget).name();
    std::string name = raw;
#ifdef OROSMO_HAS_CXXABI
    int status = 0;
    char *demangled = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
    if (status == 0 && demangled != nullptr)
        name = demangled;
    std::free(demangled);
#endif
    // MSVC : « class orosmo::ui::Button » ; partout : sans espace de noms ni mot-clé.
    for (std::string_view prefix : {"class ", "struct "})
        if (name.starts_with(prefix))
            name.erase(0, prefix.size());
    if (const auto colon = name.rfind("::"); colon != std::string::npos && name.find('<') == std::string::npos)
        name.erase(0, colon + 2);
    return name;
}

std::vector<WidgetInfo> describeWidgetTree(const Widget &root, std::size_t maxRows)
{
    std::vector<WidgetInfo> out;
    describeInto(root, 0, true, maxRows, out);
    return out;
}

std::string describeWidget(const WidgetInfo &info)
{
    std::string out(static_cast<std::size_t>(std::max(info.depth, 0)) * 2, ' ');
    out += info.type;
    if (!info.id.empty())
        out += " #" + info.id;
    out += " [" + info.style + "] " + number(info.rect.position.x) + "," + number(info.rect.position.y) + " " + number(info.rect.size.x) +
           "x" + number(info.rect.size.y);
    if (!info.visible)
        out += " (hidden)";
    return out;
}

std::string describeEntity(const scene::World &world, scene::Entity entity)
{
    std::string out = "#" + std::to_string(entity.index) + "." + std::to_string(entity.generation);
    if (!world.alive(entity))
        return out + "  (destroyed)";
    std::string names;
    for (const scene::ComponentId id : world.componentIds())
    {
        if (!world.has(entity, id))
            continue;
        const scene::ComponentDesc *desc = world.componentDesc(id);
        names += (names.empty() ? "" : ", ") + (desc != nullptr ? desc->name : std::string("?"));
    }
    return out + "  " + (names.empty() ? std::string("(no component)") : names);
}

std::unique_ptr<Widget> entityPanel(scene::World &world, scene::Entity entity)
{
    auto box = std::make_unique<VBox>();
    if (!world.alive(entity))
    {
        box->emplace<Label>("Entity destroyed", "debugText");
        return box;
    }
    box->emplace<Label>("Entity #" + std::to_string(entity.index) + "." + std::to_string(entity.generation), "debugText");
    for (const scene::ComponentId id : world.componentIds())
    {
        if (!world.has(entity, id))
            continue;
        const scene::ComponentDesc *desc = world.componentDesc(id);
        if (desc == nullptr)
            continue;
        auto &section = box->emplace<Section>(desc->name);
        section.setId(desc->name);
        if (desc->fields.empty())
        {
            section.body().emplace<Label>(std::to_string(desc->size) + " bytes (no described field)", "debugText");
            continue;
        }
        // Relu à chaque accès : l'adresse change quand un composant du même type est ajouté ou retiré.
        scene::World *w = &world;
        section.body().add(reflectedPanel([w, entity, id] { return w->get(entity, id); }, desc->fields, desc->name + "."));
    }
    return box;
}

bool debugTabFromName(std::string_view name, DebugTab &out)
{
    static constexpr std::pair<std::string_view, DebugTab> kNames[] = {
        {"console", DebugTab::kConsole}, {"widgets", DebugTab::kWidgets}, {"entities", DebugTab::kEntities}, {"profiler", DebugTab::kProfiler}};
    for (const auto &[n, tab] : kNames)
    {
        if (n == name)
        {
            out = tab;
            return true;
        }
    }
    return false;
}

// ── DebugTools ─────────────────────────────────────────────────────────────────────────────────────────────────────────

DebugTools::DebugTools(const Font &font) : ui_(std::make_unique<Ui>(font)) { build(); }

DebugTools::~DebugTools() = default;

void DebugTools::build()
{
    auto root = std::make_unique<Overlay>();
    auto &panel = root->emplace<VBox>("debugPanel");
    panel.setWidth(SizeSpec::fixed(480.0f)).setHeight(SizeSpec::fill()).setAlign(Align::kEnd, Align::kStretch);
    panel.setId("debugTools");
    auto &tabs = panel.emplace<Tabs>(Binding<int>::ref(tab_));
    tabs.setHeight(SizeSpec::fill());
    tabs.setId("debugTabs");

    // Console : les lignes les plus récentes, puis la saisie.
    {
        auto page = std::make_unique<VBox>();
        page->setHeight(SizeSpec::fill());
        auto &lines = page->emplace<VBox>();
        lines.setHeight(SizeSpec::fill());
        lines.setId("debugConsoleLines");
        for (std::size_t i = 0; i < kConsoleRows; ++i)
        {
            consoleRows_.push_back(&lines.emplace<Label>(std::string(), "debugText"));
        }
        page->emplace<ConsoleInput>(console_).setWidth(SizeSpec::fill()).setId("debugConsoleInput");
        tabs.addPage("Console", std::move(page));
    }
    // Widgets (UI-10).
    {
        auto page = std::make_unique<VBox>();
        page->setHeight(SizeSpec::fill());
        page->emplace<Label>(std::string(), "debugText").bindText([this] { return std::to_string(tree_.size()) + " widgets"; });
        auto &scroll = page->emplace<ScrollView>();
        scroll.setHeight(SizeSpec::fill()).setWidth(SizeSpec::fill());
        treeList_ = &scroll.emplace<VBox>();
        treeList_->setId("debugWidgetTree");
        tabs.addPage("Widgets", std::move(page));
    }
    // Entités (UI-04).
    {
        auto page = std::make_unique<VBox>();
        page->setHeight(SizeSpec::fill());
        page->emplace<Label>(std::string(), "debugText")
            .bindText([this] { return world_ == nullptr ? std::string("No world") : std::to_string(entities_.size()) + " entities"; });
        auto &list = page->emplace<ScrollView>();
        list.setHeight(SizeSpec::fill(1.0f)).setWidth(SizeSpec::fill());
        entityList_ = &list.emplace<VBox>();
        entityList_->setId("debugEntityList");
        page->emplace<Separator>();
        auto &body = page->emplace<ScrollView>();
        body.setHeight(SizeSpec::fill(2.0f)).setWidth(SizeSpec::fill());
        entityBody_ = &body.emplace<VBox>();
        entityBody_->setId("debugEntity");
        tabs.addPage("Entities", std::move(page));
    }
    // Profileur.
    {
        auto page = std::make_unique<VBox>();
        page->setHeight(SizeSpec::fill());
        page->emplace<Label>(std::string(), "debugText").bindText([this] { return frameText_; });
        page->emplace<Plot>([this] { return frames_.cpu(); }).setWidth(SizeSpec::fill()).setHeight(SizeSpec::fixed(70.0f)).setId("debugFramePlot");
        auto &scroll = page->emplace<ScrollView>();
        scroll.setHeight(SizeSpec::fill()).setWidth(SizeSpec::fill());
        profilerList_ = &scroll.emplace<VBox>();
        profilerList_->setId("debugProfiler");
        tabs.addPage("Profiler", std::move(page));
    }
    ui_->setRoot(std::move(root));
}

void DebugTools::open() { open_ = true; }

void DebugTools::close()
{
    open_ = false;
    ui_->focus(nullptr);
    ui_->closePopup();
    hoveredRow_ = -1;
    highlight_ = {};
}

void DebugTools::selectTab(DebugTab tab) { tab_ = std::clamp(static_cast<int>(tab), 0, 3); }

void DebugTools::inspectWorld(scene::World *world)
{
    world_ = world;
    selectedIndex_ = scene::Entity::kInvalidIndex;
    selectedSignature_.clear();
    entities_.clear();
    if (entityBody_ != nullptr)
        entityBody_->clearChildren();
}

void DebugTools::selectEntity(scene::Entity entity)
{
    selectedIndex_ = entity.index;
    selectedGeneration_ = entity.generation;
    selectedSignature_ = "\x01"; // force la reconstruction du panneau
}

scene::Entity DebugTools::selectedEntity() const noexcept
{
    return selectedIndex_ == scene::Entity::kInvalidIndex ? scene::kNullEntity : scene::Entity{selectedIndex_, selectedGeneration_};
}

bool DebugTools::handleToggle(const InputFrame &input)
{
    const bool pressed = std::find(input.shortcuts.begin(), input.shortcuts.end(), kToggleShortcut) != input.shortcuts.end();
    if (pressed)
        toggle();
    return pressed;
}

void DebugTools::update(const InputFrame &input, glm::vec2 viewport)
{
    if (!open_)
        return; // UI-13 : fermés, rien n'est lu ni calculé
    (void)console_.pollLog();
    switch (tab())
    {
    case DebugTab::kConsole: refreshConsole(); break;
    case DebugTab::kWidgets: refreshWidgets(); break;
    case DebugTab::kEntities: refreshEntities(); break;
    case DebugTab::kProfiler: refreshProfiler(input.deltaTime); break;
    }
    InputFrame own = input;
    // F12 bascule les outils : il n'est pas proposé à leurs widgets.
    std::erase(own.shortcuts, std::string(kToggleShortcut));
    ui_->update(own, viewport);

    // Surlignage (UI-10) : ligne survolée, sinon ligne choisie, sinon widget survolé de l'interface inspectée.
    highlight_ = {};
    if (tab() != DebugTab::kWidgets)
        return;
    const int row = hoveredRow_ >= 0 ? hoveredRow_ : pinnedRow_;
    if (row >= 0 && static_cast<std::size_t>(row) < tree_.size())
        highlight_ = {true, tree_[static_cast<std::size_t>(row)].rect};
    else if (target_ != nullptr && target_->hovered() != nullptr)
        highlight_ = {true, target_->hovered()->rect()};
}

void DebugTools::draw(renderer::DrawList &list) const
{
    if (!open_)
        return; // UI-13 : aucune commande de dessin
    if (highlight_.has)
        Widget::drawBox(list, highlight_.rect, ui_->theme().style("debugHighlight", WidgetState::kNormal));
    ui_->draw(list);
}

void DebugTools::refreshConsole()
{
    if (console_.revision() == consoleRevision_)
        return;
    consoleRevision_ = console_.revision();
    const auto &lines = console_.lines();
    const std::size_t shown = std::min(lines.size(), consoleRows_.size());
    const std::size_t first = lines.size() - shown;
    for (std::size_t i = 0; i < consoleRows_.size(); ++i)
    {
        auto *label = static_cast<Label *>(consoleRows_[i]);
        if (i < shown)
        {
            const ConsoleLine &line = lines[first + i];
            label->setText(line.text);
            label->setStyleName(lineStyle(line.kind));
            label->setVisible(true);
        }
        else
        {
            label->setText(std::string());
            label->setVisible(false);
        }
    }
}

void DebugTools::refreshWidgets()
{
    tree_.clear();
    if (target_ != nullptr && target_->root() != nullptr)
        tree_ = describeWidgetTree(*target_->root());
    treeText_.resize(tree_.size());
    for (std::size_t i = 0; i < tree_.size(); ++i)
        treeText_[i] = describeWidget(tree_[i]);
    if (pinnedRow_ >= static_cast<int>(tree_.size()))
        pinnedRow_ = -1;
    // Lignes : une par widget, réutilisées (seul leur nombre change la structure).
    const std::size_t have = treeList_->children().size();
    if (have != tree_.size())
    {
        if (have > tree_.size())
        {
            treeList_->clearChildren();
            hoveredRow_ = -1;
        }
        for (std::size_t i = treeList_->children().size(); i < tree_.size(); ++i)
        {
            const int index = static_cast<int>(i);
            auto &row = treeList_->emplace<Row>([this, i] { return i < treeText_.size() ? treeText_[i] : std::string(); });
            row.onHover([this, index](const Event &e) { hoveredRow_ = e.active ? index : (hoveredRow_ == index ? -1 : hoveredRow_); });
            row.onClick([this, index](const Event &) { pinnedRow_ = pinnedRow_ == index ? -1 : index; });
        }
    }
    for (std::size_t i = 0; i < treeList_->children().size(); ++i)
        treeList_->children()[i]->setStyleName(static_cast<int>(i) == pinnedRow_ ? "debugRowSelected" : "debugRow");
}

void DebugTools::refreshEntities()
{
    entities_.clear();
    if (world_ != nullptr)
    {
        entities_ = world_->entities();
        if (entities_.size() > kMaxEntityRows)
            entities_.resize(kMaxEntityRows);
    }
    entityText_.resize(entities_.size());
    for (std::size_t i = 0; i < entities_.size(); ++i)
        entityText_[i] = describeEntity(*world_, entities_[i]);
    if (entityList_->children().size() != entities_.size())
    {
        entityList_->clearChildren();
        for (std::size_t i = 0; i < entities_.size(); ++i)
        {
            auto &row = entityList_->emplace<Row>([this, i] { return i < entityText_.size() ? entityText_[i] : std::string(); });
            row.setId("debugEntity" + std::to_string(i));
            row.onClick(
                [this, i](const Event &)
                {
                    if (i < entities_.size())
                        selectEntity(entities_[i]);
                });
        }
    }
    const scene::Entity selected = selectedEntity();
    for (std::size_t i = 0; i < entityList_->children().size(); ++i)
        entityList_->children()[i]->setStyleName(i < entities_.size() && entities_[i] == selected ? "debugRowSelected" : "debugRow");
    // Panneau de l'entité choisie : refait quand elle change ou quand ses composants changent.
    const std::string signature = world_ != nullptr && !selected.isNull() ? "e" + componentSignature(*world_, selected) : std::string();
    if (signature != selectedSignature_)
    {
        selectedSignature_ = signature;
        entityBody_->clearChildren();
        if (world_ != nullptr && !selected.isNull())
            entityBody_->add(entityPanel(*world_, selected));
    }
}

void DebugTools::refreshProfiler(float deltaTime)
{
    sinceProfilerRefresh_ += deltaTime;
    if (sinceProfilerRefresh_ < kProfilerRefreshSeconds)
        return;
    sinceProfilerRefresh_ = 0.0f;
    const FpsStats fps = frames_.fps();
    frameText_ = frames_.size() == 0 ? std::string("Frame: no sample")
                                     : formatMs("Frame CPU %.2f ms   FPS %.0f", frames_.averageCpuMs(), fps.average);
    const std::vector<std::string> names = core::Profiler::Names();
    profilerText_.resize(names.size());
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        const double average = core::Profiler::GetAverageTimeMs(names[i]);
        const double max = static_cast<double>(core::Profiler::GetMaxTime(names[i]).count()) * 1e-6;
        profilerText_[i] = names[i] + formatMs("   avg %.3f ms   max %.3f ms", average, max);
    }
    if (profilerList_->children().size() != names.size())
    {
        profilerList_->clearChildren();
        for (std::size_t i = 0; i < names.size(); ++i)
            profilerList_->emplace<Label>(std::string(), "debugText").bindText([this, i] { return i < profilerText_.size() ? profilerText_[i] : std::string(); });
    }
}

} // namespace orosmo::ui::debug
