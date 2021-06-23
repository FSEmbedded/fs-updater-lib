#pragma once

#include "LoggerEntry.h"
#include <memory>


namespace logger
{
    class LoggerSinkBase
    {
        public:
            virtual void setLogEntry(const std::shared_ptr<logger::LogEntry> &) = 0;
    };
};