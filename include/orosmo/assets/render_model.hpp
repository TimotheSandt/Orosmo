// Modèle importé prêt à dessiner (ASSET-01 + REN-10) : chaque primitive devient un `renderer::Mesh` aux sommets PBR, chaque matériau
// glTF un `renderer::PbrMaterial` (textures par le `TextureCache` : couleur et émission en sRGB, le reste en linéaire), et chaque
// nœud qui porte un maillage un dessin. Une primitive dessinée par plusieurs nœuds garde un seul jeu de tampons (une tranche du
// modèle par dessin, `SceneRenderer::recordModelSlice`).
#pragma once

#include "orosmo/assets/model.hpp"
#include "orosmo/assets/texture_cache.hpp"
#include "orosmo/core/error.hpp"
#include "orosmo/renderer/blend_mode.hpp"
#include "orosmo/renderer/camera.hpp"
#include "orosmo/renderer/material.hpp"
#include "orosmo/renderer/mesh.hpp"
#include "orosmo/renderer/scene_renderer.hpp"

#include <memory>
#include <string>
#include <vector>

namespace orosmo::assets
{

// Mode alpha de glTF → mode de fusion du renderer (REN-37) : OPAQUE → kOpaque, MASK → kMask, BLEND → kBlend.
renderer::BlendMode blendModeForAlpha(AlphaMode mode) noexcept;

// Matériau glTF → description PBR ; les textures viennent de `textures` (clé des images incluses : `sourceKey#imageN`).
core::Result<renderer::PbrMaterialDesc> pbrMaterialDesc(const Model &model, const MaterialData &material, TextureCache &textures,
                                                       const std::string &sourceKey);

// Sommets entrelacés de `renderer::kPbrVertexAttributeSizes` (16 flottants). Normales absentes : calculées par face et lissées
// par sommet ; tangentes absentes : calculées depuis les UV (repli (1, 0, 0, 1) sans UV) ; couleur absente : blanc.
std::vector<float> pbrVertices(const MeshPrimitive &primitive);

struct RenderModelOptions
{
    const renderer::ShadowFeature *shadows = nullptr; // récepteur d'ombres si non nul
    std::vector<renderer::Define> extraDefines;        // ex. aovShaderDefines, forwardPlusDefines
    std::string sourceKey = "model";                   // nom du modèle dans le cache de textures et les noms de debug
    int scene = -1;                                    // -1 : scène par défaut (toutes les racines si le fichier n'en a pas)
};

class RenderModel
{
  public:
    static core::Result<RenderModel> create(rhi::Device &device, const Model &model, TextureCache &textures, renderer::PbrShaderCache &shaders,
                                            std::shared_ptr<const renderer::PbrDefaults> defaults, const RenderModelOptions &options = {});

    // Matrice qui place tout le modèle dans le monde.
    void setRootTransform(const glm::mat4 &root) noexcept { root_ = root; }
    // Matrices monde des nœuds à utiliser (pose animée, `Model::worldMatrices` d'un modèle modifié) ; taille = nombre de nœuds.
    core::Result<> setNodeMatrices(std::vector<glm::mat4> world);
    // Enregistre un dessin par (nœud, primitive) de la scène choisie.
    core::Result<> draw(renderer::SceneRenderer &scene, const renderer::Camera &camera);

    std::size_t drawCount() const noexcept { return items_.size(); }
    std::size_t meshCount() const noexcept { return meshes_.size(); }
    const std::vector<std::shared_ptr<renderer::PbrMaterial>> &materials() const noexcept { return materials_; }
    renderer::Mesh &mesh(std::size_t i) { return *meshes_[i]; }

  private:
    struct Item
    {
        int node = -1;
        std::size_t mesh = 0; // dans meshes_
    };
    std::vector<std::unique_ptr<renderer::Mesh>> meshes_; // une par primitive (unique_ptr : un Mesh déplacé garde ses tampons)
    std::vector<std::shared_ptr<renderer::PbrMaterial>> materials_;
    std::vector<Item> items_;
    std::vector<glm::mat4> world_;
    glm::mat4 root_{1.0f};
};

} // namespace orosmo::assets
