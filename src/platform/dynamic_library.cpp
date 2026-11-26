#include <orosmo/platform/dynamic_library.hpp>

#include <orosmo/core/system.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <atomic>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>

namespace orosmo::platform
{

namespace fs = std::filesystem;
using core::ErrorCode;
using core::makeError;

namespace
{

#if defined(_WIN32)
std::string lastSystemError()
{
    const DWORD code = GetLastError();
    char *text = nullptr;
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, reinterpret_cast<char *>(&text), 0, nullptr);
    std::string message = length > 0 && text != nullptr ? std::string(text, length) : std::string("error ");
    if (text != nullptr)
        LocalFree(text);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' '))
        message.pop_back();
    return message + " (code " + std::to_string(code) + ")";
}

void *openLibrary(const fs::path &path, std::string &error)
{
    // Pas de boîte de dialogue du système si une dépendance manque : l'erreur revient par le résultat.
    DWORD previousMode = 0;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &previousMode);
    HMODULE module = LoadLibraryW(path.c_str());
    if (module == nullptr)
        error = lastSystemError();
    SetThreadErrorMode(previousMode, nullptr);
    return reinterpret_cast<void *>(module);
}

void *findSymbol(void *handle, const std::string &name)
{
    return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle), name.c_str()));
}

void closeLibrary(void *handle) { FreeLibrary(static_cast<HMODULE>(handle)); }
#else
void *openLibrary(const fs::path &path, std::string &error)
{
    // RTLD_NOW : un symbole du moteur introuvable dans l'hôte fait échouer le chargement ici, pas un appel plus tard.
    // RTLD_LOCAL : deux versions chargées l'une après l'autre ne se prêtent pas leurs symboles.
    void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
    {
        const char *message = dlerror();
        error = message != nullptr ? message : "dlopen failed";
    }
    return handle;
}

void *findSymbol(void *handle, const std::string &name)
{
    dlerror();
    return dlsym(handle, name.c_str());
}

void closeLibrary(void *handle) { dlclose(handle); }
#endif

fs::path uniqueCopyPath(const fs::path &source, const fs::path &directory)
{
    static std::atomic<std::uint64_t> counter{0};
    const std::uint64_t n = counter.fetch_add(1, std::memory_order_relaxed);
    fs::path name = source.stem();
    name += "-" + std::to_string(core::system::processId()) + "-" + std::to_string(n);
    name += source.extension();
    return (directory.empty() ? source.parent_path() : directory) / name;
}

} // namespace

std::string_view DynamicLibrary::extension() noexcept
{
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

core::Result<DynamicLibrary> DynamicLibrary::load(const fs::path &path, const DynamicLibraryOptions &options)
{
    std::error_code ec;
    if (!fs::is_regular_file(path, ec))
        return makeError(ErrorCode::kNotFound, "dynamic library not found: " + path.string());

    DynamicLibrary library;
    library.sourcePath_ = fs::absolute(path, ec);
    if (ec)
        library.sourcePath_ = path;
    library.loadedPath_ = library.sourcePath_;

    if (options.copyBeforeLoad)
    {
        const fs::path copy = uniqueCopyPath(library.sourcePath_, options.copyDirectory);
        if (!fs::copy_file(library.sourcePath_, copy, fs::copy_options::overwrite_existing, ec))
            return makeError(ErrorCode::kIo, "cannot copy " + library.sourcePath_.string() + " to " + copy.string() + ": " + ec.message());
        library.loadedPath_ = copy;
        library.ownsCopy_ = true;
    }

    std::string error;
    library.handle_ = openLibrary(library.loadedPath_, error);
    if (library.handle_ == nullptr)
    {
        const fs::path loaded = library.loadedPath_;
        library.unload(); // efface la copie
        return makeError(ErrorCode::kIo, "cannot load " + loaded.string() + ": " + error);
    }
    return library;
}

DynamicLibrary::DynamicLibrary(DynamicLibrary &&other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)), ownsCopy_(std::exchange(other.ownsCopy_, false)),
      sourcePath_(std::move(other.sourcePath_)), loadedPath_(std::move(other.loadedPath_))
{
}

DynamicLibrary &DynamicLibrary::operator=(DynamicLibrary &&other) noexcept
{
    if (this != &other)
    {
        unload();
        handle_ = std::exchange(other.handle_, nullptr);
        ownsCopy_ = std::exchange(other.ownsCopy_, false);
        sourcePath_ = std::move(other.sourcePath_);
        loadedPath_ = std::move(other.loadedPath_);
    }
    return *this;
}

DynamicLibrary::~DynamicLibrary() { unload(); }

core::Result<void *> DynamicLibrary::symbol(std::string_view name) const
{
    if (handle_ == nullptr)
        return makeError(ErrorCode::kInvalidArgument, "symbol '" + std::string(name) + "': no library loaded");
    void *address = findSymbol(handle_, std::string(name));
    if (address == nullptr)
        return makeError(ErrorCode::kNotFound, "symbol '" + std::string(name) + "' not found in " + loadedPath_.string());
    return address;
}

void DynamicLibrary::unload() noexcept
{
    if (handle_ != nullptr)
    {
        closeLibrary(handle_);
        handle_ = nullptr;
    }
    if (ownsCopy_)
    {
        std::error_code ec;
        fs::remove(loadedPath_, ec); // au mieux : un échec laisse un fichier de plus, sans conséquence sur le chargement
        ownsCopy_ = false;
    }
}

} // namespace orosmo::platform
