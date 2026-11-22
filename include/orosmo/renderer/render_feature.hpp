// REN-01 : plugins de rendu (docs/03 §11, « render features »). Un plugin ajoute ses passes au graphe de l'image sans modifier le
// cœur du renderer : ombres, post-traitements, interface, et plus tard voxels ou ray tracing (règle 14). `SceneRenderer` appelle,
// à chaque `endFrame`, `prepare` puis `declarePasses` de chaque plugin enregistré, APRÈS avoir déclaré ses propres passes (scène,
// post-traitements, mise à l'échelle, génération d'images, historique).
#pragma once

#include "orosmo/renderer/aov.hpp"
#include "orosmo/renderer/frame_data.hpp"
#include "orosmo/renderer/render_graph.hpp"
#include "orosmo/rhi/capabilities.hpp"

#include <array>
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

    RenderGraphBuilder(RenderGraph &graph, const Resources &resources, const rhi::Capabilities &capabilities, std::string scenePass)
        : graph_(graph), resources_(resources), capabilities_(capabilities), scenePass_(std::move(scenePass))
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

    // Écrire APRÈS la mise à l'échelle (interface, superpositions) : une passe déclarée ici qui écrit `resources().output` suit
    // l'écriture de la mise à l'échelle (écrivains dans l'ordre de déclaration). Écrire AVANT (post-traitement de la scène) :
    // une passe qui écrit `resources().sceneColor` ; la mise à l'échelle la lit avec `readFinal`, donc après elle.

  private:
    RenderGraph &graph_;
    Resources resources_;
    const rhi::Capabilities &capabilities_;
    std::string scenePass_;
};

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
    // Données de l'image (caméra, temps, lumière principale), avant `declarePasses`. Un plugin peut y lire ce qu'il prépare
    // côté CPU (culling, mises à jour de tampons).
    virtual core::Result<> prepare(const FrameData &frame)
    {
        (void)frame;
        return {};
    }
    // Ajoute ses passes et ses ressources au graphe de l'image.
    virtual core::Result<> declarePasses(RenderGraphBuilder &builder) = 0;
};

} // namespace orosmo::renderer
