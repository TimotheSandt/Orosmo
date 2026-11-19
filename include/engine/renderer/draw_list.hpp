// Liste de dessin 2D (UI-02) : ce que l'interface produit et que `CanvasRenderer` exécute. Données pures : aucune dépendance au RHI, à
// OpenGL ni à GLFW. Coordonnées logiques en pixels, origine en HAUT à gauche, Y vers le bas ; l'ordre des commandes est l'ordre de
// dessin (z).
#pragma once

#include <glm/glm.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace engine::renderer
{

// Atlas de glyphes (RGBA8, MSDF) partagé entre le producteur de texte et le dessinateur ; `id` identifie le contenu (un nouvel atlas
// = un nouvel id), ce qui permet au dessinateur de garder sa texture GPU d'une image à l'autre.
struct AtlasImage
{
    std::uint64_t id = 0;
    // Incrémentée par le producteur quand les pixels changent (image produite par le jeu à chaque image : mini-carte…) : le
    // dessinateur renvoie alors les pixels dans la même texture (même taille) au lieu d'en créer une.
    std::uint64_t version = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba; // width * height * 4 ; la ligne i correspond à v = i / height (la ligne 0 à v = 0)
};

// Identifiant neuf pour une `AtlasImage` (atlas de glyphes, images de thème) : unique dans le processus.
inline std::uint64_t newAtlasId() noexcept
{
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1);
}

// Rectangle plein. Forme facultative (UI-07) : coins arrondis (`radius`), bordure intérieure (`borderWidth`, `borderColor`) et
// bord adouci sur `softness` pixels (ombre portée : rectangle agrandi de `softness`, plein au centre, transparent au bord). Sans
// forme (les trois à 0), le rectangle est exactement la couleur, sans anticrénelage.
struct DrawRect
{
    glm::vec2 position{0.0f};
    glm::vec2 size{0.0f};
    glm::vec4 color{1.0f};
    float radius = 0.0f;
    float borderWidth = 0.0f;
    glm::vec4 borderColor{0.0f};
    float softness = 0.0f;
};

// Remplace les pixels du rectangle par `color` sans mélange (effacement partiel d'une couche : `color` = transparent).
struct DrawClearRect
{
    glm::vec2 position{0.0f};
    glm::vec2 size{0.0f};
    glm::vec4 color{0.0f};
};

// Triangles de glyphes : 4 flottants par sommet (x, y en pixels logiques ; u, v dans l'atlas), 6 sommets par glyphe.
struct DrawGlyphs
{
    std::shared_ptr<const AtlasImage> atlas;
    std::vector<float> vertices;
    glm::vec3 color{1.0f};
    float msdfPixelRange = 8.0f;
    float msdfWeight = 0.0f;
};

// Image texturée (UI-07, fonds en neuf parties) : 4 flottants par sommet (x, y en pixels logiques ; u, v dans l'image), 6 sommets
// par quadrilatère ; couleur = texel × `tint`. Une commande peut porter beaucoup de quadrilatères (un seul dessin).
struct DrawImage
{
    std::shared_ptr<const AtlasImage> image;
    std::vector<float> vertices;
    glm::vec4 tint{1.0f};
};

// Dessine une couche (voir BeginLayer) : `scrollOffset` / `contentSize` choisissent la partie visible du contenu de la couche, `color`
// est la couleur de fond sous les parties peu opaques.
struct DrawLayerImage
{
    std::uint64_t layerId = 0;
    glm::vec2 position{0.0f};
    glm::vec2 size{0.0f};
    glm::vec2 scrollOffset{0.0f};
    glm::vec2 contentSize{0.0f};
    glm::vec4 color{0.0f};
};

// Ouvre une couche hors écran de `size` pixels (les commandes suivantes y sont dessinées, avec cette taille comme taille logique)
// jusqu'à EndLayer. `clear` : la couche est d'abord effacée en transparent ; sinon son contenu précédent est conservé.
struct BeginLayer
{
    std::uint64_t id = 0;
    glm::vec2 size{0.0f};
    bool clear = true;
};
struct EndLayer
{
};

// Limite le dessin au rectangle (intersection avec les découpages ouverts) jusqu'à PopClip.
struct PushClip
{
    glm::vec2 position{0.0f};
    glm::vec2 size{0.0f};
};
struct PopClip
{
};

using DrawCommand = std::variant<DrawRect, DrawClearRect, DrawGlyphs, DrawLayerImage, BeginLayer, EndLayer, PushClip, PopClip, DrawImage>;

class DrawList
{
  public:
    // Taille logique de la surface sur laquelle les coordonnées sont exprimées (celle du conteneur racine).
    glm::vec2 logicalSize{0.0f};
    std::vector<DrawCommand> commands;

    void clear() { commands.clear(); }
    bool empty() const noexcept { return commands.empty(); }
    std::size_t size() const noexcept { return commands.size(); }

    void rect(const glm::vec2 &position, const glm::vec2 &size, const glm::vec4 &color)
    {
        commands.emplace_back(DrawRect{.position = position, .size = size, .color = color});
    }
    void rect(const DrawRect &shape) { commands.emplace_back(shape); }
    void clearRect(const glm::vec2 &position, const glm::vec2 &size) { commands.emplace_back(DrawClearRect{position, size, glm::vec4(0.0f)}); }
    void glyphs(DrawGlyphs run) { commands.emplace_back(std::move(run)); }
    void image(DrawImage image) { commands.emplace_back(std::move(image)); }
    void layerImage(const DrawLayerImage &image) { commands.emplace_back(image); }
    void beginLayer(std::uint64_t id, const glm::vec2 &size, bool clear = true) { commands.emplace_back(BeginLayer{id, size, clear}); }
    void endLayer() { commands.emplace_back(EndLayer{}); }
    void pushClip(const glm::vec2 &position, const glm::vec2 &size) { commands.emplace_back(PushClip{position, size}); }
    void popClip() { commands.emplace_back(PopClip{}); }

    template <typename T> std::size_t count() const
    {
        std::size_t n = 0;
        for (const DrawCommand &c : commands)
        {
            n += std::holds_alternative<T>(c) ? 1 : 0;
        }
        return n;
    }
};

} // namespace engine::renderer
