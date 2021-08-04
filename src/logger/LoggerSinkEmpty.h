#pragma once

#include "LoggerSinkBase.h"


namespace logger
{
    class LoggerSinkEmpty : public LoggerSinkBase
    {
        private:
            logger::logLevel log_level;
        public:
            LoggerSinkEmpty(logger::logLevel level);
            ~LoggerSinkEmpty() = default;
            virtual void setLogEntry(const std::shared_ptr<logger::LogEntry> &) override;
    };
};