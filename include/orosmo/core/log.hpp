// Journal du moteur (repris de l'ancien `Utilities/Logger.h`, comportement inchangé) : messages horodatés regroupés quand
// ils se répètent, couleur par niveau dans la console, fichier de journal optionnel (`SET_LOG_FILE`), pile d'appels en DEBUG.
// S'utilise par les macros `LOG_INFO(...)`, `LOG_ERROR(code, ...)`, etc. définies en bas de ce fichier.
#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
// Stack traces in DEBUG builds need <stacktrace> (C++23) with a working implementation. Apple's libc++ has none:
// the Logger then simply records an empty trace. Windows/GCC may need -lstdc++exp (see tools/engine-sdk/engine.mk).
#ifdef DEBUG
#if __has_include(<stacktrace>)
#include <stacktrace>
#endif
#if defined(__cpp_lib_stacktrace)
#define LOGGER_HAS_STACKTRACE 1
#endif
#endif

namespace orosmo::core
{

enum LogLevel : std::int8_t
{
#ifdef DEBUG
    L_DEBUGGING = -1, // Mostly for testing variables when added, shouldn't be keep long even for debugging
#endif
    L_DEBUG,
    L_TRACE,
    L_INFO,
    L_WARNING,
    L_ERROR,
    L_FATAL
};

class Logger
{
  private:
    struct LogMessage
    {
        LogLevel level;
        size_t repetition = 1;
        std::chrono::time_point<std::chrono::system_clock> time;
#ifdef DEBUG
        std::string file;
        int line;
#endif
        int errorCode = 0; // 0 = no error
        std::uint64_t sequence = 0; // numéro d'ordre du message (RecordsSince), hors comparaison des doublons
        std::string message;
#ifdef DEBUG
        std::string stackTrace;
#endif

        bool operator==(const LogMessage &other) const
        {
            return (level == other.level && message == other.message && errorCode == other.errorCode
#ifdef DEBUG
                    && file == other.file && line == other.line && stackTrace == other.stackTrace
#endif
            );
        }

        bool operator!=(const LogMessage &other) const { return !(*this == other); }
    };

  public:
    // Copie publique d'un message gardé (console de développement, UI-04) : `sequence` croît à chaque nouveau message (un doublon
    // regroupé garde le sien et augmente `repetition`).
    struct Record
    {
        LogLevel level;
        std::uint64_t sequence = 0;
        std::size_t repetition = 1;
        int errorCode = 0;
        std::string message;
    };

    // Messages gardés en mémoire au plus (au-delà, les plus anciens sont écrits dans le fichier puis oubliés) et profondeur de la
    // recherche d'un doublon (messages identiques regroupés).
    static constexpr size_t kMaxKeptLogs = 8192;
    static constexpr size_t kDuplicateSearchDepth = 64;

    static void Initialize(const std::string &filename = "");
    static size_t KeptLogCount();
    static void Clear();
    static void Print();
    static void SetMinimumLevel(LogLevel level);
    static LogLevel GetMinimumLevel();
    static void FlushToFile() noexcept;
    // Messages gardés dont le numéro d'ordre dépasse `after`, du plus ancien au plus récent, les `max` derniers au plus. Les
    // messages déjà oubliés (au-delà de kMaxKeptLogs, ou après Clear) ne reviennent pas.
    static std::vector<Record> RecordsSince(std::uint64_t after, std::size_t max = kMaxKeptLogs);
    // Numéro d'ordre du dernier message ajouté (0 : aucun).
    static std::uint64_t LastSequence();

    template <typename... Args>
    static void Log(LogLevel level,
#ifdef DEBUG
                    const std::string &file, int line,
#endif
                    Args &&...args) noexcept;

    template <typename... Args>
    static void LogError(LogLevel level,
#ifdef DEBUG
                         const std::string &file, int line,
#endif
                         int errorCode, Args &&...args) noexcept;

  private:
    static void AddLog(LogMessage &&msg);
    static size_t CalculateLogLength(const LogMessage &log);
    static size_t CalculateNumberOfLines(const LogMessage &log, size_t terminalWidth);
    static void FlushLocked(bool announce);
    static void TrimLocked();
    static std::string FormatTimestamp(const std::chrono::time_point<std::chrono::system_clock> &time);
    static void PrintLog(const LogMessage &log);
    static std::string LevelToString(LogLevel level);
    static std::string GetDefaultColor();
    static std::string GetColorLevel(LogLevel level);
    static void ChangeColor(LogLevel level);
    static void ChangeColor(const std::string &color);
    static void ResetColor();
#ifdef DEBUG
    static std::string CaptureStackTrace();
#endif

