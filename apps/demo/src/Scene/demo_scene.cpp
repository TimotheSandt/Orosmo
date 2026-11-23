#include "Scene/demo_scene.hpp"

#include <orosmo/core/log.hpp>
#include <orosmo/renderer/forward_plus.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace demo
{

namespace
{

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kDegToRad = kPi / 180.0f;

constexpr float kOrthoHeight = 18.0f;
constexpr float kHeroBaseY = 2.85f;
constexpr float kHeroBobAmplitude = 0.20f;
constexpr float kHeroBobSpeed = 1.6f;

struct OrbitSpec
{
    float radius;
    float baseHeight;
    float angularSpeed;
    float initialAngle;
    float bobAmplitude;
    float bobFrequency;
    float spinSpeedX;
    float spinSpeedY;
    float spinSpeedZ;
};

constexpr std::array<OrbitSpec, 3> kOrbitSpecs{{
    {2.35f, 2.35f, 0.95f, 0.4f, 0.16f, 2.1f, 32.0f, 54.0f, 18.0f},
    {2.90f, 3.05f, -0.68f, 2.5f, 0.22f, 1.7f, 24.0f, -42.0f, 30.0f},
    {2.15f, 3.45f, 1.25f, 4.6f, 0.14f, 2.5f, -38.0f, 60.0f, -22.0f},
}};

struct MeshBuilder
{
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;

    void pushVertex(const glm::vec3 &pos, const glm::vec3 &normal, const glm::vec3 &color)
    {
        vertices.push_back(pos.x);
        vertices.push_back(pos.y);
        vertices.push_back(pos.z);
        vertices.push_back(normal.x);
        vertices.push_back(normal.y);
        vertices.push_back(normal.z);
        vertices.push_back(color.r);
        vertices.push_back(color.g);
        vertices.push_back(color.b);
    }

    std::uint32_t vertexCount() const noexcept { return static_cast<std::uint32_t>(vertices.size() / 9); }

    void addTriangle(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &color)
    {
        const glm::vec3 cross = glm::cross(b - a, c - a);
        const float lenSq = glm::dot(cross, cross);
        const glm::vec3 normal = lenSq > 1.0e-12f ? (cross / std::sqrt(lenSq)) : glm::vec3(0.0f, 1.0f, 0.0f);
        const std::uint32_t base = vertexCount();
        pushVertex(a, normal, color);
        pushVertex(b, normal, color);
        pushVertex(c, normal, color);
        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
    }

    void addQuad(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d, const glm::vec3 &color)
    {
        const glm::vec3 cross = glm::cross(b - a, c - a);
        const float lenSq = glm::dot(cross, cross);
        const glm::vec3 normal = lenSq > 1.0e-12f ? (cross / std::sqrt(lenSq)) : glm::vec3(0.0f, 1.0f, 0.0f);
        const std::uint32_t base = vertexCount();
        pushVertex(a, normal, color);
        pushVertex(b, normal, color);
        pushVertex(c, normal, color);
        pushVertex(d, normal, color);
        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    void addBox(const glm::vec3 &minCorner, const glm::vec3 &maxCorner, const glm::vec3 &color)
    {
        const float x0 = minCorner.x;
        const float y0 = minCorner.y;
        const float z0 = minCorner.z;
        const float x1 = maxCorner.x;
        const float y1 = maxCorner.y;
        const float z1 = maxCorner.z;

        // +Y (top)
        addQuad({x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, color);
        // -Y (bottom)
        addQuad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, color);
        // +Z (front)
        addQuad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, color);
        // -Z (back)
        addQuad({x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, color);
        // +X (right)
        addQuad({x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, color);
        // -X (left)
        addQuad({x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, color);
    }

    void addPrism(const glm::vec3 &bottomCenter, float bottomRadius, float topRadius, float height, int sides,
                  const glm::vec3 &sideColor, const glm::vec3 &capColor)
    {
        const float y0 = bottomCenter.y;
        const float y1 = bottomCenter.y + height;
        const glm::vec3 topCenter(bottomCenter.x, y1, bottomCenter.z);

        for (int i = 0; i < sides; ++i)
        {
            const float a0 = kTwoPi * static_cast<float>(i) / static_cast<float>(sides);
            const float a1 = kTwoPi * static_cast<float>((i + 1) % sides) / static_cast<float>(sides);

            const glm::vec3 b0(bottomCenter.x + bottomRadius * std::cos(a0), y0, bottomCenter.z + bottomRadius * std::sin(a0));
            const glm::vec3 b1(bottomCenter.x + bottomRadius * std::cos(a1), y0, bottomCenter.z + bottomRadius * std::sin(a1));
            const glm::vec3 t0(bottomCenter.x + topRadius * std::cos(a0), y1, bottomCenter.z + topRadius * std::sin(a0));
            const glm::vec3 t1(bottomCenter.x + topRadius * std::cos(a1), y1, bottomCenter.z + topRadius * std::sin(a1));

            // Outward-facing side quad: counter-clockwise viewed from outside.
            addQuad(b1, b0, t0, t1, sideColor);
            // Top cap (+Y normal).
            addTriangle(topCenter, t1, t0, capColor);
            // Bottom cap (-Y normal).
            addTriangle(bottomCenter, b0, b1, capColor);
        }
    }

    void addBipyramid(float radius, float halfHeight, int sides, const glm::vec3 &upperColor, const glm::vec3 &lowerColor)
    {
        const glm::vec3 top(0.0f, halfHeight, 0.0f);
        const glm::vec3 bottom(0.0f, -halfHeight, 0.0f);
        for (int i = 0; i < sides; ++i)
        {
            const float a0 = kTwoPi * static_cast<float>(i) / static_cast<float>(sides);
            const float a1 = kTwoPi * static_cast<float>((i + 1) % sides) / static_cast<float>(sides);
            const glm::vec3 r0(radius * std::cos(a0), 0.0f, radius * std::sin(a0));
            const glm::vec3 r1(radius * std::cos(a1), 0.0f, radius * std::sin(a1));
            addTriangle(r1, r0, top, upperColor);
            addTriangle(r0, r1, bottom, lowerColor);
        }
    }

    orosmo::renderer::MeshData finish(std::string name)
    {
        orosmo::renderer::MeshData data;
        data.vertices = std::move(vertices);
        data.indices = std::move(indices);
        data.attributeSizes = {3, 3, 3};
        data.name = std::move(name);
        return data;
    }
};

glm::vec3 clampColor(const glm::vec3 &c)
{
    return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
}

glm::vec3 sunDirectionFromAngles(float elevationDegrees, float azimuthDegrees)
{
    const float elevRad = elevationDegrees * kDegToRad;
    const float azimRad = azimuthDegrees * kDegToRad;
    const float cosElev = std::cos(elevRad);
    return glm::normalize(glm::vec3(cosElev * std::sin(azimRad), std::sin(elevRad), cosElev * std::cos(azimRad)));
}

orosmo::renderer::MeshData buildCourtyardMeshData()
{
    MeshBuilder builder;

    constexpr int kGridCount = 12;
    constexpr float kTileSize = 1.0f;
    constexpr float kHalfExtent = 0.5f * static_cast<float>(kGridCount) * kTileSize; // 6.0f
    constexpr float kTileInset = 0.03f;

    const glm::vec3 warmStone(0.76f, 0.72f, 0.64f);
    const glm::vec3 warmStoneInner(0.81f, 0.77f, 0.68f);
    const glm::vec3 coolSlate(0.45f, 0.50f, 0.58f);
    const glm::vec3 coolSlateInner(0.39f, 0.45f, 0.54f);
    const glm::vec3 groutColor(0.24f, 0.26f, 0.30f);
    const glm::vec3 borderStone(0.58f, 0.56f, 0.52f);
    const glm::vec3 cornerPlinthColor(0.66f, 0.62f, 0.56f);
    const glm::vec3 plinthBaseColor(0.26f, 0.28f, 0.32f);

    // Grout sub-slab under the courtyard tiles.
    builder.addBox({-kHalfExtent, -0.34f, -kHalfExtent}, {kHalfExtent, -0.02f, kHalfExtent}, groutColor);

    // 12x12 alternating courtyard floor tiles.
    for (int iz = 0; iz < kGridCount; ++iz)
    {
        for (int ix = 0; ix < kGridCount; ++ix)
        {
            const float x0 = -kHalfExtent + static_cast<float>(ix) * kTileSize + kTileInset;
            const float z0 = -kHalfExtent + static_cast<float>(iz) * kTileSize + kTileInset;
            const float x1 = x0 + kTileSize - 2.0f * kTileInset;
            const float z1 = z0 + kTileSize - 2.0f * kTileInset;
            const bool isWarm = ((ix + iz) % 2) == 0;
            const bool isInnerRing = (ix >= 3 && ix <= 8 && iz >= 3 && iz <= 8);
            const glm::vec3 tileColor = isWarm ? (isInnerRing ? warmStoneInner : warmStone) : (isInnerRing ? coolSlateInner : coolSlate);
            builder.addBox({x0, -0.04f, z0}, {x1, 0.0f, z1}, tileColor);
        }
    }

    // Slightly raised outer stone border curbs.
    constexpr float kBorderWidth = 0.65f;
    constexpr float kOuter = kHalfExtent + kBorderWidth;
    constexpr float kBorderTop = 0.22f;
    constexpr float kBorderBottom = -0.34f;
    // North (-Z) and South (+Z) curbs spanning full width.
    builder.addBox({-kOuter, kBorderBottom, -kOuter}, {kOuter, kBorderTop, -kHalfExtent}, borderStone);
    builder.addBox({-kOuter, kBorderBottom, kHalfExtent}, {kOuter, kBorderTop, kOuter}, borderStone);
    // West (-X) and East (+X) curbs between North and South.
    builder.addBox({-kOuter, kBorderBottom, -kHalfExtent}, {-kHalfExtent, kBorderTop, kHalfExtent}, borderStone);
    builder.addBox({kHalfExtent, kBorderBottom, -kHalfExtent}, {kOuter, kBorderTop, kHalfExtent}, borderStone);

    // Four raised corner anchor blocks.
    constexpr float kCornerSize = 0.84f;
    constexpr float kCornerTop = 0.34f;
    const std::array<float, 2> signs{-1.0f, 1.0f};
    for (const float sx : signs)
    {
        for (const float sz : signs)
        {
            const float cx = sx * (kOuter - 0.5f * kCornerSize + 0.04f);
            const float cz = sz * (kOuter - 0.5f * kCornerSize + 0.04f);
            const float half = 0.5f * kCornerSize;
            builder.addBox({cx - half, kBorderBottom, cz - half}, {cx + half, kCornerTop, cz + half}, cornerPlinthColor);
        }
    }

    // Deep charcoal base plinth beneath the whole courtyard.
    constexpr float kBaseOuter = kOuter + 0.16f;
    builder.addBox({-kBaseOuter, -0.62f, -kBaseOuter}, {kBaseOuter, kBorderBottom, kBaseOuter}, plinthBaseColor);

    return builder.finish("diorama.ground");
}

orosmo::renderer::MeshData buildStructureMeshData()
{
    MeshBuilder builder;

    const glm::vec3 limestone(0.75f, 0.72f, 0.66f);
    const glm::vec3 creamMarble(0.84f, 0.81f, 0.76f);
    const glm::vec3 carvedTrim(0.68f, 0.65f, 0.60f);
    const glm::vec3 darkSlate(0.42f, 0.46f, 0.53f);
    const glm::vec3 bronzeAccent(0.56f, 0.49f, 0.38f);

    // ── Stepped central pedestal ──
    builder.addBox({-2.10f, 0.00f, -2.10f}, {2.10f, 0.28f, 2.10f}, limestone);
    builder.addBox({-1.55f, 0.28f, -1.55f}, {1.55f, 0.64f, 1.55f}, creamMarble);
    builder.addBox({-0.96f, 0.64f, -0.96f}, {0.96f, 1.18f, 0.96f}, darkSlate);
    builder.addBox({-1.12f, 1.18f, -1.12f}, {1.12f, 1.32f, 1.12f}, creamMarble);
    builder.addBox({-0.78f, 1.32f, -0.78f}, {0.78f, 1.38f, 0.78f}, bronzeAccent);

    // Corner buttress blocks on the first pedestal tier.
    const std::array<float, 2> signs{-1.0f, 1.0f};
    for (const float sx : signs)
    {
        for (const float sz : signs)
        {
            const float bx = sx * 1.72f;
            const float bz = sz * 1.72f;
            builder.addBox({bx - 0.26f, 0.28f, bz - 0.26f}, {bx + 0.26f, 0.52f, bz + 0.26f}, carvedTrim);
        }
    }

    // ── Four corner columns with plinths, octagonal shafts, and capitals ──
    constexpr float kColOffset = 3.80f;
    for (const float sx : signs)
    {
        for (const float sz : signs)
        {
            const float cx = sx * kColOffset;
            const float cz = sz * kColOffset;

            // Column plinth & base molding.
            builder.addBox({cx - 0.56f, 0.00f, cz - 0.56f}, {cx + 0.56f, 0.28f, cz + 0.56f}, carvedTrim);
            builder.addBox({cx - 0.44f, 0.28f, cz - 0.44f}, {cx + 0.44f, 0.44f, cz + 0.44f}, limestone);

            // Octagonal column shaft with slight classical taper.
            builder.addPrism({cx, 0.44f, cz}, 0.37f, 0.33f, 3.30f, 8, creamMarble, limestone);

            // Capital echinus and abacus.
            builder.addBox({cx - 0.44f, 3.74f, cz - 0.44f}, {cx + 0.44f, 3.90f, cz + 0.44f}, limestone);
            builder.addBox({cx - 0.58f, 3.90f, cz - 0.58f}, {cx + 0.58f, 4.14f, cz + 0.58f}, carvedTrim);
        }
    }

    // ── Overhead architrave beams, cornice, and keystones ──
    constexpr float kBeamBottom = 4.14f;
    constexpr float kBeamTop = 4.52f;
    constexpr float kBeamHalfThick = 0.28f;
    constexpr float kCorniceTop = 4.68f;
    constexpr float kCorniceHalfThick = 0.38f;
    constexpr float kSpan = kColOffset + 0.46f;

    // Beams parallel to X (at z = +/- kColOffset).
    for (const float sz : signs)
    {
        const float cz = sz * kColOffset;
        builder.addBox({-kSpan, kBeamBottom, cz - kBeamHalfThick}, {kSpan, kBeamTop, cz + kBeamHalfThick}, limestone);
        builder.addBox({-kSpan - 0.12f, kBeamTop, cz - kCorniceHalfThick}, {kSpan + 0.12f, kCorniceTop, cz + kCorniceHalfThick}, creamMarble);
        // Center keystone accent.
        builder.addBox({-0.42f, kBeamBottom - 0.06f, cz - 0.34f}, {0.42f, kBeamTop + 0.04f, cz + 0.34f}, darkSlate);
    }

    // Beams parallel to Z (at x = +/- kColOffset).
    for (const float sx : signs)
    {
        const float cx = sx * kColOffset;
        builder.addBox({cx - kBeamHalfThick, kBeamBottom, -kSpan}, {cx + kBeamHalfThick, kBeamTop, kSpan}, limestone);
        builder.addBox({cx - kCorniceHalfThick, kBeamTop, -kSpan - 0.12f}, {cx + kCorniceHalfThick, kCorniceTop, kSpan + 0.12f}, creamMarble);
        // Center keystone accent.
        builder.addBox({cx - 0.34f, kBeamBottom - 0.06f, -0.42f}, {cx + 0.34f, kBeamTop + 0.04f, 0.42f}, darkSlate);
    }

    return builder.finish("diorama.structure");
}

MeshBuilder buildHeroCrystalBuilder(const glm::vec3 &accentColor)
{
    MeshBuilder builder;

    constexpr int kSides = 8;
    constexpr float kRadius = 0.62f;
    constexpr float kHalfBelt = 0.34f;
    constexpr float kApexY = 1.14f;
    constexpr float kTwist = kPi / static_cast<float>(kSides);

    const glm::vec3 topApex(0.0f, kApexY, 0.0f);
    const glm::vec3 bottomApex(0.0f, -kApexY, 0.0f);

    const glm::vec3 crownLight = clampColor(accentColor * 1.18f + glm::vec3(0.08f));
    const glm::vec3 crownMid = clampColor(accentColor * 1.04f + glm::vec3(0.03f));
    const glm::vec3 beltLight = clampColor(accentColor * 0.98f);
    const glm::vec3 beltDark = clampColor(accentColor * 0.84f);
    const glm::vec3 pavilionColor = clampColor(accentColor * 0.72f);

    std::array<glm::vec3, kSides> upperRing{};
    std::array<glm::vec3, kSides> lowerRing{};
    for (int i = 0; i < kSides; ++i)
    {
        const float aUpper = kTwoPi * static_cast<float>(i) / static_cast<float>(kSides);
        const float aLower = aUpper + kTwist;
        upperRing[static_cast<std::size_t>(i)] = {kRadius * std::cos(aUpper), kHalfBelt, kRadius * std::sin(aUpper)};
        lowerRing[static_cast<std::size_t>(i)] = {kRadius * std::cos(aLower), -kHalfBelt, kRadius * std::sin(aLower)};
    }

    for (int i = 0; i < kSides; ++i)
    {
        const std::size_t cur = static_cast<std::size_t>(i);
        const std::size_t next = static_cast<std::size_t>((i + 1) % kSides);

        const glm::vec3 &u0 = upperRing[cur];
        const glm::vec3 &u1 = upperRing[next];
        const glm::vec3 &l0 = lowerRing[cur];
        const glm::vec3 &l1 = lowerRing[next];

        // Upper crown facet.
        builder.addTriangle(u1, u0, topApex, (i % 2 == 0) ? crownLight : crownMid);
        // Antiprism girdle facets (2 triangles per segment).
        builder.addTriangle(u0, u1, l0, beltLight);
        builder.addTriangle(l0, u1, l1, beltDark);
        // Lower pavilion facet.
        builder.addTriangle(l0, l1, bottomApex, pavilionColor);
    }

    return builder;
}

orosmo::renderer::MeshData buildOrbitMeshData(std::size_t index)
{
    MeshBuilder builder;
    if (index == 0)
    {
        // Satellite 0: crisp coral-amber cube.
        const glm::vec3 half(0.28f);
        builder.addBox(-half, half, glm::vec3(0.94f, 0.48f, 0.24f));
        return builder.finish("diorama.orbit.cube");
    }
    if (index == 1)
    {
        // Satellite 1: emerald-teal faceted bipyramid.
        builder.addBipyramid(0.36f, 0.44f, 6, glm::vec3(0.26f, 0.86f, 0.60f), glm::vec3(0.16f, 0.64f, 0.46f));
        return builder.finish("diorama.orbit.bipyramid");
    }
    // Satellite 2: violet-lavender hexagonal prism.
    builder.addPrism({0.0f, -0.30f, 0.0f}, 0.34f, 0.34f, 0.60f, 6, glm::vec3(0.76f, 0.44f, 0.92f), glm::vec3(0.88f, 0.64f, 0.96f));
    return builder.finish("diorama.orbit.prism");
}

} // namespace

DemoScene::~DemoScene() { destroy(); }

glm::vec3 DemoScene::defaultCameraPosition() { return glm::vec3(9.5f, 6.8f, 11.5f); }

glm::vec3 DemoScene::defaultCameraOrientation()
{
    return glm::normalize(glm::vec3(0.0f, 1.5f, 0.0f) - defaultCameraPosition());
}

std::vector<orosmo::renderer::Light> DemoScene::pointLightField(std::uint32_t count)
{
    std::vector<orosmo::renderer::Light> lights;
    lights.reserve(count);
    const auto hash = [](std::uint32_t v)
    {
        v ^= v >> 16;
        v *= 0x7feb352du;
        v ^= v >> 15;
        v *= 0x846ca68bu;
        v ^= v >> 16;
        return v;
    };
    const auto unit = [&](std::uint32_t v) { return static_cast<float>(hash(v) >> 8) / 16777216.0f; };
    constexpr float kRadius = 12.0f;
    constexpr float kGoldenAngle = 2.39996323f;
    const float strength = count <= 64 ? 0.6f : 0.6f * std::sqrt(64.0f / static_cast<float>(count));
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const float r = kRadius * std::sqrt((static_cast<float>(i) + 0.5f) / static_cast<float>(count));
        const float angle = static_cast<float>(i) * kGoldenAngle;
        const float height = 0.35f + 1.6f * unit(i * 4u);
        const glm::vec3 color(0.35f + 0.65f * unit(i * 4u + 1u), 0.35f + 0.65f * unit(i * 4u + 2u), 0.35f + 0.65f * unit(i * 4u + 3u));
        lights.push_back(orosmo::renderer::pointLight({r * std::cos(angle), height, r * std::sin(angle)}, color, strength, 60.0f, 3.0f));
    }
    return lights;
}

orosmo::core::Result<> DemoScene::init(orosmo::rhi::Device &device, orosmo::renderer::ShaderLibrary &shaders,
                                       const std::filesystem::path &shaderDirectory, bool forwardPlus, std::uint32_t pointLights,
                                       const std::vector<orosmo::renderer::Define> &aovDefines)
{
    destroy();

    // Variante forward+ (REN-04) et sorties auxiliaires (REN-21) : leurs macros s'additionnent.
    std::vector<orosmo::renderer::Define> meshDefines = forwardPlus ? orosmo::renderer::forwardPlusDefines() : std::vector<orosmo::renderer::Define>{};
    meshDefines.insert(meshDefines.end(), aovDefines.begin(), aovDefines.end());
    auto program = shaders.loadGraphics(shaderDirectory / "mesh.vert", shaderDirectory / "mesh.frag", meshDefines);
    if (!program)
    {
        return std::unexpected(program.error());
    }
    meshProgram_ = *program;
    if (shadows_ != nullptr)
    {
        // Ombres (variante OROSMO_SHADOWS), combinées au forward+ et aux sorties auxiliaires (macros de meshDefines).
        auto receiver = shadows_->receiverProgramWith(meshDefines);
        if (!receiver)
        {
            return std::unexpected(receiver.error());
        }
        meshProgram_ = *receiver;
    }

    auto lights = orosmo::renderer::LightManager::create(device, 4);
    if (!lights)
    {
        destroy();
        return std::unexpected(lights.error());
    }
    lights_ = std::move(*lights);

    const DemoSceneParams defaults{};
    if (auto r = syncLights(defaults); !r)
    {
        destroy();
        return r;
    }
    if (pointLights > 0)
    {
        lights_.addLights(pointLightField(pointLights)); // après le soleil (indice 0, mis à jour par syncLights)
        if (auto r = lights_.upload(); !r)
        {
            destroy();
            return r;
        }
    }

    if (shadows_ != nullptr)
    {
        shadows_->setLights(&lights_);
    }
    const orosmo::renderer::MeshData groundData = buildCourtyardMeshData();
    auto ground = orosmo::renderer::Mesh::create(device, groundData);
    if (!ground)
    {
        destroy();
        return std::unexpected(ground.error());
    }
    ground->setProgram(meshProgram_);
    groundMesh_ = std::move(*ground);

    const orosmo::renderer::MeshData structureData = buildStructureMeshData();
    auto structure = orosmo::renderer::Mesh::create(device, structureData);
    if (!structure)
    {
        destroy();
        return std::unexpected(structure.error());
    }
    structure->setProgram(meshProgram_);
    structureMesh_ = std::move(*structure);

    MeshBuilder heroBuilder = buildHeroCrystalBuilder(defaults.accentColor);
    heroVertices_ = heroBuilder.vertices;
    const orosmo::renderer::MeshData heroData = heroBuilder.finish("diorama.hero");
    auto hero = orosmo::renderer::Mesh::create(device, heroData);
    if (!hero)
    {
        destroy();
        return std::unexpected(hero.error());
    }
    hero->setProgram(meshProgram_);
    heroMesh_ = std::move(*hero);
    lastAccentColor_ = defaults.accentColor;
    accentInitialized_ = true;

    for (std::size_t i = 0; i < orbitMeshes_.size(); ++i)
    {
        const orosmo::renderer::MeshData orbitData = buildOrbitMeshData(i);
        auto orbit = orosmo::renderer::Mesh::create(device, orbitData);
        if (!orbit)
        {
            destroy();
            return std::unexpected(orbit.error());
        }
        orbit->setProgram(meshProgram_);
        orbitMeshes_[i] = std::move(*orbit);
        if (shadows_ != nullptr)
        {
            if (auto r = registerCaster(orbitMeshes_[i], orbitData, orosmo::renderer::CasterMobility::kMovable); !r)
            {
                destroy();
                return r;
            }
        }
    }
    if (shadows_ != nullptr)
    {
        // Le sol reçoit sans projeter (plat, sous tout le reste) ; la structure est fixe (cache), le cristal est mobile.
        for (auto *mesh : {&groundMesh_, &structureMesh_, &heroMesh_})
        {
            mesh->setMaterialGroup(shadows_->receiverGroup());
        }
        for (auto &orbit : orbitMeshes_)
        {
            orbit.setMaterialGroup(shadows_->receiverGroup());
        }
        if (auto r = registerCaster(structureMesh_, structureData, orosmo::renderer::CasterMobility::kStatic); !r)
        {
            destroy();
            return r;
        }
        if (auto r = registerCaster(heroMesh_, heroData, orosmo::renderer::CasterMobility::kMovable); !r)
        {
            destroy();
            return r;
        }
    }
    (void)groundData;

    elapsedSeconds_ = 0.0f;
    applyTransforms();
    initialized_ = true;
    return {};
}

orosmo::core::Result<> DemoScene::registerCaster(const orosmo::renderer::Mesh &mesh, const orosmo::renderer::MeshData &data,
                                                orosmo::renderer::CasterMobility mobility)
{
    // Boîte locale depuis les positions (3 premiers flottants de chaque sommet) : culling par vue d'ombre.
    std::uint32_t stride = 0;
    for (const std::uint32_t size : data.attributeSizes)
    {
        stride += size;
    }
    orosmo::renderer::Aabb bounds = orosmo::renderer::Aabb::empty();
    for (std::size_t v = 0; stride >= 3 && v + 2 < data.vertices.size(); v += stride)
    {
        const glm::vec3 p(data.vertices[v], data.vertices[v + 1], data.vertices[v + 2]);
        bounds.merge({p, p});
    }
    auto id = shadows_->addMeshCaster(mesh, bounds, mobility);
    if (!id)
    {
        return std::unexpected(id.error());
    }
    casterIds_.push_back(*id);
    return {};
}

orosmo::core::Result<> DemoScene::initInstances(orosmo::rhi::Device &device, orosmo::renderer::ShaderLibrary &shaders,
                                                orosmo::renderer::PipelineCache &pipelines, const std::filesystem::path &shaderDirectory,
                                                std::uint32_t count)
{
    namespace rd = orosmo::renderer;
    if (count == 0)
    {
        return {};
    }
    auto renderer = rd::InstanceRenderer::create(device, shaders, pipelines, shaderDirectory);
    if (!renderer)
    {
        return std::unexpected(renderer.error());
    }
    // Niveau 0 : bipyramide à facettes ; niveau 1 : cube (12 triangles). Sommets blancs, la couleur vient de l'instance.
    MeshBuilder detailed;
    detailed.addBipyramid(0.36f, 0.44f, 8, glm::vec3(1.0f), glm::vec3(0.78f));
    MeshBuilder coarse;
    coarse.addBox(glm::vec3(-0.3f), glm::vec3(0.3f), glm::vec3(0.9f));
    std::array<rd::MeshData, 2> lodData{detailed.finish("instances.lod0"), coarse.finish("instances.lod1")};
    std::array<std::uint32_t, 2> lodIds{};
    for (std::size_t i = 0; i < lodData.size(); ++i)
    {
        auto mesh = rd::Mesh::create(device, lodData[i]);
        if (!mesh)
        {
            return std::unexpected(mesh.error());
        }
        instanceLods_[i] = std::move(*mesh);
        auto id = (*renderer)->addMesh(instanceLods_[i]);
        if (!id)
        {
            return std::unexpected(id.error());
        }
        lodIds[i] = *id;
    }
    // Deux matériaux identiques (le regroupement se fait par identifiant) : quatre lots au plus, deux LOD × deux matériaux.
    rd::InstanceMaterial material;
    if (shadows_ != nullptr)
    {
        material.program = shadows_->instancedReceiverProgram();
        material.textures = shadows_->receiverGroup();
    }
    const std::array<std::uint32_t, 2> materials{(*renderer)->addMaterial(material), (*renderer)->addMaterial(material)};

    rd::LodSettings lod;
    lod.switchDistances = {28.0f};
    lod.transitionWidth = 6.0f;
    const std::uint32_t lodSettings = instanceObjects_.addLodSettings(lod);

    // Grille déterministe (aucun hasard) autour de la cour : pas de 1,6 m, les cellules de la cour (rayon 9 m) sont sautées.
    constexpr float kSpacing = 1.6f;
    constexpr float kClearRadius = 9.0f;
    const float clearCells = kPi * (kClearRadius / kSpacing) * (kClearRadius / kSpacing);
    const auto side = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<float>(count) + clearCells))) + 2;
    const float centre = static_cast<float>(side - 1) * 0.5f;
    const glm::vec3 palette[4] = {{0.95f, 0.55f, 0.30f}, {0.35f, 0.80f, 0.60f}, {0.55f, 0.60f, 0.95f}, {0.90f, 0.85f, 0.40f}};
    std::uint32_t placed = 0;
    for (std::uint32_t cell = 0; placed < count && cell < side * side; ++cell)
    {
        const std::uint32_t gx = cell % side;
        const std::uint32_t gz = cell / side;
        const float x = (static_cast<float>(gx) - centre) * kSpacing;
        const float z = (static_cast<float>(gz) - centre) * kSpacing;
        if (x * x + z * z < kClearRadius * kClearRadius)
        {
            continue;
        }
        rd::ObjectDesc desc;
        desc.lodMeshes = {lodIds[0], lodIds[1]};
        desc.material = materials[placed % 2];
        desc.lodSettings = lodSettings;
        desc.localBounds = {glm::vec3(-0.45f), glm::vec3(0.45f)};
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.45f, z));
        transform = glm::rotate(transform, static_cast<float>(placed % 17) * 0.37f, glm::vec3(0.0f, 1.0f, 0.0f));
        desc.transform = transform;
        desc.color = glm::vec4(palette[(gx + gz * 3) % 4], 1.0f);
        if (auto id = instanceObjects_.add(desc); !id)
        {
            return std::unexpected(id.error());
        }
        ++placed;
    }
    if (shadows_ != nullptr)
    {
        casterIds_.push_back(shadows_->addObjectCasters(instanceObjects_, **renderer, rd::CasterMobility::kStatic));
    }
    instanceRenderer_ = std::move(*renderer);
    return {};
}

