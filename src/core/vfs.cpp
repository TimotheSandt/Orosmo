#include <engine/core/vfs.hpp>

#include <engine/core/system.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace engine::core
{

namespace fs = std::filesystem;

namespace
{
// Segments normalisés d'un chemin logique : '.' et segments vides ignorés ; '..' et noms de lecteur refusés.
Result<std::vector<std::string>> segmentsOf(const std::string &relative, std::string_view logicalPath)
{
    std::vector<std::string> segments;
    std::stringstream ss(relative);
    for (std::string seg; std::getline(ss, seg, '/');)
    {
        if (seg.empty() || seg == ".")
            continue;
        // Refuse toute remontée au-dessus de la racine : ".." est refusé tel quel (pas de normalisation indulgente).
        if (seg == "..")
            return makeError(ErrorCode::kInvalidArgument, "'..' is not allowed in a logical path: " + std::string(logicalPath));
        // Un nom de lecteur ("C:", "C:x") ferait remplacer le montage par un autre volume sous Windows : refusé partout,
        // pour que le même chemin logique ait le même sens sur toutes les plateformes.
        if (seg.find(':') != std::string::npos)
            return makeError(ErrorCode::kInvalidArgument, "':' (drive name) is not allowed in a logical path: " + std::string(logicalPath));
        segments.push_back(std::move(seg));
    }
    return segments;
}
} // namespace

Result<Vfs::Parts> Vfs::split(std::string_view p)
{
    const auto pos = p.find("://");
    if (pos == std::string_view::npos || pos == 0)
        return makeError(ErrorCode::kInvalidArgument, "logical path needs a scheme (res://...): " + std::string(p));
    Parts parts{std::string(p.substr(0, pos)), std::string(p.substr(pos + 3))};
    std::replace(parts.relative.begin(), parts.relative.end(), '\\', '/');
    return parts;
}

void Vfs::mount(std::string scheme, fs::path directory) { mMounts[std::move(scheme)].push_back(std::move(directory)); }

void Vfs::unmount(std::string_view scheme)
{
    if (auto it = mMounts.find(scheme); it != mMounts.end())
        mMounts.erase(it);
}

Result<std::vector<fs::path>> Vfs::candidates(std::string_view logicalPath) const
{
    auto parts = split(logicalPath);
    if (!parts)
        return std::unexpected(parts.error());
    auto it = mMounts.find(parts->scheme);
    if (it == mMounts.end() || it->second.empty())
        return makeError(ErrorCode::kNotFound, "no mount for scheme '" + parts->scheme + "'");

    auto segments = segmentsOf(parts->relative, logicalPath);
    if (!segments)
        return std::unexpected(segments.error());
    fs::path relative;
    for (const auto &seg : *segments)
        relative /= seg;

    std::vector<fs::path> out;
    for (auto m = it->second.rbegin(); m != it->second.rend(); ++m) // le plus récent d'abord
        out.push_back(*m / relative);
    return out;
}

Result<fs::path> Vfs::resolve(std::string_view logicalPath) const
{
    auto c = candidates(logicalPath);
    if (!c)
        return std::unexpected(c.error());
    for (const auto &p : *c)
    {
        std::error_code ec;
        const bool found = fs::exists(p, ec);
        if (ec) // erreur d'accès (permission, E/S) : ne pas retomber silencieusement sur un montage plus ancien
            return makeError(ErrorCode::kIo, "cannot access " + p.string() + ": " + ec.message());
        if (found)
            return p;
    }
    return c->front();
}

bool Vfs::exists(std::string_view logicalPath) const
{
    auto c = candidates(logicalPath);
    if (!c)
        return false;
    std::error_code ec;
    return std::any_of(c->begin(), c->end(), [&](const fs::path &p) { return fs::exists(p, ec); });
}

Result<std::vector<std::uint8_t>> Vfs::readBytes(std::string_view logicalPath) const
{
    auto path = resolve(logicalPath);
    if (!path)
        return std::unexpected(path.error());
    std::error_code ec;
    if (!fs::is_regular_file(*path, ec))
        return makeError(ErrorCode::kNotFound, "file not found: " + std::string(logicalPath));
    std::ifstream in(*path, std::ios::binary);
    if (!in)
        return makeError(ErrorCode::kIo, "cannot open: " + std::string(logicalPath));
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return bytes;
}

Result<std::string> Vfs::readText(std::string_view logicalPath) const
{
    auto bytes = readBytes(logicalPath);
    if (!bytes)
        return std::unexpected(bytes.error());
    return std::string(bytes->begin(), bytes->end());
}

Result<> Vfs::writeBytes(std::string_view logicalPath, const std::vector<std::uint8_t> &content) const
{
    auto c = candidates(logicalPath);
    if (!c)
        return std::unexpected(c.error());
    const fs::path &target = c->front(); // montage le plus récent
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (ec)
        return makeError(ErrorCode::kIo, "cannot create directory for " + std::string(logicalPath) + ": " + ec.message());
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out)
        return makeError(ErrorCode::kIo, "cannot write: " + std::string(logicalPath));
    out.write(reinterpret_cast<const char *>(content.data()), static_cast<std::streamsize>(content.size()));
    out.close(); // le vidage peut échouer à la fermeture (disque plein) : seul l'état après close() le révèle
    if (!out)
        return makeError(ErrorCode::kIo, "write failed: " + std::string(logicalPath));
    return {};
}

Result<> Vfs::writeText(std::string_view logicalPath, std::string_view content) const
{
    return writeBytes(logicalPath, std::vector<std::uint8_t>(content.begin(), content.end()));
}

Result<std::vector<std::string>> Vfs::list(std::string_view logicalDirectory) const
{
    auto parts = split(logicalDirectory);
    if (!parts)
        return std::unexpected(parts.error());
    auto c = candidates(logicalDirectory);
    if (!c)
        return std::unexpected(c.error());
    auto segments = segmentsOf(parts->relative, logicalDirectory);
    if (!segments)
        return std::unexpected(segments.error());
    // Préfixe construit depuis le chemin normalisé ("res://a//./b/" donne le même résultat que "res://a/b").
    std::string prefix = parts->scheme + ":/"; // "res://" + nom => "res://nom"
    if (!segments->empty())
    {
        prefix += "/";
        for (std::size_t i = 0; i < segments->size(); ++i)
            prefix += (i ? "/" : "") + (*segments)[i];
    }

    std::set<std::string> names;
    std::error_code ec;
    for (const auto &dir : *c)
    {
        ec.clear();
        if (!fs::is_directory(dir, ec))
        {
            if (ec)
                return makeError(ErrorCode::kIo, "cannot access " + dir.string() + ": " + ec.message());
            continue;
        }
        fs::directory_iterator it(dir, ec), end;
        for (; !ec && it != end; it.increment(ec))
        {
            std::error_code e2;
            if (it->is_regular_file(e2))
                names.insert(prefix + "/" + it->path().filename().generic_string());
        }
        if (ec) // énumération interrompue : une liste partielle serait trompeuse
            return makeError(ErrorCode::kIo, "cannot list " + dir.string() + ": " + ec.message());
    }
    return std::vector<std::string>(names.begin(), names.end());
}

fs::path Vfs::defaultUserDirectory(const std::string &appName)
{
    // Le dossier de données de la plateforme (variables d'environnement, API du système) vient de platform via
    // engine/core/system.hpp ; core ne lit plus l'environnement lui-même.
    if (const fs::path base = system::userDataDirectory(); !base.empty())
        return base / appName;
    return fs::temp_directory_path() / appName;
}

} // namespace engine::core
