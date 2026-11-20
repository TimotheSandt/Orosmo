#include "World.h"

#include <engine/core/log.hpp>

#include <algorithm>
#include <vector>

namespace rd = engine::renderer;
namespace w = engine::world;

namespace
{
// Sélection des tuiles autour de la caméra : racines de 1 024 m (niveau 4 avec 64 quadrilatères de 1 m), une racine de part et
// d'autre de celle de la caméra, soit au moins 1 km de terrain dans toutes les directions (le plan lointain de la caméra).
constexpr w::LodConfig kLod{.rootLod = 4, .rootRadius = 1, .distanceFactor = 2.0f};
constexpr std::size_t kMemoryBudgetBytes = 128u * 1024u * 1024u;
// Capture : tours de génération au plus avant de renoncer à une sélection complète (en pratique 1 ou 2 suffisent).
constexpr int kMaxCompletionRounds = 64;
constexpr float kStartHeightAboveGround = 40.0f; // m
} // namespace

World::World() {}
World::~World() { this->Destroy(); }

w::TerrainParams World::TerrainParameters()
{
    // Valeurs par défaut du module (`TerrainParams`) : relief de Perlin à 6 octaves, 120 m d'amplitude, 1 m entre échantillons.
    return w::TerrainParams{};
}

glm::vec3 World::StartPosition(std::uint32_t seed)
{
    const float ground = w::heightAt(TerrainParameters(), seed, 0.0f, 0.0f);
    return {0.0f, ground + kStartHeightAboveGround, 0.0f};
}

glm::vec3 World::StartOrientation() { return glm::normalize(glm::vec3(0.0f, -0.35f, -1.0f)); }

engine::core::Result<> World::Init(engine::rhi::Device &device, rd::ShaderLibrary &library, const std::filesystem::path &shaderDirectory,
                                   std::uint32_t terrainSeed, bool waitForAllTiles)
{
    auto lights = rd::LightManager::create(device);
    if (!lights)
    {
        return std::unexpected(lights.error());
    }
    this->lightManager = std::move(*lights);
    this->lightManager.setAmbientLight(glm::vec3(1.0f, 1.0f, 1.0f), 0.2f);
    // Direction VERS la lumière (convention des shaders : dot(normale, direction)) : l'opposé du vecteur (2, -3, 0,5) de l'ancienne
    // démo, qui donnait le sens de propagation ; la lumière vient donc toujours d'en haut.
    this->lightManager.addLight(rd::directionalLight(glm::vec3(-2.0f, 3.0f, -0.5f), glm::vec3(0.99f, 0.76f, 0.81f), 1.0f));
    if (auto uploaded = this->lightManager.upload(); !uploaded)
    {
        return uploaded;
    }

    auto terrain = rd::TerrainRenderer::create(device, library, shaderDirectory);
    if (!terrain)
    {
        return std::unexpected(terrain.error());
    }
    this->terrainRenderer = std::move(*terrain);

    this->params = TerrainParameters();
    this->seed = terrainSeed;
    this->waitForTiles = waitForAllTiles;
    this->tasks = std::make_unique<engine::core::TaskSystem>();
    this->streamer = std::make_unique<w::TileStreamer>(this->params, this->seed, kLod, w::StreamingConfig{.memoryBudgetBytes = kMemoryBudgetBytes},
                                                       *this->tasks);
    LOG_INFO("World terrain ready: seed=", this->seed, ", streamed tiles of ", w::tileSize(this->params, 0), " m");
    return {};
}

void World::Destroy()
{
    this->streamer.reset(); // attend les générations en cours (elles n'utilisent que des copies)
    this->tasks.reset();
    this->shown.clear();
    this->terrainRenderer.reset();
    this->lightManager = {};
}

void World::Update() {}

