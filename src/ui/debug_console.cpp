#include "orosmo/ui/debug_console.hpp"

#include "orosmo/core/log.hpp"

#include <utility>

namespace orosmo::ui::debug
{

using core::ErrorCode;
using core::makeError;

core::Result<std::vector<std::string>> splitCommandLine(std::string_view line)
{
    std::vector<std::string> words;
    std::size_t i = 0;
    const auto space = [](char c) { return c == ' ' || c == '\t'; };
    while (i < line.size())
    {
        while (i < line.size() && space(line[i]))
            ++i;
        if (i == line.size())
            break;
        std::string word;
        if (line[i] == '"')
        {
            ++i;
            bool closed = false;
            while (i < line.size())
            {
                const char c = line[i++];
                if (c == '"')
                {
                    closed = true;
                    break;
                }
                if (c == '\\' && i < line.size() && (line[i] == '"' || line[i] == '\\'))
                    word.push_back(line[i++]);
                else
                    word.push_back(c);
            }
            if (!closed)
                return makeError(ErrorCode::kInvalidArgument, "unterminated quote");
        }
        else
        {
            while (i < line.size() && !space(line[i]))
                word.push_back(line[i++]);
        }
        words.push_back(std::move(word));
    }
    return words;
}

Console::Console()
{
    logSequence_ = core::Logger::LastSequence();
    (void)add("help", "help : list the commands",
              [this](std::span<const std::string>) -> core::Result<std::string>
              {
                  std::string out;
                  for (const auto &[name, entry] : commands_)
                  {
                      if (!out.empty())
                          out += '\n';
                      out += entry.help.empty() ? name : entry.help;
                  }
                  return out;
              });
    (void)add("clear", "clear : clear the console",
              [this](std::span<const std::string>) -> core::Result<std::string>
              {
                  clear();
                  return std::string();
              });
}

core::Result<> Console::add(std::string name, std::string help, ConsoleCommand command)
{
    if (name.empty() || name.find_first_of(" \t\"") != std::string::npos)
        return makeError(ErrorCode::kInvalidArgument, "invalid console command name '" + name + "'");
    if (!command)
        return makeError(ErrorCode::kInvalidArgument, "console command '" + name + "' has no function");
    if (commands_.contains(name))
        return makeError(ErrorCode::kAlreadyExists, "console command '" + name + "' already exists");
    commands_.emplace(std::move(name), Entry{std::move(help), std::make_shared<ConsoleCommand>(std::move(command))});
    return {};
}

bool Console::remove(std::string_view name)
{
    const auto it = commands_.find(name);
    if (it == commands_.end())
        return false;
    commands_.erase(it);
    return true;
}

bool Console::contains(std::string_view name) const { return commands_.find(name) != commands_.end(); }

std::vector<std::string> Console::names() const
{
    std::vector<std::string> out;
    out.reserve(commands_.size());
    for (const auto &entry : commands_)
        out.push_back(entry.first);
    return out;
}

core::Result<std::string> Console::execute(std::string_view line)
{
    auto words = splitCommandLine(line);
    if (words && words->empty())
        return std::string();
    print("> " + std::string(line), ConsoleLineKind::kInput);
    if (!words)
    {
        print(words.error().message, ConsoleLineKind::kError);
        return std::unexpected(words.error());
    }
    const auto it = commands_.find((*words)[0]);
    if (it == commands_.end())
    {
        auto error = makeError(ErrorCode::kNotFound, "unknown command '" + (*words)[0] + "' (help: list the commands)");
        print(error.error().message, ConsoleLineKind::kError);
        return error;
    }
    // Référence partagée (pas une copie, qui perdrait l'état) : la commande peut se retirer elle-même pendant l'appel.
    const std::shared_ptr<ConsoleCommand> command = it->second.command;
    auto result = (*command)(std::span<const std::string>(*words).subspan(1));
    if (!result)
    {
        print(result.error().message, ConsoleLineKind::kError);
        return result;
    }
    if (!result->empty())
    {
        std::size_t start = 0;
        while (start <= result->size())
        {
            const std::size_t end = result->find('\n', start);
            print(result->substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    return result;
}

void Console::print(std::string text, ConsoleLineKind kind)
{
    lines_.push_back(ConsoleLine{std::move(text), kind});
    while (lines_.size() > kMaxLines)
        lines_.pop_front();
    ++revision_;
}

void Console::clear()
{
    lines_.clear();
    ++revision_;
}

bool Console::pollLog()
{
    const std::uint64_t last = core::Logger::LastSequence();
    if (last == logSequence_)
        return false;
    const auto records = core::Logger::RecordsSince(logSequence_, kMaxLines);
    // Messages oubliés entre-temps (Logger::Clear) : aucun ; un message arrivé depuis `last` sera lu au prochain appel.
    logSequence_ = records.empty() ? last : records.back().sequence;
    for (const auto &record : records)
    {
        const ConsoleLineKind kind = record.level >= core::L_ERROR     ? ConsoleLineKind::kError
                                     : record.level == core::L_WARNING ? ConsoleLineKind::kWarning
                                                                       : ConsoleLineKind::kOutput;
        print(record.message, kind);
    }
    return !records.empty();
}

} // namespace orosmo::ui::debug