orosmo::core::Result<> DemoScene::enableGpuDriven(orosmo::renderer::SceneRenderer &renderer, orosmo::rhi::Device &device,
                                                 orosmo::renderer::ShaderLibrary &shaders, orosmo::renderer::PipelineCache &pipelines,
                                                 const std::filesystem::path &shaderDirectory)
{
    if (!instanceRenderer_)
    {
        return {};
    }
    auto enabled = orosmo::renderer::enableGpuDriven(renderer, device, shaders, pipelines, shaderDirectory, *instanceRenderer_);
    if (!enabled)
    {
        return std::unexpected(enabled.error());
    }
    gpuDriven_ = *enabled;
    gpuDrivenRenderer_ = &renderer;
    LOG_INFO("GPU-driven: ", instanceObjects_.size(), " objects culled on the GPU (frustum, Hi-Z occlusion, LOD), multi-draw indirect");
    return {};
}

orosmo::core::Result<> DemoScene::renderInstances(orosmo::renderer::SceneRenderer &renderer, const orosmo::renderer::Camera &camera)
{
    namespace rd = orosmo::renderer;
    rd::CullView view = rd::CullView::fromMatrices(camera.viewMatrix(), camera.projectionMatrix(), static_cast<float>(renderer.frameHeight()),
                                                   camera.projection() == rd::Camera::Projection::kOrthographic);
    view.minScreenRadius = 1.0f; // moins d'un pixel de rayon : rejeté
    // REN-35 : gel du frustum (la vue de culling reste celle de l'image du gel, la pyramide gelée est dessinée) et couleurs de LOD.
    if (freezeFrustum_ && !frustumFreeze_.frozen())
    {
        frustumFreeze_.freeze(view, camera.projectionMatrix() * camera.viewMatrix());
    }
    view = frustumFreeze_.apply(view);
    // Fil de fer : raccourci de la caméra ou vue de débogage (SceneRenderer l'applique à ses maillages, pas aux dessins personnalisés).
    const bool wireframe = camera.wireframe() || debugView_ == rd::DebugView::kWireframe;
    // REN-35 : les couleurs de LOD ne sont produites que par le culling CPU (VisibilityOptions::lodColors) : la vue kLod le garde.
    if (gpuDriven_ != nullptr && debugView_ != rd::DebugView::kLod)
    {
        // Journal (image 2 puis toutes les 600) : compteurs GPU de l'image précédente, comparés au culling CPU de la même vue.
        const bool log = instanceFrames_ == 2 || (instanceFrames_ > 0 && instanceFrames_ % 600 == 0);
        if (log)
        {
            auto gpu = gpuDriven_->readStats();
            const rd::VisibilityStats cpu = instanceObjects_.prepare(view, instanceBatches_);
            if (gpu)
            {
                LOG_INFO("GPU-driven: ", gpu->objects, " objects, ", gpu->frustumVisible, " in view (", gpu->frustumCulled, " outside, ", gpu->smallCulled,
                         " too small), ", gpu->occlusionCulled, " occluded (Hi-Z), ", gpu->visible, " visible, ", gpu->instances, " instances in ",
                         gpu->drawsWithInstances, " indirect draws | CPU culling: ", cpu.cull.visible, " visible, ", cpu.instances, " instances in ",
                         instanceBatches_.batches().size(), " draw calls");
            }
        }
        rd::GpuDrivenView gpuView;
        gpuView.cull = view;
        gpuView.viewProjection = camera.projectionMatrix() * camera.viewMatrix();
        rd::GpuDrivenOptions options;
        options.wireframe = wireframe;
        auto drawn = gpuDriven_->draw(renderer, instanceObjects_, gpuView, options);
        if (!drawn)
        {
            return std::unexpected(drawn.error());
        }
        ++instanceFrames_;
        return {};
    }
    if (gpuDriven_ != nullptr)
    {
        gpuDriven_->resetOcclusion(); // Hi-Z non reconstruite pendant le culling CPU : périmée au retour sur le GPU
    }
    rd::VisibilityOptions visibility;
    visibility.lodColors = debugView_ == rd::DebugView::kLod;
    instanceVisibility_ = instanceObjects_.prepare(view, instanceBatches_, visibility);

    auto drawn = instanceRenderer_->draw(renderer, instanceBatches_, wireframe);
    if (!drawn)
    {
        return std::unexpected(drawn.error());
    }
    instanceDrawStats_ = *drawn;
    if (instanceFrames_++ % 600 == 0)
    {
        LOG_INFO("Instances: ", instanceVisibility_.objects, " objects, ", instanceVisibility_.cull.visible, " visible (", instanceVisibility_.cull.frustumCulled,
                 " outside the view, ", instanceVisibility_.cull.smallCulled, " too small), ", instanceVisibility_.crossFades, " LOD cross-fades, ",
                 instanceDrawStats_.instances, " instances in ", instanceDrawStats_.drawCalls, " draw calls");
    }
    return {};
}

