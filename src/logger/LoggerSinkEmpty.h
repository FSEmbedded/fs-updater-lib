/**
 * Implement endpoint for sink.
 * Messages will be trasnfered into "nothing".
 */

#pragma once

#include "LoggerSinkBase.h"


namespace logger
{
    /**
     * Class description of a logger endpoint.
     * This endpoint points to nothing.
     */
    class LoggerSinkEmpty : public LoggerSinkBase
    {
        private:
            logger::logLevel log_level;

        public:
            /**
             * Construct an endpoint that will only consume logger entries for given log-level.
             * @param level log level for filter the endpoint
             */
            explicit LoggerSinkEmpty(logger::logLevel level);

            ~LoggerSinkEmpty() = default;

            /**
             * Override virtual function with empty body.
             * @param ptr Contain the logger entry as reference.
             */
            virtual void setLogEntry(const std::shared_ptr<logger::LogEntry> &) override;
    };
}