    static std::vector<LogMessage> logs;
    static LogLevel lLevelPrinted;
    static std::mutex logMutex;
    static std::unique_ptr<std::ofstream> logFile;
    static bool isLoggingToFile;
    static size_t lastFlushedIndex;
    static std::uint64_t nextSequence;
};

template <typename... Args>
void Logger::Log(LogLevel level,
#ifdef DEBUG
                 const std::string &file, int line,
#endif
                 Args &&...args) noexcept
{
    Logger::LogError(level,
#ifdef DEBUG
                     file, line,
#endif
                     0, std::forward<Args>(args)...);
}

template <typename... Args>
void Logger::LogError(LogLevel level,
#ifdef DEBUG
                      const std::string &file, int line,
#endif
                      int errorCode, Args &&...args) noexcept
{
    try
    {
        std::ostringstream oss;
        ((oss << args), ...);

        LogMessage msg;
        msg.level = level;
        msg.time = std::chrono::system_clock::now();
#ifdef DEBUG
        msg.file = file;
        msg.line = line;
#endif
        msg.message = oss.str();
        msg.errorCode = errorCode;
#ifdef DEBUG
        msg.stackTrace = CaptureStackTrace();
#endif

        Logger::AddLog(std::move(msg));
    }
    catch (...)
    {
        std::fputs("Logger::LogError failed\n", stderr);
    }
}

} // namespace orosmo::core

#define LOG_LEVEL ::orosmo::core::Logger::GetMinimumLevel()
#ifdef DEBUG
#define SET_LOG_LEVEL_DEBUG ::orosmo::core::Logger::SetMinimumLevel(::orosmo::core::L_DEBUGGING)
#else
#define SET_LOG_LEVEL_DEBUG ::orosmo::core::Logger::SetMinimumLevel(::orosmo::core::L_DEBUG)
#endif
#define SET_LOG_LEVEL_TRACE ::orosmo::core::Logger::SetMinimumLevel(::orosmo::core::L_TRACE)
#define SET_LOG_LEVEL_INFO ::orosmo::core::Logger::SetMinimumLevel(::orosmo::core::L_INFO)
#define SET_LOG_LEVEL_WARNING ::orosmo::core::Logger::SetMinimumLevel(::orosmo::core::L_WARNING)
#define SET_LOG_LEVEL_ERROR ::orosmo::core::Logger::SetMinimumLevel(::orosmo::core::L_ERROR)
#define SET_LOG_LEVEL_FATAL ::orosmo::core::Logger::SetMinimumLevel(::orosmo::core::L_FATAL)
#define SET_LOG_FILE(filename) ::orosmo::core::Logger::Initialize(filename)
#define SET_LOG_FILE_DEFAULT ::orosmo::core::Logger::Initialize()
#define FLUSH_LOG_TO_FILE ::orosmo::core::Logger::FlushToFile()

#ifdef DEBUG
#define LOG(level, ...) ::orosmo::core::Logger::Log(level, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUGGING(...) ::orosmo::core::Logger::Log(::orosmo::core::L_DEBUGGING, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) ::orosmo::core::Logger::Log(::orosmo::core::L_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_TRACE(...) ::orosmo::core::Logger::Log(::orosmo::core::L_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) ::orosmo::core::Logger::Log(::orosmo::core::L_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) ::orosmo::core::Logger::Log(::orosmo::core::L_WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_EWARNING(...) ::orosmo::core::Logger::LogError(::orosmo::core::L_WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) ::orosmo::core::Logger::LogError(::orosmo::core::L_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) ::orosmo::core::Logger::LogError(::orosmo::core::L_FATAL, __FILE__, __LINE__, __VA_ARGS__)
#else
#define LOG(level, ...) ::orosmo::core::Logger::Log(level, __VA_ARGS__)
#define LOG_DEBUGGING(...)
#define LOG_DEBUG(...) ::orosmo::core::Logger::Log(::orosmo::core::L_DEBUG, __VA_ARGS__)
#define LOG_TRACE(...) ::orosmo::core::Logger::Log(::orosmo::core::L_TRACE, __VA_ARGS__)
#define LOG_INFO(...) ::orosmo::core::Logger::Log(::orosmo::core::L_INFO, __VA_ARGS__)
#define LOG_WARNING(...) ::orosmo::core::Logger::Log(::orosmo::core::L_WARNING, __VA_ARGS__)
#define LOG_EWARNING(...) ::orosmo::core::Logger::LogError(::orosmo::core::L_WARNING, __VA_ARGS__)
#define LOG_ERROR(...) ::orosmo::core::Logger::LogError(::orosmo::core::L_ERROR, __VA_ARGS__)
#define LOG_FATAL(...) ::orosmo::core::Logger::LogError(::orosmo::core::L_FATAL, __VA_ARGS__)
#endif