void DemoScene::destroy()
{
    if (shadows_ != nullptr)
    {
        for (const auto id : casterIds_)
        {
            shadows_->removeCaster(id); // le plugin vit dans SceneRenderer : il ne doit plus voir ces maillages
        }
        shadows_->setLights(nullptr);
    }
    casterIds_.clear();
    shadowFrames_ = 0;
    if (gpuDriven_ != nullptr && gpuDrivenRenderer_ != nullptr)
    {
        orosmo::renderer::disableGpuDriven(*gpuDrivenRenderer_); // référence InstanceRenderer et ses maillages
    }
    gpuDriven_ = nullptr;
    gpuDrivenRenderer_ = nullptr;
    instanceRenderer_.reset(); // référence les maillages des LOD
    for (auto &mesh : instanceLods_)
    {
        mesh.reset();
    }
    instanceObjects_.clear();
    instanceBatches_.clear();
    instanceVisibility_ = {};
    instanceDrawStats_ = {};
    instanceFrames_ = 0;
    frustumFreeze_.unfreeze();
    groundMesh_.reset();
    structureMesh_.reset();
    heroMesh_.reset();
    for (auto &orbit : orbitMeshes_)
    {
        orbit.reset();
    }
    meshProgram_.reset();
    lights_ = orosmo::renderer::LightManager{};
    debugDraw_.clear();
    heroVertices_.clear();
    heroPosition_ = glm::vec3(0.0f, kHeroBaseY, 0.0f);
    elapsedSeconds_ = 0.0f;
    initialized_ = false;
    lightingInitialized_ = false;
    accentInitialized_ = false;
}

