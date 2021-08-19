/**
 * Implement endpoint for sink.
 * Messages will be trasnfered into stdout.
 */

#pragma once

#include "LoggerSinkBase.h"

namespace logger
{
    /**
     * Class description of a logger endpoint.
     * This endpoint points to stdout.
     */
    class LoggerSinkStdout : public LoggerSinkBase
    {
        private:
            logger::logLevel log_level;

        public:
            /**
             * Construct an endpoint that will only consume logger entries for given log-level.
             * @param level log level for filter the endpoint
             */
            LoggerSinkStdout(logger::logLevel level);

            ~LoggerSinkStdout() = default;

            /**
             * Overloaded function of base class. Will be called through the logger to place the entries.
             * @param ptr Contain the logger entry as reference.
             */
            virtual void setLogEntry(const std::shared_ptr<logger::LogEntry> &) override;
    };
}