#include "LoggerSinkEmpty.h"

#include <sstream>
#include <iostream>
#include <time.h>
#include <iomanip>

logger::LoggerSinkEmpty::LoggerSinkEmpty(logger::logLevel level)
{
    this->log_level = level;
}

void logger::LoggerSinkEmpty::setLogEntry(const std::shared_ptr<logger::LogEntry> &ptr)
{
    return;
}