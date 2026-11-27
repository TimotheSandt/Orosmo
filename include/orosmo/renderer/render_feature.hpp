// REN-01 : plugins de rendu (docs/03 §11, « render features »). Un plugin ajoute ses passes au graphe de l'image sans modifier le
// cœur du renderer : ombres, post-traitements, interface, et plus tard voxels ou ray tracing (règle 14). `SceneRenderer` appelle,
// à chaque `endFrame`, `prepare` de chaque plugin enregistré, puis (quand le graphe est reconstruit, A61 C : voir `passSignature`)
// `declarePasses` de chacun, APRÈS avoir déclaré ses propres passes (scène, mise à l'échelle, génération d'images, historique) et
// AVANT ses `IPostProcessPass`, puis `frameEnded` de chacun.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/aov.hpp"
#include "orosmo/renderer/frame_data.hpp"
#include "orosmo/renderer/render_graph.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/rhi/capabilities.hpp"
#include "orosmo/rhi/types.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orosmo::renderer
{

// Vue du graphe de l'image donnée aux plugins : le graphe lui-même et les ressources connues du renderer.
class RenderGraphBuilder
{
  public:
    struct Resources
    {
        RgTexture sceneColor;    // couleur de la scène (résolution de rendu)
        RgTexture sceneDepth;    // profondeur de la scène (invalide si absente)
        RgTexture motionVectors; // vecteurs de mouvement (invalide s'ils ne sont pas activés)
        RgTexture output;        // destination de l'image (résolution de sortie)
        std::array<RgTexture, kAovCount> aovs{}; // AOV de la passe de scène (REN-21) ; invalides s'ils ne sont pas actifs
        rhi::Extent2D renderSize{};
        rhi::Extent2D outputSize{};
    };

    // `frameBindings` : liaisons que les plugins ajoutent au groupe 0 de l'image (nul : `addFrameBinding` est refusé).
    RenderGraphBuilder(RenderGraph &graph, const Resources &resources, const rhi::Capabilities &capabilities, std::string scenePass,
                       std::vector<rhi::BindGroupEntry> *frameBindings = nullptr)
        : graph_(graph), resources_(resources), capabilities_(capabilities), scenePass_(std::move(scenePass)), frameBindings_(frameBindings)
    {
    }

    RenderGraph &graph() noexcept { return graph_; }
    const Resources &resources() const noexcept { return resources_; }
    const rhi::Capabilities &capabilities() const noexcept { return capabilities_; }

    // Texture d'un AOV (invalide si personne ne l'a demandé : voir `RenderFeature::requiredAovs`).
    RgTexture aov(Aov which) const noexcept { return resources_.aovs[static_cast<std::size_t>(which)]; }

    // La passe de scène lit `texture` : la passe qui l'écrit s'exécute avant la scène et n'est pas écartée (ex. carte d'ombre,
    // REN-06 ; la texture est ensuite liée au groupe 1 des matériaux qui la lisent).
    void addSceneInput(RgTexture texture)
    {
        if (auto scene = graph_.pass(scenePass_))
        {
            scene->readFinal(texture); // déclarée avant les plugins : sans readFinal, elle lirait le contenu d'avant l'image
        }
    }
    // Même chose pour un tampon (ex. les clusters de lumières du forward+, REN-04, lus par les matériaux).
    void addSceneInput(RgBuffer buffer)
    {
        if (auto scene = graph_.pass(scenePass_))
        {
            scene->readFinal(buffer);
        }
    }

    // Ajoute une liaison au groupe 0 de l'image (caméra 0, lumières 1, FrameData 2 sont au renderer ; liaisons
    // kFirstFeatureFrameBinding à 7 : plugins). Liée à la passe de scène et à toute passe qui utilise `ctx.frame()->group`, pour
    // cette image. Refusée (kInvalidArgument) hors de [kFirstFeatureFrameBinding, rhi::kBindingsPerGroup), déjà prise, ou sans
    // renderer qui l'accepte. Exemple : les tampons de clusters du forward+ (liaisons 3 et 4).
    static constexpr std::uint32_t kFirstFeatureFrameBinding = 3;
    core::Result<> addFrameBinding(const rhi::BindGroupEntry &entry)
    {
        if (frameBindings_ == nullptr)
        {
            return core::makeError(core::ErrorCode::kInvalidArgument, "addFrameBinding: this renderer does not take frame bindings");
        }
        if (entry.binding < kFirstFeatureFrameBinding || entry.binding >= rhi::kBindingsPerGroup)
        {
            return core::makeError(core::ErrorCode::kInvalidArgument, "addFrameBinding: binding " + std::to_string(entry.binding) + " is reserved or out of range");
        }
        for (const rhi::BindGroupEntry &existing : *frameBindings_)
        {
            if (existing.binding == entry.binding)
            {
                return core::makeError(core::ErrorCode::kInvalidArgument, "addFrameBinding: binding " + std::to_string(entry.binding) + " is already taken");
            }
        }
        frameBindings_->push_back(entry);
        return {};
    }

    // Écrire APRÈS la mise à l'échelle (interface, superpositions) : une passe déclarée ici qui écrit `resources().output` suit
    // l'écriture de la mise à l'échelle (écrivains dans l'ordre de déclaration). Écrire AVANT (post-traitement de la scène) :
    // une passe qui écrit `resources().sceneColor` ; la mise à l'échelle la lit avec `readFinal`, donc après elle.

  private:
    RenderGraph &graph_;
    Resources resources_;
    const rhi::Capabilities &capabilities_;
    std::string scenePass_;
    std::vector<rhi::BindGroupEntry> *frameBindings_ = nullptr;
};

// A61 C : mélange une valeur dans une signature de passes (`RenderFeature::passSignature`) ; même suite, même résultat.
constexpr std::uint64_t mixPassSignature(std::uint64_t seed, std::uint64_t value) noexcept
{
    std::uint64_t z = seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; // finaliseur de splitmix64
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
template <typename Handle> constexpr std::uint64_t mixPassSignature(std::uint64_t seed, Handle handle) noexcept
    requires requires { handle.index; handle.generation; }
{
    return mixPassSignature(mixPassSignature(seed, std::uint64_t{handle.index}), std::uint64_t{handle.generation});
}

class RenderFeature
{
  public:
    virtual ~RenderFeature() = default;
    virtual std::string name() const = 0;
    // Faux : le plugin est refusé à l'enregistrement (capacité optionnelle absente, docs/03 §3 règle 4).
    virtual bool supports(const rhi::Capabilities &capabilities) const noexcept
    {
        (void)capabilities;
        return true;
    }
    // REN-21 : AOV que ce plugin lit. Ils sont ajoutés à la passe de scène (cibles supplémentaires) tant que le plugin est
    // enregistré ; sans plugin ni demande, aucune cible de plus n'existe.
    virtual AovSet requiredAovs() const { return {}; }
    // REN-19 : vrai si ce plugin veut une caméra décalée d'un sous-pixel à chaque image (suite de Halton du renderer, en mode
    // `JitterMode::kAuto`), comme un mode de mise à l'échelle temporel. Lu à `beginFrame` ; faux par défaut (aucun décalage).
    virtual bool wantsJitter() const noexcept { return false; }
    // A75.3 : définitions de préprocesseur que ce plugin demande aux programmes de maillage (ex. OROSMO_FORWARD_PLUS). Réunies par
    // `SceneRenderer::shaderDefines()`, dans l'ordre d'enregistrement des plugins ; aucune par défaut.
    virtual std::vector<Define> shaderDefines() const { return {}; }
    // A63 : vrai si ce plugin, à cette image, modifie ou échantillonne la couleur de scène après la passe de scène (effets de
    // post-traitement). La scène n'est alors pas dessinée droit dans la sortie : l'effet garde sa place avant la superposition
    // (interface) et la couleur qu'il lit reste échantillonnable. Faux par défaut.
    virtual bool postProcessesScene() const noexcept { return false; }
    // Données de l'image (caméra, temps, lumière principale), avant `declarePasses`. Un plugin peut y lire ce qu'il prépare
    // côté CPU (culling, mises à jour de tampons).
    virtual core::Result<> prepare(const FrameData &frame)
    {
        (void)frame;
        return {};
    }
    // Ajoute ses passes et ses ressources au graphe de l'image.
    virtual core::Result<> declarePasses(RenderGraphBuilder &builder) = 0;
    // A61 C : graphe de passes gardé d'une image à l'autre. Résumé de ce que `declarePasses` déclarerait à cette image (passes et
    // accès, ressources importées ET leurs ressources GPU, liaisons `addFrameBinding`), lu après `prepare`. Tant que chaque plugin
    // rend la même valeur qu'à l'image précédente (et que plugins, cibles et tailles du renderer ne changent pas), le renderer
    // garde le graphe : `declarePasses` n'est PAS rappelé, les passes déjà déclarées s'exécutent de nouveau. Leurs fonctions lisent
    // donc l'état de l'image au moment de l'exécution (membres du plugin, `ctx.texture`, `ctx.frame()`), jamais une valeur de
    // l'image de déclaration copiée dans la capture, et `declarePasses` ne fait aucun travail par image (envoi, création d'un
    // groupe : dans `prepare` ou dans la passe). `std::nullopt` (défaut, plugins d'avant M8) : pas de cache, le graphe entier est
    // reconstruit à chaque image. Voir `mixPassSignature`.
    virtual std::optional<std::uint64_t> passSignature() const { return std::nullopt; }
    // Fin de chaque `SceneRenderer::endFrame`, réussi ou non (après l'exécution du graphe, ou après l'échec qui l'a empêchée) : le
    // plugin oublie ce qui ne valait que pour cette image (dépôts, placements), pour que rien ne survive à l'image. Rien par défaut.
    virtual void frameEnded() noexcept {}
};

} // namespace orosmo::renderer