void DemoScene::update(float deltaSeconds, const DemoSceneParams &params)
{
    if (!initialized_)
    {
        return;
    }
    if (params.animate && deltaSeconds > 0.0f)
    {
        elapsedSeconds_ += deltaSeconds * std::max(params.animationSpeed, 0.0f);
    }
    applyTransforms();
}

void DemoScene::applyTransforms()
{
    heroPosition_ = glm::vec3(0.0f, kHeroBaseY + kHeroBobAmplitude * std::sin(elapsedSeconds_ * kHeroBobSpeed), 0.0f);
    heroMesh_.setPosition(heroPosition_);
    heroMesh_.setRotation(glm::vec3(6.0f * std::sin(elapsedSeconds_ * 0.9f), std::fmod(elapsedSeconds_ * 28.0f, 360.0f),
                                    4.0f * std::cos(elapsedSeconds_ * 0.7f)));

    for (std::size_t i = 0; i < orbitMeshes_.size(); ++i)
    {
        const OrbitSpec &spec = kOrbitSpecs[i];
        const float angle = spec.initialAngle + elapsedSeconds_ * spec.angularSpeed;
        const float y = spec.baseHeight + spec.bobAmplitude * std::sin(elapsedSeconds_ * spec.bobFrequency + spec.initialAngle);
        orbitMeshes_[i].setPosition(glm::vec3(spec.radius * std::cos(angle), y, spec.radius * std::sin(angle)));
        orbitMeshes_[i].setRotation(glm::vec3(std::fmod(elapsedSeconds_ * spec.spinSpeedX, 360.0f),
                                              std::fmod(elapsedSeconds_ * spec.spinSpeedY, 360.0f),
                                              std::fmod(elapsedSeconds_ * spec.spinSpeedZ, 360.0f)));
    }
}

