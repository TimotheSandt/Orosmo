#include "orosmo/assets/bake.hpp"

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace orosmo::assets
{

using core::ErrorCode;
using core::makeError;

namespace
{

constexpr std::uint32_t kVersion = 1;
constexpr std::array<char, 4> kMagic{'O', 'M', 'S', 'H'};

enum Attr : std::uint32_t
{
    kQuantized = 1u << 0,
    kNormals = 1u << 1,
    kTangents = 1u << 2,
    kUv0 = 1u << 3,
    kUv1 = 1u << 4,
    kColors = 1u << 5,
    kJoints = 1u << 6,
    kWeights = 1u << 7,
    kAllMask = (1u << 8) - 1,
};

std::uint32_t maskOf(const BakedPrimitive &b) noexcept
{
    std::uint32_t m = b.quantized ? kQuantized : 0u;
    const bool hasN = b.quantized ? !b.normals.empty() : !b.normalsF.empty();
    const bool hasT = b.quantized ? !b.tangents.empty() : !b.tangentsF.empty();
    const bool hasU0 = b.quantized ? !b.uv0.empty() : !b.uv0F.empty();
    const bool hasU1 = b.quantized ? !b.uv1.empty() : !b.uv1F.empty();
    m |= hasN ? kNormals : 0u;
    m |= hasT ? kTangents : 0u;
    m |= hasU0 ? kUv0 : 0u;
    m |= hasU1 ? kUv1 : 0u;
    m |= b.colors.empty() ? 0u : kColors;
    m |= b.joints.empty() ? 0u : kJoints;
    m |= b.weights.empty() ? 0u : kWeights;
    return m;
}

// Vérifie la cohérence : tailles des flux, une seule famille (quantifiée ou flottante), LOD valides.
core::Result<> checkBaked(const BakedPrimitive &b)
{
    const std::size_t n = b.positions.size();
    if (n == 0 || n > std::numeric_limits<std::uint32_t>::max())
    {
        return makeError(ErrorCode::kInvalidArgument, "primitive cuite sans sommet");
    }
    const auto sized = [n](std::size_t count) { return count == 0 || count == n; };
    const bool stray = b.quantized ? (!b.normalsF.empty() || !b.tangentsF.empty() || !b.uv0F.empty() || !b.uv1F.empty())
                                   : (!b.normals.empty() || !b.tangents.empty() || !b.uv0.empty() || !b.uv1.empty());
    if (stray || !sized(b.normals.size()) || !sized(b.tangents.size()) || !sized(b.uv0.size()) || !sized(b.uv1.size()) ||
        !sized(b.normalsF.size()) || !sized(b.tangentsF.size()) || !sized(b.uv0F.size()) || !sized(b.uv1F.size()) ||
        !sized(b.colors.size()) || !sized(b.joints.size()) || !sized(b.weights.size()))
    {
        return makeError(ErrorCode::kInvalidArgument, "attributs cuits de longueur incohérente");
    }
    if (b.lods.empty())
    {
        return makeError(ErrorCode::kInvalidArgument, "primitive cuite sans LOD");
    }
    // Le format .omesh écrit tous ses compteurs en u32 : un compteur plus grand serait tronqué en silence.
    constexpr std::size_t kMaxCount = std::numeric_limits<std::uint32_t>::max();
    if (b.lods.size() > kMaxCount)
    {
        return makeError(ErrorCode::kInvalidArgument, "trop de LOD pour le format .omesh");
    }
    for (const auto &lod : b.lods)
    {
        if (lod.size() > kMaxCount)
        {
            return makeError(ErrorCode::kInvalidArgument, "LOD de plus de 2^32 - 1 indices : non représentable dans le format .omesh");
        }
        if (lod.empty() || lod.size() % 3 != 0)
        {
            return makeError(ErrorCode::kInvalidArgument, "LOD vide ou indices non multiples de 3");
        }
        if (std::any_of(lod.begin(), lod.end(), [n](std::uint32_t i) { return i >= n; }))
        {
            return makeError(ErrorCode::kInvalidArgument, "indice de LOD hors des sommets");
        }
    }
    return {};
}

// ---- Tampon interleavé de flottants, sur lequel travaille meshoptimizer ----

struct Layout
{
    std::size_t normal = 0, tangent = 0, uv0 = 0, uv1 = 0, color = 0, joint = 0, weight = 0, stride = 3;
};

Layout layoutOf(const MeshPrimitive &p)
{
    Layout l;
    const auto take = [&l](bool present, std::size_t &offset, std::size_t size) {
        if (present)
        {
            offset = l.stride;
            l.stride += size;
        }
    };
    take(!p.normals.empty(), l.normal, 3);
    take(!p.tangents.empty(), l.tangent, 4);
    take(!p.uv0.empty(), l.uv0, 2);
    take(!p.uv1.empty(), l.uv1, 2);
    take(!p.colors.empty(), l.color, 4);
    take(!p.joints.empty(), l.joint, 4);
    take(!p.weights.empty(), l.weight, 4);
    return l;
}

template <typename V, std::size_t N> void putVec(float *dst, const V &v)
{
    for (std::size_t k = 0; k < N; ++k)
    {
        dst[k] = static_cast<float>(v[static_cast<int>(k)]);
    }
}

std::vector<float> interleave(const MeshPrimitive &p, const Layout &l)
{
    const std::size_t n = p.positions.size();
    std::vector<float> out(n * l.stride, 0.0f);
    for (std::size_t i = 0; i < n; ++i)
    {
        float *v = &out[i * l.stride];
        putVec<core::Vec3, 3>(v, p.positions[i]);
        if (!p.normals.empty()) putVec<core::Vec3, 3>(v + l.normal, p.normals[i]);
        if (!p.tangents.empty()) putVec<core::Vec4, 4>(v + l.tangent, p.tangents[i]);
        if (!p.uv0.empty()) putVec<core::Vec2, 2>(v + l.uv0, p.uv0[i]);
        if (!p.uv1.empty()) putVec<core::Vec2, 2>(v + l.uv1, p.uv1[i]);
        if (!p.colors.empty()) putVec<core::Vec4, 4>(v + l.color, p.colors[i]);
        if (!p.joints.empty()) putVec<glm::u16vec4, 4>(v + l.joint, p.joints[i]);
        if (!p.weights.empty()) putVec<core::Vec4, 4>(v + l.weight, p.weights[i]);
    }
    return out;
}

std::int16_t snorm(float v) { return static_cast<std::int16_t>(meshopt_quantizeSnorm(std::clamp(v, -1.0f, 1.0f), 16)); }
std::uint16_t unorm16(float v) { return static_cast<std::uint16_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 65535.0f)); }
std::uint8_t unorm8(float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }
float unsnorm(std::int16_t v) { return std::max(static_cast<float>(v) / 32767.0f, -1.0f); }

