// ASSET-04 (cuisson de maillages) : dédoublonnage, cache de sommets, LOD, quantification, conteneur .omesh. Sans GPU.
#include <doctest/doctest.h>

#include <orosmo/assets/bake.hpp>

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>

namespace as = orosmo::assets;
using orosmo::core::ErrorCode;

namespace
{

// Grille n x n de quads (2 triangles chacun), sommets partagés ; normales +Y, UV dans [0,1], tangentes +X.
as::MeshPrimitive grid(std::uint32_t n)
{
    as::MeshPrimitive p;
    for (std::uint32_t z = 0; z <= n; ++z)
    {
        for (std::uint32_t x = 0; x <= n; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(n);
            const float v = static_cast<float>(z) / static_cast<float>(n);
            p.positions.emplace_back(u * 10.0f, 0.0f, v * 10.0f);
            p.normals.emplace_back(0.0f, 1.0f, 0.0f);
            p.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
            p.uv0.emplace_back(u, v);
        }
    }
    const std::uint32_t row = n + 1;
    for (std::uint32_t z = 0; z < n; ++z)
    {
        for (std::uint32_t x = 0; x < n; ++x)
        {
            const std::uint32_t a = z * row + x, b = a + 1, c = a + row, d = c + 1;
            for (const std::uint32_t i : {a, c, b, b, c, d})
            {
                p.indices.push_back(i);
            }
        }
    }
    p.material = 3;
    return p;
}

// Mélange déterministe des triangles (graine fixe).
void shuffleTriangles(as::MeshPrimitive &p)
{
    std::vector<std::array<std::uint32_t, 3>> tris(p.indices.size() / 3);
    for (std::size_t i = 0; i < tris.size(); ++i)
    {
        tris[i] = {p.indices[i * 3], p.indices[i * 3 + 1], p.indices[i * 3 + 2]};
    }
    std::mt19937 rng(1234);
    std::shuffle(tris.begin(), tris.end(), rng);
    for (std::size_t i = 0; i < tris.size(); ++i)
    {
        std::copy(tris[i].begin(), tris[i].end(), p.indices.begin() + static_cast<std::ptrdiff_t>(i * 3));
    }
}

using Tri = std::array<std::array<float, 3>, 3>;

// Triangles sous forme de triplets de positions, arrondis à la grille de tolérance, triés (indépendant de l'ordre des triangles et de la rotation des sommets, pas du sens d'enroulement).
std::vector<Tri> triangleSet(const as::MeshPrimitive &p)
{
    std::vector<Tri> out;
    for (std::size_t i = 0; i < p.indices.size(); i += 3)
    {
        Tri t{};
        for (int k = 0; k < 3; ++k)
        {
            const auto &v = p.positions[p.indices[i + static_cast<std::size_t>(k)]];
            t[static_cast<std::size_t>(k)] = {std::round(v.x * 1000.0f) / 1000.0f, std::round(v.y * 1000.0f) / 1000.0f,
                                              std::round(v.z * 1000.0f) / 1000.0f};
        }
        // Rotation cyclique vers le plus petit sommet : l'orientation (a,b,c) != (a,c,b) est conservée.
        std::rotate(t.begin(), std::min_element(t.begin(), t.end()), t.end());
        out.push_back(t);
    }
    std::sort(out.begin(), out.end());
    return out;
}

double acmr(const as::MeshPrimitive &p)
{
    return meshopt_analyzeVertexCache(p.indices.data(), p.indices.size(), p.positions.size(), 16, 0, 0).acmr;
}

} // namespace