orosmo::core::Result<> DemoScene::syncLights(const DemoSceneParams &params)
{
    const bool changed = !lightingInitialized_ || params.sunElevation != lastSunElevation_ || params.sunAzimuth != lastSunAzimuth_ ||
                         params.sunIntensity != lastSunIntensity_ || params.sunColor != lastSunColor_ ||
                         params.ambientStrength != lastAmbientStrength_ || params.ambientColor != lastAmbientColor_;
    if (!changed)
    {
        return {};
    }

    const glm::vec3 sunDirection = sunDirectionFromAngles(params.sunElevation, params.sunAzimuth);
    orosmo::renderer::Light sunLight = orosmo::renderer::directionalLight(sunDirection, params.sunColor, params.sunIntensity);
    if (shadows_ != nullptr)
    {
        // Soleil réglable dans l'interface : mobile ; ombres en cache (redessinées seulement quand quelque chose bouge).
        sunLight.mobility = orosmo::renderer::LightMobility::kDynamic;
        sunLight.shadowMode = orosmo::renderer::ShadowMode::kCached;
    }

    lights_.setAmbientLight(params.ambientColor, params.ambientStrength);
    if (lights_.lights().empty())
    {
        lights_.addLight(sunLight);
    }
    else if (auto r = lights_.setLight(0, sunLight); !r)
    {
        return r;
    }

    if (auto r = lights_.upload(); !r)
    {
        return r;
    }

    lastSunElevation_ = params.sunElevation;
    lastSunAzimuth_ = params.sunAzimuth;
    lastSunIntensity_ = params.sunIntensity;
    lastSunColor_ = params.sunColor;
    lastAmbientStrength_ = params.ambientStrength;
    lastAmbientColor_ = params.ambientColor;
    lightingInitialized_ = true;
    return {};
}

