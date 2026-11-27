#include "orosmo/core/log.hpp"

#include <orosmo/core/system.hpp>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <ctime>

namespace orosmo::core
{

std::vector<Logger::LogMessage> Logger::logs;
std::mutex Logger::logMutex;
std::unique_ptr<std::ofstream> Logger::logFile;
bool Logger::isLoggingToFile = false;
size_t Logger::lastFlushedIndex = 0;
std::uint64_t Logger::nextSequence = 1;
#ifdef DEBUG
LogLevel Logger::lLevelPrinted = LogLevel::L_DEBUGGING;
#else
LogLevel Logger::lLevelPrinted = LogLevel::L_INFO;
#endif

void Logger::Initialize(const std::string &sFilename)
{
    std::lock_guard<std::mutex> lock(logMutex);

    std::string filename = sFilename;
    if (filename.empty())
    {

        // Generate timestamped filename
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        // Dossier de données de l'application (APPDATA, Application Support, XDG_DATA_HOME : src/platform/system.cpp), sinon
        // le dossier courant (comme avant).
        std::stringstream ss;
        if (const std::filesystem::path base = orosmo::core::system::userDataDirectory(); !base.empty())
        {
            ss << (base / "Orosmo").string() << '/';
        }
        ss << "logs/log_" << std::put_time(std::localtime(&time_t), "%Y-%m-%d") << ".log";
        filename = ss.str();
        std::filesystem::create_directories(std::filesystem::path(filename).parent_path());
    }
    else
    {
        // Ensure directory exists for provided filename
        std::filesystem::path filepath(filename);
        if (filepath.has_parent_path())
        {
            std::filesystem::create_directories(filepath.parent_path());
        }
    }

    // Close existing file
    if (logFile && logFile->is_open())
    {
        *logFile << "\n=== Logger Session Ended: " << FormatTimestamp(std::chrono::system_clock::now()) << " ===\n";
        logFile->close();
        isLoggingToFile = false;
    }

    // Open new file
    logFile = std::make_unique<std::ofstream>(filename, std::ios::out | std::ios::app);

    if (!logFile->is_open())
    {
        std::cerr << "Failed to open log file: " << filename << std::endl;
        isLoggingToFile = false;
        return;
    }

    // Write session header
    auto now = std::chrono::system_clock::now();
    *logFile << "\n=== Logger Session Started: " << FormatTimestamp(now) << " ===" << std::endl;
    *logFile << "Process ID: " << orosmo::core::system::processId() << std::endl;
    *logFile << "Working Directory: " << std::filesystem::current_path() << std::endl;
    *logFile << std::endl;
    logFile->flush();
    isLoggingToFile = true;
#ifdef DEBUG
    std::cout << "Logger initialized successfully. File: " << filename << std::endl;
#endif
}

void Logger::Clear()
{
    std::lock_guard<std::mutex> lock(logMutex);
    logs.clear();
    lastFlushedIndex = 0;
}

void Logger::Print()
{
    std::lock_guard<std::mutex> lock(logMutex);
    for (const LogMessage &log : logs)
    {
        PrintLog(log);
    }
}

void Logger::SetMinimumLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(logMutex);
    lLevelPrinted = level;
}

LogLevel Logger::GetMinimumLevel()
{
    std::lock_guard<std::mutex> lock(logMutex);
    return lLevelPrinted;
}

void Logger::FlushToFile() noexcept
{
    try
    {
        if (!isLoggingToFile)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(logMutex);
        FlushLocked(true);
    }
    catch (...)
    {
        std::fputs("Logger::FlushToFile failed\n", stderr);
    }
}

std::vector<Logger::Record> Logger::RecordsSince(std::uint64_t after, std::size_t max)
{
    std::lock_guard<std::mutex> lock(logMutex);
    // Numéros croissants dans `logs` : recherche du premier message plus récent que `after`.
    auto first = std::upper_bound(logs.begin(), logs.end(), after, [](std::uint64_t s, const LogMessage &m) { return s < m.sequence; });
    const auto available = static_cast<std::size_t>(logs.end() - first);
    if (available > max)
    {
        first += static_cast<std::ptrdiff_t>(available - max);
    }
    std::vector<Record> out;
    out.reserve(static_cast<std::size_t>(logs.end() - first));
    for (auto it = first; it != logs.end(); ++it)
    {
        out.push_back(Record{it->level, it->sequence, it->repetition, it->errorCode, it->message});
    }
    return out;
}

std::uint64_t Logger::LastSequence()
{
    std::lock_guard<std::mutex> lock(logMutex);
    return nextSequence - 1;
}

