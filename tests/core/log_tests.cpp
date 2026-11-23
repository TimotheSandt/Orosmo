// CORE-01 : le logger existant est conservé ; ces tests figent son comportement observable (fichier, dédoublonnage,
// codes d'erreur, niveaux, accès concurrents) avant toute migration (M1 bis).
#include <doctest/doctest.h>

#include <orosmo/core/log.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace orosmo::core; // Logger, LogLevel et ses niveaux L_*

namespace
{
std::string slurp(const fs::path &p)
{
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::size_t countOf(const std::string &text, const std::string &needle)
{
    std::size_t n = 0;
    for (auto pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + 1))
        ++n;
    return n;
}

struct LoggerFile
{
    fs::path path;
    explicit LoggerFile(const std::string &name) : path(fs::temp_directory_path() / ("engine_logger_" + name + ".log"))
    {
        fs::remove(path);
        Logger::Clear();
        Logger::Initialize(path.string());
    }
    ~LoggerFile()
    {
        Logger::Clear();
        std::error_code ec;
        fs::remove(path, ec);
    }
};
} // namespace

TEST_SUITE("Logger")
{
    TEST_CASE("messages reach the file with level, text and error code")
    {
        LoggerFile f("file");
        LOG_INFO("hello ", 42, " world");
        LOG_WARNING("careful");
        LOG_ERROR(7, "bad thing");
        Logger::FlushToFile();
        const std::string text = slurp(f.path);
        CHECK(text.find("Logger Session Started") != std::string::npos);
        CHECK(text.find("[INFO]") != std::string::npos);
        CHECK(text.find("hello 42 world") != std::string::npos);
        CHECK(text.find("[WARNING]") != std::string::npos);
        CHECK(text.find("careful") != std::string::npos);
        CHECK(text.find("bad thing (Error: 7)") != std::string::npos);
    }

    TEST_CASE("flush writes each message once, and only the new ones")
    {
        LoggerFile f("flush");
        LOG_INFO("first-message");
        Logger::FlushToFile();
        Logger::FlushToFile(); // rien de nouveau
        LOG_INFO("second-message");
        Logger::FlushToFile();
        const std::string text = slurp(f.path);
        CHECK(countOf(text, "first-message") == 1);
        CHECK(countOf(text, "second-message") == 1);
    }

    TEST_CASE("identical messages in a row are folded into one entry with a repetition counter")
    {
        LoggerFile f("dedup");
        for (int i = 0; i < 3; ++i)
            LOG_INFO("same line"); // même ligne source : même message au sens du logger
        LOG_INFO("different");
        Logger::FlushToFile();
        const std::string text = slurp(f.path);
        CHECK(countOf(text, "same line") == 1); // trois appels, une seule entrée
        CHECK(countOf(text, "different") == 1);
    }

    TEST_CASE("minimum level is stored and restored")
    {
        const LogLevel previous = Logger::GetMinimumLevel();
        Logger::SetMinimumLevel(L_WARNING);
        CHECK(Logger::GetMinimumLevel() == L_WARNING);
        Logger::SetMinimumLevel(previous);
        CHECK(Logger::GetMinimumLevel() == previous);
    }

    TEST_CASE("Clear forgets the messages and the flush position")
    {
        LoggerFile f("clear");
        LOG_INFO("before-clear");
        Logger::Clear();
        LOG_INFO("after-clear");
        Logger::FlushToFile();
        const std::string text = slurp(f.path);
        CHECK(text.find("before-clear") == std::string::npos);
        CHECK(text.find("after-clear") != std::string::npos);
    }

    TEST_CASE("memory is bounded: past kMaxKeptLogs messages the oldest are written to the file, then forgotten")
    {
        LoggerFile f("bounded");
        const LogLevel previous = Logger::GetMinimumLevel();
        Logger::SetMinimumLevel(L_WARNING); // gardés et écrits dans le fichier, pas affichés
        const std::size_t total = Logger::kMaxKeptLogs + 1000;
        for (std::size_t i = 0; i < total; ++i)
            LOG_INFO("bounded message ", i);
        CHECK(Logger::KeptLogCount() <= Logger::kMaxKeptLogs);
        Logger::FlushToFile();
        Logger::SetMinimumLevel(previous);
        const std::string text = slurp(f.path);
        CHECK(countOf(text, "bounded message ") == total); // rien de perdu pour le fichier
        CHECK(text.find("bounded message 0\n") != std::string::npos);
        CHECK(text.find("bounded message " + std::to_string(total - 1)) != std::string::npos);
    }

    TEST_CASE("concurrent logging from several threads loses nothing")
    {
        LoggerFile f("threads");
        constexpr int kThreads = 4, kPerThread = 100;
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t)
            threads.emplace_back([t] {
                for (int i = 0; i < kPerThread; ++i)
                    LOG_INFO("thread ", t, " message ", i);
            });
        for (auto &th : threads)
            th.join();
        Logger::FlushToFile();
        const std::string text = slurp(f.path);
        CHECK(countOf(text, " message ") == static_cast<std::size_t>(kThreads * kPerThread));
        CHECK(text.find("thread 3 message 99") != std::string::npos);
    }
}
