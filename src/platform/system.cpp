#include <engine/core/system.hpp>

#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#include <shlobj.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/ioctl.h>
#include <unistd.h>
#else
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#endif

#include <cstdlib>
#include <string>
#include <system_error>
#include <vector>

namespace engine::core::system
{

namespace fs = std::filesystem;

std::size_t terminalWidth()
{
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
    {
        const int columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        return columns > 0 ? static_cast<std::size_t>(columns) : 80;
    }
    return 80;
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
        return w.ws_col;
    return 80;
#endif
}

fs::path executablePath()
{
#if defined(_WIN32)
    std::vector<char> buffer(MAX_PATH, '\0');
    while (true)
    {
        DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};
        if (length < buffer.size() - 1)
            return fs::path(std::string(buffer.data(), length));
        buffer.resize(buffer.size() * 2, '\0');
    }
#elif defined(__APPLE__)
    std::uint32_t bufferSize = 0;
    char probeBuffer[1];
    _NSGetExecutablePath(probeBuffer, &bufferSize);
    std::vector<char> buffer(bufferSize, '\0');
    if (_NSGetExecutablePath(buffer.data(), &bufferSize) != 0)
        return {};
    std::error_code ec;
    fs::path canonicalPath = fs::weakly_canonical(fs::path(buffer.data()), ec);
    return ec ? fs::path(buffer.data()) : canonicalPath;
#else
    std::vector<char> buffer(1024, '\0');
    while (true)
    {
        const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0)
            return {};
        if (static_cast<std::size_t>(length) < buffer.size())
            return fs::path(std::string(buffer.data(), static_cast<std::size_t>(length)));
        buffer.resize(buffer.size() * 2, '\0');
    }
#endif
}

fs::path userDataDirectory()
{
#if defined(_WIN32)
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path)))
        return fs::path(path);
    return {};
#elif defined(__APPLE__)
    if (const char *home = std::getenv("HOME"))
        return fs::path(home) / "Library" / "Application Support";
    return {};
#else
    // XDG_DATA_HOME vide = non défini (spécification XDG) : on retombe alors sur ~/.local/share.
    if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        return fs::path(xdg);
    if (const char *home = std::getenv("HOME"))
        return fs::path(home) / ".local" / "share";
    return {};
#endif
}

void preciseSleep(std::chrono::nanoseconds sleepTime)
{
#if defined(_WIN32)
    // Windows : Sleep() à la milliseconde pour les attentes longues, attente active sur QueryPerformanceCounter en dessous.
    if (sleepTime >= std::chrono::milliseconds(1))
    {
        DWORD sleepMs = static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(sleepTime).count());
        Sleep(sleepMs);
    }
    else
    {
        LARGE_INTEGER frequency, start, current;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);

        LONGLONG targetTicks = start.QuadPart + (sleepTime.count() * frequency.QuadPart) / 1000000000LL;

        do
        {
            std::this_thread::yield();
            QueryPerformanceCounter(&current);
        } while (current.QuadPart < targetTicks);
    }
#elif defined(__linux__)
    // Linux : nanosleep pour les attentes longues, attente active sinon.
    if (sleepTime >= std::chrono::microseconds(100))
    {
        struct timespec ts;
        ts.tv_sec = sleepTime.count() / 1000000000LL;
        ts.tv_nsec = sleepTime.count() % 1000000000LL;
        nanosleep(&ts, nullptr);
    }
    else
    {
        const auto targetTime = std::chrono::high_resolution_clock::now() + sleepTime;
        while (std::chrono::high_resolution_clock::now() < targetTime)
            std::this_thread::yield();
    }
#else
    // Autres plateformes : sleep_for standard, attente active pour les très courtes durées.
    if (sleepTime >= std::chrono::microseconds(100))
    {
        std::this_thread::sleep_for(sleepTime);
    }
    else
    {
        const auto targetTime = std::chrono::high_resolution_clock::now() + sleepTime;
        while (std::chrono::high_resolution_clock::now() < targetTime)
            std::this_thread::yield();
    }
#endif
}

bool beginHighResolutionTimer()
{
#if defined(_WIN32)
    return timeBeginPeriod(1) == TIMERR_NOERROR;
#else
    return false;
#endif
}

void endHighResolutionTimer()
{
#if defined(_WIN32)
    timeEndPeriod(1);
#endif
}

} // namespace engine::core::system