size_t Logger::KeptLogCount()
{
    std::lock_guard<std::mutex> lock(logMutex);
    return logs.size();
}

// Écrit dans le fichier les messages pas encore écrits ; appelé avec logMutex tenu. `announce` : message de réussite en DEBUG
// (pas pendant l'élagage, qui ne doit rien afficher entre deux messages).
void Logger::FlushLocked(bool announce)
{
    static_cast<void>(announce); // utilisé seulement en DEBUG
    {
#ifdef DEBUG
        bool flushFailed = false;
        bool wroteLogs = false;
#endif
        if (!isLoggingToFile)
        {
            return;
        }
        if (!logFile || !logFile->is_open())
        {
            std::cerr << "Logger file is not available for flushing" << std::endl;
            return;
        }

        if (lastFlushedIndex >= logs.size())
        {
            return;
        }

        for (size_t i = lastFlushedIndex; i < logs.size(); ++i)
        {
            const LogMessage &log = logs[i];

#ifdef DEBUG
            if (log.level == L_DEBUGGING)
            {
                continue;
            }
#endif

            *logFile << "[" << FormatTimestamp(log.time) << "] ";
            *logFile << "[" << LevelToString(log.level) << "] ";

#ifdef DEBUG
            if (!log.stackTrace.empty())
            {
                *logFile << log.stackTrace << " ";
            }
#endif

#ifdef DEBUG
            *logFile << log.file << ":" << log.line << ": ";
#endif

            *logFile << log.message;

            if (log.errorCode != 0)
            {
                *logFile << " (Error: " << log.errorCode << ")";
            }

            *logFile << '\n';
#ifdef DEBUG
            wroteLogs = true;
#endif
        }
        lastFlushedIndex = logs.size();

        logFile->flush();
#ifdef DEBUG
        flushFailed = logFile->fail();
        if (flushFailed)
        {
            std::cerr << "ERROR: Failed to write to log file!" << std::endl;
        }
        else if (wroteLogs && announce)
        {
            std::cout << "\033[32m";
            std::cout << "Successfully flushed logs to file";
            std::cout << "\033[0m" << std::endl;
        }
#endif
    }
}

// Borne la mémoire : au-delà de kMaxKeptLogs messages, les plus anciens (écrits d'abord dans le fichier) sont oubliés.
void Logger::TrimLocked()
{
    if (logs.size() <= kMaxKeptLogs)
    {
        return;
    }
    FlushLocked(false);
    const size_t drop = logs.size() - kMaxKeptLogs / 2;
    logs.erase(logs.begin(), logs.begin() + static_cast<std::ptrdiff_t>(drop));
    lastFlushedIndex = lastFlushedIndex > drop ? lastFlushedIndex - drop : 0;
}

size_t getTerminalWidth() { return orosmo::core::system::terminalWidth(); } // OS : src/platform/system.cpp

size_t Logger::CalculateLogLength(const LogMessage &log)
{
    size_t length = 4;
    length += FormatTimestamp(log.time).size() + 2; // +2 for "[]"
    if (log.repetition > 1)
    {
        length += std::to_string(log.repetition).size() + 2; // +2 for "[]"
    }
    length += LevelToString(log.level).size() + 2; // +2 for "[]"
#ifdef DEBUG
    if (!log.stackTrace.empty())
    {
        length += log.stackTrace.size() + 1; // +1 for " "
    }
#endif
#ifdef DEBUG
    length += log.file.size() + 1;             // +1 for ":"
    length += std::to_string(log.line).size(); // +1 for ":"
#endif
    length += log.message.size();
    return length;
}