BakedPrimitive pack(const std::vector<float> &buf, const Layout &l, const MeshPrimitive &src, bool quantize)
{
    const std::size_t n = buf.size() / l.stride;
    BakedPrimitive b;
    b.quantized = quantize;
    b.material = src.material;
    b.positions.resize(n);
    const bool hasN = !src.normals.empty(), hasT = !src.tangents.empty(), hasU0 = !src.uv0.empty(), hasU1 = !src.uv1.empty();
    (quantize ? b.normals.resize(hasN ? n : 0) : b.normalsF.resize(hasN ? n : 0));
    (quantize ? b.tangents.resize(hasT ? n : 0) : b.tangentsF.resize(hasT ? n : 0));
    (quantize ? b.uv0.resize(hasU0 ? n : 0) : b.uv0F.resize(hasU0 ? n : 0));
    (quantize ? b.uv1.resize(hasU1 ? n : 0) : b.uv1F.resize(hasU1 ? n : 0));
    b.colors.resize(src.colors.empty() ? 0 : n);
    b.joints.resize(src.joints.empty() ? 0 : n);
    b.weights.resize(src.weights.empty() ? 0 : n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const float *v = &buf[i * l.stride];
        b.positions[i] = {v[0], v[1], v[2]};
        if (hasN)
        {
            const float *a = v + l.normal;
            quantize ? void(b.normals[i] = {snorm(a[0]), snorm(a[1]), snorm(a[2]), 0}) : void(b.normalsF[i] = {a[0], a[1], a[2]});
        }
        if (hasT)
        {
            const float *a = v + l.tangent;
            quantize ? void(b.tangents[i] = {snorm(a[0]), snorm(a[1]), snorm(a[2]), snorm(a[3])})
                     : void(b.tangentsF[i] = {a[0], a[1], a[2], a[3]});
        }
        if (hasU0)
        {
            const float *a = v + l.uv0;
            quantize ? void(b.uv0[i] = {meshopt_quantizeHalf(a[0]), meshopt_quantizeHalf(a[1])}) : void(b.uv0F[i] = {a[0], a[1]});
        }
        if (hasU1)
        {
            const float *a = v + l.uv1;
            quantize ? void(b.uv1[i] = {meshopt_quantizeHalf(a[0]), meshopt_quantizeHalf(a[1])}) : void(b.uv1F[i] = {a[0], a[1]});
        }
        if (!b.colors.empty())
        {
            const float *a = v + l.color;
            b.colors[i] = {unorm8(a[0]), unorm8(a[1]), unorm8(a[2]), unorm8(a[3])};
        }
        if (!b.joints.empty())
        {
            const float *a = v + l.joint;
            b.joints[i] = {static_cast<std::uint16_t>(a[0]), static_cast<std::uint16_t>(a[1]), static_cast<std::uint16_t>(a[2]),
                           static_cast<std::uint16_t>(a[3])};
        }
        if (!b.weights.empty())
        {
            const float *a = v + l.weight;
            b.weights[i] = {unorm16(a[0]), unorm16(a[1]), unorm16(a[2]), unorm16(a[3])};
        }
    }
    return b;
}

