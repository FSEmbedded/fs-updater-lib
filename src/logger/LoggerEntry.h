#pragma once

#include "LoggerLevel.h"

#include <chrono>
#include <string>
#include <array>

namespace logger
{
    class LogEntry
    {
        private:
            const std::string logMessage, logDomain;
            const std::chrono::time_point<std::chrono::system_clock> time_of_occurance;
            const logger::logLevel logLevel;

        public:
            LogEntry(
                const std::string &logDomain,
                const std::string &logMessage,
                const logger::logLevel level
            );

            LogEntry(const LogEntry &);
            LogEntry &operator=(const LogEntry &) = delete;
            LogEntry(LogEntry &&) = delete;
            LogEntry &&operator=(LogEntry &) = delete;

            ~LogEntry();

            std::string getLogMessage() const;
            std::chrono::time_point<std::chrono::system_clock> getTimepoint() const;
            logger::logLevel getLogLevel() const;
            std::string getLogDomain() const;

    };
};