TEST_SUITE("assets/bake")
{
    TEST_CASE("le dédoublonnage réduit les sommets dupliqués")
    {
        // Deux triangles qui ne partagent aucun indice mais ont des sommets identiques : 6 sommets, 4 uniques.
        as::MeshPrimitive p;
        const orosmo::core::Vec3 a(0, 0, 0), b(1, 0, 0), c(0, 0, 1), d(1, 0, 1);
        p.positions = {a, b, c, b, d, c};
        p.normals.assign(6, orosmo::core::Vec3(0, 1, 0));
        p.uv0 = {{0, 0}, {1, 0}, {0, 1}, {1, 0}, {1, 1}, {0, 1}};
        p.indices = {0, 1, 2, 3, 4, 5};
        auto baked = as::bakePrimitive(p);
        REQUIRE(baked.has_value());
        CHECK((baked->vertexCount() == 4));
        CHECK((baked->lods[0].size() == 6));
        CHECK((baked->material == -1));

        as::BakeOptions off;
        off.optimize = false;
        auto raw = as::bakePrimitive(p, off);
        REQUIRE(raw.has_value());
        CHECK((raw->vertexCount() == 6));
    }

    TEST_CASE("le cache de sommets n'est pas dégradé et les triangles sont conservés")
    {
        as::MeshPrimitive p = grid(32);
        shuffleTriangles(p);
        const double before = acmr(p);
        as::BakeOptions options;
        options.lodRatios.clear();
        auto baked = as::bakePrimitive(p, options);
        REQUIRE(baked.has_value());
        auto back = as::unbake(*baked);
        REQUIRE(back.has_value());
        CHECK((acmr(*back) <= before));
        CHECK((acmr(*back) < before * 0.6)); // une grille mélangée gagne beaucoup
        CHECK((baked->vertexCount() == p.positions.size()));
        CHECK((triangleSet(*back) == triangleSet(p)));
        CHECK((back->material == 3));
    }

    TEST_CASE("les LOD réduisent les indices et restent valides")
    {
        const as::MeshPrimitive p = grid(64);
        auto baked = as::bakePrimitive(p);
        REQUIRE(baked.has_value());
        REQUIRE((baked->lods.size() >= 2));
        const std::size_t full = baked->lods[0].size();
        CHECK((full == p.indices.size()));
        CHECK((baked->lods[1].size() <= full * 6 / 10));
        for (std::size_t l = 1; l < baked->lods.size(); ++l)
        {
            CHECK((baked->lods[l].size() < baked->lods[l - 1].size()));
            CHECK((baked->lods[l].size() % 3 == 0));
            CHECK((*std::max_element(baked->lods[l].begin(), baked->lods[l].end()) < baked->vertexCount()));
        }
        auto lod1 = as::unbake(*baked, 1);
        REQUIRE(lod1.has_value());
        CHECK((lod1->indices.size() == baked->lods[1].size()));
        const auto missing = as::unbake(*baked, baked->lods.size());
        REQUIRE_FALSE(missing.has_value());
        CHECK((missing.error().code == ErrorCode::kOutOfRange));
    }

    TEST_CASE("la quantification reste dans la tolérance")
    {
        as::MeshPrimitive p = grid(8);
        for (std::size_t i = 0; i < p.normals.size(); ++i)
        {
            const float t = static_cast<float>(i) * 0.37f;
            p.normals[i] = glm::normalize(orosmo::core::Vec3(std::sin(t), std::cos(t * 0.7f) + 1.5f, std::sin(t * 1.3f)));
        }
        as::BakeOptions options;
        options.optimize = false;
        options.lodRatios.clear();
        auto baked = as::bakePrimitive(p, options);
        REQUIRE(baked.has_value());
        CHECK((baked->quantized));
        auto back = as::unbake(*baked);
        REQUIRE(back.has_value());
        REQUIRE((back->positions == p.positions));
        for (std::size_t i = 0; i < p.normals.size(); ++i)
        {
            CHECK((glm::length(back->normals[i] - p.normals[i]) < 1e-4f));
            CHECK((glm::length(back->tangents[i] - p.tangents[i]) < 1e-4f));
            CHECK((glm::length(back->uv0[i] - p.uv0[i]) < 1e-3f));
        }

        options.quantize = false;
        auto exact = as::bakePrimitive(p, options);
        REQUIRE(exact.has_value());
        CHECK_FALSE(exact->quantized);
        auto exactBack = as::unbake(*exact);
        REQUIRE(exactBack.has_value());
        CHECK((exactBack->normals == p.normals));
        CHECK((exactBack->uv0 == p.uv0));
    }

    TEST_CASE("couleurs, articulations et poids traversent la cuisson")
    {
        as::MeshPrimitive p = grid(2);
        const std::size_t n = p.positions.size();
        p.colors.assign(n, orosmo::core::Vec4(1.0f, 0.5f, 0.0f, 1.0f));
        p.joints.assign(n, glm::u16vec4(1, 2, 3, 4));
        p.weights.assign(n, orosmo::core::Vec4(0.5f, 0.25f, 0.25f, 0.0f));
        p.uv1 = p.uv0;
        auto baked = as::bakePrimitive(p);
        REQUIRE(baked.has_value());
        auto back = as::unbake(*baked);
        REQUIRE(back.has_value());
        CHECK((back->joints.front() == glm::u16vec4(1, 2, 3, 4)));
        CHECK((std::abs(back->weights.front().x - 0.5f) < 1e-4f));
        CHECK((std::abs(back->colors.front().y - 0.5f) < 1e-2f));
        CHECK((back->uv1.size() == n));
    }

    TEST_CASE("aller-retour .omesh, déterminisme")
    {
        as::MeshPrimitive p = grid(16);
        shuffleTriangles(p);
        auto a = as::bakePrimitive(p);
        auto b = as::bakePrimitive(p);
        REQUIRE((a.has_value() && b.has_value()));
        as::BakeOptions exact;
        exact.quantize = false;
        auto c = as::bakePrimitive(grid(4), exact);
        REQUIRE(c.has_value());
        const std::array<as::BakedPrimitive, 2> prims{*a, *c};
        auto bytesA = as::writeBakedMesh(std::span<const as::BakedPrimitive>(prims));
        const std::array<as::BakedPrimitive, 2> prims2{*b, prims[1]};
        auto bytesB = as::writeBakedMesh(std::span<const as::BakedPrimitive>(prims2));
        REQUIRE((bytesA.has_value() && bytesB.has_value()));
        CHECK((*bytesA == *bytesB));

        auto read = as::readBakedMesh(*bytesA);
        REQUIRE(read.has_value());
        REQUIRE((read->size() == 2));
        for (std::size_t i = 0; i < 2; ++i)
        {
            const as::BakedPrimitive &x = prims[i];
            const as::BakedPrimitive &y = (*read)[i];
            CHECK((x.quantized == y.quantized));
            CHECK((x.positions == y.positions));
            CHECK((x.normals == y.normals));
            CHECK((x.normalsF == y.normalsF));
            CHECK((x.tangents == y.tangents));
            CHECK((x.tangentsF == y.tangentsF));
            CHECK((x.uv0 == y.uv0));
            CHECK((x.uv0F == y.uv0F));
            CHECK((x.lods == y.lods));
            CHECK((x.material == y.material));
            CHECK((x.boundsMin == y.boundsMin));
            CHECK((x.boundsMax == y.boundsMax));
        }
        // Réécrire ce qui a été lu redonne les mêmes octets.
        auto again = as::writeBakedMesh(*read);
        REQUIRE(again.has_value());
        CHECK((*again == *bytesA));
    }

    TEST_CASE("fichiers .omesh invalides")
    {
        auto baked = as::bakePrimitive(grid(8));
        REQUIRE(baked.has_value());
        const std::array<as::BakedPrimitive, 1> one{*baked};
        auto bytes = as::writeBakedMesh(std::span<const as::BakedPrimitive>(one));
        REQUIRE(bytes.has_value());

        // Tronqué à toutes les longueurs : jamais d'accès hors tampon, toujours kParse.
        for (std::size_t len = 0; len < bytes->size(); len += (len < 80 ? 1 : 97))
        {
            auto r = as::readBakedMesh(std::span<const std::byte>(bytes->data(), len));
            REQUIRE_FALSE(r.has_value());
            CHECK((r.error().code == ErrorCode::kParse));
        }

        auto badMagic = *bytes;
        badMagic[0] = std::byte{'X'};
        auto r1 = as::readBakedMesh(badMagic);
        REQUIRE_FALSE(r1.has_value());
        CHECK((r1.error().code == ErrorCode::kParse));

        auto badVersion = *bytes;
        badVersion[4] = std::byte{99};
        auto r2 = as::readBakedMesh(badVersion);
        REQUIRE_FALSE(r2.has_value());
        CHECK((r2.error().code == ErrorCode::kVersionMismatch));

        auto trailing = *bytes;
        trailing.push_back(std::byte{0});
        auto r3 = as::readBakedMesh(trailing);
        REQUIRE_FALSE(r3.has_value());
        CHECK((r3.error().code == ErrorCode::kParse));

        // Indice hors bornes : le dernier u32 du fichier est un indice du dernier LOD.
        auto badIndex = *bytes;
        for (std::size_t k = badIndex.size() - 4; k < badIndex.size(); ++k)
        {
            badIndex[k] = std::byte{0xff};
        }
        auto r4 = as::readBakedMesh(badIndex);
        REQUIRE_FALSE(r4.has_value());
        CHECK((r4.error().code == ErrorCode::kParse));

        // Nombre de primitives gigantesque : refusé sans allocation.
        auto hugeCount = *bytes;
        hugeCount[8] = hugeCount[9] = hugeCount[10] = hugeCount[11] = std::byte{0xff};
        auto r5 = as::readBakedMesh(hugeCount);
        REQUIRE_FALSE(r5.has_value());
        CHECK((r5.error().code == ErrorCode::kParse));
    }

    TEST_CASE("entrées invalides")
    {
        const auto expectInvalid = [](const as::MeshPrimitive &p, const as::BakeOptions &o = {})
        {
            auto r = as::bakePrimitive(p, o);
            REQUIRE_FALSE(r.has_value());
            CHECK((r.error().code == ErrorCode::kInvalidArgument));
        };
        expectInvalid(as::MeshPrimitive{});

        as::MeshPrimitive badIndex = grid(2);
        badIndex.indices.back() = 9999;
        expectInvalid(badIndex);

        as::MeshPrimitive badCount = grid(2);
        badCount.indices.pop_back();
        expectInvalid(badCount);

        as::MeshPrimitive badAttr = grid(2);
        badAttr.uv0.pop_back();
        expectInvalid(badAttr);

        as::BakedPrimitive none;
        const std::array<as::BakedPrimitive, 1> arr{none};
        auto w = as::writeBakedMesh(std::span<const as::BakedPrimitive>(arr));
        REQUIRE_FALSE(w.has_value());
        CHECK((w.error().code == ErrorCode::kInvalidArgument));
    }

    TEST_CASE("un ratio de LOD non fini est refusé avant toute conversion")
    {
        as::BakeOptions nan;
        nan.lodRatios = {std::numeric_limits<float>::quiet_NaN()};
        auto a = as::bakePrimitive(grid(4), nan);
        REQUIRE_FALSE(a.has_value());
        CHECK((a.error().code == ErrorCode::kInvalidArgument));
        as::BakeOptions inf;
        inf.lodRatios = {0.5f, std::numeric_limits<float>::infinity()};
        auto b = as::bakePrimitive(grid(4), inf);
        REQUIRE_FALSE(b.has_value());
        CHECK((b.error().code == ErrorCode::kInvalidArgument));
    }

    TEST_CASE("un UV fini hors de la plage half ne devient jamais infini")
    {
        as::MeshPrimitive p = grid(2);
        p.uv0.front() = {70000.25f, 0.5f};
        auto quantized = as::bakePrimitive(p);
        REQUIRE_FALSE(quantized.has_value());
        CHECK((quantized.error().code == ErrorCode::kInvalidArgument));
        as::BakeOptions full;
        full.quantize = false;
        auto exact = as::bakePrimitive(p, full);
        REQUIRE(exact.has_value());
        auto back = as::unbake(*exact);
        REQUIRE(back.has_value());
        CHECK(std::isfinite(back->uv0.front().x));
    }
}