core::Result<> checkSource(const MeshPrimitive &p)
{
    const std::size_t n = p.positions.size();
    if (n == 0 || p.indices.empty())
    {
        return makeError(ErrorCode::kInvalidArgument, "primitive vide");
    }
    if (n > std::numeric_limits<std::uint32_t>::max() || p.indices.size() % 3 != 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "indices non multiples de 3 ou trop de sommets");
    }
    const auto sized = [n](std::size_t count) { return count == 0 || count == n; };
    if (!sized(p.normals.size()) || !sized(p.tangents.size()) || !sized(p.uv0.size()) || !sized(p.uv1.size()) ||
        !sized(p.colors.size()) || !sized(p.joints.size()) || !sized(p.weights.size()))
    {
        return makeError(ErrorCode::kInvalidArgument, "attribut de longueur différente des positions");
    }
    if (std::any_of(p.indices.begin(), p.indices.end(), [n](std::uint32_t i) { return i >= n; }))
    {
        return makeError(ErrorCode::kInvalidArgument, "indice hors des sommets");
    }
    return {};
}

// ---- Sérialisation ----

class Writer
{
public:
    void u8(std::uint8_t v) { out_.push_back(static_cast<std::byte>(v)); }
    void u16(std::uint16_t v) { u8(static_cast<std::uint8_t>(v & 0xff)), u8(static_cast<std::uint8_t>(v >> 8)); }
    void u32(std::uint32_t v) { u16(static_cast<std::uint16_t>(v & 0xffff)), u16(static_cast<std::uint16_t>(v >> 16)); }
    void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void magic() { for (char c : kMagic) u8(static_cast<std::uint8_t>(c)); }
    std::vector<std::byte> take() { return std::move(out_); }

private:
    std::vector<std::byte> out_;
};

