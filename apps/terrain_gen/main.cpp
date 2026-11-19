// TOOL-02 : terrain-gen : génère une région de terrain SANS fenêtre ni GPU et l'exporte (TER-11).
//
//   terrain-gen --seed 42 --size 4096 --out relief.png
//
// Format choisi par l'extension : .png (hauteurs 16 bits), .exr (hauteurs float), .obj, .gltf, .stl ; --biomes carte.png en plus.
// Codes de retour : 0 succès, 1 erreur d'utilisation (argument invalide), 2 erreur d'entrée/sortie (lecture ou écriture).
// Déterministe : même graine et mêmes paramètres -> mêmes octets ; la somme de contrôle des hauteurs est affichée (TEST-03).
#include <engine/core/serialization.hpp>
#include <engine/core/tasks.hpp>
#include <engine/world/checksum.hpp>
#include <engine/world/export.hpp>

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace w = engine::world;
namespace fs = std::filesystem;

namespace
{

constexpr int kOk = 0;
constexpr int kUsage = 1;
constexpr int kIo = 2;

void usage(std::FILE *to)
{
    std::fputs("Usage: terrain-gen --seed N --size S --out FILE [options]\n"
               "  --seed N            graine (entier >= 0)\n"
               "  --size S            échantillons par côté (2 à 16384)\n"
               "  --out FILE          .png (hauteurs 16 bits) | .exr (float) | .obj | .gltf | .stl\n"
               "  --biomes FILE.png   carte de biomes (RGB) en plus\n"
               "  --params FILE.json  paramètres du terrain (document {\"version\", \"data\"} de TerrainParams)\n"
               "  --spacing M         espacement des échantillons (m), défaut : celui des paramètres\n"
               "  --origin X Z        premier échantillon global (défaut 0 0)\n"
               "  --base-depth M      profondeur du socle du STL sous le point le plus bas (défaut 5)\n"
               "  --threads N         threads de génération (0 : automatique)\n"
               "  --help              cette aide\n"
               "Codes de retour : 0 succès, 1 erreur d'utilisation, 2 erreur d'entrée/sortie.\n",
               to);
}

template <typename T> std::optional<T> number(std::string_view text)
{
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "terrain-gen: %s\n", message.c_str());
    if (code == kUsage)
    {
        usage(stderr);
    }
    return code;
}

} // namespace

int main(int argc, char **argv)
{
    std::optional<std::uint32_t> seed;
    std::optional<std::uint32_t> size;
    std::optional<std::int64_t> originX, originZ;
    std::optional<float> spacing;
    fs::path out, biomes, paramsFile;
    float baseDepth = 5.0f;
    unsigned threads = 0;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        auto value = [&]() -> std::optional<std::string_view>
        {
            if (i + 1 >= argc)
            {
                return std::nullopt;
            }
            return std::string_view(argv[++i]);
        };
        if (arg == "--help" || arg == "-h")
        {
            usage(stdout);
            return kOk;
        }
        const auto v = value();
        if (!v)
        {
            return fail(kUsage, "missing value after " + std::string(arg));
        }
        if (arg == "--seed")
        {
            seed = number<std::uint32_t>(*v);
            if (!seed) return fail(kUsage, "invalid --seed: " + std::string(*v));
        }
        else if (arg == "--size")
        {
            size = number<std::uint32_t>(*v);
            if (!size || *size < 2 || *size > 16384) return fail(kUsage, "invalid --size (2 to 16384): " + std::string(*v));
        }
        else if (arg == "--out") out = std::string(*v);
        else if (arg == "--biomes") biomes = std::string(*v);
        else if (arg == "--params") paramsFile = std::string(*v);
        else if (arg == "--spacing")
        {
            spacing = number<float>(*v);
            if (!spacing || !(*spacing > 0.0f)) return fail(kUsage, "invalid --spacing: " + std::string(*v));
        }
        else if (arg == "--origin")
        {
            originX = number<std::int64_t>(*v);
            const auto z = value();
            originZ = z ? number<std::int64_t>(*z) : std::nullopt;
            if (!originX || !originZ) return fail(kUsage, "invalid --origin X Z");
        }
        else if (arg == "--base-depth")
        {
            const auto d = number<float>(*v);
            if (!d || *d < 0.0f) return fail(kUsage, "invalid --base-depth: " + std::string(*v));
            baseDepth = *d;
        }
        else if (arg == "--threads")
        {
            const auto t = number<unsigned>(*v);
            if (!t) return fail(kUsage, "invalid --threads: " + std::string(*v));
            threads = *t;
        }
        else
        {
            return fail(kUsage, "unknown option " + std::string(arg));
        }
    }
    if (!seed || !size || out.empty())
    {
        return fail(kUsage, "--seed, --size and --out are required");
    }
    std::string extension = out.extension().string();
    for (char &c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (extension != ".png" && extension != ".exr" && extension != ".obj" && extension != ".gltf" && extension != ".stl")
    {
        return fail(kUsage, "unsupported output format '" + extension + "' (.png .exr .obj .gltf .stl)");
    }

    w::TerrainParams params;
    if (!paramsFile.empty())
    {
        std::ifstream in(paramsFile, std::ios::binary);
        if (!in)
        {
            return fail(kIo, "cannot read " + paramsFile.generic_string());
        }
        std::stringstream text;
        text << in.rdbuf();
        auto json = engine::core::parseJson(text.str());
        if (!json)
        {
            return fail(kUsage, paramsFile.generic_string() + ": " + json.error().message);
        }
        if (auto loaded = engine::core::loadDocument(params, *json); !loaded)
        {
            return fail(kUsage, paramsFile.generic_string() + ": " + loaded.error().message);
        }
    }
    if (spacing)
    {
        params.sampleSpacing = *spacing;
    }

    engine::core::TaskSystem tasks(threads);
    const w::HeightField field = w::generateRegion(params, *seed, originX.value_or(0), originZ.value_or(0), *size, *size, &tasks);
    const float lo = field.minHeight();
    const float hi = field.maxHeight();
    engine::core::Result<> written;
    if (extension == ".png") written = w::writeHeightPng16(out, field, lo, hi > lo ? hi : lo + 1.0f);
    else if (extension == ".exr") written = w::writeHeightExr(out, field);
    else if (extension == ".obj") written = w::writeObj(out, field);
    else if (extension == ".gltf") written = w::writeGltf(out, field);
    else written = w::writeStl(out, field, baseDepth);
    if (!written)
    {
        return fail(kIo, written.error().message);
    }
    if (!biomes.empty())
    {
        if (auto r = w::writeBiomePng(biomes, field, {}); !r)
        {
            return fail(kIo, r.error().message);
        }
    }
    std::printf("terrain-gen: seed %u, %ux%u samples, spacing %g m, heights [%.3f, %.3f] m\n", *seed, *size, *size,
                static_cast<double>(params.sampleSpacing), static_cast<double>(lo), static_cast<double>(hi));
    std::printf("checksum: 0x%016llx\n", static_cast<unsigned long long>(w::checksum64(field.heights)));
    std::printf("written: %s\n", out.generic_string().c_str());
    return kOk;
}
