#include <orosmo/core/error.hpp>

#include <atomic>
#include <cstdlib>

#include <orosmo/core/log.hpp>

namespace orosmo::core
{

const char *toString(ErrorCode code) noexcept
{
    switch (code)
    {
    case ErrorCode::kUnknown: return "kUnknown";
    case ErrorCode::kInvalidArgument: return "kInvalidArgument";
    case ErrorCode::kNotFound: return "kNotFound";
    case ErrorCode::kAlreadyExists: return "kAlreadyExists";
    case ErrorCode::kIo: return "kIo";
    case ErrorCode::kParse: return "kParse";
    case ErrorCode::kUnsupported: return "kUnsupported";
    case ErrorCode::kOutOfRange: return "kOutOfRange";
    case ErrorCode::kCancelled: return "kCancelled";
    case ErrorCode::kBusy: return "kBusy";
    case ErrorCode::kVersionMismatch: return "kVersionMismatch";
    }
    return "kUnknown";
}

std::string Error::describe() const
{
    std::string out = toString(code);
    out += ": ";
    out += message;
    out += " (";
    out += where.file_name();
    out += ':';
    out += std::to_string(where.line());
    out += ')';
    return out;
}

const Error &logError(const Error &error)
{
    LOG_ERROR(static_cast<int>(error.code), error.describe());
    return error;
}

namespace
{
std::atomic<AssertHandler> gHandler{nullptr};
}

AssertHandler setAssertHandler(AssertHandler handler) noexcept { return gHandler.exchange(handler); }

void assertFailed(const char *expression, const char *message, std::source_location where)
{
    LOG_ERROR(1, std::string("assertion failed: ") + expression + " — " + message + " (" + where.file_name() + ":" +
              std::to_string(where.line()) + ")");
    if (AssertHandler handler = gHandler.load())
        handler(expression, message, where);
    std::abort();
}

} // namespace orosmo::core
