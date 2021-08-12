/**
 * Classes of transfer objects for log entry.
 *
 * The log object contain the message, log level and domain.
 */

#pragma once

#include "LoggerLevel.h"

#include <chrono>
#include <string>

/**
 * Namespace define all logger related error and classes.
 */
namespace logger
{
    /**
     * Transfer object for the log object.
     */
    class LogEntry
    {
        private:
            const std::string logMessage, logDomain;
            const std::chrono::time_point<std::chrono::system_clock> time_of_occurance;
            const logger::logLevel logLevel;

        public:

            /**
             * Create log object. Will also mark the timepoint of creation.
             * @param logDomain Domain under what the log entry will be crated.
             * @param logMessage Message of log entry.
             * @param level Level of of created log entry.
             */
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

            /**
             * Return log message.
             * @return Message of log entry.
             */
            std::string getLogMessage() const;

            /**
             * Return the timepoint of creation.
             * @return Timepoint of creation of log entry.
             */
            std::chrono::time_point<std::chrono::system_clock> getTimepoint() const;

            /**
             * Return log level of log entry.
             * @return Log level of log entry.
             */
            logger::logLevel getLogLevel() const;

            /**
             * Return domain level of log entry.
             * @return Domain level of log entry.
             */
            std::string getLogDomain() const;

    };
};