size_t Logger::CalculateNumberOfLines(const LogMessage &log, size_t terminalWidth)
{
    if (log.level < lLevelPrinted)
    {
        return 0; // jamais affiché (PrintLog)
    }
    if (terminalWidth == 0)
    {
        return 1;
    }

    // Calculer la longueur du préfixe (tout sauf le message)
    size_t prefixLength = 4;
    prefixLength += FormatTimestamp(log.time).size() + 2;
    if (log.repetition > 1)
    {
        prefixLength += std::to_string(log.repetition).size() + 2;
    }
    prefixLength += LevelToString(log.level).size() + 2;
#ifdef DEBUG
    if (!log.stackTrace.empty())
    {
        prefixLength += log.stackTrace.size() + 1;
    }
#endif
#ifdef DEBUG
    prefixLength += log.file.size() + 1;
    prefixLength += std::to_string(log.line).size();
#endif

    // Diviser le message par les \n
    std::vector<std::string> messageLines;
    std::stringstream ss(log.message);
    std::string line;

    while (std::getline(ss, line))
    {
        messageLines.push_back(line);
    }

    // Si pas de \n dans le message, ajouter le message complet
    if (messageLines.empty())
    {
        messageLines.push_back(log.message);
    }

    size_t totalLines = 0;

    for (size_t i = 0; i < messageLines.size(); i++)
    {
        size_t lineLength;

        if (i == 0)
        {
            // Première ligne : préfixe + contenu
            lineLength = prefixLength + messageLines[i].size();
        }
        else
        {
            // Lignes suivantes : seulement le contenu (avec indentation possible)
            lineLength = messageLines[i].size();
        }

        // Calculer combien de lignes cette partie prend
        if (lineLength == 0)
        {
            totalLines++; // Ligne vide
        }
        else
        {
            totalLines += (lineLength + terminalWidth - 1) / terminalWidth;
        }
    }

    return totalLines > 0 ? totalLines : 1;
}

void Logger::AddLog(LogMessage &&log)
{
    std::lock_guard<std::mutex> lock(logMutex);

    const auto timeLimit = std::chrono::system_clock::now() - std::chrono::seconds(3);

    // Doublon récent : parmi les kDuplicateSearchDepth derniers messages des 3 dernières secondes (comparaisons seulement ;
    // les lignes à remonter ne sont comptées que si un doublon est trouvé).
    bool isDuplicate = false;
    size_t duplicateIndex = 0;
    for (size_t i = logs.size(), scanned = 0; i > 0 && scanned < kDuplicateSearchDepth; --i, ++scanned)
    {
        const size_t idx = i - 1;
        if (logs[idx].time < timeLimit)
        {
            break; // Trop ancien, arrêter la recherche
        }
        if (logs[idx] == log)
        {
            duplicateIndex = idx;
            isDuplicate = true;
            break;
        }
    }

    if (isDuplicate)
    {
        LogMessage &duplicateLog = logs[duplicateIndex];
        if (duplicateLog.level < lLevelPrinted)
        {
            duplicateLog.repetition++; // jamais affiché : rien à réécrire
            return;
        }
        const size_t terminalWidth = getTerminalWidth();
        size_t dstUp = 0;
        for (size_t idx = duplicateIndex; idx < logs.size(); ++idx)
        {
            dstUp += CalculateNumberOfLines(logs[idx], terminalWidth);
        }
        duplicateLog.repetition++;

        // Remonter à la ligne du duplicate
        std::cout << "\033[" << dstUp << "F";

        bool needFullRewrite = std::to_string(duplicateLog.repetition).size() != std::to_string(duplicateLog.repetition - 1).size() ||
                               duplicateLog.repetition == 2;

        if (needFullRewrite)
        {
            PrintLog(duplicateLog);
        }
        else
        {
            // Mise à jour partielle du compteur de répétition
            std::string timestamp = FormatTimestamp(duplicateLog.time);
            size_t prefixLen = timestamp.size() + 4; // timestamp + "] ["
            std::cout << "\033[" << prefixLen << "C";
            std::cout << duplicateLog.repetition;
        }

        // Redescendre à la position originale
        std::cout << "\033[" << dstUp - (needFullRewrite ? 1 : 0) << "E";
    }
    else
    {
        log.sequence = nextSequence++;
        logs.push_back(std::move(log));
        PrintLog(logs.back());

        if (logs.back().level == L_FATAL)
        {
            std::exit(1);
        }
        TrimLocked();
    }
}

std::string Logger::FormatTimestamp(const std::chrono::time_point<std::chrono::system_clock> &timestamp)
{
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()) % 1000;

    // La date à la seconde est mise en cache : std::localtime relit le fuseau horaire (/etc/localtime) à chaque appel.
    thread_local std::time_t cachedSecond = static_cast<std::time_t>(-1);
    thread_local std::string cachedText;
    if (time_t != cachedSecond)
    {
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &time_t);
#else
        localtime_r(&time_t, &local);
#endif
        std::stringstream ss;
        ss << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
        cachedText = ss.str();
        cachedSecond = time_t;
    }
    char millis[8];
    std::snprintf(millis, sizeof(millis), ".%03d", static_cast<int>(ms.count()));
    return cachedText + millis;
}

