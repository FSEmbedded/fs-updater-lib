#pragma once

#include "LoggerSinkBase.h"
#include <sstream>
#include <iostream>
#include <time.h>
#include <iomanip>


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