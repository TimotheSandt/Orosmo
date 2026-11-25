// Modèle importé (ASSET-01) : données CPU d'un fichier glTF 2.0, sans GPU. Maillages, matériaux métal/rugosité, textures et
// images, nœuds, caméras, lumières KHR_lights_punctual, peaux et animations, scènes. Les indices (`int`) renvoient aux tableaux
// du `Model` ; -1 = absent. Repère et unités de glTF : Y en haut, mètres, quaternions (x, y, z, w), matrices en colonnes.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/core/math.hpp"

#include <glm/gtc/type_precision.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace orosmo::assets
{

// Un ensemble de triangles d'un seul matériau. Attributs absents = tableaux vides ; sinon même longueur que `positions`.
struct MeshPrimitive
{
    std::vector<core::Vec3> positions;
    std::vector<core::Vec3> normals;
    std::vector<core::Vec4> tangents; // w = signe de la bitangente
    std::vector<core::Vec2> uv0;
    std::vector<core::Vec2> uv1;
    std::vector<core::Vec4> colors;
    std::vector<glm::u16vec4> joints; // indices dans `SkinData::joints`
    std::vector<core::Vec4> weights;  // somme 1
    std::vector<std::uint32_t> indices; // triangles ; les primitives non indexées reçoivent 0, 1, 2…
    int material = -1;
    core::Vec3 boundsMin{0.0f};
    core::Vec3 boundsMax{0.0f};
};

struct MeshData
{
    std::string name;
    std::vector<MeshPrimitive> primitives;
};

enum class WrapMode : std::uint8_t
{
    kRepeat,
    kClampToEdge,
    kMirroredRepeat,
};

struct SamplerData
{
    WrapMode wrapS = WrapMode::kRepeat;
    WrapMode wrapT = WrapMode::kRepeat;
    bool nearest = false; // filtre « plus proche voisin » demandé par le fichier
};

// Image d'un fichier : soit externe (`path` : absolu après `loadGltf`/`parseGltf` ; relatif, il est relatif au dossier du fichier écrit par `saveGltf`), soit incluse (`encoded`, PNG ou JPG).
struct ImageData
{
    std::string name;
    std::filesystem::path path;
    std::vector<std::byte> encoded;
    std::string mimeType;
};

struct TextureData
{
    int image = -1;
    int sampler = -1;
};

struct TextureRef
{
    int texture = -1;
    int texCoord = 0;
    float scale = 1.0f; // échelle de la normale ou force de l'occlusion
    bool valid() const noexcept { return texture >= 0; }
};

enum class AlphaMode : std::uint8_t
{
    kOpaque,
    kMask,
    kBlend,
};

struct MaterialData
{
    std::string name;
    core::Vec4 baseColorFactor{1.0f};
    TextureRef baseColor;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    TextureRef metallicRoughness; // B = métal, G = rugosité
    TextureRef normal;
    TextureRef occlusion; // R
    TextureRef emissive;
    core::Vec3 emissiveFactor{0.0f};
    float emissiveStrength = 1.0f; // KHR_materials_emissive_strength
    AlphaMode alphaMode = AlphaMode::kOpaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    bool unlit = false; // KHR_materials_unlit
};

struct CameraData
{
    std::string name;
    bool perspective = true;
    float yfov = 0.8f;
    float aspectRatio = 0.0f; // 0 = celui de la fenêtre
    float znear = 0.1f;
    float zfar = 0.0f; // 0 = infini
    float xmag = 1.0f;
    float ymag = 1.0f;
};

enum class LightKind : std::uint8_t
{
    kDirectional,
    kPoint,
    kSpot,
};

// KHR_lights_punctual : la lumière éclaire vers -Z de son nœud. Intensité en candelas (ponctuelle, spot) ou lux (directionnelle).
struct LightData
{
    std::string name;
    LightKind kind = LightKind::kPoint;
    core::Vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 0.0f; // 0 = infinie
    float innerConeAngle = 0.0f;
    float outerConeAngle = 0.7853982f;
};

struct NodeData
{
    std::string name;
    int parent = -1;
    std::vector<int> children;
    core::Vec3 translation{0.0f};
    core::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    core::Vec3 scale{1.0f};
    int mesh = -1;
    int skin = -1;
    int camera = -1;
    int light = -1;

    core::Mat4 localMatrix() const;
};

struct SkinData
{
    std::string name;
    std::vector<int> joints;                     // nœuds
    std::vector<core::Mat4> inverseBindMatrices; // un par articulation (identité si le fichier n'en donne pas)
    int skeleton = -1;
};

enum class AnimationPath : std::uint8_t
{
    kTranslation,
    kRotation,
    kScale,
    kWeights,
};

enum class Interpolation : std::uint8_t
{
    kLinear,
    kStep,
    kCubicSpline,
};

// Une piste : `times` croissants (secondes) ; `values` à plat, `componentCount` flottants par clé (3 pour translation et échelle,
// 4 pour la rotation) ; en kCubicSpline, trois groupes par clé (tangente d'entrée, valeur, tangente de sortie).
struct AnimationChannel
{
    int node = -1;
    AnimationPath path = AnimationPath::kTranslation;
    Interpolation interpolation = Interpolation::kLinear;
    std::uint32_t componentCount = 3;
    std::vector<float> times;
    std::vector<float> values;
};

struct AnimationData
{
    std::string name;
    std::vector<AnimationChannel> channels;
    float duration = 0.0f; // dernière clé de toutes les pistes
};

struct SceneData
{
    std::string name;
    std::vector<int> roots;
};

struct ModelStats
{
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    std::size_t primitives = 0;
};

struct Model
{
    std::vector<MeshData> meshes;
    std::vector<MaterialData> materials;
    std::vector<TextureData> textures;
    std::vector<ImageData> images;
    std::vector<SamplerData> samplers;
    std::vector<CameraData> cameras;
    std::vector<LightData> lights;
    std::vector<NodeData> nodes;
    std::vector<SkinData> skins;
    std::vector<AnimationData> animations;
    std::vector<SceneData> scenes;
    int defaultScene = -1;

    // Matrices monde de tous les nœuds (parent avant enfant), à partir de leurs TRS.
    std::vector<core::Mat4> worldMatrices() const;
    ModelStats stats() const;
    // Vérifie que tous les indices sont dans les bornes et que les attributs ont la bonne longueur. kParse sinon.
    core::Result<> validate() const;
};

// `.gltf` (JSON, tampons et images externes ou en data URI) ou `.glb` (binaire). Les images externes ne sont pas lues : leur chemin
// est rendu absolu. Erreurs : kIo (fichier), kParse (contenu invalide), kUnsupported (extension requise inconnue, Draco…).
core::Result<Model> loadGltf(const std::filesystem::path &path);
// Contenu en mémoire ; `baseDir` résout les URI relatives ; `name` sert aux messages.
core::Result<Model> parseGltf(std::span<const std::byte> bytes, const std::filesystem::path &baseDir, std::string_view name);
// Écrit le modèle en `.glb` (un seul fichier, images incluses) ou en `.gltf` (JSON + `<nom>.bin` à côté ; les images incluses
// sont mises dans le .bin). Le format suit l'extension de `path`. Sert à l'aller-retour des tests et aux modèles générés.
core::Result<> saveGltf(const Model &model, const std::filesystem::path &path);

} // namespace orosmo::assets