class Reader
{
public:
    explicit Reader(std::span<const std::byte> d) : d_(d) {}
    std::size_t remaining() const noexcept { return d_.size() - pos_; }
    // Vrai si `count` éléments de `size` octets tiennent encore (sans dépassement d'entier).
    bool has(std::size_t count, std::size_t size) const noexcept { return count <= remaining() / size; }
    bool fail() const noexcept { return failed_; }
    std::uint8_t u8()
    {
        if (remaining() < 1)
        {
            failed_ = true;
            return 0;
        }
        return static_cast<std::uint8_t>(d_[pos_++]);
    }
    std::uint16_t u16()
    {
        const std::uint16_t lo = u8();
        return static_cast<std::uint16_t>(lo | (u8() << 8));
    }
    std::uint32_t u32()
    {
        const std::uint32_t lo = u16();
        return lo | (static_cast<std::uint32_t>(u16()) << 16);
    }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    float f32() { return std::bit_cast<float>(u32()); }

private:
    std::span<const std::byte> d_;
    std::size_t pos_ = 0;
    bool failed_ = false;
};

template <typename V, typename F> void writeStream(Writer &w, const std::vector<V> &s, F &&each)
{
    for (const V &v : s) each(w, v);
}

} // namespace

core::Result<BakedPrimitive> bakePrimitive(const MeshPrimitive &primitive, const BakeOptions &options)
{
    if (auto ok = checkSource(primitive); !ok)
    {
        return std::unexpected(ok.error());
    }
    for (const float ratio : options.lodRatios)
    {
        if (!std::isfinite(ratio))
        {
            return makeError(ErrorCode::kInvalidArgument, "bakePrimitive: lodRatios contient une valeur non finie");
        }
    }
    if (options.quantize)
    {
        // meshopt_quantizeHalf renvoie l'infini au-delà de 65504 (borne de binary16) : refus explicite plutôt qu'un UV infini.
        constexpr float kHalfMax = 65504.0f;
        const auto outside = [](const std::vector<core::Vec2> &uvs)
        { return std::any_of(uvs.begin(), uvs.end(), [](const core::Vec2 &v) { return !(std::abs(v.x) <= kHalfMax && std::abs(v.y) <= kHalfMax); }); };
        if (outside(primitive.uv0) || outside(primitive.uv1))
        {
            return makeError(ErrorCode::kInvalidArgument,
                             "bakePrimitive: UV hors de [-65504, 65504] (binary16) ; utiliser quantize = false pour ces UV");
        }
    }
    const Layout layout = layoutOf(primitive);
    std::vector<float> vertices = interleave(primitive, layout);
    std::vector<std::uint32_t> indices = primitive.indices;
    const std::size_t vsize = layout.stride * sizeof(float);

    if (options.optimize)
    {
        std::vector<unsigned int> remap(indices.size() > 0 ? primitive.positions.size() : 0);
        const std::size_t unique =
            meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(), vertices.data(), primitive.positions.size(), vsize);
        std::vector<float> merged(unique * layout.stride);
        meshopt_remapVertexBuffer(merged.data(), vertices.data(), primitive.positions.size(), vsize, remap.data());
        std::vector<std::uint32_t> remapped(indices.size());
        meshopt_remapIndexBuffer(remapped.data(), indices.data(), indices.size(), remap.data());
        vertices = std::move(merged);
        indices = std::move(remapped);

        meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), unique);
        meshopt_optimizeOverdraw(indices.data(), indices.data(), indices.size(), vertices.data(), unique, vsize, 1.05f);
        std::vector<float> fetched(vertices.size());
        const std::size_t kept = meshopt_optimizeVertexFetch(fetched.data(), indices.data(), indices.size(), vertices.data(), unique, vsize);
        fetched.resize(kept * layout.stride);
        vertices = std::move(fetched);
    }

    const std::size_t vertexCount = vertices.size() / layout.stride;
    BakedPrimitive baked = pack(vertices, layout, primitive, options.quantize);
    baked.lods.push_back(indices);

    core::Vec3 lo(std::numeric_limits<float>::max());
    core::Vec3 hi(std::numeric_limits<float>::lowest());
    for (const core::Vec3 &p : baked.positions)
    {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    baked.boundsMin = lo;
    baked.boundsMax = hi;

    // LOD : simplification sur les positions ; un niveau qui ne réduit pas assez est abandonné, et les suivants avec lui.
    std::size_t previous = indices.size();
    for (const float ratio : options.lodRatios)
    {
        const std::size_t target = static_cast<std::size_t>(static_cast<double>(indices.size()) * std::clamp(ratio, 0.0f, 1.0f)) / 3 * 3;
        if (target < 3 || target >= previous)
        {
            break;
        }
        std::vector<std::uint32_t> lod(indices.size());
        const std::size_t got = meshopt_simplify(lod.data(), indices.data(), indices.size(), vertices.data(), vertexCount, vsize, target,
                                                 options.lodError, 0, nullptr);
        if (got < 3 || got >= previous)
        {
            break;
        }
        lod.resize(got);
        previous = got;
        baked.lods.push_back(std::move(lod));
    }
    return baked;
}

