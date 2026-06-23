#include "LoggerSinkStdout.h"

#include <sstream>
#include <time.h>
#include <iomanip>
#include <string_view>
#include <unistd.h>

namespace {

constexpr std::string_view level_prefix(logger::logLevel level)
{
    switch (level) {
        case logger::logLevel::ERROR:   return "ERROR";
        case logger::logLevel::WARNING: return "WARNING";
        case logger::logLevel::INFO:    return "INFO";
        case logger::logLevel::DEBUG:   return "DEBUG";
    }
    return "";
}

} // namespace

logger::LoggerSinkStdout::LoggerSinkStdout(logger::logLevel level)
{
    this->log_level = level;
}

void logger::LoggerSinkStdout::setLogEntry(const std::shared_ptr<logger::LogEntry> &ptr)
{
    const auto entry_level = ptr->getLogLevel();

    // Monotonic threshold (shared predicate): drop everything more verbose
    // than the configured sink level.
    if (!logger::should_log(entry_level, this->log_level)) {
        return;
    }

    const auto time_t_val = std::chrono::system_clock::to_time_t(ptr->getTimepoint());
    struct tm time_buf;
    localtime_r(&time_t_val, &time_buf);
    // Format the output string
    std::ostringstream out;
    out << level_prefix(entry_level) << ": "
        << "[" << std::put_time(&time_buf, "%Y-%m-%d %X") << "]"
        << " - " << ptr->getLogDomain()
        << ": " << ptr->getLogMessage();

    out << '\n';
    const std::string line = out.str();
    // Best-effort diagnostic write; assign-then-discard satisfies write()'s
    // warn_unused_result (a plain (void) cast does not suppress it in GCC).
    const ssize_t ret = ::write(STDOUT_FILENO, line.data(), line.size());
    (void)ret;
}
