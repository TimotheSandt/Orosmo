#include "orosmo/world/export.hpp"

#include "orosmo/core/tasks.hpp"

#include <nlohmann/json.hpp>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <tuple>

namespace orosmo::world
{

using core::ErrorCode;
using core::makeError;

namespace
{

// ── Écriture d'octets (petit-boutiste explicite : mêmes fichiers sur tous les systèmes) ─────────────────────────────

struct Bytes
{
    std::vector<std::uint8_t> data;
    void u8(std::uint8_t v) { data.push_back(v); }
    void u16le(std::uint16_t v)
    {
        u8(static_cast<std::uint8_t>(v));
        u8(static_cast<std::uint8_t>(v >> 8));
    }
    void u32le(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            u8(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void u64le(std::uint64_t v)
    {
        for (int i = 0; i < 8; ++i)
            u8(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void u32be(std::uint32_t v)
    {
        for (int i = 3; i >= 0; --i)
            u8(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void f32le(float v) { u32le(std::bit_cast<std::uint32_t>(v)); }
    void str(const std::string &s, bool terminate)
    {
        data.insert(data.end(), s.begin(), s.end());
        if (terminate)
            u8(0);
    }
};

core::Result<> writeFile(const std::filesystem::path &path, const void *bytes, std::size_t size)
{
    std::error_code ec;
    if (const auto parent = path.parent_path(); !parent.empty())
    {
        std::filesystem::create_directories(parent, ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char *>(bytes), static_cast<std::streamsize>(size));
    out.close();
    if (!out)
    {
        return makeError(ErrorCode::kIo, "cannot write " + path.generic_string());
    }
    return {};
}

core::Result<> writeFile(const std::filesystem::path &path, const std::string &text) { return writeFile(path, text.data(), text.size()); }

// ── PNG 16 bits : zlib en blocs « stockés » (sans compression : sortie identique partout, aucune dépendance) ─────────

std::uint32_t crc32(const std::uint8_t *bytes, std::size_t size, std::uint32_t crc = 0)
{
    static const std::array<std::uint32_t, 256> table = []
    {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n)
        {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1U) != 0 ? 0xedb88320U ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i)
        crc = table[(crc ^ bytes[i]) & 0xffU] ^ (crc >> 8);
    return ~crc;
}

void pngChunk(Bytes &out, const char *type, const std::vector<std::uint8_t> &payload)
{
    out.u32be(static_cast<std::uint32_t>(payload.size()));
    const std::size_t start = out.data.size();
    out.str(type, false);
    out.data.insert(out.data.end(), payload.begin(), payload.end());
    out.u32be(crc32(out.data.data() + start, out.data.size() - start));
}

std::vector<std::uint8_t> zlibStored(const std::vector<std::uint8_t> &raw)
{
    std::vector<std::uint8_t> out{0x78, 0x01};
    std::uint32_t a = 1, b = 0; // Adler-32
    for (std::uint8_t v : raw)
    {
        a = (a + v) % 65521U;
        b = (b + a) % 65521U;
    }
    std::size_t pos = 0;
    do
    {
        const std::size_t len = std::min<std::size_t>(65535, raw.size() - pos);
        const bool last = pos + len == raw.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(len));
        out.push_back(static_cast<std::uint8_t>(len >> 8));
        out.push_back(static_cast<std::uint8_t>(~len));
        out.push_back(static_cast<std::uint8_t>(~len >> 8));
        out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
        pos += len;
    } while (pos < raw.size());
    const std::uint32_t adler = (b << 16) | a;
    for (int i = 3; i >= 0; --i)
        out.push_back(static_cast<std::uint8_t>(adler >> (8 * i)));
    return out;
}

// ── Maillage commun à OBJ, glTF, STL ────────────────────────────────────────────────────────────────────────────────

struct GridMesh
{
    std::vector<float> positions; // x, y, z
    std::vector<float> normals;
    std::vector<std::uint32_t> indices;
};

std::array<float, 3> normalAt(const HeightField &f, std::uint32_t i, std::uint32_t j)
{
    const float hl = f.at(i > 0 ? i - 1 : i, j), hr = f.at(i + 1 < f.width ? i + 1 : i, j);
    const float hd = f.at(i, j > 0 ? j - 1 : j), hu = f.at(i, j + 1 < f.height ? j + 1 : j);
    const float dx = static_cast<float>((i + 1 < f.width ? i + 1 : i) - (i > 0 ? i - 1 : i)) * f.spacing;
    const float dz = static_cast<float>((j + 1 < f.height ? j + 1 : j) - (j > 0 ? j - 1 : j)) * f.spacing;
    float nx = -(hr - hl) / std::max(dx, 1e-6f);
    float ny = 1.0f;
    float nz = -(hu - hd) / std::max(dz, 1e-6f);
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (std::isfinite(len))
        return {nx / len, ny / len, nz / len}; // cas courant : mêmes octets qu'avant
    // Pente immense (hauteurs de l'ordre de 1e30) : les carrés débordent en float. Gradients en double, divisés par la plus
    // grande composante avant la racine : la normale reste finie et unitaire (glTF l'exige).
    const double gx = -(static_cast<double>(hr) - hl) / std::max(static_cast<double>(dx), 1e-6);
    const double gz = -(static_cast<double>(hu) - hd) / std::max(static_cast<double>(dz), 1e-6);
    const double scale = std::max({std::abs(gx), 1.0, std::abs(gz)});
    const double sx = gx / scale, sy = 1.0 / scale, sz = gz / scale;
    const double l = std::sqrt(sx * sx + sy * sy + sz * sz);
    nx = static_cast<float>(sx / l);
    ny = static_cast<float>(sy / l);
    nz = static_cast<float>(sz / l);
    return {nx, ny, nz};
}

GridMesh gridMesh(const HeightField &f)
{
    GridMesh m;
    m.positions.reserve(static_cast<std::size_t>(f.width) * f.height * 3);
    for (std::uint32_t j = 0; j < f.height; ++j)
    {
        for (std::uint32_t i = 0; i < f.width; ++i)
        {
            const auto n = normalAt(f, i, j);
            m.positions.insert(m.positions.end(), {f.xAt(i), f.at(i, j), f.zAt(j)});
            m.normals.insert(m.normals.end(), n.begin(), n.end());
        }
    }
    for (std::uint32_t j = 0; j + 1 < f.height; ++j)
    {
        for (std::uint32_t i = 0; i + 1 < f.width; ++i)
        {
            const std::uint32_t a = j * f.width + i, b = a + 1, c = a + f.width, d = c + 1;
            m.indices.insert(m.indices.end(), {a, c, b, b, c, d}); // anti-horaire vu du dessus (Y vers le haut)
        }
    }
    return m;
}

std::string base64(const std::uint8_t *data, std::size_t size)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (std::size_t i = 0; i < size; i += 3)
    {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) | (i + 1 < size ? static_cast<std::uint32_t>(data[i + 1]) << 8 : 0) |
                                (i + 2 < size ? data[i + 2] : 0);
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += i + 1 < size ? kAlphabet[(v >> 6) & 63] : '=';
        out += i + 2 < size ? kAlphabet[v & 63] : '=';
    }
    return out;
}

core::Result<> checkField(const HeightField &f, const char *what)
{
    if (f.width < 2 || f.height < 2 || f.heights.size() != static_cast<std::size_t>(f.width) * f.height)
    {
        return makeError(ErrorCode::kInvalidArgument, std::string(what) + ": the height field needs at least 2 x 2 samples");
    }
    return {};
}

} // namespace

float HeightField::minHeight() const noexcept { return heights.empty() ? 0.0f : *std::ranges::min_element(heights); }
float HeightField::maxHeight() const noexcept { return heights.empty() ? 0.0f : *std::ranges::max_element(heights); }

HeightField generateRegion(const TerrainParams &params, std::uint32_t seed, std::int64_t firstX, std::int64_t firstZ, std::uint32_t width,
                           std::uint32_t height, core::TaskSystem *tasks)
{
    HeightField f;
    f.spacing = params.sampleSpacing;
    // Le dernier indice global (first + n - 1) doit être représentable AVANT toute allocation ou tâche : sinon région vide, que
    // chaque export refuse (kInvalidArgument).
    auto fits = [](std::int64_t first, std::uint32_t n)
    { return n == 0 || first <= std::numeric_limits<std::int64_t>::max() - static_cast<std::int64_t>(n - 1); };
    if (!fits(firstX, width) || !fits(firstZ, height))
    {
        return f;
    }
    f.width = width;
    f.height = height;
    auto position = [&](std::int64_t global) { return static_cast<float>(static_cast<double>(global) * static_cast<double>(params.sampleSpacing)); };
    f.originX = position(firstX);
    f.originZ = position(firstZ);
    f.firstX = firstX;
    f.firstZ = firstZ;
    f.heights.resize(static_cast<std::size_t>(width) * height);
    auto row = [&](std::size_t j)
    {
        const float z = position(firstZ + static_cast<std::int64_t>(j));
        for (std::uint32_t i = 0; i < width; ++i)
        {
            f.heights[j * width + i] = heightAt(params, seed, position(firstX + i), z);
        }
    };
    if (tasks != nullptr)
    {
        tasks->parallelFor(height, 4, row);
    }
    else
    {
        for (std::size_t j = 0; j < height; ++j)
            row(j);
    }
    return f;
}

core::Result<> writeHeightPng16(const std::filesystem::path &path, const HeightField &field, float minHeight, float maxHeight)
{
    if (auto r = checkField(field, "writeHeightPng16"); !r)
        return r;
    if (!(maxHeight > minHeight))
        return makeError(ErrorCode::kInvalidArgument, "writeHeightPng16: maxHeight must be greater than minHeight");
    std::vector<std::uint8_t> raw;
    raw.reserve((static_cast<std::size_t>(field.width) * 2 + 1) * field.height);
    const double scale = 65535.0 / (static_cast<double>(maxHeight) - minHeight);
    for (std::uint32_t j = 0; j < field.height; ++j)
    {
        raw.push_back(0); // filtre « aucun »
        for (std::uint32_t i = 0; i < field.width; ++i)
        {
            const double v = std::clamp((static_cast<double>(field.at(i, j)) - minHeight) * scale, 0.0, 65535.0);
            const auto q = static_cast<std::uint16_t>(std::lround(v));
            raw.push_back(static_cast<std::uint8_t>(q >> 8)); // gros-boutiste (PNG)
            raw.push_back(static_cast<std::uint8_t>(q));
        }
    }
    Bytes png;
    png.data = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    Bytes ihdr;
    ihdr.u32be(field.width);
    ihdr.u32be(field.height);
    ihdr.data.insert(ihdr.data.end(), {16, 0, 0, 0, 0}); // 16 bits, niveaux de gris, deflate, filtrage standard, non entrelacé
    pngChunk(png, "IHDR", ihdr.data);
    // Un bloc PNG mesure au plus 2^31 - 1 octets : le flux zlib est réparti sur des blocs IDAT consécutifs (sortie inchangée sous
    // cette taille : un seul bloc).
    const std::vector<std::uint8_t> zlib = zlibStored(raw);
    constexpr std::size_t kMaxChunk = 0x7fffffffU;
    if (zlib.size() <= kMaxChunk)
    {
        pngChunk(png, "IDAT", zlib);
    }
    for (std::size_t at = 0; zlib.size() > kMaxChunk && at < zlib.size(); at += kMaxChunk)
    {
        const std::size_t size = std::min(kMaxChunk, zlib.size() - at);
        pngChunk(png, "IDAT", std::vector<std::uint8_t>(zlib.begin() + static_cast<std::ptrdiff_t>(at), zlib.begin() + static_cast<std::ptrdiff_t>(at + size)));
    }
    pngChunk(png, "IEND", {});
    return writeFile(path, png.data.data(), png.data.size());
}

core::Result<> writeHeightExr(const std::filesystem::path &path, const HeightField &field)
{
    if (auto r = checkField(field, "writeHeightExr"); !r)
        return r;
    Bytes exr;
    exr.u32le(20000630U); // nombre magique
    exr.u32le(2);         // version 2, image en lignes, noms courts
    auto attribute = [&](const char *name, const char *type, const Bytes &value)
    {
        exr.str(name, true);
        exr.str(type, true);
        exr.u32le(static_cast<std::uint32_t>(value.data.size()));
        exr.data.insert(exr.data.end(), value.data.begin(), value.data.end());
    };
    Bytes channels;
    channels.str("Y", true);
    channels.u32le(2); // FLOAT
    channels.data.insert(channels.data.end(), {0, 0, 0, 0}); // pLinear + réservé
    channels.u32le(1);
    channels.u32le(1);
    channels.u8(0);
    attribute("channels", "chlist", channels);
    attribute("compression", "compression", Bytes{{0}}); // NO_COMPRESSION
    Bytes box;
    box.u32le(0);
    box.u32le(0);
    box.u32le(field.width - 1);
    box.u32le(field.height - 1);
    attribute("dataWindow", "box2i", box);
    attribute("displayWindow", "box2i", box);
    attribute("lineOrder", "lineOrder", Bytes{{0}}); // INCREASING_Y
    Bytes one;
    one.f32le(1.0f);
    attribute("pixelAspectRatio", "float", one);
    Bytes center;
    center.f32le(0.0f);
    center.f32le(0.0f);
    attribute("screenWindowCenter", "v2f", center);
    attribute("screenWindowWidth", "float", one);
    exr.u8(0); // fin de l'en-tête
    const std::size_t lineBytes = 8 + static_cast<std::size_t>(field.width) * 4;
    const std::size_t first = exr.data.size() + static_cast<std::size_t>(field.height) * 8;
    for (std::uint32_t j = 0; j < field.height; ++j)
    {
        exr.u64le(first + j * lineBytes);
    }
    for (std::uint32_t j = 0; j < field.height; ++j)
    {
        exr.u32le(j);
        exr.u32le(field.width * 4);
        for (std::uint32_t i = 0; i < field.width; ++i)
            exr.f32le(field.at(i, j));
    }
    return writeFile(path, exr.data.data(), exr.data.size());
}

core::Result<HeightField> readHeightExr(const std::filesystem::path &path)
{
    // Un dossier (ou un fichier spécial) n'est pas un fichier EXR : refusé avant l'ouverture, qui réussit ou échoue selon le
    // système (Linux l'ouvre et la lecture échoue en EISDIR ; libc++ sous macOS lit 0 octet sans erreur).
    std::error_code kind;
    if (std::filesystem::exists(path, kind) && !std::filesystem::is_regular_file(path, kind))
        return makeError(ErrorCode::kIo, "readHeightExr: not a regular file: " + path.generic_string());
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return makeError(ErrorCode::kIo, "readHeightExr: cannot open " + path.generic_string());
    // Lecture contrôlée : istream::read transforme une erreur du système (ex. EISDIR sur un dossier ouvert sous Linux) en badbit,
    // là où istreambuf_iterator laisserait remonter l'exception de basic_filebuf::underflow.
    std::vector<std::uint8_t> d;
    std::array<char, 65536> chunk{};
    while (in)
    {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        d.insert(d.end(), chunk.begin(), chunk.begin() + in.gcount());
    }
    if (in.bad())
        return makeError(ErrorCode::kIo, "readHeightExr: cannot read " + path.generic_string());
    // Fichier non fiable : chaque lecture est bornée par `limit` (fin du fichier, ou fin de l'attribut en cours de lecture), les
    // tailles se calculent en 64 bits et sont comparées aux octets restants AVANT toute allocation ou indexation.
    std::size_t pos = 0;
    std::size_t limit = d.size();
    auto need = [&](std::size_t n) { return pos <= limit && n <= limit - pos; };
    auto u32 = [&]()
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(d[pos + static_cast<std::size_t>(i)]) << (8 * i);
        pos += 4;
        return v;
    };
    auto cstr = [&]() -> std::optional<std::string> // chaîne terminée par 0 AVANT `limit`
    {
        std::string s;
        while (pos < limit && d[pos] != 0)
            s += static_cast<char>(d[pos++]);
        if (pos >= limit)
            return std::nullopt;
        ++pos;
        return s;
    };
    if (!need(8) || u32() != 20000630U || (u32() & 0xffU) != 2)
        return makeError(ErrorCode::kParse, "readHeightExr: not an OpenEXR 2 file");
    std::int64_t xMin = 0, yMin = 0, xMax = -1, yMax = -1;
    bool singleFloat = false;
    int compression = -1;
    while (pos < d.size() && d[pos] != 0)
    {
        const auto name = cstr();
        const auto type = name ? cstr() : std::nullopt;
        if (!type || !need(4))
            return makeError(ErrorCode::kParse, "readHeightExr: truncated header");
        const std::uint32_t size = u32();
        if (!need(size))
            return makeError(ErrorCode::kParse, "readHeightExr: truncated attribute " + *name);
        const std::size_t end = pos + size;
        limit = end; // les lectures de l'attribut restent dans l'attribut
        bool ok = true;
        if (*name == "channels")
        {
            const auto channel = cstr();
            // type de pixel (4), pLinear + réservé (4), échantillonnage x et y (8), puis le 0 qui termine la liste.
            ok = channel.has_value() && need(17);
            if (ok)
            {
                const std::uint32_t pixelType = u32();
                singleFloat = !channel->empty() && pixelType == 2 && d[pos + 12] == 0; // un seul canal, FLOAT
            }
        }
        else if (*name == "compression")
        {
            ok = need(1);
            if (ok)
                compression = d[pos];
        }
        else if (*name == "dataWindow")
        {
            ok = need(16);
            if (ok)
            {
                xMin = static_cast<std::int32_t>(u32());
                yMin = static_cast<std::int32_t>(u32());
                xMax = static_cast<std::int32_t>(u32());
                yMax = static_cast<std::int32_t>(u32());
            }
        }
        if (!ok)
            return makeError(ErrorCode::kParse, "readHeightExr: malformed attribute " + *name);
        pos = end;
        limit = d.size();
    }
    ++pos;
    if (!singleFloat || compression != 0 || xMax < xMin || yMax < yMin)
        return makeError(ErrorCode::kUnsupported, "readHeightExr: only single-channel FLOAT, uncompressed scanline files are supported");
    const std::int64_t width = xMax - xMin + 1; // au plus 2^32 : pas de débordement en 64 bits
    const std::int64_t height = yMax - yMin + 1;
    const auto rowBytes = static_cast<std::uint64_t>(width) * 4U;
    // Les octets annoncés (table des offsets, puis une ligne d'en-tête + pixels par rangée) doivent tenir dans le fichier : cela
    // borne aussi les allocations par la taille du fichier.
    const std::uint64_t remaining = pos <= d.size() ? d.size() - pos : 0;
    if (static_cast<std::uint64_t>(height) > remaining / (8U + 8U + rowBytes))
        return makeError(ErrorCode::kParse, "readHeightExr: the declared size does not fit in the file");
    HeightField f;
    f.width = static_cast<std::uint32_t>(width);
    f.height = static_cast<std::uint32_t>(height);
    f.heights.resize(static_cast<std::size_t>(f.width) * f.height);
    std::vector<std::uint64_t> offsets(f.height);
    for (auto &o : offsets)
    {
        if (!need(8))
            return makeError(ErrorCode::kParse, "readHeightExr: truncated offset table");
        o = u32();
        o |= static_cast<std::uint64_t>(u32()) << 32;
    }
    std::vector<bool> seen(f.height, false);
    for (std::uint32_t j = 0; j < f.height; ++j)
    {
        if (offsets[j] > d.size())
            return makeError(ErrorCode::kParse, "readHeightExr: scanline offset outside the file");
        pos = static_cast<std::size_t>(offsets[j]);
        if (!need(8 + static_cast<std::size_t>(rowBytes)))
            return makeError(ErrorCode::kParse, "readHeightExr: truncated scanline");
        const std::int64_t row = static_cast<std::int64_t>(static_cast<std::int32_t>(u32())) - yMin;
        const std::uint32_t bytes = u32();
        if (row < 0 || row >= height || bytes != rowBytes)
            return makeError(ErrorCode::kParse, "readHeightExr: unexpected scanline");
        if (seen[static_cast<std::size_t>(row)])
            return makeError(ErrorCode::kParse, "readHeightExr: scanline read twice");
        seen[static_cast<std::size_t>(row)] = true;
        for (std::uint32_t i = 0; i < f.width; ++i)
            f.heights[static_cast<std::size_t>(row) * f.width + i] = std::bit_cast<float>(u32());
    }
    return f;
}

core::Result<> writeObj(const std::filesystem::path &path, const HeightField &field)
{
    if (auto r = checkField(field, "writeObj"); !r)
        return r;
    const GridMesh m = gridMesh(field);
    std::string out = "# terrain exporté par orosmo::world (TER-11)\n";
    char line[128];
    // std::to_chars : même sortie que "%.6f", mais indépendante de LC_NUMERIC (une virgule changerait les octets du fichier).
    auto triple = [&](const char *prefix, const float *v)
    {
        out += prefix;
        for (int k = 0; k < 3; ++k)
        {
            out += ' ';
            const auto [end, ec] = std::to_chars(line, line + sizeof line, static_cast<double>(v[k]), std::chars_format::fixed, 6);
            out.append(line, ec == std::errc{} ? end : line);
        }
        out += '\n';
    };
    for (std::size_t v = 0; v < m.positions.size(); v += 3)
    {
        triple("v", &m.positions[v]);
    }
    for (std::size_t v = 0; v < m.normals.size(); v += 3)
    {
        triple("vn", &m.normals[v]);
    }
    for (std::size_t t = 0; t < m.indices.size(); t += 3)
    {
        const unsigned a = m.indices[t] + 1, b = m.indices[t + 1] + 1, c = m.indices[t + 2] + 1;
        std::snprintf(line, sizeof line, "f %u//%u %u//%u %u//%u\n", a, a, b, b, c, c);
        out += line;
    }
    return writeFile(path, out);
}

core::Result<> writeGltf(const std::filesystem::path &path, const HeightField &field)
{
    if (auto r = checkField(field, "writeGltf"); !r)
        return r;
    const GridMesh m = gridMesh(field);
    Bytes buffer;
    for (float v : m.positions)
        buffer.f32le(v);
    const std::size_t normalsOffset = buffer.data.size();
    for (float v : m.normals)
        buffer.f32le(v);
    const std::size_t indicesOffset = buffer.data.size();
    for (std::uint32_t i : m.indices)
        buffer.u32le(i);
    // Bornes initialisées sur le premier sommet (checkField en garantit au moins quatre) : min/max sont des valeurs du tampon.
    std::array<float, 3> lo{m.positions[0], m.positions[1], m.positions[2]};
    std::array<float, 3> hi = lo;
    for (std::size_t v = 3; v < m.positions.size(); v += 3)
    {
        for (std::size_t c = 0; c < 3; ++c)
        {
            lo[c] = std::min(lo[c], m.positions[v + c]);
            hi[c] = std::max(hi[c], m.positions[v + c]);
        }
    }
    const std::size_t vertexCount = m.positions.size() / 3;
    using nlohmann::json;
    json doc = {
        {"asset", {{"version", "2.0"}, {"generator", "orosmo::world (TER-11)"}}},
        {"scene", 0},
        {"scenes", json::array({{{"nodes", json::array({0})}}})},
        {"nodes", json::array({{{"mesh", 0}, {"name", "terrain"}}})},
        {"meshes", json::array({{{"name", "terrain"},
                                 {"primitives", json::array({{{"attributes", {{"POSITION", 0}, {"NORMAL", 1}}}, {"indices", 2}, {"mode", 4}}})}}})},
        {"buffers", json::array({{{"byteLength", buffer.data.size()},
                                  {"uri", "data:application/octet-stream;base64," + base64(buffer.data.data(), buffer.data.size())}}})},
        {"bufferViews", json::array({{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", normalsOffset}, {"target", 34962}},
                                     {{"buffer", 0}, {"byteOffset", normalsOffset}, {"byteLength", indicesOffset - normalsOffset}, {"target", 34962}},
                                     {{"buffer", 0}, {"byteOffset", indicesOffset}, {"byteLength", buffer.data.size() - indicesOffset}, {"target", 34963}}})},
        {"accessors", json::array({{{"bufferView", 0}, {"componentType", 5126}, {"count", vertexCount}, {"type", "VEC3"}, {"min", lo}, {"max", hi}},
                                   {{"bufferView", 1}, {"componentType", 5126}, {"count", vertexCount}, {"type", "VEC3"}},
                                   {{"bufferView", 2}, {"componentType", 5125}, {"count", m.indices.size()}, {"type", "SCALAR"}}})},
    };
    return writeFile(path, doc.dump(1));
}

core::Result<> writeStl(const std::filesystem::path &path, const HeightField &field, float baseDepth)
{
    if (auto r = checkField(field, "writeStl"); !r)
        return r;
    // Solide fermé : surface (deux triangles par cellule), fond (même grille au niveau du socle, à l'envers), et jupes reliant le
    // bord de la surface au bord du fond, sommet par sommet : chaque arête est partagée par exactement deux triangles.
    // Le socle doit passer STRICTEMENT sous le point le plus bas : sinon le fond touche la surface, des jupes ont une aire nulle
    // et le fichier n'est plus un solide imprimable (une profondeur trop faible pour la précision du float donne aussi base == min).
    const float base = field.minHeight() - baseDepth;
    if (!(baseDepth > 0.0f) || !std::isfinite(base) || !(base < field.minHeight()))
        return makeError(ErrorCode::kInvalidArgument, "writeStl: the base must lie strictly below the lowest point (baseDepth > 0)");
    using P = std::array<float, 3>;
    std::vector<std::array<P, 3>> triangles;
    auto top = [&](std::uint32_t i, std::uint32_t j) { return P{field.xAt(i), field.at(i, j), field.zAt(j)}; };
    auto bottom = [&](std::uint32_t i, std::uint32_t j) { return P{field.xAt(i), base, field.zAt(j)}; };
    for (std::uint32_t j = 0; j + 1 < field.height; ++j)
    {
        for (std::uint32_t i = 0; i + 1 < field.width; ++i)
        {
            triangles.push_back({top(i, j), top(i, j + 1), top(i + 1, j)});
            triangles.push_back({top(i + 1, j), top(i, j + 1), top(i + 1, j + 1)});
            triangles.push_back({bottom(i, j), bottom(i + 1, j), bottom(i, j + 1)}); // vers le bas
            triangles.push_back({bottom(i + 1, j), bottom(i + 1, j + 1), bottom(i, j + 1)});
        }
    }
    // Jupes : pour chaque segment (p, q) du bord, parcouru dans le sens qui laisse l'extérieur à droite vu de l'extérieur.
    auto skirt = [&](P p, P q, P pb, P qb)
    {
        triangles.push_back({p, pb, q});
        triangles.push_back({q, pb, qb});
    };
    const std::uint32_t w = field.width - 1, h = field.height - 1;
    for (std::uint32_t i = 0; i < w; ++i)
    {
        skirt(top(i + 1, 0), top(i, 0), bottom(i + 1, 0), bottom(i, 0)); // bord z minimal
        skirt(top(i, h), top(i + 1, h), bottom(i, h), bottom(i + 1, h)); // bord z maximal
    }
    for (std::uint32_t j = 0; j < h; ++j)
    {
        skirt(top(0, j), top(0, j + 1), bottom(0, j), bottom(0, j + 1)); // bord x minimal
        skirt(top(w, j + 1), top(w, j), bottom(w, j + 1), bottom(w, j)); // bord x maximal
    }
    Bytes stl;
    std::string header = "orosmo::world TER-11 terrain (binary STL, closed solid)";
    header.resize(80, ' ');
    stl.str(header, false);
    stl.u32le(static_cast<std::uint32_t>(triangles.size()));
    for (const auto &t : triangles)
    {
        const P u{t[1][0] - t[0][0], t[1][1] - t[0][1], t[1][2] - t[0][2]};
        const P v{t[2][0] - t[0][0], t[2][1] - t[0][1], t[2][2] - t[0][2]};
        P n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (float &c : n)
            c = len > 0.0f ? c / len : 0.0f;
        for (float c : n)
            stl.f32le(c);
        for (const P &p : t)
            for (float c : p)
                stl.f32le(c);
        stl.u16le(0);
    }
    return writeFile(path, stl.data.data(), stl.data.size());
}

Biome classify(float height, float slope, const BiomeParams &params) noexcept
{
    if (height < params.seaLevel)
        return Biome::kWater;
    if (slope > params.rockSlope)
        return Biome::kRock;
    if (height < params.sandHeight)
        return Biome::kSand;
    if (height > params.snowHeight)
        return Biome::kSnow;
    return Biome::kGrass;
}

std::vector<Biome> biomeMap(const HeightField &field, const BiomeParams &params)
{
    // API publique : une grille dont le stockage ne correspond pas à width x height donnerait des lectures hors du vecteur.
    if (field.heights.size() != static_cast<std::size_t>(field.width) * field.height)
        return {};
    std::vector<Biome> out(field.heights.size());
    for (std::uint32_t j = 0; j < field.height; ++j)
    {
        for (std::uint32_t i = 0; i < field.width; ++i)
        {
            const auto n = normalAt(field, i, j);
            out[static_cast<std::size_t>(j) * field.width + i] = classify(field.at(i, j), 1.0f - n[1], params);
        }
    }
    return out;
}

core::Result<> writeBiomePng(const std::filesystem::path &path, const HeightField &field, const BiomeParams &params)
{
    if (auto r = checkField(field, "writeBiomePng"); !r)
        return r;
    static constexpr std::array<std::array<std::uint8_t, 3>, 5> kColors = {{{36, 92, 160}, {210, 194, 140}, {86, 140, 70}, {120, 112, 104}, {240, 242, 246}}};
    const auto biomes = biomeMap(field, params);
    std::vector<std::uint8_t> rgb;
    rgb.reserve(biomes.size() * 3);
    for (Biome b : biomes)
    {
        const auto &c = kColors[static_cast<std::size_t>(b)];
        rgb.insert(rgb.end(), c.begin(), c.end());
    }
    // Encodage en mémoire (mêmes octets que stbi_write_png, qui ignore l'échec de fwrite/fclose), puis writeFile : une écriture
    // ou une fermeture ratée (disque plein…) devient kIo.
    std::vector<std::uint8_t> png;
    auto append = [](void *context, void *data, int size)
    {
        auto *out = static_cast<std::vector<std::uint8_t> *>(context);
        const auto *bytes = static_cast<const std::uint8_t *>(data);
        out->insert(out->end(), bytes, bytes + size);
    };
    if (stbi_write_png_to_func(append, &png, static_cast<int>(field.width), static_cast<int>(field.height), 3, rgb.data(), static_cast<int>(field.width * 3)) == 0)
        return makeError(ErrorCode::kIo, "writeBiomePng: cannot encode " + path.generic_string());
    return writeFile(path, png.data(), png.size());
}

} // namespace orosmo::world