core::Result<MeshPrimitive> unbake(const BakedPrimitive &b, std::size_t lod)
{
    if (auto ok = checkBaked(b); !ok)
    {
        return std::unexpected(ok.error());
    }
    if (lod >= b.lods.size())
    {
        return makeError(ErrorCode::kOutOfRange, "LOD " + std::to_string(lod) + " absent (" + std::to_string(b.lods.size()) + " niveaux)");
    }
    const std::size_t n = b.positions.size();
    MeshPrimitive p;
    p.positions = b.positions;
    p.indices = b.lods[lod];
    p.material = b.material;
    p.boundsMin = b.boundsMin;
    p.boundsMax = b.boundsMax;
    if (b.quantized)
    {
        for (const auto &v : b.normals) p.normals.emplace_back(unsnorm(v.x), unsnorm(v.y), unsnorm(v.z));
        for (const auto &v : b.tangents) p.tangents.emplace_back(unsnorm(v.x), unsnorm(v.y), unsnorm(v.z), unsnorm(v.w));
        for (const auto &v : b.uv0) p.uv0.emplace_back(meshopt_dequantizeHalf(v.x), meshopt_dequantizeHalf(v.y));
        for (const auto &v : b.uv1) p.uv1.emplace_back(meshopt_dequantizeHalf(v.x), meshopt_dequantizeHalf(v.y));
    }
    else
    {
        p.normals = b.normalsF;
        p.tangents = b.tangentsF;
        p.uv0 = b.uv0F;
        p.uv1 = b.uv1F;
    }
    for (const auto &c : b.colors) p.colors.emplace_back(c.x / 255.0f, c.y / 255.0f, c.z / 255.0f, c.w / 255.0f);
    p.joints = b.joints;
    for (const auto &w : b.weights) p.weights.emplace_back(w.x / 65535.0f, w.y / 65535.0f, w.z / 65535.0f, w.w / 65535.0f);
    (void)n;
    return p;
}