orosmo::core::Result<> DemoScene::syncAccentColor(const DemoSceneParams &params)
{
    if (accentInitialized_ && params.accentColor == lastAccentColor_)
    {
        return {};
    }

    MeshBuilder rebuilt = buildHeroCrystalBuilder(params.accentColor);
    heroVertices_ = std::move(rebuilt.vertices);
    if (auto r = heroMesh_.updateVertices(std::span<const float>(heroVertices_)); !r)
    {
        return r;
    }
    lastAccentColor_ = params.accentColor;
    accentInitialized_ = true;
    return {};
}

orosmo::core::Result<> DemoScene::render(orosmo::renderer::SceneRenderer &renderer, orosmo::renderer::Camera &camera,
                                         const DemoSceneParams &params)
{
    if (!initialized_)
    {
        return orosmo::core::makeError(orosmo::core::ErrorCode::kInvalidArgument, "DemoScene::render: scene is not initialized");
    }

    camera.setFovDegrees(params.fovDegrees);
    if (params.projection == ProjectionMode::kOrthographic)
    {
        camera.setOrthographic(kOrthoHeight);
    }
    else
    {
        camera.setPerspective();
    }
    // Le filaire (menu ET touche W du contrôleur) est déjà posé sur la caméra par Game::applySceneParams : ne pas l'écraser ici.

    renderer.setCameraRelative(params.cameraRelative);
    renderer.setRenderScale(params.renderScale);
    renderer.setUpscalingEnabled(params.renderScale < 0.999f);
    if (auto r = renderer.setCamera(camera); !r)
    {
        return r;
    }

    if (auto r = syncLights(params); !r)
    {
        return r;
    }
    renderer.setLights(&lights_);

    if (auto r = syncAccentColor(params); !r)
    {
        return r;
    }

    if (shadows_ != nullptr)
    {
        // Compteurs de l'image précédente (le plugin travaille à endFrame) : 12 premières images, puis toutes les 600.
        const orosmo::renderer::ShadowStats &s = shadows_->stats();
        if (shadowFrames_ > 0 && (shadowFrames_ <= 12 || shadowFrames_ % 600 == 0))
        {
            LOG_INFO("Shadows: ", s.views, " views, ", s.viewsUpdated, " updated (", s.staticRedraws, " static redraws, ", s.movableRedraws,
                     " movable redraws, ", s.fullRedraws, " full, ", s.cacheCopies, " cache copies), ", s.cacheHits, " cache hits, ", s.deferred,
                     " deferred, ", s.renderPasses, " shadow render passes, ", s.drawCalls, " draw calls, ", s.casterInstances, " caster instances");
        }
        ++shadowFrames_;
    }
    meshDraws_ = 0;
    if (auto r = renderer.drawMesh(groundMesh_, camera); !r)
    {
        return r;
    }
    ++meshDraws_;
    if (auto r = renderer.drawMesh(structureMesh_, camera); !r)
    {
        return r;
    }
    ++meshDraws_;
    if (auto r = renderer.drawMesh(heroMesh_, camera); !r)
    {
        return r;
    }
    ++meshDraws_;
    for (auto &orbit : orbitMeshes_)
    {
        if (auto r = renderer.drawMesh(orbit, camera); !r)
        {
            return r;
        }
        ++meshDraws_;
    }

    if (instanceRenderer_)
    {
        if (auto r = renderInstances(renderer, camera); !r)
        {
            return r;
        }
    }

    debugDraw_.clear();
    const bool frozenFrustum = frustumFreeze_.frozen();
    debugDraw_.setEnabled(params.debugDraw || frozenFrustum);
    if (frozenFrustum)
    {
        frustumFreeze_.drawFrozen(debugDraw_);
    }
    if (params.debugDraw)
    {
        debugDraw_.axes(glm::mat4(1.0f), 3.5f);
        const glm::vec3 heroHalfExtents(0.75f, 1.24f, 0.75f);
        debugDraw_.box(heroPosition_ - heroHalfExtents, heroPosition_ + heroHalfExtents, glm::vec4(0.35f, 0.90f, 1.0f, 1.0f));

        const glm::vec3 sunDir = sunDirectionFromAngles(params.sunElevation, params.sunAzimuth);
        const glm::vec3 rayOrigin(0.0f, kHeroBaseY, 0.0f);
        const glm::vec3 rayEnd = rayOrigin + sunDir * 4.5f;
        const glm::vec4 sunRayColor(1.0f, 0.90f, 0.15f, 1.0f);
        debugDraw_.line(rayOrigin, rayEnd, sunRayColor);
        debugDraw_.sphere(rayEnd, 0.22f, sunRayColor, 12);
    }
    if (params.debugDraw || frozenFrustum)
    {
        if (auto r = renderer.drawDebug(debugDraw_); !r)
        {
            return r;
        }
    }

    return {};
}

} // namespace demo
