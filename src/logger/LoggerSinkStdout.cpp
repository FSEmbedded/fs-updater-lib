#include "LoggerSinkStdout.h"

#include <sstream>
#include <iostream>
#include <time.h>
#include <iomanip>

logger::LoggerSinkStdout::LoggerSinkStdout(logger::logLevel level)
{
    this->log_level = level;
}

void logger::LoggerSinkStdout::setLogEntry(const std::shared_ptr<logger::LogEntry> &ptr)
{
    std::string_view level_prefix;
    bool should_output = false;
    const auto entry_level = ptr->getLogLevel();
    const auto sink_level = this->log_level;

    if ((entry_level == logger::logLevel::DEBUG) && (sink_level == logger::logLevel::DEBUG)) {
        level_prefix = "DEBUG";
        should_output = true;
    }
    else if ((entry_level == logger::logLevel::WARNING) &&
             ((sink_level == logger::logLevel::ERROR) || (sink_level == logger::logLevel::DEBUG))) {
        level_prefix = "WARNING";
        should_output = true;
    }
    else if ((entry_level == logger::logLevel::ERROR) &&
             ((sink_level == logger::logLevel::ERROR) || (sink_level == logger::logLevel::WARNING) || (sink_level == logger::logLevel::DEBUG))) {
        level_prefix = "ERROR";
        should_output = true;
    }

    if (!should_output) {
        return;
    }

    const auto time_t_val = std::chrono::system_clock::to_time_t(ptr->getTimepoint());
    struct tm time_buf;
    localtime_r(&time_t_val, &time_buf);
    // Format the output string
    std::ostringstream out;
    out << level_prefix << ": "
        << "[" << std::put_time(&time_buf, "%Y-%m-%d %X") << "]"
        << " - " << ptr->getLogDomain()
        << ": " << ptr->getLogMessage();

    std::cout << out.str() << std::endl;
    std::cout.flush();
}
