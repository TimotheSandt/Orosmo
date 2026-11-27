// Commande du système (voir process.hpp) : popen / _popen, sorties réunies par « 2>&1 ».
#include <orosmo/platform/process.hpp>

#include <cstdio>
#include <string>
#include <system_error>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace orosmo::platform
{
namespace
{

#if defined(_WIN32)
std::string quoted(const std::string &path) { return "\"" + path + "\""; } // cmd.exe : un chemin ne contient pas de guillemet
#else
std::string quoted(const std::string &path)
{
    std::string out = "'";
    for (const char c : path)
        out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}
#endif

} // namespace

core::Result<CommandResult> runCommand(const std::string &command, const std::filesystem::path &directory)
{
    if (command.empty())
        return core::makeError(core::ErrorCode::kInvalidArgument, "runCommand: empty command");
    std::string line;
    if (!directory.empty())
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(directory, ec))
            return core::makeError(core::ErrorCode::kIo, "runCommand: no directory " + directory.string());
#if defined(_WIN32)
        line = "cd /d " + quoted(directory.string()) + " && ";
#else
        line = "cd " + quoted(directory.string()) + " && ";
#endif
    }
    line += "(" + command + ") 2>&1";
    std::fflush(nullptr); // sorties du programme écrites avant celles de l'enfant
#if defined(_WIN32)
    FILE *pipe = _popen(line.c_str(), "r");
#else
    FILE *pipe = popen(line.c_str(), "r");
#endif
    if (pipe == nullptr)
        return core::makeError(core::ErrorCode::kIo, "runCommand: cannot start: " + command);
    CommandResult result;
    char buffer[4096];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof buffer, pipe)) > 0)
        result.output.append(buffer, read);
#if defined(_WIN32)
    result.exitCode = _pclose(pipe);
#else
    const int status = pclose(pipe);
    if (status == -1)
        return core::makeError(core::ErrorCode::kIo, "runCommand: cannot wait for: " + command);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    return result;
}

} // namespace orosmo::platform
