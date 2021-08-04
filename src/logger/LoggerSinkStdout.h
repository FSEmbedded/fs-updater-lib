#pragma once

#include "LoggerSinkBase.h"

namespace logger
{
    class LoggerSinkStdout : public LoggerSinkBase
    {
        private:
            logger::logLevel log_level;
        public:
            LoggerSinkStdout(logger::logLevel level);
            ~LoggerSinkStdout() = default;
            virtual void setLogEntry(const std::shared_ptr<logger::LogEntry> &) override;
    };
};