void Logger::PrintLog(const LogMessage &log)
{
    if (log.level < lLevelPrinted)
    {
        return;
    }

    std::cout << "[" << FormatTimestamp(log.time) << "] ";
    if (log.repetition > 1)
    {
        std::cout << "[" << log.repetition << "x] ";
    }
    ChangeColor(log.level);
    std::cout << "[" << LevelToString(log.level) << "]";

#ifdef DEBUG
    if (!log.stackTrace.empty() && (log.level == L_DEBUGGING || log.level >= L_WARNING))
    {
        ResetColor();
        std::cout << " " << log.stackTrace;
    }
    ResetColor();
    std::cout << " " << log.file << ":" << log.line << " ";
    ChangeColor(log.level);
    if (log.level >= L_WARNING)
        std::cerr << log.message;
    else
        std::cout << log.message;
#else
    std::cout << " " << log.message;
#endif

    if (log.errorCode != 0)
    {
        std::cerr << " (Error: " << log.errorCode << ")";
    }

    ResetColor();
    std::cout << std::endl;
}

std::string Logger::LevelToString(LogLevel level)
{
    switch (level)
    {
#ifdef DEBUG
        case LogLevel::L_DEBUGGING:
            return "DEBUGGING";
#endif
        case LogLevel::L_DEBUG:
            return "DEBUG";
        case LogLevel::L_TRACE:
            return "TRACE";
        case LogLevel::L_INFO:
            return "INFO";
        case LogLevel::L_WARNING:
            return "WARNING";
        case LogLevel::L_ERROR:
            return "ERROR";
        case LogLevel::L_FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

std::string Logger::GetDefaultColor()
{
    return "39;49"; // Default color: text and background are white
}

std::string Logger::GetColorLevel(LogLevel level)
{
    switch (level)
    {
#ifdef DEBUG
        case LogLevel::L_DEBUGGING:
            return "95;49";
#endif
        case LogLevel::L_DEBUG:
            return "90;49";
        case LogLevel::L_TRACE:
            return GetDefaultColor();
        case LogLevel::L_INFO:
            return "36;49";
        case LogLevel::L_WARNING:
            return "33;49";
        case LogLevel::L_ERROR:
            return "31;49";
        case LogLevel::L_FATAL:
            return "97;41";
        default:
            return GetDefaultColor();
    }
}

void Logger::ChangeColor(LogLevel level) { std::cout << "\033[" << GetColorLevel(level) << "m"; }

void Logger::ChangeColor(const std::string &color) { std::cout << "\033[" << color << "m"; }

void Logger::ResetColor() { std::cout << "\033[" << GetDefaultColor() << "m"; }

#if defined(DEBUG) && !defined(LOGGER_HAS_STACKTRACE)
std::string Logger::CaptureStackTrace() { return {}; }
#endif

#ifdef LOGGER_HAS_STACKTRACE
std::string Logger::CaptureStackTrace()
{
    std::ostringstream ss;

    // Obtenir la stack trace actuelle
    // On capture tout et on filtrera manuellement
    auto trace = std::stacktrace::current();

    std::vector<std::string> frames;
    for (const auto &entry : trace)
    {
        std::string desc = entry.description();

        // Fallback si vide
        if (desc.empty())
        {
            std::ostringstream oss;
            oss << entry;
            desc = oss.str();
        }

        // Filtrer les fonctions internes du Logger
        // On vérifie si ça contient "Logger::"
        if (desc.find("Logger::") != std::string::npos)
        {
            continue;
        }

        // Detecter le point d'entrée "main" ou les fonctions de démarrage système pour arrêter
        // On vérifie si c'est main, WinMain, ou des trucs de démarrage comme register_frame_ctor
        if (desc.find("main") != std::string::npos || desc.find("WinMain") != std::string::npos)
        {
            frames.push_back("main"); // On normalise le nom
            break;                    // On s'arrête ici, on ne veut pas ce qu'il y a avant main
        }

        // Si on tombe sur des fonctions de démarrage connues qui ne contiennent pas "main"
        if (desc.find("register_frame_ctor") != std::string::npos || desc.find("__tmainCRTStartup") != std::string::npos ||
            desc.find("BaseThreadInitThunk") != std::string::npos)
        {
            break; // On s'arrête AVANT d'ajouter cette frame
        }

        frames.push_back(desc);
    }

    // Le format demandé est main > Game > ... > function
    // stacktrace::current() renvoie function -> caller -> ... -> main
    // Donc on doit inverser l'ordre
    std::reverse(frames.begin(), frames.end());

    // Construire la chaîne
    bool first = true;
    for (size_t i = 0; i < frames.size(); ++i)
    {
        if (!first)
        {
            ss << " > ";
        }
        ss << frames[i];
        first = false;
    }

    return ss.str();
}
#endif

} // namespace orosmo::core