void World::Stream(const glm::vec3 &cameraPosition)
{
    this->streamer->update(cameraPosition.x, cameraPosition.z);
    if (!this->waitForTiles)
    {
        return;
    }
    // Capture : toute la sélection est générée avant l'image (même image à chaque lancement).
    for (int round = 0; round < kMaxCompletionRounds && this->streamer->stats().pendingTiles > 0; ++round)
    {
        this->streamer->finishPending();
        this->streamer->update(cameraPosition.x, cameraPosition.z);
    }
}

// Le jeu de tuiles affiché change d'un coup, quand toute la nouvelle sélection est prête : jamais de trou ni de recouvrement entre
// deux niveaux de détail (même règle que terrain-viewer). Une tuile est renvoyée si elle est nouvelle ou si son maillage a été
// recousu (le streamer refait le maillage EN PLACE quand les voisines changent de niveau : le pointeur seul ne suffit pas).
engine::core::Result<> World::SyncTiles(bool &complete)
{
    complete = false;
    const std::vector<w::TileKey> &selection = this->streamer->selection();
    std::vector<const w::ResidentTile *> ready;
    ready.reserve(selection.size());
    for (const w::TileKey &key : selection)
    {
        const w::ResidentTile *tile = this->streamer->find(key);
        if (tile == nullptr)
        {
            return {}; // sélection incomplète : l'ancienne reste affichée
        }
        ready.push_back(tile);
    }
    std::vector<std::uint64_t> ids;
    ids.reserve(selection.size());
    std::vector<std::uint64_t> added; // envoyées par cette tentative, absentes de l'affichage précédent
    std::vector<float> relative;
    for (std::size_t i = 0; i < selection.size(); ++i)
    {
        const w::TileKey &key = selection[i];
        const w::ResidentTile *tile = ready[i];
        const auto id = static_cast<std::uint64_t>(w::TileKeyHash{}(key));
        ids.push_back(id);
        const auto it = this->shown.find(id);
        if (it != this->shown.end() && it->second.tile == tile && it->second.stitch == tile->stitch)
        {
            continue;
        }
        const bool isNew = it == this->shown.end();
        // Positions relatives à l'origine de la tuile (précision du float loin de l'origine du monde).
        const glm::vec3 origin{w::sampleX(this->params, key, 0), 0.0f, w::sampleZ(this->params, key, 0)};
        relative = tile->mesh.vertices;
        for (std::size_t v = 0; v < relative.size(); v += w::TileMesh::kFloatsPerVertex)
        {
            relative[v] -= origin.x;
            relative[v + 2] -= origin.z;
        }
        if (auto uploaded = this->terrainRenderer->updateTile({.id = id, .origin = origin, .vertices = relative, .indices = tile->mesh.indices});
            !uploaded)
        {
            // Les tuiles nouvelles de cette tentative sont retirées : l'ancienne sélection reste seule à l'écran (nouvel essai à
            // l'image suivante, comme celle qui vient d'échouer).
            for (const std::uint64_t a : added)
            {
                this->terrainRenderer->removeTile(a);
                this->shown.erase(a);
            }
            this->shown.erase(id);
            return std::unexpected(uploaded.error());
        }
        if (isNew)
        {
            added.push_back(id);
        }
        this->shown[id] = {tile, tile->stitch};
    }
    this->terrainRenderer->retainOnly(ids);
    std::erase_if(this->shown, [&](const auto &entry) { return std::ranges::find(ids, entry.first) == ids.end(); });
    complete = true;
    return {};
}

engine::core::Result<> World::Render(rd::SceneRenderer &renderer, const rd::Camera &camera)
{
    this->Stream(camera.position());
    bool complete = false;
    if (auto synced = this->SyncTiles(complete); !synced)
    {
        return synced;
    }
    if (this->waitForTiles && !complete)
    {
        return engine::core::makeError(engine::core::ErrorCode::kBusy, "the terrain tiles around the camera are not all generated");
    }
    renderer.setLights(&this->lightManager);
    return this->terrainRenderer->draw(renderer, camera);
}