core::Result<std::vector<std::byte>> writeBakedMesh(std::span<const BakedPrimitive> primitives)
{
    if (primitives.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return makeError(ErrorCode::kInvalidArgument, "trop de primitives pour le format .omesh");
    }
    Writer w;
    w.magic();
    w.u32(kVersion);
    w.u32(static_cast<std::uint32_t>(primitives.size()));
    for (const BakedPrimitive &b : primitives)
    {
        if (auto ok = checkBaked(b); !ok)
        {
            return std::unexpected(ok.error());
        }
        w.i32(b.material);
        for (int k = 0; k < 3; ++k) w.f32(b.boundsMin[k]);
        for (int k = 0; k < 3; ++k) w.f32(b.boundsMax[k]);
        w.u32(static_cast<std::uint32_t>(b.positions.size()));
        w.u32(maskOf(b));
        w.u32(static_cast<std::uint32_t>(b.lods.size()));
        for (const auto &lod : b.lods) w.u32(static_cast<std::uint32_t>(lod.size()));
        writeStream(w, b.positions, [](Writer &o, const core::Vec3 &v) { o.f32(v.x), o.f32(v.y), o.f32(v.z); });
        writeStream(w, b.normals, [](Writer &o, const glm::i16vec4 &v) { o.i16(v.x), o.i16(v.y), o.i16(v.z), o.i16(v.w); });
        writeStream(w, b.normalsF, [](Writer &o, const core::Vec3 &v) { o.f32(v.x), o.f32(v.y), o.f32(v.z); });
        writeStream(w, b.tangents, [](Writer &o, const glm::i16vec4 &v) { o.i16(v.x), o.i16(v.y), o.i16(v.z), o.i16(v.w); });
        writeStream(w, b.tangentsF, [](Writer &o, const core::Vec4 &v) { o.f32(v.x), o.f32(v.y), o.f32(v.z), o.f32(v.w); });
        writeStream(w, b.uv0, [](Writer &o, const glm::u16vec2 &v) { o.u16(v.x), o.u16(v.y); });
        writeStream(w, b.uv0F, [](Writer &o, const core::Vec2 &v) { o.f32(v.x), o.f32(v.y); });
        writeStream(w, b.uv1, [](Writer &o, const glm::u16vec2 &v) { o.u16(v.x), o.u16(v.y); });
        writeStream(w, b.uv1F, [](Writer &o, const core::Vec2 &v) { o.f32(v.x), o.f32(v.y); });
        writeStream(w, b.colors, [](Writer &o, const glm::u8vec4 &v) { o.u8(v.x), o.u8(v.y), o.u8(v.z), o.u8(v.w); });
        writeStream(w, b.joints, [](Writer &o, const glm::u16vec4 &v) { o.u16(v.x), o.u16(v.y), o.u16(v.z), o.u16(v.w); });
        writeStream(w, b.weights, [](Writer &o, const glm::u16vec4 &v) { o.u16(v.x), o.u16(v.y), o.u16(v.z), o.u16(v.w); });
        for (const auto &lod : b.lods) for (const std::uint32_t i : lod) w.u32(i);
    }
    return w.take();
}

