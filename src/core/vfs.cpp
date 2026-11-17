#include <engine/core/vfs.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace engine::core
{

namespace fs = std::filesystem;

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

    // Refuse toute remontée au-dessus de la racine : ".." est refusé tel quel (pas de normalisation indulgente).
    std::vector<std::string> segments;
    std::stringstream ss(parts->relative);
    for (std::string seg; std::getline(ss, seg, '/');)
    {
        if (seg.empty() || seg == ".")
            continue;
        if (seg == "..")
            return makeError(ErrorCode::kInvalidArgument, "'..' is not allowed in a logical path: " + std::string(logicalPath));
        segments.push_back(seg);
    }
    fs::path relative;
    for (const auto &s : segments)
        relative /= s;

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
    std::error_code ec;
    for (const auto &p : *c)
        if (fs::exists(p, ec))
            return p;
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
    std::string prefix = std::string(logicalDirectory);
    while (!prefix.empty() && prefix.back() == '/')
        prefix.pop_back();
    if (parts->relative.empty())
        prefix = parts->scheme + ":/"; // "res://" + nom => "res://nom"

    std::set<std::string> names;
    std::error_code ec;
    for (const auto &dir : *c)
    {
        if (!fs::is_directory(dir, ec))
            continue;
        for (const auto &entry : fs::directory_iterator(dir, ec))
            if (entry.is_regular_file(ec))
                names.insert(prefix + "/" + entry.path().filename().generic_string());
    }
    return std::vector<std::string>(names.begin(), names.end());
}

fs::path Vfs::defaultUserDirectory(const std::string &appName)
{
#if defined(_WIN32)
    if (const char *appData = std::getenv("APPDATA"))
        return fs::path(appData) / appName;
    return fs::temp_directory_path() / appName;
#elif defined(__APPLE__)
    if (const char *home = std::getenv("HOME"))
        return fs::path(home) / "Library" / "Application Support" / appName;
    return fs::temp_directory_path() / appName;
#else
    if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        return fs::path(xdg) / appName;
    if (const char *home = std::getenv("HOME"))
        return fs::path(home) / ".local" / "share" / appName;
    return fs::temp_directory_path() / appName;
#endif
}

} // namespace engine::core
