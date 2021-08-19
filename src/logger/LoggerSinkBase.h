/**
 * Sink class for logger base.
 * Define your own sinks to create endpoints.
 */

#pragma once

#include "LoggerEntry.h"
#include <memory>


namespace logger
{
    /**
     * Base class as interface for logger.
     * You have to write your own implementiation for the different endpoints.
     */
    class LoggerSinkBase
    {
        public:
            /**
             * Will be called through logger to transfer the entries to the function.
             * The refrence will be contain all relevant information.
             * @param ptr Reference with transfer function to log entry.
             */
            virtual void setLogEntry(const std::shared_ptr<logger::LogEntry> &ptr) = 0;
    };
}