core::Result<std::vector<BakedPrimitive>> readBakedMesh(std::span<const std::byte> bytes)
{
    const auto truncated = [] { return makeError(ErrorCode::kParse, "fichier .omesh tronqué ou corrompu"); };
    Reader r(bytes);
    if (!r.has(kMagic.size() + 8, 1))
    {
        return truncated();
    }
    for (char c : kMagic)
    {
        if (r.u8() != static_cast<std::uint8_t>(c))
        {
            return makeError(ErrorCode::kParse, "signature .omesh invalide");
        }
    }
    if (const std::uint32_t version = r.u32(); version != kVersion)
    {
        return makeError(ErrorCode::kVersionMismatch, "version .omesh " + std::to_string(version) + " inconnue (attendue " +
                                                          std::to_string(kVersion) + ")");
    }
    const std::uint32_t count = r.u32();
    constexpr std::size_t kMinPrimitive = 4 + 24 + 12; // matériau, bornes, sommets/masque/LOD
    if (!r.has(count, kMinPrimitive))
    {
        return truncated();
    }
    std::vector<BakedPrimitive> out;
    out.reserve(count);
    for (std::uint32_t pi = 0; pi < count; ++pi)
    {
        if (!r.has(1, kMinPrimitive))
        {
            return truncated();
        }
        BakedPrimitive b;
        b.material = r.i32();
        for (int k = 0; k < 3; ++k) b.boundsMin[k] = r.f32();
        for (int k = 0; k < 3; ++k) b.boundsMax[k] = r.f32();
        const std::uint32_t n = r.u32();
        const std::uint32_t mask = r.u32();
        const std::uint32_t lodCount = r.u32();
        if ((mask & ~kAllMask) != 0 || n == 0 || lodCount == 0 || !r.has(lodCount, 4))
        {
            return makeError(ErrorCode::kParse, "en-tête de primitive .omesh invalide");
        }
        b.quantized = (mask & kQuantized) != 0;
        std::vector<std::uint32_t> lodSizes(lodCount);
        std::size_t totalIndices = 0;
        for (auto &s : lodSizes)
        {
            s = r.u32();
            if (s == 0 || s % 3 != 0)
            {
                return makeError(ErrorCode::kParse, "LOD .omesh invalide");
            }
            totalIndices += s;
        }
        // Tous les flux annoncés doivent tenir dans le reste du fichier avant la moindre allocation.
        const auto flag = [mask](Attr a) { return (mask & a) != 0; };
        const bool q = b.quantized;
        const std::size_t perVertex = 12 + (flag(kNormals) ? (q ? 8 : 12) : 0) + (flag(kTangents) ? (q ? 8 : 16) : 0) +
                                      (flag(kUv0) ? (q ? 4 : 8) : 0) + (flag(kUv1) ? (q ? 4 : 8) : 0) + (flag(kColors) ? 4 : 0) +
                                      (flag(kJoints) ? 8 : 0) + (flag(kWeights) ? 8 : 0);
        if (!r.has(n, perVertex) || !r.has(totalIndices, 4) || r.remaining() < static_cast<std::size_t>(n) * perVertex + totalIndices * 4)
        {
            return truncated();
        }
        const auto readAll = [n](auto &vec, auto &&one) {
            vec.resize(n);
            for (auto &v : vec) v = one();
        };
        readAll(b.positions, [&] { const float x = r.f32(), y = r.f32(), z = r.f32(); return core::Vec3(x, y, z); });
        if (flag(kNormals))
        {
            if (q) readAll(b.normals, [&] { const auto x = r.i16(), y = r.i16(), z = r.i16(), w = r.i16(); return glm::i16vec4(x, y, z, w); });
            else readAll(b.normalsF, [&] { const float x = r.f32(), y = r.f32(), z = r.f32(); return core::Vec3(x, y, z); });
        }
        if (flag(kTangents))
        {
            if (q) readAll(b.tangents, [&] { const auto x = r.i16(), y = r.i16(), z = r.i16(), w = r.i16(); return glm::i16vec4(x, y, z, w); });
            else readAll(b.tangentsF, [&] { const float x = r.f32(), y = r.f32(), z = r.f32(), w = r.f32(); return core::Vec4(x, y, z, w); });
        }
        if (flag(kUv0))
        {
            if (q) readAll(b.uv0, [&] { const auto x = r.u16(), y = r.u16(); return glm::u16vec2(x, y); });
            else readAll(b.uv0F, [&] { const float x = r.f32(), y = r.f32(); return core::Vec2(x, y); });
        }
        if (flag(kUv1))
        {
            if (q) readAll(b.uv1, [&] { const auto x = r.u16(), y = r.u16(); return glm::u16vec2(x, y); });
            else readAll(b.uv1F, [&] { const float x = r.f32(), y = r.f32(); return core::Vec2(x, y); });
        }
        if (flag(kColors)) readAll(b.colors, [&] { const auto x = r.u8(), y = r.u8(), z = r.u8(), w = r.u8(); return glm::u8vec4(x, y, z, w); });
        if (flag(kJoints)) readAll(b.joints, [&] { const auto x = r.u16(), y = r.u16(), z = r.u16(), w = r.u16(); return glm::u16vec4(x, y, z, w); });
        if (flag(kWeights)) readAll(b.weights, [&] { const auto x = r.u16(), y = r.u16(), z = r.u16(), w = r.u16(); return glm::u16vec4(x, y, z, w); });
        for (const std::uint32_t s : lodSizes)
        {
            std::vector<std::uint32_t> lod(s);
            for (auto &i : lod)
            {
                i = r.u32();
                if (i >= n)
                {
                    return makeError(ErrorCode::kParse, "indice .omesh hors des sommets");
                }
            }
            b.lods.push_back(std::move(lod));
        }
        if (r.fail())
        {
            return truncated();
        }
        out.push_back(std::move(b));
    }
    if (r.remaining() != 0)
    {
        return makeError(ErrorCode::kParse, "octets en trop après les primitives .omesh");
    }
    return out;
}

} // namespace orosmo::assets
